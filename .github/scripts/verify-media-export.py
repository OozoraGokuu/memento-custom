"""Exercise the exact portable binary with a fresh profile and no build PATH."""
from pathlib import Path
import argparse
import array
import math
import os
import shutil
import subprocess
import sys
import tempfile
import wave

parser = argparse.ArgumentParser()
parser.add_argument('executable', nargs='?')
parser.add_argument('--flatpak', help='Test an installed Flatpak app ID')
args = parser.parse_args()
if bool(args.executable) == bool(args.flatpak):
    parser.error('Provide an executable or --flatpak APP_ID')
source = Path(args.executable).resolve() if args.executable else None
fixture = Path('tests/fixtures/sentence.mkv').resolve()
with tempfile.TemporaryDirectory(prefix='Memento package ',
        dir=Path.cwd() if args.flatpak else None) as temp:
    root = Path(temp) / '日本語 العربية'
    root.mkdir()
    if os.name == 'nt' and source:
        package = root / 'Memento'
        shutil.copytree(source.parent, package)
        executable = package / source.name
    else:
        executable = source
    media = root / 'Example Episode 日本語.mkv'
    shutil.copyfile(fixture, media)
    output = root / 'Sentence media'
    env = os.environ.copy()
    env['MEMENTO_TEST_MEDIA'] = str(media)
    env['MEMENTO_TEST_EXPORT'] = str(output)
    # Ensure dependencies come from the portable package or Windows itself.
    if os.name == 'nt':
        # os.environ is case-insensitive on Windows, but its dict copy is not.
        # MSYS2 and native Python may expose different key capitalization.
        system_root = next(value for key, value in env.items()
                           if key.casefold() == 'systemroot')
        env['PATH'] = os.pathsep.join([str(executable.parent),
            str(Path(system_root) / 'System32'), system_root])
        for key in list(env):
            if key.startswith(('QT_', 'QML', 'SSL_CERT', 'CURL_CA', 'OPENSSL')):
                del env[key]
    if args.flatpak:
        command = ['flatpak', 'run', f'--env=MEMENTO_TEST_MEDIA={media}',
            f'--env=MEMENTO_TEST_EXPORT={output}', args.flatpak]
    else:
        command = [str(executable)]
    subprocess.run(command + ['--smoke-test', '--ao=null'], env=env, check=True, timeout=60)
    assert len(list(output.glob('*.jpg'))) == 2, 'Missing screenshots'
    assert len(list(output.glob('*.mp3'))) == 2, 'Missing sentence audio'
    with wave.open(str(output / 'decoded.wav'), 'rb') as audio:
        assert audio.getsampwidth() == 2, 'Expected 16-bit decoded audio'
        duration = audio.getnframes() / audio.getframerate()
        assert 0.8 <= duration <= 1.2, f'Unexpected decoded length: {duration}'
        samples = array.array('h', audio.readframes(audio.getnframes()))
        rms = math.sqrt(sum(int(s) ** 2 for s in samples) / len(samples))
        assert rms > 100, f'Sentence audio is silent: RMS={rms}'
    print('PASS: isolated portable runtime, Japanese/Arabic paths, playback, subtitle capture,')
    print('JPG/MP3 export, duplicate protection, decoded duration and audible PCM signal.')
