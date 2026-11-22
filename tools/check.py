#!/usr/bin/env python3
"""Builds the firmware against the emulator and runs every scenario. Exit 0 only if all pass.

Usage: check.py [scenario ...]   (no names: everything for the current firmware).
FIRMWARE, LIBDEPS and CARD can be set in the environment. CARD is a folder laid out like the
microSD; without it the scenarios that need audio are skipped and say so.
"""
import os
import re
import socket
import time
import subprocess
import sys
from multiprocessing.pool import ThreadPool
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CARD = os.environ.get('CARD', str(Path.home() / 'Cardputer-card'))
SCALE = os.environ.get('SCALE', '5')
FRENCH = {'french', 'french-keyboard'}  # Run on a French build.
NEEDS_CARD = {'card-book', 'book-sleep', 'school-night', 'boot-skip', 'contain', 'stress-apps', 'books-flaky'}
OWN_CARD = {'card-formats': ROOT / 'run' / 'formats-card', 'card-sync': ROOT / 'run' / 'sync-card'}  # made by tools/formats-card.sh, sync_card()
ALL = ['home-pages', 'book-again', 'boot-online', 'boot-offline', 'boot-skip', 'away-nonav', 'away-nowifi', 'away-captive', 'away-nointernet', 'away-joinfail', 'net-add', 'place-power-cut', 'libc-bypass', 'card-book', 'next-chapter', 'contain', 'walkie', 'chat', 'stress-apps', 'stream', 'french', 'french-keyboard', 'clock-timer', 'clock-screen', 'clock-alarm', 'clock-alarm-typing', 'clock-noclock', 'card-formats', 'books-flaky', 'outbox', 'outbox-refused', 'link-certrenewed', 'link-badcert', 'link-dnsfail', 'link-block443', 'link-auth401', 'clock-idle', 'book-sleep', 'alarm-song', 'school-night', 'chat-emoji', 'notify-reboot', 'charge-normal', 'charge-charging', 'card-sync']


def build(tag='current', extra=()):
    subprocess.run(['make', '-s', '-j12', f'TAG={tag}', *extra, 'build'], cwd=ROOT, check=True)


def run(name, tag='current', time_scale=SCALE, sanitizer=''):
    folder = ROOT / 'run' / name
    card = OWN_CARD.get(name, CARD)
    if name in OWN_CARD and not Path(card).is_dir():
        subprocess.run([ROOT / 'tools/formats-card.sh', card], check=True, capture_output=True)
    subprocess.run([ROOT / 'tools/seed.sh', folder, str(card)], check=True)
    binary = ROOT / 'build' / f'{tag}{"-" + sanitizer if sanitizer else ""}' / 'cardputer-emulator'
    first = (ROOT / 'scenarios' / f'{name}.scn').read_text().split('\n', 1)[0]
    if first.startswith('# time-scale '):
        time_scale = first.split()[-1]  # Real sockets don't speed up with the virtual clock.
    done = subprocess.run([binary, '--card', folder / 'card', '--state', folder / 'state', '--scenario', ROOT / 'scenarios' / f'{name}.scn',
                           '--time-scale', time_scale], capture_output=True, text=True, errors='replace', env={**os.environ, 'SDL_VIDEODRIVER': 'dummy'}, timeout=900)
    (folder / 'log.txt').write_text(done.stdout + done.stderr)
    return done


# card-sync runs as Andrew against the real Navidrome, whose address MUSIC_HOST can override (the
# tailnet's, away from home). Andrew's playlist names stay out of this public repo: the card is made here.
def music_host():
    if os.environ.get('MUSIC_HOST'):
        return os.environ['MUSIC_HOST']
    header = Path(os.environ.get('SECRETS_DIR', Path.home() / 'Work/adv-cardputer-kidputer/firmware/include')) / 'music_secrets.h'
    match = re.search(r'SUPACHAT_MUSIC_HOST "([^"]+)"', header.read_text()) if header.exists() else None
    return match[1] if match else ''


def andrew_call_url(host, method, **params):
    import hashlib, secrets, urllib.parse
    header = (Path(os.environ.get('SECRETS_DIR', Path.home() / 'Work/adv-cardputer-kidputer/firmware/include')) / 'music_secrets.h').read_text()
    found = re.search(r'defined\(SUPACHAT_DEVICE_ANDREW\)\n#define SUPACHAT_MUSIC_USER "[^"]*"\n#define SUPACHAT_MUSIC_PASSWORD "([^"]*)"', header)
    password = found[1] if found else ''
    salt = secrets.token_hex(6)
    query = dict(u='andrew', t=hashlib.md5((password + salt).encode()).hexdigest(), s=salt, v='1.16.1', c='cardputer', f='json', **params)
    return f'http://{host}/rest/{method}?{urllib.parse.urlencode(query)}'


def warm_proxy():
    """Ask the proxy ahead of the run everything the device will ask, so it answers at once."""
    import json, urllib.request
    via = f'127.0.0.1:{PROXY_PORT}'
    for _ in range(50):
        try:
            socket.create_connection(('127.0.0.1', PROXY_PORT), timeout=1).close(); break
        except OSError:
            time.sleep(0.2)
    get = lambda method, **p: json.load(urllib.request.urlopen(andrew_call_url(via, method, **p), timeout=120))['subsonic-response']
    lists = get('getPlaylists').get('playlists', {}).get('playlist', [])
    for playlist in lists:
        for entry in get('getPlaylist', id=playlist['id'])['playlist'].get('entry', []):
            if playlist['name'] == 'Andrew':
                get('getLyricsBySongId', id=entry['id'])


def andrew_playlist():
    import hashlib, json, secrets, urllib.parse, urllib.request
    header = (Path(os.environ.get('SECRETS_DIR', Path.home() / 'Work/adv-cardputer-kidputer/firmware/include')) / 'music_secrets.h').read_text()
    found = re.search(r'defined\(SUPACHAT_DEVICE_ANDREW\)\n#define SUPACHAT_MUSIC_USER "[^"]*"\n#define SUPACHAT_MUSIC_PASSWORD "([^"]*)"', header)
    password = found[1] if found else ''
    def call(method, **params):
        salt = secrets.token_hex(6)
        query = dict(u='andrew', t=hashlib.md5((password + salt).encode()).hexdigest(), s=salt, v='1.16.1', c='emu', f='json', **params)
        with urllib.request.urlopen(f'http://{music_host()}/rest/{method}?{urllib.parse.urlencode(query)}', timeout=10) as reply:
            return json.load(reply)['subsonic-response']
    playlist = [p for p in call('getPlaylists')['playlists']['playlist'] if p['name'] == 'Andrew'][0]
    return call('getPlaylist', id=playlist['id'])['playlist']['entry']


def safe(text):
    return re.sub(r'[\\/:*?"<>|]', '_', text).strip('. ')


def card_name(number, entry):
    return f'{number:03d} {safe(entry["title"])} - {safe(entry["artist"].split(", ")[0])}.mp3'


STRAY = '006 Taken Off The List - Nobody.mp3'
PROXY_PORT = 20453  # tools/navidrome-proxy.py, for card-sync only


def sync_card():
    """Andrew's card before a sync: his second song under another number (to be renamed), a song no longer on his list, the rest missing."""
    card = ROOT / 'run' / 'sync-card'
    subprocess.run(['rm', '-rf', str(card)], check=True)
    (card / 'music' / 'Andrew').mkdir(parents=True)
    entries = andrew_playlist()
    (card / 'music' / 'Andrew' / card_name(9, entries[1])).write_bytes(b'\xff\xfb' + bytes(4094))
    # A song since taken off his playlist: it goes to /music/.removed/Andrew, not deleted.
    (card / 'music' / 'Andrew' / STRAY).write_bytes(b'\xff\xfb' + bytes(4094))
    return entries


def check_sync_card(folder, entries):
    have = sorted(p.name for p in (folder / 'card' / 'music' / 'Andrew').iterdir())  # the renamed song is the seed's link
    want = sorted(card_name(n, e) for n, e in enumerate(entries, 1))
    songs = [n for n in have if n.endswith('.mp3')]  # beside them: each song's .lrc or .nolrc
    problems = []
    if songs != want: problems.append(f'card has {songs}, playlist wants {want}')
    if any(n.startswith('.part') for n in have): problems.append('a .part file was left')
    for n, e in enumerate(entries, 1):
        stem = card_name(n, e)[:-4]
        if not (folder / 'card' / 'music' / 'Andrew' / f'{stem}.lrc').exists() and not (folder / 'card' / 'music' / 'Andrew' / f'{stem}.nolrc').exists():
            problems.append(f'no lyrics file for {stem}')
    first = folder / 'card' / 'music' / 'Andrew' / f'{card_name(1, entries[0])[:-4]}.lrc'
    screen = (folder / 'log.txt').read_text(errors='replace').rsplit('[screen]', 1)[-1] if (folder / 'log.txt').exists() else ''
    if first.exists():
        sung = [re.sub(r'^\[[^\]]*\]', '', l).strip() for l in first.read_text().splitlines()]
        if not any(len(l) > 3 and l.split()[0] in screen for l in sung):
            problems.append('no line of the first song\'s lyrics on the screen 40 s in')
    else:
        problems.append('the first song has no .lrc')
    tones = list((folder / 'card' / 'music' / 'Sonneries').glob('*.mp3')) if (folder / 'card' / 'music' / 'Sonneries').is_dir() else []
    if len(tones) < 30: problems.append(f'{len(tones)} ringtones on the card, the Sonneries playlist has 30')
    removed = folder / 'card' / 'music' / '.removed' / 'Andrew' / STRAY
    if not removed.exists(): problems.append(f'{STRAY} was not moved to /music/.removed/Andrew')
    return problems


def server_reachable():
    # The address is in the same gitignored header the firmware builds with.
    header = Path(os.environ.get('SECRETS_DIR', Path.home() / 'Work/adv-cardputer-kidputer/firmware/include')) / 'music_secrets.h'
    match = re.search(r'SUPACHAT_MUSIC_HOST "([^":]+):(\d+)"', header.read_text()) if header.exists() else None
    if not match:
        return False
    try:
        socket.create_connection((match[1], int(match[2])), timeout=3).close()
        return True
    except OSError:
        return False


def runnable(name):
    if not (ROOT / 'scenarios' / f'{name}.scn').exists():
        return False
    if name in NEEDS_CARD and not Path(CARD).is_dir():
        return False
    if name == 'card-sync':
        try:
            socket.create_connection(tuple(music_host().rsplit(':', 1)), timeout=3).close()
            return True
        except (OSError, ValueError):
            return False
    return name != 'stream' or server_reachable()


def main():
    names = sys.argv[1:] or ALL
    build()
    failed = 0
    french = [name for name in names if name in FRENCH]
    if french:
        names = [name for name in names if name not in FRENCH]
        # A French device, built with the language flag.
        build('french', ['DEVICE_DEFS=-DSUPACHAT_LANGUAGE_FR=1 -DSUPACHAT_DEVICE_NAME="\\"Test\\"" -DSUPACHAT_DEVICE_ID="\\"test\\""'])
    for name in french:
        done = run(name, tag='french')
        ok = done.returncode == 0
        failed += 0 if ok else 1
        print(f'{name}: {"PASS" if ok else "FAIL"}')
        if not ok:
            print('\n'.join(line for line in done.stdout.splitlines() if 'FAILED' in line or 'CRASH' in line or '[screen]' in line))
    if 'card-sync' in names:
        names = [name for name in names if name != 'card-sync']
        if runnable('card-sync'):
            entries = sync_card()
            # The real Navidrome through a local proxy that serves the song files from this machine's copies.
            proxy = subprocess.Popen(['python3', '-I', str(ROOT / 'tools' / 'navidrome-proxy.py'), str(PROXY_PORT), music_host()],
                                     stderr=open(ROOT / 'run' / 'card-sync-proxy.log', 'w'))
            warm_proxy()
            # make doesn't see a change of flags: start this build clean, so the address below is the one compiled in.
            subprocess.run(['rm', '-rf', str(ROOT / 'build' / 'andrew')], check=True)
            build('andrew', [f'DEVICE_DEFS=-DSUPACHAT_DEVICE_ANDREW -DSUPACHAT_DEVICE_NAME="\\"Andrew\\"" -DSUPACHAT_DEVICE_ID="\\"andrew\\"" -DSUPACHAT_MUSIC_HOST_OVERRIDE="\\"127.0.0.1:{PROXY_PORT}\\""'])
            try:
                done = run('card-sync', tag='andrew')
            finally:
                proxy.terminate()
            problems = [] if done.returncode else check_sync_card(ROOT / 'run' / 'card-sync', entries)
            ok = done.returncode == 0 and not problems
            failed += 0 if ok else 1
            low = re.findall(r'lowest ever (\d+)', done.stdout)
            print(f'card-sync: {"PASS" if ok else "FAIL"}' + (f', lowest free heap {low[-1]}' if low else ''))
            for line in problems + [l for l in done.stdout.splitlines() if 'FAILED' in l or 'CRASH' in l]: print(line)
        else:
            print('card-sync: SKIPPED, no music server reachable (MUSIC_HOST)')
    # Scenarios run side by side: they share nothing but the CPU, and the virtual clock waits little.
    with ThreadPool(4) as pool:
        finished = {}
        for name, done in pool.imap_unordered(lambda n: (n, run(n)) if runnable(n) else (n, None), names):
            finished[name] = done
    for name in names:
        if not (ROOT / 'scenarios' / f'{name}.scn').exists():
            print(f'{name}: no such scenario'); failed += 1; continue
        if name in NEEDS_CARD and not Path(CARD).is_dir():
            print(f'{name}: SKIPPED, no card at {CARD}'); continue
        if name == 'stream' and not server_reachable():
            print('stream: SKIPPED, no music server reachable'); continue
        done = finished[name]
        ok = done.returncode == 0
        failed += 0 if ok else 1
        low = re.findall(r'lowest ever (\d+)', done.stdout)
        print(f'{name}: {"PASS" if ok else "FAIL"}' + (f', lowest free heap {low[-1]}' if low else ''))
        if not ok:
            print('\n'.join(line for line in done.stdout.splitlines() if 'FAILED' in line or 'CRASH' in line or 'on task' in line))
    print('ALL PASS' if not failed else f'{failed} FAILED')
    return 1 if failed else 0


sys.exit(main())
