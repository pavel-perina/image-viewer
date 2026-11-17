# Visual consistency and usability review — 17 November 2026

Status: initial review retained as design context, before the Settings SVG.
Neutral surfaces, shared selection styling, pane focus and preview/status organization
have since been implemented following discussion; accepted choices are recorded in
`decisions.md`. Other recommendations below remain proposals.

## Recommendation

Keep the compact desktop layout, Inter, restrained gradients, and existing controls.
Use neutral surfaces by default, with the existing green accent. Preserve surface
tint as an Appearance preference. Address dialog footers and keyboard access before
adding decorative detail: those are the clearest consistency and usability gaps.

This is a design judgement from the captures below, not a measured user preference
or a claim that the current tint alters image data. Neutral chrome provides a steadier
reference beside pictures and gives selection and actions more exclusive use of colour.

## Evidence and limits

Inspected QML pages, shared controls, palette derivation, and settings ownership.
Rendered the existing local build offscreen with the software renderer at 1280 × 800,
plus the 640 × 480 controls gallery. Images are generated HSV ramps, with isolated
test settings and cache. No real desktop windows were opened or user pictures loaded.
The browser tree nevertheless lists real directory names, as the normal test fixture does.
Captures are local working artifacts, intentionally ignored by Git. The paths below
are relative to this document and are not included in a fresh checkout.

| Capture | What it establishes |
| --- | --- |
| `2026-11-17_visual-review/controls.png` | Custom button, field, selection and menu treatment |
| `2026-11-17_visual-review/browser.png` | Original grid, preview, path bar and status hierarchy |
| `2026-11-17_visual-review/viewer.png` | Neutral image surround and icon toolbar |
| `2026-11-17_visual-review/info.png` | Histogram, metadata hierarchy and panel tint |
| `2026-11-17_visual-review/crop.png` | Representative tool panel, fields, instructions and actions |
| `2026-11-17_visual-review/crop-neutral.png` | Same layout and image, changing only surface chroma to zero |
| `2026-11-17_visual-review/resize.png` | In-window dialog and its different footer buttons |

Captures are observations of the available build, cross-checked against source, not a
fresh production build. They do not establish Windows appearance, native window
decorations, GPU colour output, high-DPI behaviour, screen-reader output, or usability
with photographic content. Those remain acceptance checks, not reported failures.

## What already works

- `Theme` and `ThemePalette` centralize colours; `ToolPanel`, `AppDialog`, and
  `DialogWindow` provide useful shared structure. Extend those instead of creating
  parallel panel and dialog systems.
- The picture surround is neutral. Tool lines remain visible through light/dark
  outlines. Editing controls do not need a visual redesign to become usable.
- Custom controls have explicit hover, disabled and focus treatments. The Crop action
  stands apart from Cancel and Reset, and its label names the operation.
- Thumbnail selection and current image are separate states. Keep that distinction:
  the current image can be outside the selection.
- Tool panels already scroll their content while keeping heading and actions in place;
  they also reveal fields reached by keyboard focus.

## Findings, in priority order

| Priority | Finding and evidence | Proposed change and acceptance check |
| --- | --- | --- |
| 1 | Resize footer buttons are visibly smaller, square and equally emphasized, unlike Crop's rounded 28 px actions. `ResizeDialog.qml` uses `standardButtons`; the custom style has no `DialogButtonBox.qml`. | Make shared dialog footers use the existing button treatment. Use **Resize** rather than **OK** for this operation and emphasize the affirmative action. Check Resize, Save options and messages; preserve platform button ordering and dialog accept/reject semantics. |
| 1 | Some useful actions cannot receive Tab focus: `ToolPanel.qml` sets the extra action (Reset) to `Qt.NoFocus`; `BarButton.qml` does the same, including the favorites/recent-folders entry point. `FolderButton.qml`'s custom popup has mouse delegates without keyboard activation handlers. | Provide a reachable keyboard path for Reset and favorites. Use deliberate toolbar navigation or an equivalent menu action to avoid excessive Tab stops. Verify open, move, activate and dismiss with keyboard only, with a visible focus indicator. Existing Back/Forward/Up shortcuts should remain. |
| 1 | The Preview collapse heading is an `Item` with a `TapHandler`, with no keyboard activation or accessible button state in `PaneHeader.qml`. Info-panel location links similarly use a `MouseArea` inside a label. | Give these interactive elements appropriate focus, accessible names/states and Enter/Space activation. Extend shared controls; check that a non-collapsible heading remains a heading. Verify with keyboard and a screen reader. |
| 2 | Panels and muted text inherit surface chroma (`palette.cpp`), although the brief asks for neutral presentation over pictures. The tinted Crop and neutral Crop captures show the difference directly. | Default surface chroma to zero; retain tint as an option. Keep image-adjacent surfaces and their ordinary text neutral even when chrome is tinted. Accent focus/selection and semantic warnings may remain coloured. Check info, tool panels, bubbles and badges against a gray image. |
| 2 | A selected thumbnail paints almost the entire 180 × 200 cell green; its current-image frame is a bright 2 px outline (`ThumbnailCell.qml`). This makes selection a large colour block beside a small landscape thumbnail. | Try a quieter selected fill with a clear accent edge, and a distinct neutral current-image outline. Compare selected/current/both/neither with and without grid focus, including multiselection. Keep the filename readable and do not introduce a new selection mechanism. |
| 2 | In the browser, pane headings, path controls, thumbnails and two bottom information rows have little difference in surface treatment. The selected item is much more prominent than the pane structure. | Use the existing `header` role to establish pane headings, align shared insets, and clarify the scope of each status row. Retain file/selection totals and current-image details; decide on consolidation only after the browser mockup. |
| 3 | Shared 28 px controls coexist with repeated literal 26/34 px headers, 16 px dialog padding, and varying 8/10/12 px row gaps. These can be intentional, but repeated metrics are distributed across files. | Name genuinely shared header, dialog and section-spacing metrics in `Theme`. Keep compact data grids; distinguish normal row spacing from space between sections. Check text enlargement before imposing fixed heights. |
| 3 | Resize casts the operation as generic OK; Crop explains its gestures in a dense paragraph. Toolbar names are mostly available only through menus and tooltips. | Name committing actions by their result, shorten tool hints without losing gesture information, and retain shortcut-bearing tooltips. Keep icon toolbars and menu access; do not add permanent text to every icon. |

The keyboard findings are source-backed gaps; the capture fixture did not execute a full
keyboard journey. Visual preferences in priorities 2 and 3 should be judged in mockups,
not treated as bug fixes already agreed upon.

## Proposed visual foundation

Start with existing role lightnesses and accents. This isolates the effect of removing
surface tint; do not change lightness, borders, spacing and tint simultaneously.
Candidate inputs: hue **150°**, accent chroma **0.075**, surface chroma **0**.
The following colours were generated with the project's `derivePalette` / `fromOklch`,
not sampled from screenshots. Oklch values are inputs before 8-bit rounding and any gamut
mapping. At chroma zero, hue has no visible effect. Alpha is separate from RGB.

| Role | Hex RGB | Oklch (L, C, h°) | Alpha |
| --- | --- | --- | --- |
| window | #222222 | .25, 0, 150 | 1 |
| base | #161616 | .20, 0, 150 | 1 |
| alternateBase | #262626 | .27, 0, 150 | 1 |
| button | #353535 | .33, 0, 150 | 1 |
| text | #e4e4e4 | .92, 0, 150 | 1 |
| placeholderText | #868686 | .62, 0, 150 | 1 |
| textMuted | #ababab | .74, 0, 150 | 1 |
| textDisabled | #696969 | .52, 0, 150 | 1 |
| highlight | #4c6d52 | .50, .05625, 150 | 1 |
| highlightedText | #f5f5f5 | .97, 0, 150 | 1 |
| accent | #9ccca4 | .80, .075, 150 | 1 |
| link | #b0d6b6 | .84, .060, 150 | 1 |
| linkVisited | #96c8cb | .80, .0525, 200 | 1 |
| raised | #303030 | .31, 0, 150 | 1 |
| header | #292929 | .28, 0, 150 | 1 |
| surfaceTop | #353535 | .33, 0, 150 | 1 |
| surfaceBottom | #2e2e2e | .30, 0, 150 | 1 |
| controlTop | #404040 | .37, 0, 150 | 1 |
| controlBottom | #333333 | .32, 0, 150 | 1 |
| defaultButtonTop | #4a664f | .48, .04875, 150 | 1 |
| defaultButtonBottom | #3b5440 | .42, .045, 150 | 1 |
| defaultButtonEdge | #7ea385 | .68, .060, 150 | 1 |
| light | #4d4d4d | .42, 0, 150 | 1 |
| midlight | #404040 | .37, 0, 150 | 1 |
| mid | #454545 | .39, 0, 150 | 1 |
| dark | #0f0f0f | .17, 0, 150 | 1 |
| shadow | #060606 | .12, 0, 150 | 1 |
| toolTip | #353535 | .33, 0, 150 | 1 |
| buttonDisabled | #292929 | .28, 0, 150 | 1 |
| highlightDisabled | #354137 | .36, .0225, 150 | 1 |
| panel | #1b1b1b | .22, 0, 150 | .90 (230/255) |
| bubble | #1b1b1b | .22, 0, 150 | .82 (209/255) |
| badge | #1b1b1b | .22, 0, 150 | .63 (161/255) |

Retain fixed `Theme` colours: imageBackground #202020, onImage #e0e0e0,
onImageBright #f0f0f0, warning #e0a050, danger #e06c60, folder #e2b955.
Retain `frame` as text at 25% alpha and `hover` as text at 12% alpha initially.
These are a palette baseline, not a complete SVG colour manifest: list every actual
fill, stroke and alpha in the Settings mockup's accompanying table when it is drawn.

| Size or role | Proposed value | Existing or new |
| --- | --- | --- |
| controlHeight | 28 logical px minimum for standard fields/buttons | Existing; allow growth for font metrics |
| controlRadius | 4 | Existing |
| controlPadding | 12 | Existing button padding; fields currently use smaller insets |
| panelWidth | 280 | Existing tool-panel width |
| panelMargin / panelSpacing | 12 / 8 | Existing |
| Body / small / panel title | 10 / 9 / 11 pt at default font size | Existing; preserve relative sizing |
| toolIconSize | 24 | Existing; button hit area may be larger |
| dialogPadding | 16 | Proposed shared token for existing value |
| sectionSpacing | 16 | Proposed shared token; separate groups without cards everywhere |
| paneHeaderHeight | 28 | Proposed shared minimum replacing repeated 26 px headings |
| Settings initial size | 760 × 560 | From brief; not its minimum size |
| Settings section list | 160 wide | Proposed; verify label fit and font scaling |

No new control type is proposed. The Settings section list can use existing list and
selection treatments; groups can use labels and spacing. A shared dialog footer is
styling/infrastructure for existing buttons, not a new user-facing control.

## How this guides Settings

Keep the six sections in the brief: Appearance, Viewing, Browser, Files, Thumbnails,
Info panel. Start with an Appearance SVG at 760 × 560, using neutral surfaces and
green accents, with section navigation, three labelled palette inputs, current values,
and a live-preview area made from existing controls. Avoid extra decorative cards.

Proposed interaction contract: edit a draft; preview Appearance live; **Apply** saves
the draft, **OK** saves and closes, **Cancel** restores the last applied state and closes.
Escape and window close behave like Cancel. Changing sections retains the draft.
Clear-cache and clear-history operations must have explicit immediate-action wording;
Cancel cannot restore deleted cache entries or cleared history. Settle these semantics
before implementing persistence.

Implementation constraints to reflect in later section designs:

- Appearance setters already update the palette live, but `ThemePalette` currently has
  no persistence. Preview rollback and saving need an explicit settings owner.
- Viewing settings exist across `ViewSettings` and `ColorSettings`; reuse them.
  Output bits are currently requested at startup through `IV_OUTPUT_BITS`, not a live
  preference. Show requested and actual output separately and explain any restart.
  Do not promise 10-bit support merely because 10 was requested.
- Browser sorting lives in `Main.qml`, file preferences in `FileActions.qml`, and info
  preferences in `ViewerPage.qml`. Settings and menu shortcuts should share owners,
  rather than making a second independently persisted copy.
- Thumbnail cache already accepts a size budget in core; the settings UI needs a bridge
  for configuring it, with usage measurement and clear work remaining asynchronous.

## Verification and next step

The existing offscreen checks passed: 14 Controls tests, 3 ToolPanel tests, Resize
ratio behaviour, and 2 theme propagation tests (20 total). The toolbar naming test
also passed during capture. A temporary capture fixture exercised browser, viewer,
info, Crop, Resize and neutral theme switching without QML warnings. No application
code changed; full-suite and sanitizer runs were not part of this documentation review.

After agreement on direction, draw the Settings SVG first, with exact colour and size
tables. Then draw the tool panel and browser, one screen at a time. Record accepted
decisions with reasons in `decisions.md` when that agreement is reached. Later
implementation should test dialog button semantics, keyboard paths, palette rollback
and settings synchronization, and include real Linux/Windows, scaling and photo checks.
