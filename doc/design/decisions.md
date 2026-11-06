# Decisions

Choices made while implementing `architecture.md` and `main-window.md`.

## Structure
* `src/core`: pure C++23 without Qt. Decoders, tile model, viewport maths, work queue.
  `iv_core_tests` links no Qt, so a Qt dependency entering core breaks the build.
* `src/ui`: QML module `ImageViewer`. Public headers expose Qt types only where QML
  requires them, and hold everything else behind pimpl. Core types stay out of the headers.
* `src/app`: only `main()`.

## Image model
* Everything is tiles of pyramid levels of subimages, including a small JPEG. There is a
  single display path, so big TIFFs are a new decoder rather than a new viewer.
* Subimage: a page or an animation frame, each with its own format, orientation, ICC
  profile and levels. Reduced resolution copies are levels, not subimages.
* JPEG uses libjpeg's 1/2, 1/4 and 1/8 scaled decoding as its levels. It decodes a whole
  level and serves tiles as zero-copy views, because a JPEG cannot be entered mid-stream.
* `PixelFormat` already lists 16-bit and float formats, but only `Bgrx8` is decoded and
  displayed so far.

## Rendering
* Public Qt Quick scene graph API only (`QSGSimpleTextureNode`), so it runs on every RHI
  backend. Qt Quick already runs on Vulkan through RHI, so no direct Vulkan dependency.
* 16-bit display and colour management will need a custom `QSGMaterial` with shaders,
  which adds `qt6-shadertools`.
* Orientation is applied by the scene graph transform, not by rotating pixels.

## Threads
* Own work queue instead of oneTBB, because visible-first priorities and cancelling work
  that scrolled away are the central requirement. It defaults to 2/3 of the logical cores.
* Anything posting results to a QObject must cancel and wait for its work queue group in
  the destructor.

## Testing
* ThreadSanitizer with GCC 16 aborts on any `longjmp`, even in a three-line program. When
  running core tests under it, add `--gtest_filter=-Jpeg.ReportsErrorsForDamagedFiles`.

## Multiple views
* `BrowseContext` is one browsing context: directory, file list, current image, and later
  the selection. QML creates it, so a tab or file panel is one context plus its views.
  Tabs, dual panels, compare and batch tools build on this. Nothing in the UI may assume
  there is only one.

## Libraries
* Exiv2 is GPL and ruled out. Candidates for metadata are a permissive EXIF reader, or
  external ExifTool for full metadata.
* Qt Charts and Qt Graphs are not available under LGPL, so the histogram is drawn by us.
* The project itself will be MIT or BSD-2. Link Qt dynamically to keep LGPL simple.
* Thumbnails will be stored as compressed blobs, so they do not need zstd.

## Planned, not built
* Formats: TIFF (BigTIFF, tiled, pyramids, multipage, 16-bit), PNG, WebP, AVIF, JPEG XL,
  and PCX, BMP, TGA, GIF. Animation in GIF, WebP and JPEG XL.
* Colour management with LittleCMS. ICC profiles are already read and kept.
* Tools: crop, compare, image strip builder, batch resize, convert and rename. Some act on
  the selection of the current panel.
* Possibly a dual-panel file manager. File icons are OS dependent, so they belong in the
  optional system integration layer, along with KDE or Windows places.

## Browser
* The browser and large view are two pages sharing one `BrowseContext`, each with its own
  `ImageView`. A page's view loads only while the page is active.
* Thumbnails go through an async QML image provider on the shared work queue, below all
  other work. The newest request runs first, and Qt cancels requests when their delegate
  goes away. Qt deletes a response only after `finished()`, so `cancel()` must emit it.

## Known limitations
* Thumbnails are cached only in memory, by Qt's pixmap cache. The SQLite cache is still to
  do, and a changed file keeps its old thumbnail until the application restarts.
* Directories are not watched, so new or removed files appear only after reopening. The
  grid lists images only, not subdirectories.
* Colour management is missing, so wide-gamut JPEGs look slightly off. CMYK JPEGs use a
  rough conversion.
* Tiles do not overlap, so a faint seam is possible at tile edges between 100% and 200%
  zoom. From 200% up, filtering is nearest-neighbour and this does not apply.
* Opening decodes the smallest level, then the one the view needs. For JPEG this costs
  about twice the time, because both need the same entropy decoding.
