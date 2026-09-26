#!/usr/bin/env python3
"""Package the experimental XP player without touching the modern distribution."""
import argparse
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--vlc', type=Path, required=True)
parser.add_argument('--build', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parent
args.output.mkdir(parents=True, exist_ok=True)
for name in ('memento-xp-prototype.exe', 'xp_core_tests.exe'):
    binary = args.build / name
    imports = subprocess.check_output(['objdump', '-p', str(binary)], text=True)
    forbidden = ['InitializeConditionVariable', 'SleepConditionVariableCS',
                 'WakeConditionVariable', 'WakeAllConditionVariable',
                 'GetThreadId', 'GetTickCount64', 'InitializeCriticalSectionEx']
    if any(function in imports for function in forbidden):
        raise SystemExit(f'{binary}: imports post-XP functions; use the GCC 10 Win32 runtime')
    if 'pei-i386' not in imports:
        raise SystemExit(f'{binary}: expected a 32-bit PE executable')
    shutil.copy2(binary, args.output / name)
for name in ('libvlc.dll', 'libvlccore.dll'):
    shutil.copy2(args.vlc / name, args.output / name)
if (args.output / 'plugins').exists():
    shutil.rmtree(args.output / 'plugins')
shutil.copytree(args.vlc / 'plugins', args.output / 'plugins')
licenses = args.output / 'licenses' / 'vlc'
licenses.mkdir(parents=True, exist_ok=True)
for name in ('COPYING.txt', 'AUTHORS.txt'):
    shutil.copy2(args.vlc / name, licenses / name)
shutil.copy2(source / 'README.md', args.output / 'README.md')
shutil.copy2(source.parents[1] / 'LICENSE', args.output / 'LICENSE')
shutil.copytree(source, args.output / 'source', dirs_exist_ok=True,
                ignore=shutil.ignore_patterns('__pycache__'))
print(f'Packaged prototype: {args.output}')
