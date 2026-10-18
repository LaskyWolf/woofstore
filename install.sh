#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# WoofStore installer for Arch Linux and CachyOS.
#
#   Install or update:  curl -fsSL https://raw.githubusercontent.com/LaskyWolf/woofstore/main/install.sh | bash
#   Uninstall:          curl -fsSL https://raw.githubusercontent.com/LaskyWolf/woofstore/main/install.sh | bash -s -- --uninstall
#
# Builds a normal pacman package (woofstore) from the source and installs it, so pacman can update and remove it.
# It asks for your password through sudo, and pacman asks you to confirm before it installs anything.
set -euo pipefail

REPO="${WOOFSTORE_REPO:-https://github.com/LaskyWolf/woofstore}"
REF="${WOOFSTORE_REF:-main}"
SOURCE="${WOOFSTORE_SOURCE:-}"  # a local checkout instead of downloading (for testing)
YES="${WOOFSTORE_ASSUME_YES:-}"  # 1 answers pacman's questions

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
fail() { printf '\033[31mError: %s\033[0m\n' "$*" >&2; exit 1; }
# A pipe has no keyboard: pacman and makepkg read their questions from the terminal instead
ask_from_tty() { if [ -z "$YES" ] && [ -r /dev/tty ]; then "$@" </dev/tty; else "$@"; fi; }

[ "$(id -u)" -ne 0 ] || fail "run this as your normal user, not as root. makepkg refuses to build as root; sudo is used where it is needed."
[ -r /etc/os-release ] || fail "can't tell which Linux distribution this is (/etc/os-release is missing)."
. /etc/os-release
case " $ID ${ID_LIKE:-} " in *" arch "*|*" cachyos "*) ;; *) fail "WoofStore needs Arch Linux or a distribution based on it, like CachyOS. This is: ${PRETTY_NAME:-$ID}." ;; esac

if [ "${1:-}" = "--uninstall" ]; then
    say "Removing woofstore"
    ask_from_tty sudo pacman -Rns ${YES:+--noconfirm} woofstore
    say "Removed. Settings and caches stay in ~/.config/WoofStore, ~/.local/share/WoofStore and ~/.cache/WoofStore."
    exit 0
fi

# 1. Tools to build the package
say "Installing the build tools with pacman: base-devel cmake"
ask_from_tty sudo pacman -S --needed ${YES:+--noconfirm} base-devel cmake

# 2. The source
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
if [ -n "$SOURCE" ]; then
    say "Using the local source in $SOURCE"
    tar -C "$SOURCE" --exclude=./build --exclude=./.git -cf - . | tar -C "$work" -xf -
else
    say "Downloading WoofStore ($REF)"
    curl -fsSL "$REPO/archive/$REF.tar.gz" | tar -xz -C "$work" --strip-components=1
fi
[ -f "$work/PKGBUILD" ] || fail "the download has no PKGBUILD. Check WOOFSTORE_REPO and WOOFSTORE_REF."

# 3. Build and install the package; makepkg installs the runtime dependencies listed in the PKGBUILD
say "Building and installing woofstore (this takes a few minutes)"
cd "$work"
ask_from_tty makepkg -si ${YES:+--noconfirm}

# 4. KRunner and web pages offer "Install this app" as an appstream: link; make the store the program that opens it
handler=io.github.laskywolf.WoofStore.desktop
if gio mime x-scheme-handler/appstream "$handler" >/dev/null 2>&1 || xdg-mime default "$handler" x-scheme-handler/appstream >/dev/null 2>&1; then
  say "KRunner suggestions for apps you do not have now open in WoofStore"
else
  echo "Could not set WoofStore as the handler of appstream: links. Run: gio mime x-scheme-handler/appstream $handler"
fi

say "Installed WoofStore. Open it from the application menu, or run: woof-store"
cat <<EOF

Update: run the same command again.
Uninstall: add  -s -- --uninstall  after "bash" in the command.
Flatpak apps need flatpak, AUR packages need paru (and devtools for clean-chroot builds): install the ones you want with pacman.
EOF
