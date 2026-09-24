#!/bin/bash

# Based off of https://github.com/amazingfate/baka-build/blob/master/bakamplayer.sh

# Exit on errors, unset variables, and failed pipeline components.
set -euo pipefail

ARCH=x86_64
PREFIX=/ucrt64

# build Memento
mkdir -p build
cd build
$PREFIX/bin/cmake "$@" ..
$PREFIX/bin/cmake --build . -- -j$(nproc)
tests_failed=0
python3 ../windows/run-tests.py || tests_failed=1

# move DLLs and exe to a new directory
$PREFIX/bin/cmake -E remove_directory "Memento_$ARCH"
mkdir -p "Memento_$ARCH"
if [[ -f memento.exe ]]
then
    MEMENTO_EXE=memento.exe
elif [[ -f src/memento.exe ]]
then
    MEMENTO_EXE=src/memento.exe
else
    echo "Could not find the built memento.exe." >&2
    exit 1
fi
cp "$MEMENTO_EXE" Memento_$ARCH/memento.exe
cp ../LICENSE Memento_$ARCH/LICENSE.txt
cp ../README.md ../RELEASE_NOTES.md Memento_$ARCH/
cp -r ../docs Memento_$ARCH/
pacman -Q > Memento_$ARCH/package-versions.txt
if [[ -d translations ]]
then
    cp -r translations Memento_$ARCH
elif [[ -d src/translations ]]
then
    cp -r src/translations Memento_$ARCH
fi
if [[ " $@ " =~ ' -DMEMENTO_MECAB_SUPPORT=ON ' ]]
then
    cp -r ../dic Memento_$ARCH
fi
if [[ " $@ " =~ ' -DMEMENTO_OCR_SUPPORT=ON '  ]]
then
    cp _deps/libmocr-src/build/libmocr.dll Memento_$ARCH
    cp _deps/libmocr-src/build/libmocr++.dll Memento_$ARCH
fi

MINGW_BUNDLEDLLS_SEARCH_PATH="$(cygpath -w "$PREFIX/bin")" \
    python3 ../windows/mingw-bundledlls.py \
    --copy \
    ./Memento_$ARCH/memento.exe
cp "$PREFIX"/bin/libssl-*.dll ./Memento_$ARCH
cp "$PREFIX/etc/ssl/certs/ca-bundle.crt" ./Memento_$ARCH/cacert.pem
mkdir -p ./Memento_$ARCH/licenses
cp -r "$PREFIX/share/licenses/"* ./Memento_$ARCH/licenses/

cd Memento_$ARCH
PATH="${PREFIX}/share/qt6/bin:$PATH" windeployqt --qmldir ../../src/qml memento.exe

# Qt first tries the system GPU driver, then loads this name when OpenGL 2+
# is unavailable (for example, on a VM or a machine with a basic display driver).
# Keep the system's opengl32.dll untouched so hardware acceleration remains the
# default. Include the Gallium driver used by Mesa's WGL implementation.
cp "$PREFIX/bin/opengl32.dll" opengl32sw.dll
cp "$PREFIX/bin/libgallium_wgl.dll" .

# Resolve every Qt plugin and its transitive imports into the portable folder.
# Index paths once; repeatedly searching the QML tree for each imported name is
# particularly expensive on Windows runners.
python3 ../../windows/resolve-runtime.py . "$(cygpath -w "$PREFIX/bin")"

# Ensure non-Qt dependencies were bundled before producing release archives.
for dll in libgcc_s_seh-1.dll libstdc++-6.dll libmpv-2.dll
do
    if [[ ! -f "$dll" ]]
    then
        echo "Missing required runtime dependency: $dll" >&2
        exit 1
    fi
done
if ! compgen -G 'libtorrent-rasterbar*.dll' >/dev/null
then
    echo "Missing required runtime dependency: libtorrent-rasterbar*.dll" >&2
    exit 1
fi

# Bundle a reviewed yt-dlp release. Keep the version and upstream checksum in
# lockstep so a mutable or corrupted download cannot enter a release package.
YTDLP_VERSION=2026.08.19
YTDLP_SHA256=66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a
curl \
    --fail \
    --location \
    --retry 3 \
    --show-error \
    "https://github.com/yt-dlp/yt-dlp/releases/download/$YTDLP_VERSION/yt-dlp.exe" \
    --output yt-dlp.exe
test -s yt-dlp.exe
printf '%s  %s\n' "$YTDLP_SHA256" yt-dlp.exe | sha256sum --check --strict
objdump -f yt-dlp.exe | grep -Eq \
    'file format pei-x86-64|architecture: i386:x86-64'

# Recheck the final runtime set, including the separately downloaded yt-dlp.
python3 ../../windows/resolve-runtime.py . "$(cygpath -w "$PREFIX/bin")" --verify-only

# Initialize the packaged application, including its Qt/QML and backend
# context, using the exact executable and DLL set that will be distributed.
python3 - <<'PYTEST'
import os
import subprocess
import sys
import shutil
import ctypes
# Avoid modal Windows crash dialogs on unattended release-check runners.
ctypes.windll.kernel32.SetErrorMode(3)
environment = os.environ.copy()
environment['QT_LOGGING_RULES'] = 'qt.qpa.gl=true;qt.scenegraph.general=true'
def diagnose():
    debugger = shutil.which('gdb')
    if not debugger:
        return
    try:
        trace = subprocess.run([debugger, '--batch', '-ex', 'set pagination off',
            '-ex', 'run', '-ex', 'thread apply all bt 20', '--args',
            os.path.abspath('memento.exe'), '--smoke-test'],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=environment, timeout=90)
        print(trace.stdout.decode('utf-8', errors='replace'), flush=True)
    except subprocess.TimeoutExpired as error:
        print((error.stdout or b'').decode('utf-8', errors='replace'), flush=True)
try:
    result = subprocess.run([os.path.abspath('memento.exe'), '--smoke-test'],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=environment, timeout=45)
    print(result.stdout.decode('utf-8', errors='replace'), flush=True)
    print(f'Packaged startup exit code: {result.returncode} (0x{result.returncode & 0xffffffff:08x})', flush=True)
    if result.returncode != 0:
        diagnose()
    sys.exit(0 if result.returncode == 0 else 1)
except subprocess.TimeoutExpired as error:
    print((error.stdout or b'').decode('utf-8', errors='replace'), flush=True)
    diagnose()
    raise SystemExit('Packaged startup timed out after 45 seconds')
PYTEST

# Failed tests may still collect startup diagnostics, but never produce an
# installer or archive that could be mistaken for a validated release.
test "$tests_failed" -eq 0

# Validate the portable payload before spending time compressing installers.
(
    cd ../..
    python3 .github/scripts/audit-distribution.py
    python3 .github/scripts/audit-distribution.py build/Memento_x86_64
    python3 .github/scripts/verify-media-export.py build/Memento_x86_64/memento.exe
)
