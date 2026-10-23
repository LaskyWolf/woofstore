# Builds WoofStore from the folder this file is in: makepkg -si
pkgname=woofstore
pkgver=1.2.1
pkgrel=1
pkgdesc="Software store and system updater for CachyOS and Arch: Flathub, pacman repositories, AUR and AppImages"
arch=(x86_64)
url="https://github.com/LaskyWolf/woofstore"
license=(GPL-3.0-or-later)
depends=(qt6-base kcoreaddons kparts appstream-qt zlib konsole polkit pacman-contrib expac git breeze-icons archlinux-appstream-data)
makedepends=(cmake gcc)
optdepends=(
  'flatpak: Flathub apps and their updates'
  'paru: AUR packages and their updates'
  'devtools: build AUR packages in a clean chroot'
)

build() {
  cmake -S "$startdir" -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DBUILD_TESTING=OFF
  cmake --build build
}

package() {
  DESTDIR="$pkgdir" cmake --install build
  install -Dm644 "$startdir/LICENSE" "$pkgdir/usr/share/licenses/$pkgname/LICENSE"
}
