#!/usr/bin/env python3
"""Builds the firmware against the emulator and runs every scenario. Exit 0 only if all pass.

Usage: check.py [scenario ...]   (no names: everything for the current firmware).
FIRMWARE, LIBDEPS and CARD can be set in the environment. CARD is a folder laid out like the
microSD; without it the scenarios that need audio are skipped and say so.
"""
import os
import re
import socket
import subprocess
import sys
from multiprocessing.pool import ThreadPool
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CARD = os.environ.get('CARD', str(Path.home() / 'Cardputer-card'))
SCALE = os.environ.get('SCALE', '5')
FRENCH = {'french', 'french-keyboard'}  # Run on a French build.
NEEDS_CARD = {'card-book', 'boot-skip', 'contain', 'stress-apps'}
ALL = ['home-pages', 'book-again', 'boot-online', 'boot-offline', 'boot-skip', 'away-nonav', 'away-nowifi', 'away-captive', 'away-nointernet', 'away-joinfail', 'net-add', 'place-power-cut', 'libc-bypass', 'card-book', 'next-chapter', 'contain', 'walkie', 'chat', 'stress-apps', 'stream', 'french', 'french-keyboard', 'clock-timer', 'clock-screen', 'clock-alarm', 'clock-alarm-typing', 'clock-noclock', 'outbox', 'outbox-refused', 'link-certrenewed', 'link-badcert', 'link-dnsfail', 'link-block443', 'link-auth401']


def build(tag='current', extra=()):
    subprocess.run(['make', '-s', '-j12', f'TAG={tag}', *extra, 'build'], cwd=ROOT, check=True)


def run(name, tag='current', time_scale=SCALE, sanitizer=''):
    folder = ROOT / 'run' / name
    subprocess.run([ROOT / 'tools/seed.sh', folder, CARD], check=True)
    binary = ROOT / 'build' / f'{tag}{"-" + sanitizer if sanitizer else ""}' / 'cardputer-emulator'
    first = (ROOT / 'scenarios' / f'{name}.scn').read_text().split('\n', 1)[0]
    if first.startswith('# time-scale '):
        time_scale = first.split()[-1]  # Real sockets don't speed up with the virtual clock.
    done = subprocess.run([binary, '--card', folder / 'card', '--state', folder / 'state', '--scenario', ROOT / 'scenarios' / f'{name}.scn',
                           '--time-scale', time_scale], capture_output=True, text=True, errors='replace', env={**os.environ, 'SDL_VIDEODRIVER': 'dummy'}, timeout=900)
    (folder / 'log.txt').write_text(done.stdout + done.stderr)
    return done


def server_reachable():
    # The address is in the same gitignored header the firmware builds with.
    header = Path(os.environ.get('SECRETS_DIR', Path.home() / 'Work/cardputer-adv-supachat/firmware/include')) / 'music_secrets.h'
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
