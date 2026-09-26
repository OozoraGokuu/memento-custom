#!/usr/bin/env bash
set -euo pipefail
# Flatpak owns these locations; arbitrary application folders use the portable builds.
bundle="$(cd -- "$(dirname -- "$0")" && pwd)/Memento_Linux_x86_64.flatpak"
if [[ ! -f "$bundle" ]]; then
    printf 'Place Memento_Linux_x86_64.flatpak beside this installer.\n' >&2
    exit 1
fi
scope="${1:-}"
if [[ -z "$scope" ]] && command -v zenity >/dev/null 2>&1; then
    choice=$(zenity --list --radiolist --title='Install Memento Custom' \
        --text='Choose where Memento will be installed:' \
        --column='Choose' --column='Location' \
        TRUE "Only me: ${XDG_DATA_HOME:-$HOME/.local/share}/flatpak" \
        FALSE 'All users: /var/lib/flatpak') || exit 0
    case "$choice" in 'Only me:'*) scope=--user;; *) scope=--system;; esac
elif [[ -z "$scope" ]]; then
    printf '1) Only me: %s/flatpak\n2) All users: /var/lib/flatpak\n' "${XDG_DATA_HOME:-$HOME/.local/share}"
    read -r -p 'Installation location [1]: ' choice
    case "$choice" in ''|1) scope=--user;; 2) scope=--system;; *) exit 1;; esac
fi
case "$scope" in --user|--system) ;; *) printf 'Use --user or --system.\n' >&2; exit 1;; esac
exec flatpak install "$scope" "$bundle"
