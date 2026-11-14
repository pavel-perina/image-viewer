# Image Viewer

An image viewer and basic editor in the spirit of old ACDSee and FastStone Image Viewer, but
multiplatform and fast. Qt 6.8+ (QML), C++23, Linux and Windows.

## Where things are

| Path | What |
| --- | --- |
| `src/core` | Library `iv_core`. Pure C++23, **no Qt**. Decoders, tiles, colour, edits, writers. |
| `src/ui` | Library `iv_ui`, QML module `ImageViewer`. C++ classes that QML uses. |
| `src/ui/qml` | The QML. Pages, panels, dialogs, shared controls. |
| `src/ui/controls` | QML module `ImageViewerStyle`: the controls' own look (buttons, check boxes, fields, menus), falling back to Fusion for the rest. |
| `src/app` | Only `main()` and the Windows install rules. |
| `assets` | What is not code: `fonts`, `shaders` (resampling `mks_*`, the colour preview), `icons`. |
| `tests` | GoogleTest. `iv_core_tests` (no Qt) and `iv_ui_tests` (offscreen QML). |
| `doc/design` | `architecture.md` (goals), `decisions.md` (what was decided and why), `main-window.md`. |
| `doc` | `building-windows.md`, `manual-checks.md`. |
| `installer` | Inno Setup script and the PowerShell that stages it. |

### Core, by area

* **Image model:** `image_source.h` (the interface every format implements: subimages, levels,
  tiles), `image_buffer`, `geometry`, `viewport`, `overview_source`, `whole_image_source`.
* **Formats:** `jpeg_source`, `tiff_source`, `png_source`, `webp_source`, `avif_source`,
  `jxl_source`. Metadata: `exif`, `xmp`, `image_details`.
* **Colour:** `color` (ICC, display transform), `color_edit` (levels, curves, cube tables),
  `oklab.h`, `histogram`, `pixel_probe`, `palette` (the interface's colours, worked out from a
  hue), `rgba.h`.
* **Editing:** `document` (the edit list, undo), `crop`, `perspective` (straighten),
  `image_ops` (resize, grayscale), `shapes` (annotation shapes: points, grips, hit-testing),
  `image_layer` (blending a drawn layer into the picture, pixelate), `coverage` (growing,
  moving and blurring what a shape covers, for its dark edge and shadow).
* **Saving:** `image_writer` (every format), `save_image`, `jpeg_transform` (lossless JPEG),
  `jxl_transcode`, `file_io`.
* **Small helpers:** `fuzzy_match` (finding names by a few typed letters, fzf style;
  standalone), `font_families` (one entry per typeface, without its styles).
* **Background work:** `work_queue` (priorities, cancelling), `disk_gate`, `thumbnail`,
  `thumbnail_cache` (SQLite).

### UI, by area

* **Viewing:** `image_view` (the tiled view item; also the entry point for edits),
  `edited_image`, `color_settings`, `color_lut`, `histogram_view`.
* **Look:** `theme_palette` (the palette's choices and colours, for `Theme`).
* **Tools drawn on the image:** `image_overlay` (the base), `overlay_style` (their colours,
  from `Theme`), `crop_overlay`, `straighten_overlay`, `probe_overlay`, `annotate_overlay`, and
  `shape_painter` (draws annotation shapes, for the screen and for burning in).
* **Browsing:** `browse_context` (one folder, its files, the current image), `file_list_model`,
  `directory_tree_model`, `folder_listing`, `subfolder_list`, `folder_lists`, `thumbnail_store`,
  `thumbnail_provider`.
* **Files:** `file_operations`, `file_job_runner`, `file_clipboard`.
* **QML pages:** `Main.qml` (window, menus, dialogs), `BrowserPage.qml`, `ViewerPage.qml`,
  `FileActions.qml` (what the file menus do), `FileChooser.qml`.
* **QML shared controls:** `AppDialog` (a dialog inside the window), `DialogWindow` (a large
  dialog in a window of its own), `AppMenu`, `AppMenuItem`, `BarButton`, `AppScrollBar`,
  `MessageBox`, `NumberRow` (a label with a number to set), `DecimalSpinBox`, `ListFrame` (what
  a list stands on: base colour and a frame), `PaneHeader` (a pane's caption, which may collapse
  it), `SearchCombo` (a combo box with
  fzf-style search, over `fuzzy_list_model`), `ToolPanel` (the base of every tool's panel:
  heading, rows, Cancel and Apply), `Theme` (every colour, and the sizes pages share).

## Build and test

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build build
    ctest --test-dir build -j8

* While working, run only the tests of the area changed, with `--gtest_filter`. Run the whole
  suite and the sanitizers once, at the end.
* UI tests must run offscreen, or they open windows on the desktop:

      QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
          build/tests/iv_ui_tests --gtest_filter='Crop.*'

* The tests use the program's own style, whatever `QT_QUICK_CONTROLS_STYLE` says, and its own
  font (Inter, bundled) at 10 points, whatever the platform gives.
* The software renderer skips the GPU shader tests. Run those under Xvfb with OpenGL.
* Tests use made-up images and a temporary home folder. Never read the user's pictures, cache
  or settings.

## Rules for code

1. **Core has no Qt.** `iv_core_tests` links none, so a Qt include in core breaks the build.
2. **Look before adding.** Before a new class, QML component or helper, search for one that
   does the job or nearly does. Extend it, or say why not. On the second copy of anything,
   extract the shared part before going on.
3. **Logic in C++, not QML.** QML holds layout, bindings and short handlers. Anything with a
   rule worth a test goes in C++.
4. **QML uses the shared controls and `Theme`.** No colours or fonts written into a page, nor
   sizes that pages share: text sizes, panel widths, margins and spacing come from `Theme`. A
   number that lays out one page alone may stay in it. A dialog is an `AppDialog`, a menu an
   `AppMenu`.
5. **Tools on the image derive from `ImageOverlay`** (`src/ui/image_overlay.h`), which owns
   the view binding, the coordinates, panning and click against drag, and draws the common
   lines and grips. A tool adds its own model and painting. Colours come from `Theme` through
   `OverlayStyle`, never written into the C++.
6. **Headers keep libraries to themselves.** No header of core or UI includes a header of a
   codec or other outside library, so nothing depends on libtiff or the like by way of
   another class's private part: such members go behind pimpl. Core's own value types
   (points, rectangles, shapes) may appear in UI headers. A class with much state of its own
   hides it behind pimpl too, but that is a preference, not a rule.
7. **Nothing blocks the UI thread.** Disk and decoding go through the work queue. An object
   that receives results cancels its group and waits in its destructor.
8. **Nothing assumes one view.** A `BrowseContext` is one tab or panel.
9. **Caches of decoded images are keyed by the file's time and size**, so a changed file is
   never shown stale.
10. **Every behaviour gets a test**, named for what the program promises.
11. **A decision goes in `doc/design/decisions.md`**, with the reason, in the same change.
12. **Briefly document tests**, for example explain motivation why it exists, what are
    inputs and outputs and what is source of truth, if applicable

## Rules for libraries

* No GPL libraries, including as a dependency of a dependency.
* From other viewers and editors, take behaviour only, never code.
* A new library needs asking first. On Windows it goes in `conanfile.txt`; Linux uses the
  system's packages.

## How to work

* Design before code for anything larger than a fix: say what will be added and where it fits,
  and wait for agreement.
* Small changes, one topic each, that can be read in one sitting.
* Small changes are committed on `main`. Only a feature of several commits gets a branch,
  from `main`: it is merged with a merge commit (`--no-ff`), named for what it brings, and
  deleted at once, so that branches do not pile up.
* Commit only when asked, one topic per commit, files named one by one. Never push or merge
  unasked.
* Say before opening a window on the real desktop.

## Not wanted

* A slideshow.
