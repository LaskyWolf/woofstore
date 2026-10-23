# WoofStore

<!-- impeccable:product-schema 1 -->

## Platform

Native Linux desktop, initially CachyOS on KDE Plasma.

## Stack

Qt 6 Widgets and KDE Frameworks. KonsolePart provides an embedded terminal without a custom terminal emulator.

## Users

A CachyOS desktop user who wants graphical software discovery and maintenance on their own computer.

## Product Purpose

Discover software, inspect app details, and manage installed software within one native KDE window.

## Current State

Explore shows the real catalogue: Flathub, every pacman repository including CachyOS, and the AUR. The same app from several sources is one entry with a source menu. Search, categories, featured apps and suggestion sections use this catalogue and daily popularity rankings from Flathub and pkgstats. App details show the publisher description, screenshots, latest release, add-ons, links, ratings, and permissions of installed Flatpaks. Install, Reinstall and Remove run the real package manager; AUR builds require a review and run in a clean chroot.

Installed lists the apps in the application menu like the Start menu, with the source of each (Flatpak, Arch, AUR / local, AppImage, Other). Add AppImage and Add Flatpak accept files by click or drop and use the normal install window.

Updates checks Arch, AUR, Flatpak and AppImage updates at start and on demand. System Update updates everything in one run: a full pacman upgrade, then the AUR, Flatpak and AppImages, then cache cleanup and a list of services to restart. The run continues if the store closes or crashes, and the store restarts itself after a system upgrade. A failed system upgrade stops the run. A new kernel triggers a desktop notification that offers to restart.

News shows Arch Linux, CachyOS, Linux kernel, KDE, LWN.net, 9to5Linux and Phoronix items in two columns; unread Arch Linux and CachyOS items badge the sidebar.

Activity shows package operation history; each entry reopens its progress window. The header of Explore, Installed, Updates and News stays at the top while the page scrolls.

## Constraints

- Keep each software source explicit.
- Download AUR build files without executing them and require review before a first build.
- Never describe automated AUR checks as proof of safety. AUR updates skip the review step and say so.
- Import AppImages into user storage and add desktop launchers without executing the files.
- Preserve package-manager prompts and refuse concurrent package operations.
- Keep the graphical application running as the normal user; privileges come from polkit.

## Brand Commitments

Use existing publisher images and app icons for app artwork. AI-generated images are permitted only for design mockups, never as application assets.

Use GNOME Software-style app discovery with persistent KDE navigation. Inherit the user's KDE theme, colors, fonts, and icons.

## Replaces

WoofStore is meant to replace Cachy-Update (arch-update) and Shelly. Missing before both can be removed: background update checks with a tray icon and notification, listing every installed package (not only apps with a launcher), downgrade, hold and ignore marks, `.pacnew` handling, orphan and cache cleanup, news before the update.

## Open Decisions

Cross-distribution support and a graphical replacement for terminal prompts remain outside the current scope.
