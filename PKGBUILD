# Builds WoofStore from the folder this file is in: makepkg -si
pkgname=woofstore
pkgver=2.0.0
pkgrel=1
pkgdesc="Software store and system updater for CachyOS and Arch: Flathub, pacman repositories, AUR and AppImages"
arch=(x86_64)
url="https://github.com/LaskyWolf/woofstore"
license=(GPL-3.0-or-later)
depends=(qt6-base qt6-declarative kirigami qqc2-desktop-style libnotify polkit pacman-contrib expac git diffutils breeze-icons archlinux-appstream-data)
makedepends=(cargo)
optdepends=(
  'flatpak: Flathub apps and their updates'
  'paru: AUR packages and their updates'
  'devtools: build AUR packages in a clean chroot'
  'kdialog: password prompts for sudo (zenity works too)'
)
options=(!lto !debug)

build() {
  cd "$startdir"
  export CARGO_TARGET_DIR="$srcdir/target"
  cargo build --release --locked -p woofstore-app
}

package() {
  install -Dm755 "$srcdir/target/release/woof-store" "$pkgdir/usr/bin/woof-store"
  # The steps that need root, and the polkit actions that give their password dialog WoofStore's messages
  install -Dm755 "$startdir/packaging/woofstore-root" "$pkgdir/usr/lib/woofstore/woofstore-root"
  install -Dm644 "$startdir/packaging/io.github.laskywolf.woofstore.policy" "$pkgdir/usr/share/polkit-1/actions/io.github.laskywolf.woofstore.policy"
  install -Dm644 "$startdir/io.github.laskywolf.WoofStore.desktop" "$pkgdir/usr/share/applications/io.github.laskywolf.WoofStore.desktop"
  install -Dm644 "$startdir/icons/io.github.laskywolf.WoofStore.svg" "$pkgdir/usr/share/icons/hicolor/scalable/apps/io.github.laskywolf.WoofStore.svg"
  install -d "$pkgdir/usr/share/woofstore"
  cp -r "$startdir/metadata" "$pkgdir/usr/share/woofstore/metadata"
  install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
