"""Preserve QtTest diagnostics even when Windows routes console logs to a debugger."""
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys

ctypes.windll.kernel32.SetErrorMode(3)
result = subprocess.run(['ctest', '--output-on-failure', '--timeout', '120'])
if result.returncode:
    failed = json.loads(subprocess.check_output(['ctest', '--rerun-failed', '--show-only=json-v1']))
    for test in failed['tests']:
        report = Path(f'test-failure-{test["name"]}.txt').resolve()
        properties = {p['name']: p['value'] for p in test.get('properties', [])}
        environment = os.environ.copy()
        for item in properties.get('ENVIRONMENT', []):
            name, _, value = item.partition('=')
            environment[name] = value
        try:
            retry = subprocess.run(test['command'] + ['-o', str(report) + ',txt'],
                cwd=properties.get('WORKING_DIRECTORY'), env=environment, timeout=120)
            print(f'{test["name"]} diagnostic exit: {retry.returncode}', flush=True)
        except subprocess.TimeoutExpired:
            print(f'{test["name"]} diagnostic timed out', flush=True)
        if report.exists():
            print(report.read_text(encoding='utf-8', errors='replace'), flush=True)
sys.exit(result.returncode)
