"""navidrome-proxy.py: the real Navidrome for card-sync, without waiting on the network for the songs.

python3 -I tools/navidrome-proxy.py <port> <upstream host:port>
Every Subsonic call goes to the real server and its answer comes back as it is: the playlists, the
lyrics, the logins are real. A download (rest/download) is answered from the identical file already on
this machine (Navidrome serves the original file, which wolfpack-sync and ringtones.py copied there from
~/Music/wolfpack and ~/Music/sonneries), found by the song's title and first artist from getSong; only a
song with no local copy is fetched from the server. From away, the home link ran at 0.1 MB/s and the
scenario took longer than its runner allowed (2026-11-21).
"""
import http.server, json, re, socketserver, sys, time, urllib.parse, urllib.request
from pathlib import Path

PORT, UPSTREAM = int(sys.argv[1]), sys.argv[2]
LOCAL = [Path.home() / "Music" / "wolfpack", Path.home() / "Music" / "sonneries"]
safe = lambda s: re.sub(r'[\\/:*?"<>|]', "_", s).strip(". ")
# Songs seen in getPlaylist answers, by id: a download then needs no call to the server at all (over a
# slow link that call alone outlasted the device's 4 s wait for an answer).
SEEN = {}
# The server's answers to everything but downloads, by call and arguments (not the login's salt and token),
# fetched once (check.py asks ahead of the run) and given again at once: a call over the slow link took
# longer than the device's 4 s wait. They are the real server's answers, only earlier.
ANSWERS = {}
AUTH = {"u", "t", "s", "c", "v", "p"}


def answer_key(url, query):
    return url.path + "?" + urllib.parse.urlencode(sorted((k, v[0]) for k, v in query.items() if k not in AUTH))


def local_copy(song):
    title, artists = safe(song.get("title", "")), song.get("artist") or ""
    # rip.py names files by the first artist split at a comma (some credits join with a non-breaking space).
    firsts = {safe(artists.split(", ")[0]), safe(artists.split(",")[0])}
    for name in [f"{first} - {title}.mp3" for first in firsts] + [f"{title}.mp3"]:
        for folder in LOCAL:
            if (folder / name).is_file():
                return folder / name
    return None


class Proxy(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.0"

    def log_message(self, *args):
        pass

    def upstream(self, path):
        return urllib.request.urlopen(f"http://{UPSTREAM}{path}", timeout=60)

    def do_GET(self):
        url = urllib.parse.urlsplit(self.path)
        query = urllib.parse.parse_qs(url.query)
        if url.path == "/rest/download" and "id" in query:
            song = SEEN.get(query["id"][0])
            if song is None:
                getsong = url.path.replace("download", "getSong") + "?" + urllib.parse.urlencode(
                    {**{k: v[0] for k, v in query.items()}, "f": "json"})
                with self.upstream(getsong) as reply:
                    song = json.load(reply)["subsonic-response"].get("song", {})
            copy = local_copy(song)
            print(f"download {song.get('artist')!r} - {song.get('title')!r}: {'local ' + copy.name if copy else 'from the server'}", file=sys.stderr, flush=True)
            if copy:
                body = copy.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "audio/mpeg")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                started = time.time()
                self.wfile.write(body)
                print(f"  sent {len(body)} bytes in {time.time() - started:.1f} s", file=sys.stderr, flush=True)
                return
        key = answer_key(url, query)
        try:
            if key in ANSWERS:
                status, kind, body = ANSWERS[key]
                self.send_response(status)
                self.send_header("Content-Type", kind)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            with self.upstream(self.path) as reply:
                body = reply.read()
            ANSWERS[key] = (reply.status, reply.headers.get("Content-Type", "application/json"), body)
            if url.path == "/rest/getPlaylist":
                try:
                    for entry in json.loads(body)["subsonic-response"]["playlist"].get("entry", []):
                        SEEN[entry["id"]] = entry
                except (ValueError, KeyError):
                    pass
            self.send_response(reply.status)
            self.send_header("Content-Type", reply.headers.get("Content-Type", "application/json"))
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except urllib.error.HTTPError as error:
            self.send_response(error.code)
            self.end_headers()


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


Server(("127.0.0.1", PORT), Proxy).serve_forever()
