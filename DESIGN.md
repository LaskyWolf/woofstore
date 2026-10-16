---
name: Cachy Store
description: Native KDE software discovery and maintenance
colors:
  category-create: "#674296"
  category-play: "#a24465"
  category-work: "#315c91"
  category-develop: "#356b58"
  category-connect: "#8d5134"
  category-utilities: "#5c567e"
typography:
  page-title:
    fontSize: "14pt"
    fontWeight: 700
  app-detail-title:
    fontSize: "16pt"
    fontWeight: 700
  section-title:
    fontWeight: 700
  app-name:
    fontWeight: 700
  updates-title:
    fontSize: "23pt"
    fontWeight: 700
  installed-title:
    fontSize: "23pt"
    fontWeight: 700
  explore-title:
    fontSize: "23pt"
    fontWeight: 700
  explore-section:
    fontSize: "12pt"
    fontWeight: 700
  hero-title:
    fontSize: "22pt"
    fontWeight: 700
  hero-title-narrow:
    fontSize: "18pt"
    fontWeight: 700
  hero-caption:
    fontSize: "13pt"
  prototype-detail-title:
    fontSize: "28pt"
    fontWeight: 700
  prototype-detail-summary:
    fontSize: "12pt"
  activity-title:
    fontSize: "23pt"
    fontWeight: 700
  confirmation-title:
    fontSize: "18pt"
    fontWeight: 700
  metadata:
    fontSize: "10pt"
rounded:
  update-panel: "12px"
  app-list: "10px"
  explore-tile: "10px"
  explore-hero: "10px"
  category: "9px"
  navigation: "8px"
  detail-panel: "12px"
spacing:
  section-gap: "12px"
  panel-padding: "16px"
  content-inline: "18px"
components:
  app-list:
    rounded: "{rounded.app-list}"
    padding: "14px"
  explore-tile:
    rounded: "{rounded.explore-tile}"
  explore-hero:
    rounded: "{rounded.explore-hero}"
  carousel-dot:
    width: "28px"
    height: "36px"
  carousel-dot-active:
    width: "84px"
    height: "36px"
  pagination-bar:
    width: "24px"
    height: "7px"
  pagination-bar-active:
    width: "80px"
    height: "7px"
  preview-install:
    height: "36px"
  update-all:
    height: "44px"
  update-actions:
    rounded: "{rounded.detail-panel}"
    padding: "14px"
  activity-control:
    height: "48px"
  navigation-item:
    height: "40px"
    rounded: "{rounded.navigation}"
    padding: "10px"
  detail-panel:
    rounded: "{rounded.detail-panel}"
    padding: "24px"
  sample-password:
    height: "36px"
  update-confirmation:
    width: "420px"
    height: "220px"
    padding: "16px 20px 20px"
  load-more:
    height: "32px"
---

# Design System: Cachy Store

## Overview

**Creative North Star: "Native KDE software discovery"**

Use Quickpeek’s native KDE visual language with persistent store navigation. Native Qt Widgets and KDE Frameworks provide the controls and embedded operation terminal. Inherit the desktop's font family, colors, icon theme, and control style.

**Key Characteristics:**

- Persistent navigation beside scrollable software discovery.
- Bundled application icons and publisher screenshots in the offline Explore prototype.
- Native dialogs and package-manager prompts inside one window.

### Direction contract

**THESIS:** Discover sample apps through categories, publisher screenshots, compact cards, and a predictable alphabetical directory.

**OWN-WORLD:** Preserve Quickpeek-style KDE chrome, system fonts, native controls, and the collapsible sidebar without a dividing line. Explore adds six dedicated category colors over palette-derived app surfaces.

**STORY:** Search or choose a category, inspect three featured apps, browse the discovery collection, then use the A–Z directory. Explore activation opens a separate local app-detail page. New app actions simulate progress without changing packages or launching apps. Installed rows use the same main detail page. File and update actions remain previews; Check for updates retains its read-only command.

**FIRST VIEWPORT:** Native search and a separate Filters button precede an expandable source/sort panel and nine colored category buttons. A three-slide carousel follows with app name and short caption left, publisher screenshot right, a radial fade from above, and a continuous leading-edge join. Only clickable pagination bars sit beneath the carousel. Stable mixed-width and mixed-height discovery cards and the always-A–Z directory follow.

**FORM:** Precisely specified Explore composition within the user-pinned Quickpeek visual language. No concept-seed run or comp tournament applies. The surface contract is recorded in `.impeccable/explore-brief.md`.

**FINISH:** Verify desktop and narrow Explore viewports, discovery cards, the directory, and all three carousel slides. Keep asset origins and design documentation consistent. The current refinement review covers 13 captures under `.impeccable/review/refine-*.png`; its disposition is ship after documentation sync. Build and all four tests passed. Real package transactions remain untested.

**Signature interaction:** Whole Explore tiles open a separate app-detail page. Back or Alt+Left restores Explore without rebuilding its filters, carousel, or scroll position. Clickable pagination bars switch the three featured slides. Installed and Updates share preview progress in Activity. The embedded terminal remains available for the harmless demo.

**Motion grammar:** Preserve native control behavior. App-detail screenshot changes use a 300 ms OutCubic QVariantAnimation, with circular wrap and flat rectangular neighboring previews.

## Colors

Shell and text colors use runtime QPalette roles. Explore category colors are the fixed frontmatter tokens.

### Primary

Native sidebar buttons use Button on hover and Midlight when checked. Highlight supplies their keyboard-focus border.

### Secondary

Create, Play, Work, Develop, Connect, and Utilities use their respective category tokens. Category buttons use white text. Explore tiles apply their category color as a translucent wash: alpha 22 on discovery tiles and alpha 48 on heroes.

### Neutral

Window supplies the uniform shell. The root fills this background; the sidebar and navigation host remain transparent. The themed content-top frame is omitted. Button at alpha 150 supplies Tray fills; Mid at alpha 90 supplies faint one-pixel edges. Explore and Installed use native framed search fields. The shared application list uses AlternateBase for its single background, Button for hovered rows, and Midlight for selected rows. Navigation Base equals Window. Default text inherits its widget palette. Active and Inactive PlaceholderText use their corresponding Text color; Disabled remains inherited.

**The Theme Inheritance Rule.** Resolve shell and text colors from QPalette and preserve system light and dark themes. Fixed category colors are limited to Explore category controls and tile washes.

## Typography

All proportional text inherits the desktop font family. The frontmatter records the repeated explicit sizes and bold roles. Body text retains the desktop's default size. AUR build-file text uses QFontDatabase::FixedFont.

Labels wrap and render as plain text. Shared list summaries wrap naturally and determine each row height. Explore discovery tiles use wrapped summary text as a minimum height; deterministic grid spans and free-cell fill add height. Hero titles use the narrow role below a 650 px tile width. No separate display font ships.

**The System Font Rule.** Preserve the desktop family and native control typography when extending the app.

## Layout

The initial window is 1180 × 850 px; its minimum is 720 × 580 px. The sidebar is 156 px wide when expanded and 56 px wide when collapsed. Its layout has 8 px side and bottom margins, a 16 px top margin, and 16 px spacing. The 40 × 40 px collapse toggle aligns with the Explore title. Explore, Installed, and Updates use margins of 20 px left and right, 16 px top, and 24 px bottom, with 16 px section gaps.

Installed uses one shared A–Z list at every desktop width. Explore discovery retains its content-driven multi-column geometry. No mobile layout is implemented.

Explore uses 20 px horizontal margins, 16 px top and 24 px bottom margins, and 16 px section gaps. Categories use three columns and 10 px gaps. Discovery uses fully filled bands with 12 px gaps and two to five base columns, calculated from the container width divided by 185 px. Each band partitions its full width into a large publisher-screenshot card and paired smaller cards, or a compact row when no spotlight candidate is present. Large cards alternate position and span one or two columns. Bottles, GIMP, Obsidian, and OBS Studio are eligible spotlight apps; carousel apps are never spotlights. Band height fits every card's wrapped copy. No empty grid cells remain. Resizing recalculates geometry without reshuffling app choices. The alphabetical directory remains A–Z regardless of discovery sorting. The carousel is visible only without a query or category and when AUR is not selected. Its height is 220 px below a 650 px viewport width and 246 px otherwise. Installed puts Add AppImage, Add Flatpak, and Refresh beside its title. A centered Load more control follows the A–Z directory; it disables as All apps loaded without fetching. Updates places Update all, Check for updates, and Review updates in a separate bottom panel.

The sidebar contains a collapse button and Explore, Installed, and Updates buttons. Compact mode uses icon-only buttons, retains accessible names and tooltips, and preserves the current page. QSettings preserves the compact-sidebar preference. Source filtering remains in Explore. A bottom sidebar control opens a modeless Activity popup with history and embedded KonsolePart prompts. Closing the popup hides it without stopping the task.

## Elevation & Depth

Trays use translucent palette surfaces and a faint one-pixel Mid border. No custom shadows ship. Native dialog and control depth remains supplied by the desktop style.

## Shapes

Updates retain the update-panel radius. Shared application lists use the app-list radius. Explore discovery tiles and heroes use their respective radii; category controls use the category radius. Native sidebar buttons use the navigation radius. Changelog and permissions panels use the detail-panel radius. Cards are rectangular containers around application icons and text. Search, buttons, selectors, checkboxes, and dialogs retain native shapes.

## Components

### Buttons

QPushButton supplies focus, hover, pressed, and disabled rendering. Shared buttons have a minimum height of 32 px and themed icons. Sidebar collapse and navigation use QToolButton controls; the collapse button is 40 × 40 px. Do not describe a custom primary-color button variant; none is implemented.

### Shared application list

Explore's alphabetical directory and Installed share AppList. One rounded AlternateBase panel has a single 14 px inset matching ExploreTile's text and image inset. Rows add only 8 px horizontal margins inside hover and selection backgrounds. Vertical row margins and view spacing remain zero.

Each row has a 40 px icon, a 12 px gap, and vertically centered name and summary with a 5 px text gap. Native labels wrap and set content-driven heights with a 56 px minimum. A delegate suppresses duplicate default item text and icons. Mouse and keyboard activate the entire row. List height includes the final row and the panel's top and bottom inset. Fix each row width to the current viewport before measuring its height, including after outer-scrollbar and sidebar changes.

Explore directory rows place Install preview on the right. Installed rows show source, remote, scope, and version once beneath the name, with Remove and Reinstall preview actions on the right. All new row actions share per-app preview state and change no packages. Installed sorts A–Z after search and source filtering. Each item stores its exact merged-inventory index so filtering and sorting preserve detail identity. Backend inventory and session-only file previews share that merged list. `--installed-preview` labels sample content and opens offline details.

Updates results and bottom actions use neutral DetailPanel surfaces with the update-panel radius and 14 px internal padding.

### Updates

Match the Explore and Installed heading size, page margins, and section spacing. Available updates expands into the remaining page height. Omit the former CachyOS summary. Results use WindowText at alpha 14, rounded update-panel corners, and 14 px padding.

The results header combines Available updates at 12 pt with Not checked, Checking…, Check complete, or Check failed. Read-only QPlainTextEdit has a transparent background, no frame, and a 220 px minimum height. Keep placeholders brief.

A separate bottom DetailPanel uses 14 px padding and a 10 px gap. Update all spans the panel width, has a 44 px minimum height, and is the default native action. Check for updates and Review updates have 32 px minimum heights in the row below. Disable all three while checking; re-enable them on callback completion. Update all and Review updates open the sample-password confirmation and start simulated progress after acceptance. During the preview, all three controls are disabled; the primary reads Updating…, followed by Preview complete and explicit no-system-changes output. No real update command or authorization runs. Preserve actionable failure output. No catalogue-refresh action appears on this page.

`--updates-preview` provides labeled idle, checking, and failure samples. UI-check constructors and screenshot preview flags disable live inventory loading; the normal app retains live inventory. Preview verification does not validate real update transactions and starts no actual updates.

### Inputs / Fields

Explore and Installed use native framed QLineEdit controls with search icons, clear buttons, accessible names, and 38 px minimum height. Installed's source selector remains a separate QComboBox with a 32 px minimum height; its search row uses zero margins and an 8 px gap. Add AppImage, Add Flatpak, and Refresh remain beside the Installed title. Read-only QPlainTextEdit displays update output and AUR build files. Preserve native focus, disabled states, and readable palette-derived placeholders.

### Explore prototype

The fixed catalogue contains 18 sample apps. Search, source/category filters, sorting, and empty states are local. AUR selection displays a not-connected state. Explore widgets survive navigation and resizing; backend callbacks do not replace the prototype.

Search and Filters have 38 px minimum heights. The filter panel initially opens and groups native source/sort selectors, Clear filters, and six category buttons of at least 58 px height. Category controls include 22 px themed icons tinted white. Only one category is selected at a time.

Three whole-click heroes feature Krita, Blender, and Kdenlive. Copy occupies the left 46 percent of each tile; cropped publisher imagery fills the remainder. A radial surface wash starts above the image at its leading edge. A horizontal join gradient spans the leading 32 percent of the image width, keeping the boundary continuous. Hero copy contains only the app name and short caption. No carousel heading, arrows, counter, View app button, or source footer is present.

Pagination bars have the frontmatter inactive and active hit-target dimensions, accessible slide names, and keyboard focus. Their resting and hover appearance has no button panel; keyboard focus uses an explicit outline. Marks have fully rounded ends and use the frontmatter bar dimensions. Active marks use Highlight; inactive marks use WindowText at alpha 130. Heroes and discovery tiles use Highlight borders for hover or focus.

The Explore title uses the explore-title token; no prototype-count badge appears beside it.

Explore icons and 62 publisher screenshots across 18 app galleries are bundled for offline operation. `assets/provenance.json` records their Flathub AppStream origins; PNG files carry embedded origin metadata. No generated artwork is included.

### Navigation

Three native QToolButtons provide Explore, Installed, and Updates navigation. An exclusive QButtonGroup preserves one checked page. Buttons use 20 px theme icons, the navigation-item dimensions, and 8 px vertical gaps. Checked and hover states use neutral palette surfaces; keyboard focus uses a Highlight border.

Expanded buttons place text beside icons. Compact buttons show icons only while preserving accessible names and tooltips. Every activation explicitly switches the page stack, including an already-selected Explore button. Explore remains intact after queued backend callbacks. Settings is absent; the sidebar toggle persists its compact preference.

### Prototype app details

Adapt the prominent app header and screenshot presentation of the Flathub Firefox page to native KDE controls. The separate scroll page has 28 px horizontal margins, 16 px top and 32 px bottom margins, 24 px section spacing, and a content column spanning the viewport width minus its 56 px horizontal margins.

Preserve this order: header, description, publisher screenshot, Latest changelog, Permissions. The header uses a 96 px app icon, the prototype-detail-title token, and the prototype-detail-summary token. The description is selectable plain text spanning the content column. Preserve all publisher paragraphs and list entries; do not truncate to two paragraphs. Cached descriptions use base English publisher metadata; `tools/refresh-descriptions.py --check` checks base-language selection and paragraph preservation in parser fixtures.

Each app gallery contains up to four real publisher screenshots. Every screenshot retains its aspect ratio within equal bounds of 76 percent of widget width and 96 percent of widget height. Images form a flat horizontal strip with 12 px gaps and neighboring side peeks. Mouse focus adds no selection outline; keyboard focus adds a Highlight underline. Images have no perspective tilt or skew. The gallery wraps circularly and responds to clickable pagination bars, deliberate dragging, and Left/Right keys. Wheel and trackpad input pass through to page scrolling without switching images. Its height follows half its width, clamped between 220 and 420 px. Missing imagery has an explicit unavailable message. Latest changelog and Permissions occupy separate neutral DetailPanel surfaces, using WindowText at alpha 14 and the detail-panel radius and padding. Latest changelog displays Not rated with unrated theme icons; no numeric score is invented. Permissions show an installed-package snapshot with a version caveat when present, or an explicit unavailable message. The top toolbar includes an Install preview button and Flatpak/AUR source menu beside Back, both using the preview-install minimum height. A native QToolButton MenuButtonPopup keeps the drop-down arrow visible; clicking its main area also opens the menu. Flatpak and AUR are initial preview choices; detail binding adds an additional platform entry when needed. Source availability is not checked. Install progresses through Installing… to Open. Open reports a preview without launching an application. Back and Alt+Left restore the existing Explore page state.

### Installed details and file previews

Every Installed row opens the main Explore detail page. Bundled app IDs reuse publisher metadata. Unknown apps use the available icon or a native executable fallback, with unavailable screenshot and permission sections hidden. No separate Installed management popup appears.

Both file pickers and the Installed drop surface accept local readable files with case-insensitive AppImage, flatpak, or flatpakref extensions. Remote URLs and unsupported files are rejected. This gate does not inspect the file format. Accepted files start shared simulated progress without another confirmation. Canonical paths suppress duplicates. Preview rows merge with backend inventory and survive Refresh for the current session; no file is copied, extracted, executed, or installed.

### Update confirmation preview

A modal native Confirm updates dialog starts at the update-confirmation dimensions, with 20/16/20/20 px margins and 16 px gaps. Its title uses the confirmation-title role. A masked Sample password field uses the sample-password minimum height and an accessible preview label. Cancel and Accept updates use native dialog controls. Accept stays disabled until the field is nonempty. Acceptance and cancellation clear the input; acceptance only starts shared simulated update progress. The field never authorizes a system operation.

### Preview actions

Details, Explore directory, and Installed row actions share one per-app PreviewOperations controller. Progress advances by 4 percentage points every 80 ms. Pending actions disable the app's bound buttons. Install reaches Open; Remove reaches Removed; Reinstall remains available after completion. State and history remain in memory for the current session. Simulations change no packages and launch no applications.

### Activity popup

The bottom sidebar Activity control uses the activity-control height, a 20 px themed icon. Expanded mode shows a plain icon, Activity, and a 3 px progress line. Compact mode shows the 14 px radius progress arc while busy and omits the line. Palette hover and keyboard-focus states remain visible.

A modeless QDialog starts at 800 × 600 px. It uses the shared page margins and 16 px section gaps, with the activity-title role. History uses native rounded rows, 76 px high, with 14 px horizontal and 10 px vertical padding, 8 px inter-row spacing, and a 4 px progress bar. Preview rows show app, action, state, percentage, and Preview. Embedded terminal activity uses indeterminate progress and KonsolePart prompts below history; the current UI connects only the harmless demo to that terminal.

Closing the popup hides it and leaves the task running. Main-window closing is blocked while a real package operation is active. Closed real-operation history says Ended and asks the user to check the command exit code; terminal closure alone does not establish success.

## Do's and Don'ts

### Do:

- **Do** inherit the desktop palette, font family, control style, and icons.
- **Do** keep software sources explicit on operational Installed rows and prototype source controls.
- **Do** use metadata-backed icons and publisher screenshots with recorded origins; label Explore as an offline sample prototype.
- **Do** keep details and confirmations explicit before package operations.
- **Do** preserve the shared list inset and Explore deterministic mixed-span discovery geometry in narrow desktop windows.

### Don't:

- **Don't** invent ratings, safety badges, or publisher screenshots. AI-generated images are permitted only for design mockups, never as application assets.
- **Don't** replace native controls with a web imitation or ship the structural reference as artwork.
- **Don't** override the user's configured icon theme; Breeze is the fallback.
- **Don't** describe AUR review or AppImage import as safety certification.
- **Don't** treat a source-code correction as a completed visual review.

Collapsed Activity uses the sidebar toggle’s 40 px square footprint; expanded Activity retains 48 px height for its progress line.

App descriptions default to six rendered line heights, aligned at the top. A native Show more / Show less tool button expands the unchanged publisher text. Hide the control for short descriptions; recalculate wrapping on resize and reset collapse when opening an app.

Featured carousel transitions slide horizontally with 420 ms OutBack easing and restrained 0.65 overshoot scaled by KDE’s AnimationDurationFactor. Zero switches instantly. Rapid selections start from the visible frame; hiding or resizing stops the overlay. Pagination selects the destination immediately, while the underlying native whole-card button remains available after motion.

Featured apps advance every five seconds while visible. Pause during hover over the card or pagination, keyboard focus inside pagination, filtering, navigation away, or scrolling out of view. Manual selection and resume restart the countdown. Featured indicators stretch and settle with 280 ms OutBack motion; screenshot-gallery indicators use the same spring stretch. Both motions respect KDE’s animation factor.

Collapsible filter controls, sidebar width, and description height use interruptible 240 ms OutCubic transitions scaled by KDE’s animation factor. New app descriptions and initial sidebar settings apply instantly.

Mosaic previews float inside the scroll viewport without occupying layout space. Hover or keyboard focus waits 350 ms, then shows the app category, Flatpak platform, summary, a short cached publisher introduction, and first available publisher screenshot. Grow the preview from the source card’s exact rectangle to a larger rectangle centered on that card and kept within viewport bounds. Use the existing tile treatment with 220 ms OutCubic geometry expansion and a single native opacity effect. Small cards keep the icon and name above the extra copy and screenshot; existing screenshot cards retain the image above the copy. Moving onto the preview keeps it open; leaving waits 150 ms. Clicking opens app details; Escape, scrolling, resizing, filtering, and navigation dismiss it.

Wide compact discovery cards (at least 340 px) use their available publisher screenshot in the right portion with a soft surface fade. Text wraps within the left 54 percent. This fills remaining-row cards such as Fractal and Calculator without enlarging them or changing spotlight selection.

Explore tiles have no hover or mouse-focus selection outline. Keyboard navigation retains a contained focus outline; it does not draw outside the card’s clipped shape.

Hover previews dissolve over 180 ms after the existing 150 ms leave delay. Re-entering cancels the fade and restores opacity. Navigation, Escape, filtering, and scrolling dismiss immediately. Use one opacity effect rather than stacking graphics effects on a resizing card. Description height treats Qt’s unavailable measurement (-1) as zero before content is populated. Rebinding Install disconnects only its saved application handler and preserves theme-owned connections.

Opening app details focuses the page container, rather than selecting Back. Back remains available through keyboard navigation and Alt+Left. Gallery indicators reuse the featured carousel’s 280 ms OutBack spring, including KDE animation preferences.

Featured transitions follow index order: higher indices enter from the right; lower indices enter from the left. Last-to-first rewinds toward the start, including autoplay; first-to-last moves toward the end. Keep spring timing and interruption behavior.

Featured cards place a native 130 × 36 px quick Install button 24 px from the bottom-right. Expanded mosaic previews place it 14 px from the top-right beside the icon. Both reuse shared simulated installation progress and Open state. Buttons act independently of whole-card detail navigation; no package operation occurs.

App details replace rating content with a Latest changelog panel using the existing 24 px inset and 12 px spacing. Show the latest stable release’s version, date, and base-language or English notes from bundled Flathub AppStream metadata. Hide the changelog panel when notes are missing; never substitute an older release as current. Remove Preview badges and footer copy across pages. Preserve clear no-package-change behavior and sample-password explanation. Back and Show more/less are raised native tool buttons with a 36 px minimum height, matching Install.

Navigation stores visits to Explore, Installed, Updates, and app details. Mouse Back/Forward buttons move through those visits. Save the source page’s vertical position and focus before opening details; return without inventory reload or filter reset. Details preserve the originating sidebar section and label Back as Explore or Installed. Forward reopens the same app; a new visit after Back discards the forward branch. Details entry focuses the page container.

Dragging readable local AppImage, .flatpak, or .flatpakref files over Installed covers the central content with a drop overlay. Show the detected format and up to three filenames; mixed drops show both formats. Dismiss on leave, drop, or page navigation. Reject unsupported files without an overlay. The overlay receives no mouse input and leaves the list layout unchanged.

Activity includes a native Clear button beside Close. Clear completed history while preserving running operations and app action states; disable Clear when nothing can be removed.

Detail sections follow Latest changelog, Add-ons, Permissions, Links. Add-ons come from cached AppStream extends relationships, with one entry per package ID. Native command-link rows open add-on details through navigation history; separate Install buttons use the shared simulated controller. Links show publisher HTTP(S) URLs and the Flathub manifest, with themed icons and native browser opening. Hide empty changelog, add-on, permission, link, and screenshot sections, including their headings and layout spacing. Re-evaluate visibility when changing apps.

Explore starts with Filters unchecked and its panel hidden. Use nine Flathub category buttons in three columns: Audio & Video, Developer Tools, Education & Science, Games, Graphics & Photography, Networking, Productivity, System, Utilities. Education and Science share one filter. Match bundled publisher AppStream category IDs, allowing apps to belong to multiple groups. Longer category names use two lines. Add-on command-link rows replace the native arrow with a themed add-on/package icon.

Empty Activity uses a plain two-line label above an empty transparent list, with no placeholder list item or card background. Disabled Clear remains noninteractive with an arrow cursor and 40 percent opacity; available Clear uses native appearance at full opacity.

Featured Explore carousel consumes wheel input over cards, Install, and indicators. One 120-unit wheel notch or 60 accumulated trackpad pixels moves a slide; down/right moves forward. Reuse existing spring transitions and pagination updates. Autoplay resumes after mouse focus and pointer leave, while keyboard focus still pauses rotation.
