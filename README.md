# WoofStore

A native Qt 6 / KDE store for CachyOS with a persistent sidebar and Quickpeek-style native controls and subtle trays.

## Build and run on CachyOS

Install the dependencies:

```bash
sudo pacman -S --needed base-devel cmake qt6-base qt6-svg kcoreaddons kparts konsole breeze-icons appstream-qt archlinux-appstream-data expac flatpak git cachy-update
```

Build from this directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/woof-store
```

Run the store as your normal user. Package managers request elevated privileges only when needed.

If no Flatpak catalogue is available, configure a remote first:

```bash
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak update --user --appstream
```

Restart the store after refreshing the catalogue.

### Catalogue data

- Repository apps get names, icons, and screenshots from `archlinux-appstream-data`. Without it, Explore shows the install command.
- The AUR package list downloads once a day (about 10 MB) into `~/.cache/WoofStore/WoofStore/`.
- Popularity rankings from Flathub and pkgstats refresh daily. Offline, Explore uses the last saved copy.
- Screenshots download on first view and stay in the same cache folder (at most 200 MB).
- To add descriptions or icons for CachyOS and AUR apps, or to fix a wrong merge, see [metadata/README.md](metadata/README.md).
- `./build/woof-store --catalog-stats` prints source counts, merge results, load time, and memory use.

## Usage

Use the top-left button to collapse or expand the sidebar. Compact mode keeps tooltips and keyboard navigation. The sidebar size is remembered. Settings is currently omitted; source filters remain on Explore.

- **Explore:** every app from Flathub, the pacman repositories (including CachyOS), and the AUR. The same app from several sources is one entry. Search suggests names as you type and ranks results by match and popularity; packages without app metadata appear under Other packages. The home page shows featured apps and one tabbed module with Trending, Popular, New and Updated on Flathub. Rankings refresh daily. The AUR has no equivalent lists in the store; its apps appear through search and the directory.
- **Installed:** lists the apps in your application menu, like the Start menu: Flatpaks, Arch packages, AUR/local packages, AppImages and other launchers. Steam game shortcuts are left out. Each row shows a coloured square for its source; the details are in its tooltip. Apps with no catalogue page get a description from the package database or, when nothing else knows them, a web summary.
- **Updates:** checks Flatpak, repository and AUR updates when the store starts and on demand. A list appears only when something can be updated; otherwise a short message says everything is current. Flatpak apps update from the store. Arch and system packages are listed but update with the system update of a later version.
- **Activity:** inspect simulated progress and history in a popup. The embedded-terminal demo also supports ordinary and hidden input. No package operation is started by the current file or update preview controls.

## Local file installation previews

Use Add AppImage and Add Flatpak, or drop `.AppImage`, `.flatpak` and `.flatpakref` files on the matching card. The install window is the same as for Explore. AppImages are copied to the store's data folder and added to the app menu. Flatpak files install system-wide.

Remote URLs and files of the wrong type are rejected. Click any Installed row to open its details page.

## Update authorization preview

Update all and Update start `flatpak update` in the background with the same progress window; the list refreshes afterwards.

## Installing and removing

Install, Reinstall and Remove run the real package manager in the background, one operation at a time, with a progress window (steps, one status line, "Show details" for the raw output). Passwords come from the system: polkit for `pkexec`, and a KDialog/Zenity prompt for the `sudo` that `pkgctl` uses. AUR builds happen under `~/.cache/WoofStore/aur-build`, not `/tmp`.

- **Flatpak:** `flatpak install --system -y <remote> <id>`, no questions asked; the system asks for your password through polkit if needed.
- **Repository:** `pkexec pacman -S --needed --noconfirm <name>`.
- **AUR:** the store downloads the build files, shows the AUR facts (maintainer, votes, dates), lists lines to read twice (downloads, `curl | sh`, install scripts, unusual source hosts, skipped checksums) and shows what changed since you last approved the package. The Build button stays off until you tick "I read the recipe". It then builds with `pkgctl build` in a clean chroot (package `devtools`, `sudo pacman -S --needed devtools`) and installs only the built file with `pkexec pacman -U`. The chroot setup asks for your sudo password in a system dialog. A chroot keeps the build away from your files; it is not a sandbox, and the installed package runs as root. Build dependencies from the AUR are not resolved.
- **Remove:** a dialog asks what else to delete. Caches are pre-selected; settings and data (`~/.config`, `~/.local/share`, `~/.var/app`) are not. They are deleted only after the package was removed. Only folders whose name equals the app or package name are offered.
- **Open:** Flatpaks start with `flatpak run`; other apps through their desktop file.

Pacman installs can cause a partial upgrade if your repository database is newer than your system; update the system first.

## Preview and harmless terminal test

```bash
QT_QPA_PLATFORM=offscreen ./build/woof-store --screenshot /tmp/woof-store.png
./build/woof-store --terminal-demo
QT_QPA_PLATFORM=offscreen ./build/woof-store --explore-check
QT_QPA_PLATFORM=offscreen ./build/woof-store --terminal-check
QT_QPA_PLATFORM=offscreen STORE_LIVE_TESTS=1 ./build/store-tests
```

The terminal demo asks for ordinary and hidden input without installing software. The automatic terminal check verifies both prompts. The optional live check searches the AUR, downloads a recipe into a temporary directory, and confirms that modifying it invalidates review. Neither check installs packages. Explore previews also support `--carousel-slide 1` and `--explore-bottom`. Desktop previews can use `--page installed`, `--page updates`, or `--compact`.

## Install a launcher

Optional installation for your user account:

```bash
cmake --install build --prefix "$HOME/.local"
```

The desktop launcher calls `woof-store`; ensure `$HOME/.local/bin` is on your desktop session's PATH.

App cards open a detail page inspired by Flathub: icon, publisher description collapsed to six lines with Show more / Show less, screenshot carousel, latest release notes, add-ons, links, and a source menu with every package of the app. Permissions appear only for installed Flatpaks and come from `flatpak info -M`; they are not a security verdict. Use Back or Alt+Left to return to Explore.

Browse app screenshots with the bar indicators, Left/Right keys, or deliberate dragging. Wheel and trackpad input scroll the page without switching images. Neighboring flat screenshots remain visible while the current image animates into place. Only publisher screenshots are shown.

Installed uses the same compact list as Explore. `--installed-preview` shows sample data and opens only offline app details. Remove and Reinstall run the real operation for Flatpak, Arch and AUR/local packages; AppImages can be removed, other launchers cannot.

`--updates-preview idle`, `checking`, `failed` or `available` captures labeled sample states without starting a check. UI checks use sample inventory rather than querying installed packages.

App details and the Explore list have Install buttons that run the real operation (see Installing and removing). The source menu lists real packages; the button installs from the selected one. The checks below use simulated operations. Progress is shared across buttons for the same app during this session.

The bottom sidebar Activity control opens a modeless popup with activity history and embedded terminal prompts. Collapsing the sidebar keeps an activity icon and progress arc; expanding it adds a progress line. Preview tasks show percentages. Real package-manager progress is indeterminate. Closing this popup does not stop the operation.

Use `--prototype-check` to test simulated install, source selection, remove, reinstall, and activity popup behavior without package operations. Use `--activity-preview` for a sample popup, `--activity-sidebar` for sidebar progress, or `--details-app org.kde.krita --install-preview` to preview the installing state. Checks and screenshot previews use the small test catalogue in `tests/fixtures/`. Add `--live-catalog` to a screenshot preview to use the live sources, and `--search <text>` to type a search in it.

The expanded sidebar shows a divider and uses an Activity progress line with a plain icon. Compact mode removes the divider and keeps the activity progress arc. The source control has a separate drop-down arrow.

Capture the update popup with `--updates-preview idle --authorization-preview --screenshot /tmp/authorization.png`. `--file-preview /path/to/file.AppImage` previews a selected local file without changes; `--installed-open` opens its main detail page when combined with that flag. `--explore-end` captures Load more below the directory.

Filter, sidebar, and description toggles animate smoothly using KDE’s animation speed. Hover or keyboard-focus a mosaic card to expand it into a publisher-image preview above the existing mosaic; click it to open details. The preview leaves the mosaic geometry unchanged. Capture it with `--mosaic-hover org.gimp.gimp --screenshot /tmp/hover.png`.

Expanded hover cards dissolve when the pointer leaves; returning during the fade keeps them open. Escape or scrolling closes them immediately.

Featured and expanded hover cards offer quick Install buttons. They share the simulated Install → Installing… → Open flow with app details and list rows; no software is downloaded or installed.

Back and Show more/less use native button shapes. Pages omit Preview badges and metadata footers.

Mouse Back and Forward navigate page history. App details opened from Installed return to Installed; Explore details return to the previous Explore position. Forward reopens the app, and opening another page after Back replaces the forward path.

Activity → Clear removes completed history without resetting app installation state.

App details include Changelog, Add-ons, Permissions, Ratings, and Links in that order. Ratings show ODRS stars (five or more reviews), Flathub installs, pkgstats share and AUR votes, each with its source; rows without data are hidden. Opening an app sends its Flatpak ID to odrs.gnome.org and flathub.org; answers are cached for a day. Click an add-on row to read its details; Install starts its simulated installation directly. Publisher links open in the default browser. Flathub's packaging disclaimers are left out of descriptions.

Explore starts with filters collapsed and the compact featured carousel. Nine categories combine Education and Science; each app joins one group from its AppStream categories.

Use the wheel over Explore’s featured card, Install button, or indicators to switch slides. Mouse clicks do not keep autoplay paused after the pointer leaves; rotation resumes on a fresh five-second countdown.
