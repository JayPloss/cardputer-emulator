#!/usr/bin/env python3
"""Writes a made-up power lab log with known answers, to prove the fitter recovers them.

Usage: fake-power-log.py <folder>. Nothing here is a measurement. The numbers are chosen, the fit
must find them again, and the file the fit writes is marked synthetic.
"""
import math
import random
import sys
from pathlib import Path

R_OHM = 0.28
TAU_S = 12.0
CAPACITY_MAH = 1750
LOADS = [('idle_b64', 70), ('idle_b255', 120), ('cpu_load', 110), ('wifi_idle', 95), ('download', 210), ('tls_handshake', 180),
         ('sd_read', 95), ('mp3_decode', 95), ('spk_speech_192', 230), ('spk_music_255', 420), ('play_card_wifi_off', 190), ('play_card_wifi_on', 250)]
ACTION_S, REST_S = 20, 30
BASE_MA = 70


def ocv(soc):
    return 3350 + 850 * soc - 180 * (1 - soc) ** 4


def main():
    folder = Path(sys.argv[1]); folder.mkdir(parents=True, exist_ok=True)
    random.seed(3)
    t, used_mah, cycle, sag = 0.0, 0.0, 0, 0.0
    with open(folder / 'run.csv', 'w') as out:
        out.write('millis,cycle,action,mv_min,mv_mean,mv_max,samples,free_heap\n')
        first = True
        while used_mah < CAPACITY_MAH:
            for action, current in ([('long_idle', BASE_MA)] if first else []) + LOADS:
                for name, duration, load in ((action, 1800 if action == 'long_idle' else ACTION_S, current), ('rest', REST_S, BASE_MA)):
                    for _ in range(int(duration * 10)):
                        used_mah += load * 0.1 / 3600
                        target = load * R_OHM
                        sag += (target - sag) * (1 - math.exp(-0.1 / TAU_S))
                        mean = ocv(max(0, 1 - used_mah / CAPACITY_MAH)) - sag
                        noise = random.gauss(0, 6)
                        t += 0.1
                        out.write(f'{int(t * 1000)},{cycle},{name},{int(mean + noise - 15)},{int(mean + noise)},{int(mean + noise + 15)},90,120000\n')
            cycle += 1
            first = False
    (folder / 'boots.csv').write_text(f'120,1,4190\n{int(t * 1000)},9,3300\n')


main()
