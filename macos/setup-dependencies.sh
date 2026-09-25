#!/usr/bin/env bash

set -euo pipefail

: "${RUNNER_TEMP:?RUNNER_TEMP must be set by GitHub Actions}"
: "${GITHUB_RUN_ID:?GITHUB_RUN_ID must be set by GitHub Actions}"
: "${RUNNER_ARCH:?RUNNER_ARCH must be set by GitHub Actions}"

# Homebrew stopped publishing new Intel bottles in 2026. Keep both macOS
# architectures on the same pinned MacPorts base and ports-tree snapshot.
macports_version=2.12.6
macports_sha256=2d6d58ff3ff60e70f8dc05cb2df9df3137e3d7c0c6d6f6236d68c8ba03343e34
macports_ports_commit=f1ba5ef34565b3116be5d7a2f97c20fe96c855d7
macports_pkg="${RUNNER_TEMP}/MacPorts-${macports_version}-15-Sequoia.pkg"
# MacPorts evaluates Portfiles as its unprivileged `macports` user. Keep the
# pinned tree below MacPorts' own traversable sources directory; GitHub's temp
# and workspace parent directories are private to the runner account.
macports_tree="/opt/local/var/macports/sources/memento-${GITHUB_RUN_ID}-${RUNNER_ARCH}"

curl \
    --fail \
    --location \
    --proto '=https' \
    --retry 5 \
    --show-error \
    --tlsv1.2 \
    "https://distfiles.macports.org/MacPorts/MacPorts-${macports_version}-15-Sequoia.pkg" \
    --output "${macports_pkg}"
printf '%s  %s\n' "${macports_sha256}" "${macports_pkg}" | shasum -a 256 --check
sudo installer -pkg "${macports_pkg}" -target /

if [[ -n "${MEMENTO_MACPORTS_CACHE:-}" && -s "$MEMENTO_MACPORTS_CACHE" ]]; then
    sudo tar -xzf "$MEMENTO_MACPORTS_CACHE" -C /opt
    /opt/local/bin/port installed
    exit 0
fi

sudo mkdir -p "$(dirname "${macports_tree}")"
sudo git clone \
    --filter=blob:none \
    --no-checkout \
    https://github.com/macports/macports-ports.git \
    "${macports_tree}"
sudo git -C "${macports_tree}" checkout --detach "${macports_ports_commit}"

(
    cd "${macports_tree}"
    sudo /opt/local/bin/portindex
)
sudo chmod -R a+rX "${macports_tree}"
printf 'file://%s [default]\n' "${macports_tree}" |
    sudo tee /opt/local/etc/macports/sources.conf >/dev/null

# All runtime libraries are copied into Memento.app later by BundleUtilities.
# The non-default libmpv variant is required for the embedding API.
sudo /opt/local/bin/port -N install \
    json-c \
    libtorrent-rasterbar \
    libzip \
    mecab-utf8 \
    mecab-ipadic-utf8 \
    sqlite3
sudo /opt/local/bin/port -N install mpv +libmpv

/opt/local/bin/port installed \
    json-c \
    libtorrent-rasterbar \
    libzip \
    mecab-utf8 \
    mecab-ipadic-utf8 \
    mpv \
    sqlite3
