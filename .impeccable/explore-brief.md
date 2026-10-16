# Explore prototype

Mode: Operate. Native Qt 6 desktop. Preserve Quickpeek-style KDE chrome and the collapsible sidebar.

THESIS: Let people discover apps through categories, artwork, compact app cards, and a predictable alphabetical directory.

OWN-WORLD: Native search, filter controls, and sidebar without a dividing line. Six dedicated category colors. Palette-derived app surfaces, real app icons, and existing publisher screenshots. No generated artwork.

STORY: Search or choose a category, browse featured apps and suggestion sections, then use the ranked directory. Any whole-card activation opens a separate local app-detail page; installation is not connected.

FIRST VIEWPORT: Search and a separate Filters button, an expandable panel with source/sort controls and six colored category cards, then a featured carousel with no heading, arrows, or counter; clickable pagination dots sit beneath it. The carousel has app copy on the left and a publisher screenshot on the right, faded into the card surface with a radial fade from the top and a continuous left join. Compact discovery cards follow beneath it.

FORM: Precisely specified composition, inherited native visual world. No random concept seed or comp tournament applies. Discovery uses a shortest-column layout with card height calculated from actual text. The directory always stays A–Z even when discovery is sorted differently.

## Prototype truth

- Live merged catalogue from Flathub, pacman repositories, and the AUR.
- Up to five featured apps from the Flathub popular ranking.
- Search, categories, sorting, source filtering, clickable pagination dots, detail-page navigation, and empty states are local interactions.
- AUR has an explicit not-connected state.
- Explore widgets survive navigation and resize; backend callbacks cannot replace this page.
- Icons come from cached AppStream data; screenshots download from publisher URLs into a disk cache.

## Review evidence

Desktop and narrow-desktop top viewports; discovery collection and alphabetical directory captures; the second and third carousel slides. No browser detector applies to Qt Widgets.

## App details

Reference: https://flathub.org/en/apps/org.mozilla.firefox. Adapt its prominent icon/name and screenshot presentation to native KDE controls. Preserve the requested order: header and description, publisher screenshot, score, permissions. Ratings are unavailable and show Not rated. Permissions come from installed package snapshots when present; missing metadata stays explicitly unavailable. Back restores Explore without rebuilding its search or scroll position.

## Shell and navigation

Omit the themed content-top frame. Sidebar and content share the desktop window background. Sidebar toggle is 40 px high with 16 px top inset, aligned with the Explore heading. Navigation rows are neutral with a faint selected/hover wash; theme icons are tinted with WindowText in both window states. All sidebar activation paths explicitly switch the stack, including activation of an already-selected Explore row. Regression checks send mouse events through the sidebar viewport for Installed/Updates/Explore at three widths, in expanded and compact states, and verify the Explore collection remains visible after queued backend events.

## App gallery refinement

Replace manually painted sidebar rows with native QToolButton controls: 40 px height, 20 px theme icons, matching toggle width, icon-only when compact, icon plus text when expanded. Remove Settings. Preserve explicit page switching and persisted sidebar size. Full base-language publisher descriptions span the entire detail content column to the page margins, without a 920 px maximum; do not truncate paragraphs. Score and permissions each receive a separate neutral background panel with 24 px padding. Use up to four real publisher screenshots per app, an animated circular perspective gallery with neighboring images visible at either side, clickable dots, wheel/trackpad scrolling, dragging, and Left/Right keys. Retain honest unavailable metadata states.

## Directory and Installed lists

Shared AppList uses rounded 10 px panel background, one 14 px panel inset matching discovery cards, no additional row margins or view spacing, 40 px icons and 12 px icon/text gap. Native labels wrap at available width; rows fit their content. Names are bold and the list remains A–Z. Installed inherits Explore heading23pt, page20/16/20/24 margins and spacing16, separate search/sourcecontrols, existing Add AppImage andRefresh actions. Each Installed row opens existing management for its exact source identity; sample preview rows only open offline details. The app screenshot gallery now uses flat rectangular previews without perspective distortion; retain sliding motion, side previews, dots, and existing inputs.

## Updates and selection padding

Updates inherits the 23 pt heading and 20/16/20/24 page margins with 16 px section gaps from Explore/Installed. Header actions remain Check for updates and Review & update. A compact CachyOS summary names System/AUR/Flatpak; a rounded results panel contains the read-only command output, a brief placeholder, and Not checked/Checking/Check complete/Check failed status. Both panels use 14 px padding. Refresh Flatpak catalogue stays separate. Longer explanations move off the page; update confirmations and failure output remain actionable. Shared list rows add only 8 px horizontal inner margins inside selection/hover backgrounds, retaining 14 px outer panel padding and compact vertical geometry.

## Preview actions and activity

Both carousels use short rounded bars (24 × 7 px) and a longer active bar (80 × 7 px), with no button panels. The app gallery is a flat horizontal strip with equally sized image bounds, side peeks, circular navigation, and no pointer-focus selection border. Keyboard focus remains visible. Keep all existing publisher artwork.

App details adds Install and a source menu in its top toolbar. The source menu lists every package of the app with source, repository or package name, and version. Install animates through Installing… to Open. Open does not launch an application. The A–Z directory adds the same quick action at the right. Installed rows put platform, origin, scope, and version once beneath the name; right actions are Remove and Reinstall. All new app actions are simulations, share per-app state, and leave packages untouched.

Updates uses the remaining page height for Available updates. A separate bottom panel has a full-width 44 px Update all action, with 32 px Check for updates and Review updates actions below. Existing real update confirmation remains required.

Activity moves into a modeless popup opened by a bottom sidebar control. Compact mode shows an icon and a progress arc while active; expanded mode adds Activity and a progress line. Simulations provide percentages; real terminal operations show indeterminate progress. Popup rows expose action, state, percentage, and Preview labels, plus embedded package-manager prompts when a real operation is running. Closing the popup keeps the task alive. Closing the main store remains blocked during package operations.

## Mosaic and file preview refinement

Expanded Activity uses the progress line with a plain icon; compact mode retains its progress arc. A native 1 px sidebar divider appears only when expanded. Source uses a native split menu button so its drop-down arrow remains visible.

The discovery mosaic uses filled bands, 12 px gaps, and existing text insets. Alternate larger screenshot cards with paired smaller cards so every band fills its width without empty cells. Large cards highlight Bottles, GIMP, Obsidian, and OBS Studio using bundled publisher screenshots; never promote apps already in the top carousel. Large cards alternate position and span one or two base columns. Preserve stable app choices while browsing. Load more sits below A–Z; the bundled catalogue already contains all 18 samples, so clicking shows All apps loaded rather than fetching or inventing apps.

Installed app clicks open the main Explore detail page for both known and unknown apps, with an available icon or a theme fallback. No Installed management popup appears. Source choices expand for additional platforms shown in Installed.

Installed adds Add Flatpak beside Add AppImage and Refresh. Both file pickers and the entire Installed drop surface preview local .AppImage, .flatpak, and .flatpakref files. Reject unsupported types and remote URLs. Queue accepted files without another confirmation, show preview entries and shared progress, suppress duplicate paths, and keep preview entries after inventory refresh. No files are copied, extracted, executed, or actually installed.

Update all and Review updates open a modal Confirm updates preview with masked Sample password field, Cancel, and Accept updates. Accept stays disabled while empty. Use sample text only; clear the input on close, and simulate update progress without system authorization or real updates. Check for updates retains its existing read-only operation.
