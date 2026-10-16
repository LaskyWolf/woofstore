# Cachy Store

A native Qt 6 / KDE store for CachyOS with a persistent sidebar and Quickpeek-style native controls and subtle trays.

## Build and run on CachyOS

Install the dependencies:

```bash
sudo pacman -S --needed base-devel cmake qt6-base qt6-svg kcoreaddons kparts konsole breeze-icons appstream-qt flatpak git cachy-update
```

Build from this directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
./build/cachy-store
```

Run the store as your normal user. Package managers request elevated privileges only when needed.

If no Flatpak catalogue is available, configure a remote first:

```bash
flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak update --user --appstream
```

Restart the store after refreshing the catalogue.

## Usage

Use the top-left button to collapse or expand the sidebar. Compact mode keeps tooltips and keyboard navigation. The sidebar size is remembered. Settings is currently omitted; source filters remain on Explore.

- **Explore:** an offline visual prototype with 18 sample Flathub apps. Search, category/source/sort filters, three spring-animated featured slides with playful bar indicators and KDE animation-speed support; they rotate every five seconds and pause on hover, keyboard focus, or when offscreen, whole-card detail pages, a gap-free mosaic with larger publisher-screenshot cards for apps outside the featured carousel, and a compact rounded A–Z list are interactive. It uses bundled app icons and existing publisher screenshots. It does not search live catalogues or install apps. AUR is not connected; selecting it shows an explicit empty state. Load more is a preview control; all bundled apps are already visible.
- **Installed:** list Flatpaks, foreign Arch packages, and AppImages imported by this store. Foreign packages are labeled **AUR / local** because `pacman -Qm` does not prove AUR origin. Official repository packages are updated by Cachy-Update but are not listed here in this version.
- **Updates:** check available updates and preview update acceptance. Update all and Review updates open a sample-password popup and simulate progress. These actions do not authorize or start real updates.
- **Activity:** inspect simulated progress and history in a popup. The embedded-terminal demo also supports ordinary and hidden input. No package operation is started by the current file or update preview controls.

## Local file installation previews

Use Add AppImage, Add Flatpak, or drop local `.AppImage`, `.flatpak`, and `.flatpakref` files anywhere on Installed. Accepted files become clearly labeled preview rows with simulated installation progress. Duplicate paths are suppressed. Unsupported types and remote URLs are rejected. Preview rows survive inventory refresh for the current session.

No files are copied, extracted, executed, or actually installed. Click any Installed row to open the main Explore detail view. Missing publisher metadata uses an explicit unavailable state and a desktop icon fallback.

## Update authorization preview

Update all and Review updates open Confirm updates. Enter sample text in the masked field; Accept updates starts simulated progress. Cancel and accept clear the field. No password is sent to sudo, a package manager, or a system authorization service. Check for updates retains its read-only Cachy-Update check.

AUR searching, recipe review, and real installations remain unconnected to this prototype interface.

## Preview and harmless terminal test

```bash
QT_QPA_PLATFORM=offscreen ./build/cachy-store --screenshot /tmp/cachy-store.png
./build/cachy-store --terminal-demo
QT_QPA_PLATFORM=offscreen ./build/cachy-store --explore-check
QT_QPA_PLATFORM=offscreen ./build/cachy-store --terminal-check
QT_QPA_PLATFORM=offscreen STORE_LIVE_TESTS=1 ./build/store-tests
```

The terminal demo asks for ordinary and hidden input without installing software. The automatic terminal check verifies both prompts. The optional live check searches the AUR, downloads a recipe into a temporary directory, and confirms that modifying it invalidates review. Neither check installs packages. Explore previews also support `--carousel-slide 1` and `--explore-bottom`. Desktop previews can use `--page installed`, `--page updates`, or `--compact`.

## Install a launcher

Optional installation for your user account:

```bash
cmake --install build --prefix "$HOME/.local"
```

The desktop launcher calls `cachy-store`; ensure `$HOME/.local/bin` is on your desktop session's PATH.

App cards open an offline detail page inspired by Flathub: icon, publisher description collapsed to six lines with Show more / Show less, screenshot carousel, latest changelog, and permission panel. Release notes come from the bundled Flathub AppStream snapshot; missing notes are stated explicitly. Permission rows are cached installed-package snapshots and are not a security verdict. Use Back or Alt+Left to return to Explore.

Check the preview description language selector with `python tools/refresh-descriptions.py --check`. Refresh descriptions with `python tools/refresh-descriptions.py /path/to/appstream.xml.gz assets/details.json`, then rebuild to bundle them.

Browse app screenshots with the bar indicators, Left/Right keys, or deliberate dragging. Wheel and trackpad input scroll the page without switching images. Neighboring flat screenshots remain visible while the current image animates into place. Only publisher screenshots are bundled.

Installed uses the same compact A–Z list as Explore. `--installed-preview` shows sample data and opens only offline app details. Installed row clicks open the main Explore detail view. Remove and Reinstall buttons simulate actions without changing packages.

Updates fills the page with available updates. Its bottom action panel contains Update all, Check for updates, and Review updates. Update all and Review updates open the sample-password preview; no real update command runs. `--updates-preview idle`, `checking`, or `failed` captures labeled sample states without starting a check. `--list-selected` previews selection padding. UI checks use sample inventory rather than querying installed packages.

App details and the Explore A–Z list have simulated Install buttons. Clicking changes Install to Installing… and then Open; Open does not launch an app. Source choices (Flatpak and AUR) demonstrate the menu only. Progress is shared across buttons for the same app during this session.

The bottom sidebar Activity control opens a modeless popup with activity history and embedded terminal prompts. Collapsing the sidebar keeps an activity icon and progress arc; expanding it adds a progress line. Preview tasks show percentages. Real package-manager progress is indeterminate. Closing this popup does not stop the operation.

Use `--prototype-check` to test simulated install, source selection, remove, reinstall, and activity popup behavior without package operations. Use `--activity-preview` for a sample popup, `--activity-sidebar` for sidebar progress, or `--details-app org.kde.krita --install-preview` to preview the installing state. Screenshot previews do not load live inventory.

The expanded sidebar shows a divider and uses an Activity progress line with a plain icon. Compact mode removes the divider and keeps the activity progress arc. The source control has a separate drop-down arrow.

Capture the update popup with `--updates-preview idle --authorization-preview --screenshot /tmp/authorization.png`. `--file-preview /path/to/file.AppImage` previews a selected local file without changes; `--installed-open` opens its main detail page when combined with that flag. `--explore-end` captures Load more below the A–Z list.

Filter, sidebar, and description toggles animate smoothly using KDE’s animation speed. Hover or keyboard-focus a mosaic card to expand it into a publisher-image preview above the existing mosaic; click it to open details. The preview leaves the mosaic geometry unchanged. Capture it with `--mosaic-hover org.gnome.Builder --screenshot /tmp/hover.png`.

Expanded hover cards dissolve when the pointer leaves; returning during the fade keeps them open. Escape or scrolling closes them immediately.

Featured and expanded hover cards offer quick Install buttons. They share the simulated Install → Installing… → Open flow with app details and list rows; no software is downloaded or installed.

Back and Show more/less use native button shapes. Pages omit Preview badges and metadata footers. Refreshing descriptions also refreshes the newest cached stable release’s notes, version, and date.

Mouse Back and Forward navigate page history. App details opened from Installed return to Installed; Explore details return to the previous Explore position. Forward reopens the app, and opening another page after Back replaces the forward path.

Drag supported local files over Installed to see a full content drop overlay with their formats and filenames. Dropping uses simulated installation. Activity → Clear removes completed history without resetting app installation state.

App details include Changelog, Add-ons, Permissions, and Links in that order. Click an add-on row to read its details; Install starts its simulated installation directly. Publisher links open in the default browser.

Explore starts with filters collapsed and the compact featured carousel. Nine Flathub categories combine Education and Science; category matches use the bundled publisher metadata.

Use the wheel over Explore’s featured card, Install button, or indicators to switch slides. Mouse clicks do not keep autoplay paused after the pointer leaves; rotation resumes on a fresh five-second countdown.
