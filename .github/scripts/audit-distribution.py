"""Reject user-profile files and common credential formats in release inputs."""
from pathlib import Path
import hashlib
import re
import subprocess
import sys

forbidden = {'anilist.json', 'anki_connect.json', 'episode-library.json',
             'episode-library.backup.json', 'dictionaries.sqlite', 'memento.conf',
             'mpv.conf', 'input.conf', '.env', 'cookies.txt', 'cookies.sqlite'}
secrets = re.compile(rb'gh[pousr]_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{30,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----\r?\n[A-Za-z0-9+/=]{32}')
# Public cryptographic self-test vectors compiled into the GnuTLS library.
# Each complete PEM was compared byte-for-byte with upstream 3.8.13:
# https://github.com/gnutls/gnutls/blob/3.8.13/lib/crypto-selftests-pk.c
# Only these exact blocks are exempt; other credentials in the DLL still fail.
gnutls_test_keys = {
    'fed7079dd4491609e4c996e232f366ac434fa4cc9df19df363391d079d470964',
    '5a09eb5df3674472eda0077d83cbccef72692c3d052ab393368ac57295439c58',
    'a4d138d7ef9748464117b44fb9c0a4b5b85a1599a127d02690abaa96d03c16e6',
    'fa0b06a72461ec0a963dcfccb8d5b61bd88a6074fc7271573bff68ab86b8c1af',
    'ef237ea8db4f2ae9ee100e8ced96d29b5dceb0e6a948443e6b8a00b1791f9ec9',
    '91ea1699ff6b1a34b4a1d500a9c75a808441e47b9ea68da6fb0195e01ce1dc61',
    'd039c8119a029ab9f9c83c04d67002d887b6bc6026c4264402ab27cdf24cf138',
}
pem = re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----\r?\n[A-Za-z0-9+/=\r\n]+-----END (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')
if len(sys.argv) > 1:
    root = Path(sys.argv[1])
    files = [p for p in root.rglob('*') if p.is_file()]
else:
    files = [Path(p) for p in subprocess.check_output(['git','ls-files','-z']).decode().split('\0') if p]
if not files:
    raise SystemExit('No release files found to audit')
errors = []
for p in files:
    if p.name.lower() in forbidden or p.suffix in {'.torrent', '.sqlite', '.sqlite3'}:
        errors.append(f'Profile/cache file: {p}')
    data = p.read_bytes()
    for match in secrets.finditer(data):
        block = pem.match(data, match.start()) if p.name == 'libgnutls-30.dll' else None
        if block and hashlib.sha256(block.group()).hexdigest() in gnutls_test_keys:
            continue
        errors.append(f'Credential pattern: {p}')
        break
if errors:
    raise SystemExit('\n'.join(errors))
print(f'PASS: audited {len(files)} files; no runtime profiles or credential patterns.')
