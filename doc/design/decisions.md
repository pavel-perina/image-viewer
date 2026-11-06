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
* TIFF keeps the file's precision: 8-bit becomes `Bgrx8`, higher depths become `Gray16`,
  `Rgba16`, `Gray32F` or `Rgba32F`. Layouts it cannot read directly (palette, 1-bit, YCbCr,
  8-bit CMYK) go through libtiff's RGBA interface as 8-bit. 16-bit CMYK is refused for now.
* Reduced images become levels whether stored in the main chain or in SubIFDs. Levels are
  sorted by size and any ratio between them is accepted. Reduced images that do not match
  their page are ignored, and masks are skipped.
* Strips are served as virtual tiles, so a file with one-row strips does not become
  thousands of textures, and a single huge strip is cut to texture size.
* One libtiff handle per source, guarded by a mutex. A pool of handles is only worth adding
  if measurements on real files justify it.

## Overviews
* A flat image larger than 2048 pixels gets generated levels, down to 2048 on the long
  edge, because fit-to-window would otherwise need every tile. They are built once, by
  reading the whole image, and kept in memory up to 2 GiB. Damaged tiles stay black.
* This is what a large flat file costs on first display: roughly a second per 100
  megapixels, single-threaded, because tiles are decoded under the source's one mutex.
  Pyramid files avoid it and open in milliseconds. Parallel decoding would speed it up, and
  a persistent cache of overviews could avoid it for flat files too.

## Display range
* Pixels are converted to `Bgrx8` on the worker thread, with a low and high value per image.
* Gray and float images default to auto contrast (0.1% percentiles of the coarsest level),
  because their useful range is often a small part of the format's range. 16-bit RGB is
  usually a photograph, where stretching would falsify the exposure, so it defaults to the
  full range. The choice is per image and does not carry over to the next one.

## Histogram and details
* A histogram measures the file's values, not what is on screen, with 256 or 65536 bins,
  or 4096 for float. Gray or RGB is decided from the pixels, so a gray image stored as RGB
  gets a gray histogram.
* It is measured on the smallest level with at least a megapixel, staying below 16
  megapixels if it can, so its cost does not grow with the image. Reduced levels soften
  extremes, so clipping can look slightly lower than it is. The panel says which level.
* `HistogramView` is a plain item with no knowledge of images, so other views can reuse it.
* `SubimageInfo` carries how a page is stored and its EXIF, because both can differ per page.
  `describeImage` turns them into text in core, so it needs no Qt and is easy to test.
* EXIF comes from our own small reader for JPEG and from libtiff for TIFF.

## Rendering
* Public Qt Quick scene graph API only (`QSGSimpleTextureNode`), so it runs on every RHI
  backend. Qt Quick already runs on Vulkan through RHI, so no direct Vulkan dependency.
* 16-bit images are converted to 8-bit on the CPU for display, so changing the display range
  redraws through the worker. Live adjustment or colour management may later need a custom
  `QSGMaterial` with shaders, which adds `qt6-shadertools`.
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
* libtiff may pull in a different libjpeg than the application links (`libjpeg.so.8` and
  `.62`). Their symbols are versioned, so both can load into one process, and a test decodes
  a JPEG and a JPEG-compressed TIFF together. The linker warning about it is harmless.
* Exiv2 is GPL and ruled out. Candidates for metadata are a permissive EXIF reader, or
  external ExifTool for full metadata.
* Qt Charts and Qt Graphs are not available under LGPL, so the histogram is drawn by us.
* The project itself will be MIT or BSD-2. Link Qt dynamically to keep LGPL simple.
* Thumbnails will be stored as compressed blobs, so they do not need zstd.

## Planned, not built
* Formats: PNG, WebP, AVIF, JPEG XL,
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

## Editing (planned, not built)
* A document is a base image, an edit list, an optional file path and an unsaved flag.
* The edit list is the first thing to build for editing, before any individual tool.
* Edits are stored as operations with parameters, not pixel copies. Undo and redo move a
  position in the list, and undoing replays from the nearest checkpoint.
* Checkpoints are rendered results kept under a memory limit. Losing one only costs a replay.
* Snapshots are named points in the list. "Unmodified" and "last saved" are two of them.
* Some operations carry their own pixels, such as a pasted layer. These pixels count
  towards the memory limit.
* If the list holds only rotate, flip and crop, saving a JPEG stays lossless.
* An edited image is an `ImageSource` wrapping the base, so views work unchanged.
* The same edit list can drive batch tools and "repeat last edit".
* Images must fit in memory to be edited at first. Gigapixel TIFFs stay view and export
  until per-tile editing exists.
* Each opened image says whether it can be edited, and if not, why, so the UI can disable
  the tools and show the reason. Editing is refused for images that are too big, and for
  images the viewer shows but does not fully keep: palette and 1-bit images, those read
  through a conversion to 8-bit, and those with alpha or extra samples, which are dropped
  on reading. The default is read-only, and a format is allowed once it survives saving.
* Saving a multipage or pyramid file back must keep its other pages and levels. Until it
  does, such files can only be saved as a new file.
* Saving writes a temporary file and renames it over the target.
* Star and delete metadata write directly to the file without touching pixels, so they do
  not recompress it. File operations such as rename, move and trash are outside the undo
  stack.

### Leaving an image with unsaved changes
* It must not be possible to leave an unsaved image in the current tab without deciding:
  moving to another image or folder asks to save or discard, or stays put. If tabs are
  added, other tabs can be used freely, since each keeps its own unsaved document.

### Images without a file
* Clipboard pastes, and later screenshots and scans, are documents with an in-memory base
  and no path. Clipboard paste comes with the first editing work.
* "Unmodified" is the image as it arrived. The first save asks for a name and format.
* They count as unsaved even with no edits, because they exist nowhere else.
* The base is written to the cache folder on arrival and removed after saving or
  discarding, so the next launch can offer to recover it.
* They are outside the folder's grid and next/previous, so they suit one tab each, named
  like "Untitled 1". Until tabs exist they open in the large view.
* `ImageView` will need to accept a document as well as a path.
* Without an embedded profile they are assumed to be sRGB.
* Screenshots (XDG desktop portal, Windows API) and scanners (SANE, WIA) belong in the
  system integration plugin. Check SANE's licence terms first.

### Plugins
* For extensibility, and so filters and formats can be prototyped without rebuilding the
  application.
* Filters and image format decoders can be plugins, through a versioned C API loaded at
  run time. No C++ standard library or Qt types cross the boundary.
* Each plugin exports one init function, which checks the API version and registers what
  the plugin provides through a host callback. Load-time constructors are avoided.
* Plugins load once on a worker thread at startup and stay loaded until exit, with no
  dependency ordering between them.
* A filter describes its parameters and the host builds the UI, so plugins need no Qt and
  the edit list can store the values.
* A filter can provide a cheap check of whether it applies to an image, given its format,
  size and metadata. Filters depend on metadata, never on other plugins.
* Built-in filters use the same API, so it stays honest and serves as public examples.
* A filter declares its name, parameters, pixel formats and how many neighbouring pixels
  it needs, and receives input and output buffers.
* A decoder maps onto `ImageSource`: open a file, list subimages, read tiles. It also adds
  file extensions and metadata, so neither can stay a fixed list.
* Plugins load only from folders the user configures, with no paths built into the app.
* Plugins run inside the application process, so a crash takes the application down.

### Open question
* How to keep the original on the first overwrite. Proposed: send it to the system trash,
  and ask first where there is no trash. The alternative is a `.original` file beside it.

## Metadata (planned, not built)
* To be built with the info panel, starting with EXIF. XMP, OME-TIFF and other schemas are
  added as blocks, so new ones need no changes elsewhere.
* A metadata block has a schema name, its original bytes, and entries flattened for
  display and lookup, with tree paths as keys such as `ome:Pixels/PhysicalSizeX`.
  Subclasses can add typed accessors.
* Saving writes the original bytes back, so metadata the application cannot interpret
  survives. Deleting metadata removes whole blocks.
* Blocks attach to the file and to each subimage.
* A normalised layer answers facts that can appear in several blocks, such as rating,
  capture time, orientation and physical pixel size, with fixed rules for which block wins.
* Metadata can be read without decoding pixels, in the background, so folders can be sorted
  by it. It can share a cache with thumbnails.
* The C++ classes stay inside the application. Across the plugin API, blocks are opaque
  handles with functions to list blocks and to read the schema, raw bytes and entries. A
  decoder plugin hands over the same three things and the host wraps them in a generic block.
* XML in XMP and OME-TIFF can use pugixml (MIT), which needs no Qt.

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
