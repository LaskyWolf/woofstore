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

## Current Prototype

Explore uses a fixed offline catalogue of 18 sample Flathub apps. Search, filters, sorting, featured slides, a stable irregular discovery mosaic, and the A–Z directory operate on those samples. Load more changes to All apps loaded because every bundled sample is already visible.

Installed reads the backend inventory and merges session-only file preview rows. Every row opens the main Explore detail page. Known apps use bundled publisher metadata; unknown apps use available metadata and an icon fallback with explicit unavailable states. Remove, Reinstall, Install, and Open are simulations; they change no packages and launch no apps.

Add AppImage, Add Flatpak, and drops anywhere on Installed accept local readable files with .AppImage, .flatpak, or .flatpakref extensions, case-insensitively. This checks file location, readability, and extension only. Accepted files queue previews without another confirmation. Canonical paths suppress duplicates. Preview rows survive inventory refresh for the current session. No files are copied, extracted, executed, or installed.

Check for updates runs the read-only `arch-update --list` command. Update all and Review updates open Confirm updates with a masked Sample password field. Accept updates requires nonempty sample text, clears it, and starts simulated shared progress. Cancel also clears the field. No system authorization or real update command occurs. The harmless embedded terminal demo remains available.

Activity presents shared preview progress and history in a modeless popup. Expanded navigation uses a plain icon and progress line with a native sidebar divider; compact navigation uses an icon progress arc without the divider.

## Future Capabilities and Constraints

- Connect configured Flatpak sources and cached AppStream metadata to live discovery.
- Keep each software source explicit.
- Download AUR build files without executing them and require review before building.
- Never describe automated AUR checks as proof of safety.
- Import AppImages into user storage and add desktop launchers without executing the files.
- Connect Cachy-Update and its interactive authentication within the store.
- Preserve package-manager prompts and refuse concurrent package operations.
- Keep the graphical application running as the normal user.

Backend import and AUR helpers exist but are not connected to this prototype interface.

## Brand Commitments

Use existing publisher images and app icons for app artwork. AI-generated images are permitted only for design mockups, never as application assets.

Use GNOME Software-style app discovery with persistent KDE navigation. Inherit the user's KDE theme, colors, fonts, and icons.

## Evidence on Hand

The development environment has Cachy-Update 4.4.2 at `/usr/bin/arch-update` and `/usr/bin/cachy-update`, plus KonsolePart, Qt 6, Flatpak, and paru. These installed tools do not imply that prototype actions execute them.

## Open Decisions

Public distribution, cross-distribution support, and a fully graphical replacement for terminal prompts remain outside the current prototype.

App detail pages show the latest available cached publisher changelog instead of scores. Visible pages omit Preview badges and footers. Simulated actions remain unconnected to package changes.
