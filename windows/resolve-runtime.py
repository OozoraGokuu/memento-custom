"""Resolve and verify PE imports without a shell subprocess for each DLL name."""
import argparse
from collections import deque
import os
from pathlib import Path
import re
import shutil
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('package', type=Path)
parser.add_argument('prefix', type=Path)
parser.add_argument('--verify-only', action='store_true')
args = parser.parse_args()
package = args.package.resolve()
system = Path(os.environ['SystemRoot']) / 'System32'
system_names = {p.name.lower() for p in system.iterdir() if p.is_file()}
source = {p.name.lower(): p for p in args.prefix.iterdir() if p.is_file()}
root_names = {p.name.lower(): p for p in package.iterdir() if p.is_file()}
queue = deque(p for p in package.rglob('*') if p.suffix.lower() in {'.dll', '.exe'})
seen = set()
missing = []
copied = 0
imports = re.compile(r'^\s*DLL Name:\s*(.+?)\s*$', re.MULTILINE)
while queue:
    binary = queue.popleft()
    key = str(binary.resolve()).lower()
    if key in seen:
        continue
    seen.add(key)
    table = subprocess.check_output(['objdump', '-p', str(binary)]).decode('utf-8', errors='replace')
    for name in imports.findall(table):
        lower = name.lower()
        if lower in system_names or lower.startswith(('api-ms-win-', 'ext-ms-win-')):
            continue
        # A plugin can load adjacent dependencies; common libraries belong in
        # the executable directory, where every plugin's loader can find them.
        if lower in root_names:
            queue.append(root_names[lower])
        elif (binary.parent / name).is_file():
            queue.append(binary.parent / name)
        elif lower in source and not args.verify_only:
            destination = package / name
            shutil.copy2(source[lower], destination)
            root_names[lower] = destination
            queue.append(destination)
            copied += 1
        else:
            missing.append(f'{binary.relative_to(package)}: {name}')
if missing:
    raise SystemExit('Unresolved runtime imports:\n' + '\n'.join(missing))
if (package / 'opengl32.dll').exists():
    raise SystemExit('The package must not shadow the Windows GPU driver; use opengl32sw.dll.')
print(f'Verified imports of {len(seen)} PE files; copied {copied} runtime dependencies.')
