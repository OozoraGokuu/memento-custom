"""Exercise the exact portable binary with a fresh profile and no build PATH."""
from pathlib import Path
import argparse
import array
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave


def boxes(data, start=0, end=None):
    """Read MP4 box boundaries without relying on a system ffprobe binary."""
    end = len(data) if end is None else end
    while start < end:
        assert start + 8 <= end, 'Truncated MP4 box header'
        size, kind = struct.unpack_from('>I4s', data, start)
        header = 8
        if size == 1:
            assert start + 16 <= end, 'Truncated extended MP4 box header'
            size = struct.unpack_from('>Q', data, start + 8)[0]
            header = 16
        elif size == 0:
            size = end - start
        assert header <= size <= end - start, 'Invalid MP4 box size'
        yield kind, start + header, start + size
        start += size


def verify_mp4(path):
    data = path.read_bytes()
    top = list(boxes(data))
    kinds = [kind for kind, _, _ in top]
    assert b'ftyp' in kinds and b'moov' in kinds and b'mdat' in kinds, 'Incomplete MP4'
    assert kinds.index(b'moov') < kinds.index(b'mdat'), 'MP4 is missing fast-start metadata'
    codecs = set()
    duration = None

    def inspect(start, end):
        nonlocal duration
        for kind, first, last in boxes(data, start, end):
            if kind == b'mvhd':
                if data[first] == 0:
                    scale, ticks = struct.unpack_from('>II', data, first + 12)
                else:
                    assert data[first] == 1, 'Unknown MP4 movie header version'
                    scale, ticks = struct.unpack_from('>IQ', data, first + 20)
                assert scale > 0, 'Invalid MP4 timescale'
                duration = ticks / scale
            elif kind == b'stsd':
                assert first + 8 <= last, 'Truncated MP4 sample description'
                codecs.update(entry[0] for entry in boxes(data, first + 8, last))
            elif kind in (b'moov', b'trak', b'mdia', b'minf', b'stbl'):
                inspect(first, last)

    inspect(0, len(data))
    assert duration is not None and 4.9 <= duration <= 5.15, f'Anki clip is not five seconds: {duration}'
    assert b'avc1' in codecs and b'mp4a' in codecs, f'Expected H.264/AAC MP4, got {codecs}'


def verify_pcm(path, minimum, maximum):
    with wave.open(str(path), 'rb') as audio:
        assert audio.getsampwidth() == 2, 'Expected 16-bit decoded audio'
        duration = audio.getnframes() / audio.getframerate()
        assert minimum <= duration <= maximum, f'Unexpected decoded length: {duration}'
        samples = array.array('h', audio.readframes(audio.getnframes()))
        assert samples, 'Decoded audio has no samples'
        if sys.byteorder != 'little':
            samples.byteswap()
        rms = math.sqrt(sum(int(s) ** 2 for s in samples) / len(samples))
        assert rms > 100, f'Sentence audio is silent: RMS={rms}'


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
    if sys.platform == 'darwin':
        env['PATH'] = '/usr/bin:/bin:/usr/sbin:/sbin'
        for key in list(env):
            if key.startswith(('QT_', 'QML', 'DYLD_', 'SSL_CERT', 'CURL_CA', 'OPENSSL')):
                del env[key]
    if args.flatpak:
        command = ['flatpak', 'run', f'--env=MEMENTO_TEST_MEDIA={media}',
            f'--env=MEMENTO_TEST_EXPORT={output}', args.flatpak]
    else:
        command = [str(executable)]
    subprocess.run(command + ['--smoke-test', '--ao=null'], env=env, check=True, timeout=90)
    assert len(list(output.glob('*.jpg'))) == 2, 'Missing screenshots'
    assert len(list(output.glob('*.mp3'))) == 2, 'Missing sentence audio'
    verify_pcm(output / 'decoded.wav', 0.8, 1.2)
    verify_mp4(output / 'anki-sentence.mp4')
    # libmpv's second encoding pass may flush a partially buffered audio block
    # at EOF. Keep the actual MP4 duration check above strict; this PCM check
    # proves the deployed decoder produces the whole clip and an audible signal.
    verify_pcm(output / 'anki-decoded.wav', 4.9, 5.30)
    print('PASS: isolated portable runtime, Japanese/Arabic paths, playback, subtitle capture,')
    print('JPG/MP3 export, five-second Anki H.264/AAC MP4, fast start, duplicate protection,')
    print('decoded audio duration and audible PCM signal.')
