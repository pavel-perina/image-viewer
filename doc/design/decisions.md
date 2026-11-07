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

* PNG, WebP, AVIF and JPEG XL cannot be entered at a tile or scaled while decoding, so they share
  a source that decodes the whole picture on first use and serves tiles as views. Large ones
  get overviews like any flat image. Each is optional at build time.
* Alpha is not displayed yet. These formats flatten it onto mid gray, where dark and light
  content both stay visible, while TIFF still drops it. Real transparency needs an alpha
  format through the whole display path.
* AV1 and JPEG XL have no flag for lossless. AVIF storing RGB directly is treated as lossless.
  JPEG XL keeping the original colour space is called "likely lossless".

## Overviews
* A flat image larger than 2048 pixels gets generated levels, down to 2048 on the long
  edge, because fit-to-window would otherwise need every tile. They are built once, by
  reading the whole image, and kept in memory up to 2 GiB. Damaged tiles stay black.
* This is what a large flat file costs on first display: roughly a second per 100
  megapixels, single-threaded, because tiles are decoded under the source's one mutex.
  Pyramid files avoid it and open in milliseconds. Parallel decoding would speed it up, and
  a persistent cache of overviews could avoid it for flat files too.

## Viewer handles
* Handles over a zoomed image show the part in view as fractions of the image, so pixels
  1000-2000 of 4000 give a handle from 25% to 50%. They are at least 32 pixels long.
* They are light with a dark edge rather than from the colour scheme, as they lie on the image.
* They show while the image pans or zooms and fade about half a second after it stops, so that a
  static image has nothing over it.
* They only indicate and take no mouse input. The image itself can be dragged, and a draggable
  strip at the right edge would conflict with the panel that appears there in full screen.

Holding the left button on an image that is shown whole and reduced shows that place at 100%, and
releasing returns to the view as it was: a fitted image fits the window as it is by then, and an
image that has changed in the meantime keeps its own view. Otherwise the cursor is a hand if there
is anything to pan to. An image enlarged or at 100% and shown whole has neither, as holding would
change nothing useful. The first press of a double click does this too, for that moment.

## Display range
* Pixels are converted to `Bgrx8` on the worker thread, with a low and high value per image.
* Float images default to auto contrast (0.1% percentiles of the coarsest level), because
  they have no natural range. 16-bit images default to the full range, because stretching a
  photograph would falsify its exposure, so one that uses a small part of the range looks
  dark until auto contrast is turned on. Thumbnails follow the same default. The choice is
  per image and does not carry over to the next one.

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
  redraws through the worker. Live adjustment could use shaders too, now that
  `qt6-shadertools` is a dependency.
* Orientation is applied by the scene graph transform, not by rotating pixels.
* Generated levels are halved with the weights 1 3 3 1 rather than averaged over 2x2 pixels, and
  8-bit ones in linear light. What a level keeps of detail too fine for it stays there, moving
  with the image, and no later filter can tell it from the picture. On a fine test chart that
  was the visible moire; it costs about twice the time of the average, 220 ms against 120 ms for
  20 megapixels. Levels stored in a file, and scaled JPEG decoding, stay as they are.
* Levels go down to 1024 pixels on the long edge. Without sharp resampling the view moves to the
  next level earlier (a reduction of at most 1.6), as bilinear filtering skips pixels beyond that.
* Sharp resampling is optional and off by default, until a settings dialog exists. It uses
  Magic Kernel Sharp 2013 in linear light, in two separable passes. That kernel already
  contains the post-sharpening step, stretched along when minifying, so no separate pass
  follows, and another would sharpen twice.
* It is built from `ShaderEffect` and layers, public API only. Tiles cannot be filtered one by
  one, as the kernel reaches past their edges, so the view draws them into a texture at the
  level's own resolution, aligned to its pixels, and the kernel reads from that.
* `ImageSurface` holds the view, its handles and the filter, so that the large view, the preview
  and a future before and after view stay the same.
* The kernel stays on while the image moves. Switching to bilinear for movement and back when it
  stopped was tried, and showed as heavy flicker.
* It stays out at exactly a level's scale, where the kernel would sharpen pixels shown as they
  are; from 200%, where pixels are meant to be seen; and on the software renderer. Tiles
  outside the view are read for the kernel's margin, so it costs slightly more decoding.
* The first pass decodes through a table of 256 values, as its input is 8-bit. The second keeps
  `pow`, because a table for its float input was no faster. On Intel UHD 620 at 1920x1000, in the
  worst case just above 50% zoom, frames take 17-18 ms with the table and 18-19 ms with `pow`,
  against 12-13 ms with no conversion at all, so the rest of the cost is in reading the texels.
* The passes assume the sRGB curve, so with another display profile linear light is
  approximate.

## Colour
* LittleCMS (MIT) converts on the CPU in `toDisplay`, on the workers, so it is part of making
  a tile and the GPU only sees the result. It is required, unlike the optional formats.
* Only what is displayed is converted. Decoded pixels, the histogram and the file stay in the
  image's own colours, so editing can work in those too.
* An image whose conversion would change no colour by more than one 8-bit level gets no
  transform at all. Most files are sRGB, and those keep their zero-copy path.
* Transforms are cached per profile, format and display, since a folder tends to use one or
  two profiles.
* Relative colorimetric with black point compensation, not a setting: colours inside the
  display's gamut stay exact and shadows do not clip.
* 16-bit and float RGB convert directly to 8-bit, as going through 8 bits first would lose
  gradations. A stretched 16-bit image goes through 8 bits, and stretched floats are not
  converted, as they are data whose encoding may be linear.
* No profile means sRGB. A damaged profile, or one that does not fit the pixels such as CMYK
  on pixels already converted to RGB, is treated the same.
* What a file says without a profile is turned into one, so there is a single path: AVIF
  code points, PNG cICP, gAMA and cHRM in the order of the PNG specification, and EXIF for
  cameras set to Adobe RGB. PNG's gamma of 1/2.2 counts as sRGB, as that is what encoders mean.
* The display defaults to sRGB, which is correct where the compositor manages colour, as KWin
  does on Wayland. Choosing a monitor profile is for systems where nothing else applies one;
  doing so under such a compositor would correct twice.
* One display target for the application, not per window, until a second window exists.

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
* Appearance in its final form: a dark and a less dark look, an accent colour or following the
  system, with other colours derived from these, such as a darker accent or a mix. `AppScrollBar`
  and `PathBar` draw flat controls from the palette in the meantime.
* Formats: PCX, BMP, TGA, GIF. Animation in GIF, WebP, AVIF and JPEG XL.
* Camera RAW files, first only their embedded preview and EXIF, without developing the sensor
  data. Most (DNG, CR2, NEF, ARW, ORF, PEF, SRW, RW2) are TIFF structures holding a large JPEG
  preview, which the TIFF and EXIF readers here can mostly reach, though some keep it in a
  maker note or a tag of their own. CR3 (ISO base media) and RAF (a header of its own) need small
  readers of their own. The preview is often smaller than the sensor, and is what the camera
  made of it, so the details should say so. Developing the raw data would need LibRaw (LGPL or
  CDDL) and colour work of its own, so it is a much later step if at all.
* Tools: crop, compare, image strip builder, batch resize, convert and rename. Some act on
  the selection of the current panel.
* Possibly a dual-panel file manager. File icons are OS dependent, so they belong in the
  optional system integration layer, along with KDE or Windows places.
* The application's name is not decided, and "Image Viewer" is a placeholder. Once it is, use one
  folder name for settings and cache, such as `~/.cache/image-viewer`, rather than the current
  `ImageViewer/Image Viewer` (`src/app/main.cpp`), and move the existing settings file or accept
  losing it.

## Browser
* The selection belongs to `BrowseContext`, so each tab or panel has its own and tools read
  it from there. It is separate from the current image: Ctrl+click can remove the current
  image from the selection, while it stays shown in the preview. Moving to another image
  selects only that one, including moving in the large view.
* The browser and large view are two pages sharing one `BrowseContext`, each with its own
  `ImageView`. A page's view loads only while the page is active.
* Thumbnails are made with Magic Kernel Sharp 2013 from a stored level with at least twice the
  pixels, at the size they are shown, because a second resize would undo the sharpening. They
  never enlarge, so a tiny image stays tiny in its cell.
* A flat image is reduced directly from its pixels, without generating levels, which is faster
  (about 120 ms against 220 ms for 20 megapixels) and at least as clean. Only one too large to
  hold at once (512 MB) goes through generated levels, which are built a tile at a time.
* The CPU resize runs as the Magic Kernel across, the same down, then -1/4 3/2 -1/4 in each
  direction on the result, which equals Magic Kernel Sharp 2013 and reads fewer source pixels.
* Thumbnails are kept on disk in a SQLite file under the system's cache folder, up to 512 MB, with
  the least recently used going first. They are JPEG XL at distance 0.5, which is about the size of
  JPEG at quality 92 with about 40% less RMS error, measured on two small images, and still decodes
  at over 1000 per second. A build without
  JPEG XL uses that JPEG, without chroma subsampling, and treats entries it cannot read as missing.
  View > Clear Thumbnail Cache removes them all and rewrites the file, with SQLite's secure
  delete, so that neither pictures nor paths stay in its free space.
  Copies of the program running at once share it, as SQLite allows. Only a file that is damaged or
  of an older version is replaced; a locked one, or a newer version, means no cache for that run.
  A file's time and size say whether its thumbnail is still valid, and the size and colour
  settings are part of the key, as colour conversion is already in the pixels.
* A thumbnail stays until it is pushed out, even if its file was deleted, and it holds a small
  copy of the image and its path outside the folder it came from. Clearing the cache is to do.
* Thumbnails go through an async QML image provider on the shared work queue, below all
  other work. The newest request runs first, and Qt cancels requests when their delegate
  goes away. Qt deletes a response only after `finished()`, so `cancel()` must emit it.
* A new scan changes only the rows that differ, so the grid keeps its scroll position when a
  file is deleted. A changed order, such as a new sort key, resets the model. The current image
  and the selection are kept by name. If the current image is gone, the one that took its place
  becomes current.
* Sorting is by name, date modified, size or type, ascending or descending, and is remembered.
  EXIF date and rating wait for metadata that can be read in the background.

* The path bar lists subfolders on a worker, so a slow network folder cannot stop the window.
  It lists them in a scrolling popup rather than a menu, which creates an item for every
  folder. History belongs to `BrowseContext`, and returns to the image that was current.

* Going straight to a folder, as from the command line, makes `QFileSystemModel` know only the
  chain leading to it, so the tree could not reach anything beside it. `readParentsOf` reads the
  parents in full, in the background, and the folder is then centred as they fill in above it.
* The buttons to move around stand apart from the path, with arrows unlike its carets, because
  they looked like part of it. One star holds favourites and recent folders, kept by `FolderLists`.
* In the large view the wheel moves between images and Ctrl+wheel zooms. The preview has no other
  use for the wheel, so there it keeps zooming, which also works with Ctrl.

* Dialogs are drawn inside the window, which the system does not decorate, so `AppDialog` brings
  a frame, title bar and shadow. Messages share `MessageBox`: caption, text, an icon for info,
  question, warning or error, and buttons, with Enter, Esc and Ctrl+C as in standard message boxes.
* Qt Quick menus neither show shortcuts nor grow with their items, so `AppMenuItem` shows the
  action's shortcut dimmed on the right, as `KeyNames` writes it for the system, and `AppMenu`
  takes the width of its widest item.

## File operations
* Trash uses `QFile::moveToTrash`: the Recycle Bin on Windows and the freedesktop Trash on
  Linux. The latter is a folder layout rather than a service, so it needs no particular
  desktop and works on Hyprland and Sway as on KDE and GNOME. File managers see those files
  in their trash.
* If a file cannot go to the trash, the error says so and the file stays. Shift+Del deletes
  files permanently for such cases. It always asks, naming the files, with No focused, and the
  answer applies to the files named even if the selection changes. Folders can only be trashed.
* The trash takes no confirmation, because it can be undone there. A folder does ask, as it
  can hold far more than the user sees. Home, its parents and drive roots are refused.
* One job runs at a time per `BrowseContext`, in the background and cancellable between files.
  Failures are collected per file and the rest continues. Afterwards the folder is scanned
  and new files in it become the selection.
* Existing files are never overwritten in place. On a conflict the user chooses skip, replace
  or keep both, and replace sends the old file to the trash first. A file copied onto itself
  gets a free name, and one moved onto itself stays.
* Copies keep the modification time, so they still sort by when they were taken.
* Folders can be created, renamed and trashed from the tree, but not yet copied or moved.
* The folder is watched and scanned again 200 ms after changes stop coming in. F5 does it
  by hand, for network folders that do not report changes.

## Lossless JPEG tools
* Rotate 90° either way, 180°, flip either way and apply the EXIF orientation. Each works on
  the image as shown, so the EXIF orientation is applied first and the tag is reset to 1. The
  menu is enabled only when every target is a JPEG, judged by extension, and the decoder
  reports any file that is not one.
* The DCT blocks are moved and mirrored with the libjpeg coefficient API, so nothing is
  decoded or recompressed. TurboJPEG's transform would do the same, but its header is not
  in every distribution's libjpeg package, and this needs only `jpeglib.h`. The output matches
  `jpegtran -perfect` and `jpegtran -trim` pixel for pixel for 4:4:4, 4:2:2, 4:2:0 and 4:4:0,
  baseline and progressive. Progressive files stay progressive; baseline ones get optimized
  Huffman tables, so files come out a little smaller.
* An edge that has to move must end on a whole block, 8 or 16 pixels. Many cameras do not
  give such sizes (4592x3448 has 8 extra rows), so trimming, up to 15 pixels, is offered in
  the menu and the confirmation. Without it such a file is reported and left as it was.
* Markers are copied, with the EXIF pixel dimensions updated. The EXIF thumbnail is unlinked,
  as it would show the old orientation. The MPF index and anything after the end of the image,
  such as the previews some cameras append, are dropped, as their offsets no longer hold.
* Files are replaced through a temporary file and a rename, keeping permissions. The
  modification time changes, so thumbnails are made again. The view showing an edited file
  loads it again.
* The files change in place, so the first use asks, with an option not to ask again. The menu
  turns asking back on.
* Convert to JPEG XL repacks the coefficients with libjxl and stores what rebuilds the original
  file. Each result is rebuilt and compared byte for byte before it is written, and a file that
  does not come back the same is reported and not converted. The new file keeps the JPEG's
  modification time. An existing `.jxl` is never overwritten. The originals can go to the
  trash, each only after its own conversion succeeded, and the dialog turns into a warning
  when that is on.

## Clipboard
* Copy puts files on the clipboard as URLs, also in the format file managers on GNOME look for.
  A single file is also added as an image, in sRGB and at most 16384 pixels along its longer
  edge. It is decoded on a worker and added a moment later, because clipboard managers ask for
  the data at once, and decoding then would stop the main thread.
* Paste with files copies them into the current folder. Otherwise it saves the image as a new
  file named after the time, such as `2026-12-24_125959.png`, adding `_1` if taken. PNG is fixed
  until the configuration dialog exists.
* Cut is not implemented. Files cut in another application are copied on paste, leaving the
  originals in place.

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
* Screenshots and scans will be documents with an in-memory base and no path. Clipboard paste
  is not one of them: it writes a file straight away, see Clipboard.
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
* An image that changes on disk while shown is not reloaded, including one replaced by a copy.
  Its thumbnail does update. The lossless JPEG tools are the exception, as they say which files
  they changed.
* Reduced levels made by the viewer, for PNG, WebP, JPEG XL and the like, are averaged in linear
  light, while a JPEG's come from libjpeg's DCT scaling, which averages the gamma-encoded
  values. Fine detail therefore looks slightly brighter below 100% in the former, and the
  histogram, taken from a reduced level, differs a little. A JPEG and the JPEG XL repacked from
  it decode within 3 levels of each other at 100%, but not at fit.
* The grid lists images only, not subdirectories. A file renamed to an unsupported extension
  leaves the grid, though it is still on disk.
* File operations have only been run on Linux. On Windows a file that is open cannot be renamed
  or trashed, and the view can hold the current image open, for instance a TIFF. Expect an error
  message there rather than the operation working, until this has been tested.
* Replace sends the old file to the trash before copying. If the copy then fails, the old file
  is in the trash and the error names the file.
* CMYK JPEGs use a rough conversion, and their CMYK profile is not applied.
* HDR images (PQ and HLG curves) are not tone mapped. In AVIF and PNG they are shown as sRGB,
  so they look flat. A JPEG XL or a file with such an ICC profile goes through that profile
  and looks dark.
* Colours outside the display's gamut are clipped. On Wayland that display is sRGB as far as
  the application knows, so a wide-gamut monitor shows no more than sRGB.
* The monitor profile is not detected. Windows and X11 could provide one (`GetICMProfile`,
  the `_ICC_PROFILE` atom), which belongs in the system integration layer.
* 16-bit gray and all stretched floats are not colour managed.
* Tiles do not overlap, so a faint seam is possible at tile edges between 100% and 200%
  zoom. From 200% up, filtering is nearest-neighbour and this does not apply.
* Opening decodes the smallest level, then the one the view needs. For JPEG this costs
  about twice the time, because both need the same entropy decoding.
