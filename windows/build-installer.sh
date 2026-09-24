#!/bin/bash

# Exit on errors, unset variables, and failed pipeline components.
set -euo pipefail

./windows/build.sh "$@"
cp -p ./windows/license.rtf ./build/.
cp -p ./windows/logo.ico ./build/.
python3 ./windows/generate-nsis-file-list.py \
    ./build/Memento_x86_64 \
    ./build/uninstall-files.nsh \
    ./build/owned-files.txt
test -s ./build/uninstall-files.nsh
test -s ./build/owned-files.txt
cd ./build
makensis -WX installer.nsi
makensis \
    -WX \
    -DMEMENTO_INSTALLER_TEST_FAILURE=1 \
    -DMEMENTO_INSTALLER_OUTFILE=Memento_Windows_x86_64_Installer_FailureTest.exe \
    installer.nsi

# Build a synthetic older package for a true cross-version rollback test. Its
# self-owned file set contains one legacy-only payload that the current package
# intentionally omits.
/ucrt64/bin/cmake -E remove_directory Memento_x86_64_legacy
cp -a Memento_x86_64 Memento_x86_64_legacy
cp -p license.rtf Memento_x86_64_legacy/legacy-only.dll
python3 ../windows/generate-nsis-file-list.py \
    Memento_x86_64_legacy \
    legacy-uninstall-files.nsh \
    legacy-owned-files.txt
test -s legacy-uninstall-files.nsh
test -s legacy-owned-files.txt
makensis \
    -WX \
    -DMEMENTO_RUNTIME_DIR=Memento_x86_64_legacy \
    -DMEMENTO_FILE_LIST=legacy-uninstall-files.nsh \
    -DMEMENTO_OWNERSHIP_MANIFEST_SOURCE=legacy-owned-files.txt \
    -DMEMENTO_INSTALLER_OUTFILE=Memento_Windows_x86_64_Installer_LegacyTest.exe \
    installer.nsi
