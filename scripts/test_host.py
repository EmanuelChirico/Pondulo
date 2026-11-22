#!/usr/bin/env python3
"""Build and run the original host tests; return nonzero on any failure."""
from pathlib import Path
import os
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
D = ROOT / 'firmware/display-node'
P = ROOT / 'firmware/price-bridge'
BUILD = ROOT / 'build/host-tests'
BUILD.mkdir(parents=True, exist_ok=True)
includes = [D / p for p in ('tests/mock', 'ui_macelleria/include', 'gesture_link/include', 'pointer_link/include', 'storage/include')]
includes += [P / 'price_link']
cases = {
    'cursor_glide': [D/'ui_macelleria/src/cursor_glide.c'],
    'gesture_link': [D/'gesture_link/src/gesture_link.c'],
    'xy_link_gesture': [D/'pointer_link/src/xy_link.c'],
    'dwell_switch': [D/'ui_macelleria/src/ui_macelleria.c', D/'ui_macelleria/src/cursor_glide.c'],
    'audio_cues': [D/'ui_macelleria/src/ui_macelleria.c', D/'ui_macelleria/src/cursor_glide.c'],
    'json_and_cart': [D/'storage/src/sd_storage.c'],
    'sd_fixture': [D/'storage/src/sd_storage.c', D/'tests/mock/sd_storage_fixture.c'],
    'prices': [P/'price_link/price_parser.c'],
}
failed = []
for name, sources in cases.items():
    test = (P if name == 'prices' else D) / f'tests/test_{name}.c'
    exe = BUILD / name
    command = shlex.split(os.environ.get('CC', 'cc')) + ['-std=c11', '-Wall', '-Wextra', '-g']
    command += [f'-I{p}' for p in includes] + [str(test)] + [str(p) for p in sources] + ['-o', str(exe)]
    print(f'\n=== {name} ===', flush=True)
    result = subprocess.run(command, cwd=ROOT)
    if result.returncode == 0:
        result = subprocess.run([str(exe)], cwd=ROOT)
    if result.returncode:
        failed.append(name)
print(f'\n{len(cases)-len(failed)}/{len(cases)} test executables passed.', flush=True)
if failed:
    print('Failed: ' + ', '.join(failed))
sys.exit(bool(failed))
