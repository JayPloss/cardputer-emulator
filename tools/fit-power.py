#!/usr/bin/env python3
"""Fits a device's power model from its power lab log (firmware/lab/power_lab.cpp).

Usage: fit-power.py <run.csv> <boots.csv> --device device-1 [--capacity-mah 1750] --out devices/device-1.json

No current sensor exists, so currents are inferred in two steps:
  1. Sag per action: rest voltage just before it minus its last second, extrapolated with the
     recovery time constant to where it would settle. That is the current above the rest's own.
  2. The long run gives the overall mean current: rated capacity over the time from full to flat,
     assuming the 1750 mAh rating (a real cell may differ; its age shows here as an error in every
     current, though not in the ratios).
  3. The long idle's voltage slope against the whole run's gives the rest's own current as a share
     of that mean. Resistance is what makes the time-weighted currents add up to the mean, and each
     action's current is the rest's plus sag / resistance.
Open-circuit voltage against state of charge comes from the end of each rest, the charge used
so far being time over total time. Recovery's time constant comes from the rest after each action.
"""
import argparse
import csv
import json
import math
from collections import defaultdict


def read(path):
    with open(path) as handle:
        for row in csv.DictReader(handle):
            try:
                yield {'ms': int(row['millis']), 'cycle': int(row['cycle']), 'action': row['action'], 'low': int(row['mv_min']),
                       'mean': int(row['mv_mean']), 'high': int(row['mv_max']), 'samples': int(row['samples'])}
            except (ValueError, KeyError, TypeError):
                continue  # A row cut off by a brownout.


def segments(rows):
    current = []
    for row in rows:
        if current and (row['action'] != current[-1]['action'] or row['ms'] < current[-1]['ms']):
            yield current
            current = []
        current.append(row)
    if current:
        yield current


def fit_recovery(rest):
    """V(t) = settled - gap * exp(-t / tau), by a grid over tau and least squares on the rest."""
    if len(rest) < 50:
        return None
    t0 = rest[0]['t']
    best = None
    for tau in [x / 2 for x in range(2, 121)]:
        xs = [math.exp(-(r['t'] - t0) / tau) for r in rest]
        ys = [r['mean'] for r in rest]
        n, sx, sy = len(xs), sum(xs), sum(ys)
        sxx, sxy = sum(x * x for x in xs), sum(x * y for x, y in zip(xs, ys))
        denominator = n * sxx - sx * sx
        if abs(denominator) < 1e-9:
            continue
        slope = (n * sxy - sx * sy) / denominator
        intercept = (sy - slope * sx) / n
        error = sum((y - (slope * x + intercept)) ** 2 for x, y in zip(xs, ys))
        if best is None or error < best[0]:
            best = (error, tau, intercept, -slope)
    return best[1:] if best and best[3] > 8 else None  # tau, settled, gap


def line_slope(points):
    n, sx, sy = len(points), sum(p[0] for p in points), sum(p[1] for p in points)
    sxx, sxy = sum(p[0] ** 2 for p in points), sum(p[0] * p[1] for p in points)
    return (n * sxy - sx * sy) / (n * sxx - sx * sx)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('run'); parser.add_argument('boots')
    parser.add_argument('--device', required=True)
    parser.add_argument('--capacity-mah', type=float, default=1750)
    parser.add_argument('--out', required=True)
    parser.add_argument('--synthetic', action='store_true')
    args = parser.parse_args()

    rows = list(read(args.run))
    if len(rows) < 100:
        raise SystemExit('too few rows to fit')
    # Each boot restarts millis, so build a running clock across brownout restarts.
    clock, last, offset = [], 0, 0
    for row in rows:
        if row['ms'] < last:
            offset += last
        last = row['ms']
        clock.append(row['ms'] + offset)
    for row, at in zip(rows, clock):
        row['t'] = at / 1000
    total_seconds = rows[-1]['t'] - rows[0]['t']
    mean_ma = args.capacity_mah * 3600 / total_seconds

    groups = list(segments(rows))
    taus = [fit[0] for fit in (fit_recovery(g) for g in groups if g[0]['action'] == 'rest') if fit]
    tau = sorted(taus)[len(taus) // 2] if taus else None

    # Sag above the rest's own load, extrapolated to where it would settle if held.
    sags, lows, weights = defaultdict(list), defaultdict(list), defaultdict(float)
    for index, group in enumerate(groups):
        action = group[0]['action']
        if action in ('rest', 'long_idle', 'boot'):
            continue
        before = next((g for g in reversed(groups[:index]) if g[0]['action'] == 'rest'), None)
        if not before:
            continue
        rest_v = sum(r['mean'] for r in before[-10:]) / len(before[-10:])
        length = group[-1]['t'] - group[0]['t']
        last = sum(r['mean'] for r in group[-10:]) / len(group[-10:])
        settled_fraction = 1 - math.exp(-length / tau) if tau else 1
        sags[action].append((rest_v - last) / settled_fraction)
        lows[action].append(rest_v - min(r['low'] for r in group))
        weights[action] += length
    mean_sag = {action: sum(values) / len(values) for action, values in sags.items()}
    peaks = {a: max(values) for a, values in lows.items()}

    # What the rest itself draws: the long idle's slope against the slope of the whole discharge
    # over the same stretch. Without a long idle in the log this stays unknown, and said so.
    rest_ma = None
    long_idle = [r for g in groups if g[0]['action'] == 'long_idle' for r in g]
    if len(long_idle) > 1000:
        idle_slope = line_slope([(r['t'], r['mean']) for r in long_idle[::20]])
        after = [g for g in groups if g[0]['action'] == 'rest' and g[0]['t'] > long_idle[-1]['t']][:40]
        overall_slope = line_slope([(g[-1]['t'], sum(r['mean'] for r in g[-10:]) / 10) for g in after]) if len(after) > 5 else None
        if overall_slope and overall_slope < 0 and idle_slope < 0:
            rest_ma = mean_ma * idle_slope / overall_slope

    rest_seconds = sum(g[-1]['t'] - g[0]['t'] for g in groups if g[0]['action'] in ('rest', 'long_idle'))
    above = sum(max(mean_sag[a], 0) * weights[a] for a in mean_sag)
    resistance_ohm = above / total_seconds / max(mean_ma - (rest_ma or 0), 1)
    currents = {a: (rest_ma or 0) + max(mean_sag[a], 0) / resistance_ohm for a in mean_sag} if resistance_ohm > 0 else {}

    # Open-circuit voltage against state of charge: each rest's end, with SoC from elapsed time.
    ocv = []
    for group in groups:
        if group[0]['action'] == 'rest' and len(group) >= 20:
            soc = 1 - (group[-1]['t'] - rows[0]['t']) / total_seconds
            ocv.append((round(soc, 3), round(sum(r['mean'] for r in group[-10:]) / len(group[-10:]))))
    ocv = ocv[:: max(1, len(ocv) // 40)]

    with open(args.boots) as handle:
        boots = [line.strip().split(',') for line in handle if line.strip()]
    brownouts = [b for b in boots if len(b) >= 3 and b[1] == '9']  # ESP_RST_BROWNOUT
    model = {
        'device': args.device,
        'measured': not args.synthetic,
        'synthetic': args.synthetic,
        'capacity_mah_assumed': args.capacity_mah,
        'hours_full_to_flat': round(total_seconds / 3600, 2),
        'mean_current_ma_inferred': round(mean_ma, 1),
        'resistance_ohm': round(resistance_ohm, 3),
        'recovery_tau_s': tau,
        'rest_current_ma_inferred': round(rest_ma, 1) if rest_ma else None,
        'ocv_by_soc': ocv,
        'current_ma_by_action': {a: round(c, 1) for a, c in sorted(currents.items())},
        'sag_mv_by_action': {a: round(v, 1) for a, v in sorted(mean_sag.items())},
        'peak_sag_mv_by_action': {a: round(v, 1) for a, v in sorted(peaks.items())},
        'brownouts_seen': len(brownouts),
        'battery_mv_at_brownout': [int(b[2]) for b in brownouts],
    }
    with open(args.out, 'w') as handle:
        json.dump(model, handle, indent=1)
    print(f'{args.device}: {model["hours_full_to_flat"]} h on the cycle, inferred {mean_ma:.0f} mA mean, R {resistance_ohm:.3f} ohm, rest {rest_ma} mA, '
          f'tau {tau} s, {len(currents)} actions, {len(brownouts)} brownouts')


main()
