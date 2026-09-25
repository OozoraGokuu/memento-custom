#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "usage: $0 ARCHIVE ARCH MIN_MACOS VERSION BUNDLE_ID" >&2
    exit 64
fi

archive=$1
expected_arch=$2
minimum_macos=$3
expected_version=$4
expected_bundle_id=$5

if [[ ! -s "$archive" ]]; then
    echo "macOS package is missing or empty: $archive" >&2
    exit 1
fi

archive_dir=$(cd "$(dirname "$archive")" && pwd)
archive_path="$archive_dir/$(basename "$archive")"
repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
temporary_root=${RUNNER_TEMP:-${TMPDIR:-/tmp}}
verification_root=$(mktemp -d "$temporary_root/memento-package-verify.XXXXXX")
trap 'rm -rf -- "$verification_root"' EXIT

while IFS= read -r entry; do
    case "$entry" in
        Memento.app | Memento.app/*) ;;
        *)
            echo "Unexpected ZIP entry: $entry" >&2
            exit 1
            ;;
    esac
done < <(zipinfo -1 "$archive_path")

ditto -x -k "$archive_path" "$verification_root"
bundle="$verification_root/Memento.app"
executable="$bundle/Contents/MacOS/Memento"
plist="$bundle/Contents/Info.plist"

test -d "$bundle"
test -x "$executable"
file "$executable" | grep -q "Mach-O.*$expected_arch"
codesign --verify --deep --strict --verbose=2 "$bundle"
python3 "$repo_root/.github/scripts/verify-macos-bundle.py" \
    "$bundle" "$expected_arch" "$minimum_macos"

# Test the extracted app without Qt or package-manager search paths.
python_executable=$(command -v python3)
unset QT_PLUGIN_PATH QML2_IMPORT_PATH QML_IMPORT_PATH DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
"$executable" --help | grep -q '^Usage: memento'
"$python_executable" - "$executable" <<'PY'
import subprocess
import sys
import os

try:
    result = subprocess.run(
        [sys.argv[1], "--smoke-test"],
        check=False,
        timeout=30,
    )
except subprocess.TimeoutExpired:
    raise SystemExit("Packaged Memento smoke test timed out after 30 seconds")

if result.returncode:
    raise SystemExit(result.returncode)
network = subprocess.run([sys.argv[1], "--smoke-test"],
    env=dict(os.environ, MEMENTO_TEST_NETWORK="1"), timeout=90)
raise SystemExit(network.returncode)
PY

"$python_executable" "$repo_root/.github/scripts/verify-media-export.py" "$executable"

test "$(/usr/libexec/PlistBuddy \
    -c 'Print :LSMinimumSystemVersion' "$plist")" = "$minimum_macos"
test "$(/usr/libexec/PlistBuddy \
    -c 'Print :CFBundleShortVersionString' "$plist")" = "$expected_version"
test "$(/usr/libexec/PlistBuddy \
    -c 'Print :CFBundleIdentifier' "$plist")" = "$expected_bundle_id"

echo "Verified extracted package: $archive_path"
