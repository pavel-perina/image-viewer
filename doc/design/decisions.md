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
* JPEG decodes the whole image and serves tiles as zero-copy views, because a JPEG cannot be
  entered mid-stream. Its reduced levels are halved like any flat image's, so they look the
  same as those of a PNG or the JPEG XL repacked from it. libjpeg's 1/2, 1/4 and 1/8 scaled
  decoding only gives the first rough preview, and the levels of a JPEG too big to decode whole
  (over 8 GB as pixels). The full decode stays in memory while the image is open, about 100 MB
  for 25 megapixels.
* Pixel formats: `Bgrx8` for 8-bit colour, the layout Qt and the GPU take directly; `Gray8`,
  `Gray16` and `Gray32F` for one channel; `Rgba16` and `Rgba32F` for deeper colour. Always
  interleaved, rows with a stride, in shared storage so crops and tiles are views. What the
  channels mean, the colour profile and what the file stored, lives beside the pixels in the
  subimage's description.
* 8-bit gray JPEG, PNG, TIFF and JPEG XL decode to `Gray8`, a quarter of the memory of `Bgrx8`,
  which matters for large scans and gray images from instruments. It is spread to `Bgrx8` only
  per tile on its way to the screen, and goes through edits and saving in one channel, mixed in
  linear light like 8-bit colour. WebP and AVIF gray still decode as colour.
* TIFF keeps the file's precision: 8-bit becomes `Bgrx8` or `Gray8`, higher depths become `Gray16`,
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
* Pixels are converted for display on the worker thread, with a low and high value per image:
  8-bit images to `Bgrx8`, deeper ones to `Rgba16` (`toDisplayWide`, colour managed 16 to 16
  bits) and then to half-float textures, so that stretching and adjusting them do not band. The
  software renderer gets `Bgrx8` throughout, as it shows no more. Half floats hold about 11 bits
  near white and more below, and the shaders work in float.
* The colour preview's shader runs on the level's pixels before Magic Kernel Sharp, as the
  adjustment comes before resampling once it is applied, and both read half-float textures.
* The last pass dithers: Magic Kernel Sharp's second, or else the colour preview's, which deeper
  images pass through for that even when nothing is adjusted. Triangular noise a step of the
  output wide (`ViewSettings.ditherStep`, 1/255 for an 8-bit window) keeps the average of each
  area at its true value, so smooth gradients show grain finer than the eye resolves instead of
  bands. The hash needs no integers, as the kernel's shaders still build for GLSL ES 1.
* Float images default to auto contrast (0.1% percentiles of the coarsest level), because
  they have no natural range. 16-bit and 8-bit gray images default to the full range, and offer
  auto contrast, since gray images are often measurements that use a small part of it. 16-bit
  images keep the full range by default because stretching a
  photograph would falsify its exposure, so one that uses a small part of the range looks
  dark until auto contrast is turned on. Thumbnails follow the same default. The choice is
  per image and does not carry over to the next one, unless it is kept with the padlock beside
  the button (Keep Auto Contrast, remembered between sessions): then every image with more than
  8 bits has it. Turning Auto Contrast off also stops keeping it, as the next image would
  otherwise have it again. 8-bit images never do: they are shown as they are.

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
* The GPS position, from our reader only, so not yet for TIFF, shows under Location as
  degrees with five decimals, about a metre. It is kept only when latitude, longitude and
  their hemispheres are all present and in range. The coordinates open GeoHack, the page
  Wikipedia uses for coordinates, which lists OpenStreetMap, Mapy.com, Google Maps,
  topographic and national maps to choose from, instead of the viewer picking one service.
  Opening a link sends the position to that site, and only on a click.

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
  20 megapixels. Levels stored in a file stay as they are.
* Levels go down to 1024 pixels on the long edge. Without sharp resampling the view moves to the
  next level earlier (a reduction of at most 1.6), as bilinear filtering skips pixels beyond that.
* Sharp resampling is on by default and can be turned off in the menu. Bilinear enlarges a level
  by up to 1.25, which shows as blur just above each level's scale. Sharp resampling uses Magic
  Kernel Sharp 2013 in linear light, in two separable passes. That kernel already
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

## Look
* Qt Quick Controls in the Fusion style, chosen in `main.cpp` unless `QT_QUICK_CONTROLS_STYLE`
  asks for another, to compare. Compared on 2026-11-09: Material, Universal and FluentWinUI3 have
  dark themes but larger controls, and the panels, laid out for Fusion, overflow in them;
  Material also paints the bars in its own colours.
* Fusion takes a palette of our own, dark, as Fusion draws every state of every control from the
  palette. `Theme.qml` holds every colour, and `Main.qml` gives all of Fusion's roles from it,
  disabled ones included: any role left out comes from the system theme, which on a light desktop
  gave white menus and black text over our dark panels. Popups take the window's palette, and
  there is one window. The surround of the image stays neutral gray, and what is drawn over
  pictures stays light. A light theme would be a second set of values. The plan from here: icons of our own, single-colour SVG tinted by the
  theme. Controls that still look wrong get restyled one by one in a style that falls back to
  Fusion for the rest. States are layered rather than drawn per combination: a base colour from
  the role, darker when pressed, a light overlay when hovered, dimmed when disabled, and a focus
  ring for keyboard focus on top.
* Inter 4.1 is embedded and set as the application font at the size the system asks for: the
  variable file only (880 KB), as it holds every weight; no italics are used. Characters it lacks
  come from the system's fonts. Its digits are proportional; labels with values that change
  under the cursor can turn on tabular figures (`font.features: { "tnum": 1 }`). The variable
  file has no hinting, which may look soft at small sizes on Windows at 100% scaling; hinted
  static files would be the fix. The tests keep the system font, as they do not go through
  `main.cpp`.

* `Theme` also holds the sizes that pages share: the heading and caption text sizes, the
  monospace family, and the tool panels' width, margin and row spacing. A number that lays
  out one page alone stays in that page; moving every such number would make `Theme` a list
  of things used once.
* No header, of core or of the UI, includes a header of a codec or other outside library, so
  no class depends on libtiff or the like through another's private part. That is the reason
  for pimpl here, and the rule is about that. Core's value types may appear in UI headers,
  and whether a class hides its own state behind pimpl is a preference.

## Threads
* Own work queue instead of oneTBB, because visible-first priorities and cancelling work
  that scrolled away are the central requirement. It defaults to 2/3 of the logical cores.
* Anything posting results to a QObject must cancel and wait for its work queue group in
  the destructor.
* Reading a file whole to decode it goes through `DiskGate`: one file at a time per disk unless
  the disk is solid-state, so thumbnails of a folder on a spinning disk are read one after
  another instead of ten workers seeking between ten files. Decoding stays parallel. Opening an
  image to show it goes before thumbnails waiting for the same disk, and a cancelled thumbnail
  stops waiting.
* The disk is found per folder and cached: on Linux the mount from `/proc/self/mountinfo`, then
  `queue/rotational` of its block device (the whole disk for a partition, the source device for
  Btrfs, whose subvolumes have device numbers of their own); network file systems count as
  seeking, memory ones as solid. On Windows the volume of the path, `GetDriveType`, and for a
  fixed drive the seek penalty from `IOCTL_STORAGE_QUERY_PROPERTY`, which needs no admin rights.
  Removable drives and anything not recognised, and every disk on other systems, get the gate of
  a spinning disk. USB sticks often report themselves as rotational, which suits them, as they
  are slow at reading several files at once too.
* Not gated: TIFF, which libtiff reads in parts while decoding, so a TIFF thumbnail would have to
  hold the disk for its whole decoding. Workers waiting at the gate are not free for other work
  meanwhile; opening an image still finds one soon, as each read on a spinning disk is short.

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
* Keys go to what has the focus. In the browser the thumbnails and the folder tree each handle the
  arrows, Home, End, PgUp, PgDown, Backspace and Enter themselves, and start with the thumbnails;
  the window's shortcuts keep only what means the same everywhere, and moving between images in
  the large view. The tree takes F2 and Del from the window's shortcuts (`Keys.onShortcutOverride`)
  for the folder, and Shift+Del too, so that it never deletes the files instead. The keys return to
  the page when a dialog closes, a tool ends or the path bar is left with Enter or Esc: a hidden
  text field otherwise keeps the focus and swallows the arrows. Moving through the tree opens
  folders without unfolding them, as file managers do; unfolding is Right.

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
* Thumbnails drag out as files (`text/uri-list`), the selection or the one under the pointer, to
  any application or to a folder of the tree. The drag is a copy unless Ctrl, Shift or the other
  side asks for a move, as in file managers. It is started by `BrowseContext::dragFiles` rather
  than Qt Quick's `Drag`, which offers no default action and so makes a move the default. A move
  is never finished here by deleting: the other side moves the file, or it stays a copy, as with
  Explorer moving between disks.
* Files dropped on a folder of the tree are copied there, or moved when the drop asks for it,
  through the same path as Copy To and Move To. The other side is always told it was a copy, so
  that it never deletes a file that was skipped here. The folder the files are all in already,
  and a folder dropped into itself, are refused. Dropped on the rest of the browser, files go the
  same way into the folder shown. A folder dropped there opens instead, as folders cannot be
  copied yet, and a file dropped on the large view opens. Over the tree only a folder takes a
  drop.

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
* Markers are copied, with the EXIF pixel dimensions updated. `tiff:Orientation` in XMP is
  reset to 1 in place, by finding it in the text, so no XML parser is needed and the packet
  keeps its size; a packet binding the TIFF namespace to another prefix is left alone, as are
  the pixel dimensions XMP may repeat. The EXIF thumbnail is unlinked,
  as it would show the old orientation. The MPF index and anything after the end of the image,
  such as the previews some cameras append, are dropped, as their offsets no longer hold.
* Files are replaced through a temporary file and a rename, keeping permissions. The
  modification time changes, so thumbnails are made again. The view showing an edited file
  loads it again.
* The files change in place, so the first use asks, with an option not to ask again. The menu
  turns asking back on.
* They are disabled on an image with edits in effect, even saved ones, as they would change the
  file while the view shows the edits; undoing them makes the tools available again.
* Convert to JPEG XL repacks the coefficients with libjxl and stores what rebuilds the original
  file. Each result is rebuilt and compared byte for byte before it is written, and a file that
  does not come back the same is reported and not converted. The new file keeps the JPEG's
  modification time. An existing `.jxl` is never overwritten. The originals can go to the
  trash, each only after its own conversion succeeded, and the dialog turns into a warning
  when that is on.

## Writers
* `encodeImage` and `writeImage` in core write JPEG, PNG, TIFF, WebP, AVIF and JPEG XL. Each format
  has its own options struct, and the struct passed chooses the format. Save As uses them.
* Defaults: JPEG quality 90, 4:2:0, progressive. PNG level 6. TIFF Deflate with a predictor.
  WebP quality 85, effort 4. AVIF quality 70, speed 6, 4:2:0. JPEG XL distance 1, effort 7.
* Lossless: PNG and TIFF always, WebP and JPEG XL as an option. Lossless AVIF and lossless
  JPEG are left out, as almost nothing uses them.
* Depth: each format keeps what it can. 16-bit stays 16-bit in PNG, TIFF and JPEG XL, becomes
  10-bit in AVIF, and 8-bit in JPEG and WebP. Float stays float in TIFF and JPEG XL and becomes
  16-bit in PNG and AVIF, taking 0 to 1 as the range. The unused fourth channel is never written,
  as the viewer does not keep alpha.
* Images whose pixels are all gray, at any depth, are written as grayscale where the format has
  it (JPEG, PNG, TIFF, JPEG XL). An RGB profile then becomes a gray one made with lcms from its
  green curve and D50 white. For a matrix profile with one curve for all three channels, which
  covers sRGB, Adobe RGB and Display P3, that shows every gray exactly as before. Other RGB
  profiles keep the pixels RGB. WebP and AVIF have no gray here.
* The ICC profile is written when its colour space matches the pixels; without one, PNG gets
  an sRGB chunk, AVIF sRGB colour codes and JPEG XL an sRGB colour encoding. Lossy JPEG XL
  stores a profile it recognises as a compact description and gives back an equivalent one,
  not the same bytes.
* EXIF is written to JPEG, PNG (eXIf), WebP, AVIF and JPEG XL, with the orientation reset to 1,
  the pixel dimensions set and the thumbnail unlinked, as the pixels are written as given.
  Not to TIFF yet, where it needs its own directory.
* XMP is written as given, with `tiff:Orientation` reset the same way as in the lossless
  JPEG tools: APP1 in JPEG, an uncompressed iTXt chunk in PNG, tag 700 in TIFF, the XMP chunk
  in WebP and an `xml ` box in JPEG XL; AVIF through libavif. A JPEG packet longer than one
  marker (about 64 kB) is left out, as it would need extended XMP. IPTC is not written.
* Files go through a temporary file and a rename, and an existing file is replaced only when
  asked.

## Save As
* File > Save As (Ctrl+Shift+S) works on one image, the current one in the viewer or the one
  selected in the browser. The file chooser picks the name, and the extension picks the format.
  A second dialog then shows that format's options, remembered per format, and what to do with
  the metadata.
* `saveSubimage` in core opens the file again by path, so the view and its levels are not
  involved, decodes the page at full size, turns it upright as shown, and writes it with the
  writers. It runs as a file job, with its progress shown, and the new file becomes the current
  one, so the viewer shows the result. A view showing a replaced file loads it again.
* The page shown is saved; an animation saves its first frame. An image whose full-size pixels
  would take over 2 GB is refused, as it is held whole two or three times while saving.
* Colours are not converted: the pixels keep their colour space and the profile goes with them,
  including one made from what the file says (CICP, PNG chunks, Adobe RGB from EXIF).
* The readers keep the EXIF block and the XMP packet as stored (`rawExif` and `xmp` in
  `SubimageInfo`): JPEG APP1, PNG eXIf and uncompressed iTXt, WebP chunks, AVIF through libavif,
  JPEG XL boxes and TIFF tag 700. A TIFF file's EXIF is not kept, as libtiff gives fields rather
  than bytes; it would have to be rebuilt.
* Keeping camera data is the default. Removing the location empties the GPS directory, zeroing
  its values as well so that no trace stays in the bytes, and overwrites the `exif:GPS*`
  properties in XMP with spaces, which keeps the packet's size and its XML valid. Place names
  typed into XMP stay.
* Saving a JPEG as JPEG compresses it again, and the dialog says so and points to the lossless
  tools.
* The format's options could still move into the chooser, so that saving is one window.

## File chooser
* Opening, saving, Copy To and Move To, the display profile and `.cube` files all use a chooser
  of our own (`FileChooser.qml`) rather than the system's or Qt's dialog. The dialogs those give
  differ per platform and behaved badly where it matters: choosing another type left the
  extension of the name as it was, and the save button acted on a folder selected in the list,
  opening it rather than saving.
* It is drawn inside the window, at most of its size, and is modal, so window shortcuts do not
  see what is typed into it. Its parts come from the browser: the path bar with its crumbs,
  subfolder menus and history (with its own browsing context, and without adding the folders
  to the recent ones), and the folder tree, made only while the chooser is shown since each tree
  watches the file system. On the left above the tree are the places (`FolderLists::places`:
  home and its usual folders, the disks, leaving out what the system mounts for itself and parts
  of the system disk such as Btrfs subvolumes), the favourites and the recent folders.
* The files come from `FolderListing`, a list model of one folder read in the background and
  again when the folder changes: folders first, hidden entries left out, names in natural order,
  the type from the MIME database. `FileListView.qml` shows it either as names in columns that
  scroll sideways (brief) or as rows of name, size, type and date with headers that sort
  (details); the choice is remembered. It is meant to serve elsewhere too, such as a list view
  in the browser.
* Saving takes the name typed, in the folder shown, and nothing else. A file clicked in the list
  only fills in the name. Choosing another type replaces the extension of the name with the
  type's, or adds it; typing a known extension chooses its type; a name without a known one gets
  the type's. A name of a folder opens that folder and leaves the file name as it was. An
  existing file is replaced only after asking, and saying no keeps the chooser open.
* Opening: Enter or a double click opens a folder or opens the file; the button does the same
  for a name typed or the entry selected. Choosing a folder chooses the folder selected, else the
  one shown, and the button names it.
* Types are given as `{label, extensions}` rather than filter strings, so nothing parses labels.
* The browser has the same places at the end of the star menu in the path bar, under a heading
  that folds them away and is remembered, rather than as a panel of their own.
* Not yet: selecting several files, typing to jump to a name, icons other than a drawn folder,
  and `~` for the home folder in a typed name.

## Clipboard
* Copy puts files on the clipboard as URLs, also in the format file managers on GNOME look for.
  A single file is also added as an image, in sRGB and at most 16384 pixels along its longer
  edge. It is decoded on a worker and added a moment later, because clipboard managers ask for
  the data at once, and decoding then would stop the main thread.
* Paste with files copies them into the current folder. Otherwise it saves the image as a new
  file named after the time, such as `2026-12-24_125959.png`, adding `_1` if taken. PNG is fixed
  until the configuration dialog exists.
* Cut puts the files on the clipboard marked as cut, as file managers do: `cut` in GNOME's format,
  `application/x-kde-cutselection` for KDE, and the "Preferred DropEffect" of a move on Windows.
  Nothing happens to them until they are pasted, and it is the application that pastes which
  moves them. No image is added, as the files are to go. Pasting files cut here or in a file
  manager moves them into the current folder and empties the clipboard, as they are no longer
  where it says.

## Comparing
* Two pictures of the same size are shown one each side of a vertical line that can be
  dragged: View > Before and After (B) for an edited image, and View > Compare Two Images for
  two images chosen in the browser, the one shown against the other. Holding O, or the toolbar's
  Original, shows the picture before editing alone while held.
* The line is a fraction of the view's width, so it stays where it is while zooming and
  scrolling. Both views zoom and scroll together whichever one is used: `ImageView.linkedView`
  copies the viewport each way while both show pictures of the same size, and a view that
  loads takes its partner's zoom and place instead of fitting.
* Each side is a whole `ImageSurface`: the second is clipped left of the line
  (`ImageSurface.clipLeft`) but keeps its full size, so its geometry, resampling and colour
  preview are unchanged, and the mouse left of the line reaches the picture under it.
  `CompareSplitter` does the linking, clipping, handle and labels for any two surfaces.
* Before is the file's page without edits, read from the same decoded source as the view
  (`ImageView.showPictureOf`), so nothing is read again. While adjusting colours it is the
  picture without the preview instead, which is always the same size and is what the sliders
  change; after applying, the comparison goes on against the result. Cancelling ends it.
* Pictures of different sizes are refused with a message, as cropping, resizing or
  straightening make them, and so are two files of different sizes. Comparing ends on another
  image, and when Crop or Straighten starts. It is not a tool, so it works with Adjust Colours.
* Holding a key needs its release, which `Shortcut` does not give, so `KeyHold` watches the
  window's key events without taking them.

## Probe
* A mode of the large view, like crop, with its panel in the same place. It reads one pixel at
  a time; averaging an area can come later if it is missed.
* The pixel is read from the full-size level of the file, never from what is on screen, so a
  zoomed-out view, a rough preview or the display's colour conversion do not change the numbers.
  The view maps the pixel shown back to the stored one, through the orientation.
* Values are shown as stored: 0 to 255, 0 to 65535, or floats as they are, which may lie outside
  0 to 1. Fractions divide by the format's range, with five decimals for 16 bits, where three
  would hide the difference. The hex is always 8-bit, as that is what other programs take.
* With a colour profile that is not sRGB, the sRGB hex and values are added, converted through
  the profile. The swatch shows the sRGB colour then. Raw values stay first, because they are
  what the file holds.
* A click or drag reads; releasing remembers the pixel read last. Arrows move by one pixel and
  only read; Enter remembers. The right button pans and the wheel zooms, as in crop.
* Remembered colours are kept in the settings, newest first, up to 48, without repeating the
  same pixel twice in a row. Clicking one shows its values and copies its hex.

## Editing
Built so far: the edit list with undo, crop, resize, straighten, grayscale, saving over the original and
the question before leaving an edited image. Still planned lists the rest.

* `Document` in core holds one page of an opened file and its edits. The page is decoded once
  at full size and turned upright, and edits apply to that, in coordinates of the upright
  picture. A crop is a view of it, so it costs no copy. Each change hands the view a new source
  of one upright page, with the original's metadata saying it is upright, so the view, the
  histogram and Save As work unchanged.
* Editing is allowed wherever Save As is: images small enough to be saved whole. What a palette or alpha image loses is what Save As loses anyway.
* The view keeps the edits of the page shown. Moving to another image, page or folder, back to
  the browser, quitting, and the file operations that change the file on show (rename, trash,
  delete, move, the lossless JPEG tools) ask first: Save (as), Discard or Cancel. Saving to the
  same name replaces the file, after the file chooser's question, and the view reloads it.
* After a crop the view fits the image again, as the old zoom and position mean little for the
  new picture.

### Tools drawn on the image
* Crop and straighten each had their own copy of the view binding, the coordinates and the
  panning, and the probe a third in QML. `ImageOverlay` now holds what they share, and all
  three derive from it, so the next tools (measurement, annotations) start from it.
* The base owns the view, the mapping between image pixels and the item, panning (the right
  or middle button, or the left one with Space held) and telling a drag from a click. Its
  mouse and key handlers are final: a tool sees only a press, drag and release of the left
  button, and keys other than Space.
* Each tool keeps its own model in C++, in pixels of the image as shown, and paints it with
  `QPainter`. Drawing in QML over a C++ model would put the geometry in two places.
* A painted item redraws its whole layer on the CPU when the view moves. That has been fast
  enough; if it stops being so, the base can change how it draws without the tools changing
  what they draw.
* Tools may look different. Crop and the probe's frame are drawn on whole device pixels.
  The base draws the common look for the rest: a thin line of a colour over a wider dark one,
  and round grips. Arcs and text join them when a tool needs them.
* Painted C++ items cannot read the QML `Theme`. The page makes one `OverlayStyle`, fills it
  from `Theme` and gives it to every tool, so `Theme` stays the one place for colours. Colours
  only one tool has, such as straighten's two for vertical and horizontal lines, are
  properties of that tool, set from `Theme` the same way.
* The last colours written into pages and overlays moved to `Theme` on 2026-11-13: the frame,
  scrim and shadow of a dialog, the carets, the scroll handles over the image, the glyph of a
  message box, and through `OverlayStyle` the surround and grid of straighten's result and the
  pattern standing in for a pixelated box. The pattern and the dialog frame changed a shade by
  taking colours that were already there. Left where they are, on purpose: black, gray and
  white in the levels control, which are the tones it sets and not its look; the magnifying
  glass cursor, black and white like a system cursor; and the number on a numbered point,
  dark or light by the colour under it, which is drawn into the picture.
* What is drawn on pictures does not follow a light or dark palette: it has to show on
  photographs, not on panels.
* No shape or style model yet. It waits for the decision on how annotations are stored.

### Panels of the tools
* Crop, straighten, adjust, annotate and the probe each had their own copy of the same sheet:
  the background, the block against the mouse, the heading, the column, and at the bottom
  Reset, Cancel and Apply, while the page repeated the slide from the right five times.
  `ToolPanel` now holds all of it, and a tool's panel derives from it and adds its rows.
* The base has the buttons every tool ends in, Cancel and Apply, and one more on the left
  where a tool asks for it by giving it a text; so far that is always Reset. The apply button
  can be renamed (Crop) and disabled. A tool that changes nothing (the probe) turns the
  buttons off.
* The rows scroll, in the base, when the window is too low for them (Annotate with text's
  options is taller than a window that is not full screen). The heading and the buttons stay
  in view. It scrolls only when the rows do not fit, so a drag on a panel that fits is never
  taken for scrolling, and a field reached with Tab is scrolled into view.
* The scroll bar sits in the panel's right margin, so the rows are as wide whether they scroll
  or not. They used to narrow to make room for it, and a panel of wrapped text that only just
  fitted then switched between the two widths each frame: the Straighten panel did so with
  the program's own controls, and pictures stopped loading.
* The rows are the default property, so a derived panel reads as a list of its controls. Only
  items become rows: `Settings`, `Binding`, `Connections` and the like may sit among them. A
  dialog a panel opens must not be an item among the rows; Adjust makes its chooser of colour
  tables on first use instead.
* The page says only whether a panel is `revealed` and whether to animate. The place and the
  slide are the panel's.
* The Info panel is not one of these: it is no tool, it scrolls, and it has no buttons.

### Choosing a typeface
* Some fonts install each weight, width and slant as a family of its own: Iosevka alone listed
  about fifty, "Iosevka Extralight Extended Oblique" among them. A family is left out when it
  is another family's name followed by style words only (Light, Extended, Oblique, and the
  short forms fonts use: Lt, Ex, Obl, SmBld, ...). Iosevka Term, Inter Display and PT Sans
  Caption stay, as they are designs of their own. Here that took the list from 358 to 127.
  The styles left out cannot be chosen; Bold still can. A second list of the family's styles
  in place of Bold would bring them back, if missed.
* The typeface is chosen in `SearchCombo`: a combo box that opens with a field on top and the
  families that fit what is typed below it, best first and with the letters matched bold and
  underlined, each drawn in its own font. A tree of families and styles was considered; with
  styles folded, the list is short enough that typing a few letters finds any.
* The matching (`core/fuzzy_match`) follows how fzf behaves, written from its description: the
  letters in order, scored per letter with bonuses for the start of a word and for letters
  together, and a cost for gaps; ties go to the shorter name, then the order given. Words of
  the query separated by spaces each match anywhere. Its numbers are fzf's, so that it ranks
  as people used to fzf expect. It depends on nothing else in the program, nor does
  `SearchCombo` on fonts, so both can serve other long lists, or other programs.

### One tool at a time
* The viewer page had a flag for each tool (`cropping`, `probing`, `straightening`,
  `adjusting`, `annotating`), and every function that started a tool cleared the other four by
  hand, as did leaving the image or the page. A sixth tool would have meant editing them all.
  The page now has one `tool`, a name or empty; the flags are read from it, so nothing can
  set two.
* `leaveTool()` ends whatever tool is at work without applying it, with the cleanup Adjust and
  Annotate need. Starting a tool leaves the one before it this way, then names itself.
* Each tool keeps its own start, apply and cancel under the old names, as what they do
  differs: Crop reads its rectangle before it ends, Adjust ends before it applies, the probe
  outlives an edit, and comparing goes with Adjust and no other tool.
* Enter and Esc are the page's to decide, so the window has one shortcut for each instead of
  one per tool. `applyTool()` applies the tool at work, and `enterApplies` says when Enter may:
  not in the probe, and not while a field of the panel has the keys. `stepBack()` takes one step
  back (a selected shape let go, an editing tool given up, a comparison ended, the probe
  stopped) and says whether there was one; only when there was none does the window leave full
  screen or the image.
* The rule stays in QML: it is state of the page, and each step calls into a panel or an
  overlay there. If it grows conditions worth testing apart from the page, it moves to C++.

### Annotations
* Annotations are burned into the pixels when applied, as one edit in the edit list, so undo,
  Save and Save As treat them like any other edit and every format can hold them. Keeping
  them editable after saving would need a sidecar or a metadata block, and is left open.
* While being made, a shape is a kind, a list of points in image pixels and a style
  (`core/shapes.h`). Moving, selecting and removing are written once for all kinds. A kind
  decides where it is grabbed and what moving a grip does, since grips are not always points:
  a rectangle has four corners from two points, and a freehand stroke the corners of the box
  around it. Measurement is to reuse this, with rules such as lines kept parallel in the
  same place.
* Sizes are in image pixels, so a shape looks the same at any zoom and in the saved file.
* A highlighter is a style, not a kind, so rectangles and polygons can both be one. It
  multiplies, as ink does: white paper turns yellow and black text stays black. On dark
  pictures it barely shows; replacing the hue would suit those and may be added as a mode.
* Shapes are drawn by Qt, which has the fonts and curves, into an 8-bit layer, and the core
  blends the layer into the picture (`core/image_layer.h`), so a 16-bit picture stays 16-bit.
  Pixelating changes what is under it, so it is done on the picture first, not in the layer.
* The layer's black and white are the ends of the range the picture is shown in. On a 12-bit
  picture in a 16-bit file, white drawn at 65535 would be far above everything shown.
* Colours drawn are taken as values of the picture's own colour space, not converted from
  sRGB. On a wide-gamut picture a red is therefore that space's red.
* Drawing in colour on a gray picture turns it into a colour one, so a red arrow stays red.
  A gray colour profile then no longer fits and is dropped.
* Edit > Annotate (D) has Select, Line, Arrow, Rectangle, Ellipse, Marker, Pixelate, Text and
  Number. With a drawing tool a drag draws and a click selects; with Select a drag moves.
  Grips reshape the shape selected, Delete removes it, and Esc lets go of it before it leaves
  the tool. Nothing is in the picture or the edit list until Apply.
* The style shown is that of the shape selected, or else of the tool. Each tool remembers
  its own between runs, so the marker stays yellow while arrows stay red. Until a width is
  set, lines start at 1/500 of the picture's long edge and blocks at 1/60.
* Rectangles and boxes keep their corners on whole pixels and draw their line inside them, so
  every edge lies between pixels and stays sharp.
* A marker and a pixelated box depend on the picture under them, so they are previewed from
  its real pixels with the code that applies them: the view reads the picture at full size
  when the tool starts and hands out the changed part for each such shape. Parts above 1.5
  megapixels are reduced first, to keep up with the mouse.
* Text is a box, drawn or placed with a click, whose words are typed in the panel, not on
  the picture: typing in place would need a caret and selection drawn by hand. The box gives
  the place and the width to wrap at, and longer text runs on below it. The typeface and bold
  are chosen from those installed; the size is that of the typeface in image pixels, which
  is taller than its capitals, and starts at 1/50 of the long edge.
* Letters are drawn by Qt's text renderer, fitted to the pixel grid, which keeps small text
  sharp. Drawing the letters from their outlines made small text thin and soft, and with the
  even-odd fill rule left dark dots where the strokes of a letter overlap.
* A shape is drawn as one flat area for the whole shape. Drawn in overlapping pieces, a
  colour that lets the picture through came out darker where they met.
* The dark edge and the shadow are made from the pixels the shape covers (`core/coverage.h`),
  not by growing each kind's geometry: the coverage is grown for the edge, and that is moved
  and blurred for the shadow. It is one piece of code for every kind, it fits what is really
  drawn, hinted letters included, and it allows soft shadows. Growing geometry needed a rule
  per kind and gave text an edge that did not match its letters.
* Growing is by distance from the shape's edge, taking a partly covered pixel as having the
  edge inside it, so the width need not be whole and the edge can fade over more than one
  pixel. Only pixels on the shape's edge are worked from, so the cost follows the length of
  the edge and not the area.
* Unless set, the dark edge is a quarter of the line (4% of a text's size) and at least two
  pixels, fading over one and a half: thinner did not show on small text. The shadow lies
  right and down by a little over half the line and is blurred by 0.4 of that distance.
* The edge's width, the shadow's distance and its blur can be set, per tool like the rest of
  the style. A shape's shadow has a distance each way; the panel sets both from one number,
  which is enough until a direction is wanted.
* The view works these out only for what it shows, and for a reduced picture at the size
  shown. A shape as large as a 4K screen with edge and shadow takes about a tenth of a
  second, an ordinary one a few milliseconds; they are not kept between paints yet.
* While the picture is shown at its own size or enlarged, shapes are drawn as pixels of the
  image, only where they are in view, and those pixels are shown enlarged like the picture's.
  So the preview is the burned-in result at any zoom, and small text can be judged at 200%.
  Drawing them scaled to the screen instead showed letters smoother than they would be saved.
  Reduced, they are drawn at the size shown, which is close enough and far less work.
* An ellipse is a box kind like the rectangle: two corners, four grips, its line inside the
  box. Shift while dragging keeps a box's sides equal, for a square or a circle, and turns a
  line or an arrow to the nearest 15 degrees about its other end, so level and upright are
  exact. The rules are in the shape model with the other rules of grips, so measurement can
  use them too.
* A number is one point with a disc around it, sized by its text size, and says one more than
  the highest number on the picture. What it says is ordinary text and can be changed, to a
  letter for instance; changed ones are passed over when counting. Its text is black or
  white, whichever reads on the disc's colour.
* The colours offered are the nine of ColorBrewer's Set1, then white and black. Set1 is a
  known set of colours that stay apart from one another, which is what marks on a picture
  need; white and black are added as the two that show on almost any photograph. The first
  row was picked without a source. Using the values asks for no credit in the program; the
  source is named in `Theme.qml`.
* Still to come: polygons, freehand strokes, a marker mode for dark pictures, and asking
  before shapes not yet applied are thrown away.
* Later, smaller: text size in points or pixels as a choice in the settings dialog, once it
  exists; a direction for the shadow in the panel; keeping each shape's edge and shadow
  between paints; any colour and not only the fixed row, and the colours remembered by
  the probe offered as swatches here too.

### Crop
* A mode of the large view rather than a window of its own, so zoom, panning and full screen
  stay as they are. Its panel takes the place of the info panel, and at full screen it comes
  and goes with the mouse at the right edge in the same way, over the image.
* The rectangle is in whole pixels of the upright image, and dragging puts edges on the nearest
  pixel boundary, so at a high zoom they sit exactly between pixels, as screenshots need. The
  fields set the same numbers.
* Ratios: free, the image's own, 1:1, 5:4, 4:3, 3:2, 16:10, 16:9, each screen connected by its
  size in pixels, A4, Letter and a custom one, each in landscape or portrait. Wide screens are
  rarely the 21:9 they are sold as (3440 x 1440 is 43:18, 3840 x 1600 is 12:5), so for
  wallpapers the screen's own size is the ratio that fits. A screen starts the way it stands.
  The choice is kept by name, so screens coming and going do not shift it. Whole pixels rarely allow an exact ratio, so the side that is
  dragged follows the mouse and the other is rounded to the nearest pixel. Changing the ratio
  fits the largest rectangle of it inside the current one.
* With a ratio, an edge sets the size and keeps the rectangle centred across it; a corner keeps
  the opposite corner. Dragging past the opposite edge turns the rectangle over. A handle keeps
  the offset at which it was grabbed. Dragging on the image outside the rectangle draws a new
  one, once the mouse has moved a few pixels, so a click changes nothing. A press outside the
  image takes the edge or corner on that side, as aiming just past an edge is common.
* The wheel zooms. The right or middle button, or the left one with Space held, pans, as the
  left button belongs to the rectangle; not every mouse has a middle button. Enter applies, Esc cancels. Enter in a field takes its value and
  gives the keys back to the image, so a second Enter applies.
* Handles stay a fixed size on screen and are computed per shape from a few stored values,
  rather than solving constraints, which is also how measuring tools would work.
* Free rotation is part of Straighten, below.

### Resize
* A dialog, as there is nothing to drag on the image. Width and height keep the image's ratio
  by default; presets are the screens, long edges of 3840, 2560, 1920 and 1280, 50% and 25%.
* The size typed is the size made. Keeping a ratio by rounding the other side gives sizes such
  as 1558 x 1200, so a target such as a screen is taken exactly: within a pixel of the image's
  ratio it stretches by less than that pixel; beyond it the dialog asks whether to fit inside,
  crop the middle to fill it, or stretch, and remembers the answer.
* Wallpaper is two steps: crop at the screen's ratio where the picture should be, then resize
  to the screen's size, which is then within a pixel.
* It uses the Magic Kernel Sharp resize of thumbnails, in linear light for 8-bit images, on
  the full-size picture. The edit keeps the size and whether to fill, so undo and replay give
  the same result. Sizes are limited to what can still be saved whole.

### Straighten
* Edit > Straighten (S) treats a crooked or keystoned picture as if the camera had been turned:
  roll around the lens axis, pitch up or down, yaw left or right. The picture is redrawn as the
  turned camera would have taken it, a homography K R K^-1 with R = Rz(roll) Rx(pitch) Ry(yaw),
  scaled so that the centre keeps its size. One model covers a level horizon, converging
  verticals of buildings and a board photographed from a little off to the side.
* Typing angles is the fallback, not the way in. Lines along edges that should be vertical or
  horizontal set them: one line levels the picture, two verticals set pitch, two horizontals set
  yaw. Each line becomes vertical or horizontal by whichever it is closer to. The fit is
  Levenberg-Marquardt over the free angles, minimising the sine of each line's lean after the
  transform, weighted by the square root of its length. A mode limits the lines to rotation, or
  to rotation and verticals, for pictures whose horizontals should keep converging.
* Detect finds long straight edges on a reduced copy of about 1200 pixels: region growing over
  pixels of similar gradient direction and a line fitted to each region. It is our own code,
  written for this, not a port of an existing detector. Lines leaning more than 15 degrees from
  both axes are dropped, as diagonal edges such as traces on a board would come in otherwise.
  Found lines are fitted robustly: those that lean much more than the rest after the fit are
  removed. Two of a kind always stay if there were two, since leaving out one of two horizontals
  stops yaw being fitted, which made the other lean too and go as well. Lines drawn or moved by
  hand are always used; when a later fit leaves out a found one, it is drawn dashed.
* Perspective needs the focal length. It comes from the camera data as the 35mm equivalent,
  converted to pixels through the picture's diagonal, and is assumed to be 28 mm otherwise. The
  field can be changed. After a resize the conversion uses the scale of the edits before, so the
  angles mean the same thing.
* Applying keeps the largest rectangle with no empty corners, of the image's ratio or of any.
  The largest rectangle inside the turned picture's outline is solved exactly, as a small linear
  program over its four edges, rather than searched.
* The outline of what will be kept follows the angles as they change. V shows the result,
  warped from the reduced copy, with a grid to judge right angles; the full image is warped only
  on Apply, with cubic interpolation in linear light for 8-bit images, in bands on several
  threads.
* The edit keeps the angles, the focal length in pixels and the ratio choice, so replay gives
  the same result.
* Planned: an amount below 100% for verticals, as fully parallel verticals in a tall building
  can look wrong; a 4-corner mode that maps a chosen quadrilateral to a rectangle, for a document
  or a painting; and the level data some cameras store (roll and pitch in maker notes, the
  gravity vector of phones, gimbal angles of drones) as a starting point.

### Grayscale
* Two ways, as a submenu that applies at once, since undo makes comparing them cheap. Luma
  weighs R, G and B as stored by Rec. 601, as JPEG does for Y. Perceptual lightness keeps the
  L of Oklab and sets the chroma to zero; with a and b at zero Oklab returns equal linear R, G
  and B of L cubed, so the gray is that value encoded, and grays stay exactly as they were. It
  follows how bright colours look: saturated blue comes out dark and yellow light.
* Lightness assumes the sRGB curve and primaries, also for 16-bit and float images; with
  another profile it is an approximation.
* The result keeps its format with R, G and B equal, so the other edits and the view work as
  before. Saving writes it as one gray channel where the format allows, as for any gray pixels;
  see Writers.
* The same Oklab conversion is meant for a later lightness, chroma and hue adjustment, of which
  grayscale is chroma at zero.

### Still planned
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
* There are three kinds, each with its own small interface: filters, metadata interpreters
  and decoders. All go through a versioned C API loaded at run time. No C++ standard library
  or Qt types cross the boundary.
* Each plugin exports one init function, which checks the API version and registers what
  the plugin provides through a host callback. Load-time constructors are avoided.
* Plugins load once on a worker thread at startup and stay loaded until exit, with no
  dependency ordering between them.
* Plugins load only from folders the user configures, with no paths built into the app.
* Plugins run inside the application process, so a crash takes the application down.

Filters:
* A filter describes its parameters and the host builds the UI, so plugins need no Qt and
  the edit list can store the values.
* A filter declares its name, parameters, one pixel format and how many neighbouring pixels
  it needs, and receives input and output buffers of the same size. The host converts to and
  from that format, so a prototype needs one code path, not one per bit depth.
* A filter can provide a cheap check of whether it applies to an image, given its format,
  size and metadata.
* A filter can read metadata entries by key, including those an interpreter plugin produced.
  The host hands them over, so filters depend on metadata, never on other plugins.
* Built-in filters use the same API, so it stays honest and serves as public examples.
* Tools that change the image size or need drawing on the image, such as crop and
  straighten, are not filters and stay built in.

Metadata interpreters:
* An interpreter explains metadata in formats the application already opens, which it keeps
  but cannot read itself. It needs no decoder of its own.
* It declares what it reads: TIFF tag numbers, an XMP namespace, a PNG chunk name, or a JPEG
  APPn marker with the signature it starts with.
* The host hands it the raw bytes of those blocks (see Metadata). It returns entries, each a
  group, label, value and unit, which the info panel shows beside the built-in ones.
* It can also answer normalised facts, such as physical pixel size and capture time, and its
  block then takes part in the rules for which block wins, like any other.
* Interpreters only read. Writing stays with the host, which writes the original bytes back.

Decoders:
* A decoder maps onto `ImageSource`: open a file, list subimages, read tiles. It also adds
  file extensions and metadata, so neither can stay a fixed list.
* It hands over metadata as blocks, the same way an interpreter's input arrives, so the two
  share that half of the API.

Left out for now:
* Commands on a selection of files, such as batch conversion and geotagging. They are built
  in first, against an internal interface that takes a list of files, their metadata and
  parameters. A plugin API can be cut from them once several exist.

### Saving over the original
* File > Save (Ctrl+S) overwrites the file with the edited image; no copy of the original is
  kept, neither in the trash nor beside it. Each save warns first, with Overwrite, Rename and
  Cancel, and Cancel has the focus, so Enter does not overwrite. Rename asks for a name in the
  same folder, suggesting `name-edited.ext`, and saves there, leaving the file as it was.
* It saves in the file's format with the options last chosen for that format in Save As, so it
  is one question rather than two. A JPEG is compressed again, as with Save As.
* A file in a format there is no writer for, or with more pages than the one edited, goes to
  Save As instead, as writing it back would lose what cannot be written. Stored lower levels of
  a pyramid are not written back; editing is limited to images small enough not to need them.
* Save in the question asked before leaving an edited image is this Save, warning included.

### Colour adjustment
* Edit > Adjust Colours (A) is one edit, `ColorEdit` in `core/color_edit.h`, with stages in a
  fixed order: a tone curve on the samples as stored, the same for R, G and B (levels: input
  black and white, gamma, and the output range, which can lift black to gray); a lightness curve
  and a chroma gain in Oklab; a colour cube; then encoding back.
* Levels are set as in other editors: handles on a gray gradient under the histogram of the
  picture as it was, black, gamma and white for the input and two more for the output range.
  Gamma applies between the two linear maps, on the input range scaled to 0 to 1, so it does
  not depend on the output range. Its handle stands where the output reaches half, at
  0.5^gamma of the way from black to white, and keeps that place when they move.
* The lightness curve combines a gamma (Midtones), contrast about the middle of the scale and
  brightness added to every value. The gamma keeps black and white where they are; brightness
  moves blacks and highlights alike and clips at white, which is what people expect from it.
  An empty stage costs nothing, and an edit with every stage empty returns the image as it was.
* Curves are tables of 16385 samples from 0 to 1, read with linear interpolation, so 16-bit
  images keep neighbouring values apart. A gamma is a curve too, not a separate stage.
* The Oklab stages work on planes of L, a and b for a chunk of rows at a time, inside the usual
  bands. That is where later operations on lightness or chroma alone go, including spatial ones
  such as local contrast, which will need a margin of rows around each chunk. Hue is left alone
  on purpose: shifting hues, protecting skin tones and grading shadows, midtones and highlights
  belong to a grading tool of their own.
* The cube is any size from 2 to 129 points a side, read with tetrahedral interpolation, which
  keeps grays gray when the cube does. `.cube` files are read: `LUT_3D_SIZE`, `DOMAIN_MIN` and
  `DOMAIN_MAX`, and `LUT_1D_SIZE` tables, which become a 33-point cube. They apply to the
  encoded samples, as such files expect.
* Colours the Oklab stages push beyond the range of an integer image are gamut mapped by
  default: lightness is clamped, then chroma reduced at the same hue until the colour fits,
  found by halving in 12 steps, in the shader too. Clipping each channel instead, which the
  panel offers for comparison, turns hues (sky towards cyan or purple, reds to orange) and
  flattens detail in strong colours. The cut is hard, so smooth gradients of extreme colours
  show a crease where mapping starts; a soft knee would need the largest chroma for every
  pixel's lightness and hue, not only for those beyond the range.
* After that, integer images are clipped to their range in linear light; float images are not
  clipped anywhere.
* Gray images take the curves only: the lightness curve works on L of the gray value, and
  saturation and the cube are disabled in the panel and ignored by the edit, so the result stays
  one channel. As in other editors, colour-only tools have nothing to do on gray.
* While adjusting, the view draws the picture through a shader (`shaders/color_preview.frag`)
  instead of computing the edit: the same stages in the same order from one half-float lookup
  image, the cube's slices side by side and a row per curve (`ui/color_lut.cpp`). Curves have 256
  samples there. Applying computes the edit on the CPU at full precision; the preview stays up
  until the result replaces it, so nothing flashes.
* The shader works on the picture as displayed, after the colour transform to the screen, so the
  preview is exact for sRGB images and close for others; for images with more than 8 bits the
  curves apply to what the display range made of them. Applying is always exact.
* It needs GLSL ES 3 or GLSL 1.30 for `texelFetch`. The software renderer has no shaders and
  shows no preview; applying still works.

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
* Decoders must keep what they don't interpret as raw blocks, for display, for writing back
  and for interpreter plugins. They don't yet: TIFF reads only the tags it names, so private
  tags and the image description are dropped; JPEG keeps only the APP1 and APP2 markers; PNG
  keeps only the EXIF chunk and the XMP text chunk. libtiff reads unknown tags as anonymous
  fields and can list them (`TIFFGetTagListCount`), so TIFF needs no parser of its own.

## Known limitations
* Rotating by 90 degrees and flipping exist only as the lossless JPEG tools, which change files
  rather than the image being edited.
* An image that changes on disk while shown is not reloaded, including one replaced by a copy.
  Its thumbnail does update. The lossless JPEG tools are the exception, as they say which files
  they changed.
* Sorting by name, date modified, size or type exists but is hidden in View > Sort By; the browser
  shows no control for it, nor what the order is. There is no sorting by the date a photo was
  taken (EXIF), which needs metadata read in the background (see Metadata).
* The grid lists images only, not subdirectories. A file renamed to an unsupported extension
  leaves the grid, though it is still on disk.
* On Windows the test suite passes except for four tests, listed with what is known about them
  in `doc/building-windows.md`. The tests cannot confirm that a file reached the Recycle Bin, as
  it renames what it receives. File operations have not been tried there by hand: a file that is
  open cannot be renamed or trashed on Windows, and the view can hold the current image open,
  for instance a TIFF, so expect an error message in that case rather than the operation working.
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
* Opening a JPEG decodes it twice: scaled to the smallest level as a quick rough preview, then
  whole, as both need the same entropy decoding. The rough preview averages gamma-encoded
  values, so fine detail may brighten a little when the halved level replaces it.
* Adjust Colours, low priority:
  * Gamut mapping keeps lightness, so a saturated mid blue that contrast or brightness lifts
    can turn almost gray: vivid blue exists only at low lightness (pure sRGB blue is near L 0.45),
    and at the lifted lightness little chroma fits. Clipping happens to keep such a sky bluer.
    The known remedy (Ottosson's gamut clipping with a projection towards L 0.5 or the cusp)
    trades a little lightness for chroma and leaves near-white colours almost as they are. It is
    a change to `intoGamut` in `core/color_edit.cpp` and its copy in the preview shader.
  * The mapping cuts chroma hard, so smooth gradients of extreme colours show a crease where it
    starts. A soft knee would need the largest chroma for every pixel's lightness and hue.
  * The preview is approximate for images with more than 8 bits shown with auto contrast: the
    shader applies levels to the stretched display values, and the display range is worked out
    again after applying. Composing the preview's tone curve with the display range would fix it.
  * Levels handles step in 1/255 of the full stored range, coarse for 12-bit data in 16-bit
    samples. The bars could follow the histogram's fit range.
  * The preview is exact only for sRGB or unmanaged images, as it runs after the display
    transform. Applying is exact for all.
  * Many film-look `.cube` files expect camera log footage and look too bright and contrasty on
    ordinary photos; lowering saturation first helps. Converting to a chosen log curve before
    the cube is not offered.
  * The loaded `.cube` is cleared each time the tool opens, so a look has to be loaded again for
    each image.

## The build is named by its commit, not a version number

The About dialog shows `git describe --tags --dirty --always` and the time of the build, as in
"c388af9-dirty, built 2026-11-13 16:00 UTC", in place of the project's 0.1.0.

* There are no releases yet, so a number would say nothing; the commit says exactly which source
  a bug report is about, and `-dirty` says when it is not quite that.
* `cmake/build_info.cmake` writes the header at every build, not at configure time, or it would
  go stale after the first commit. The file is rewritten only when its text changes, so a
  rebuild within one minute recompiles nothing; only `main.cpp` includes it.
* Without git, as in an unpacked archive, it says "unknown".
* The project version stays in `CMakeLists.txt` for the installer's file name.

## The palette is worked out from a hue

* `Theme`'s colours, but for those that must stay fixed, come from three choices: a hue, how
  colourful accents are, and how much the hue tints surfaces and text (`core/palette`, through
  `ThemePalette`). A palette then changes by one number, and everything bound to `Theme`
  follows at once, Fusion's palette and the tools on the image included.
* Each role has a fixed lightness in Oklab, and only its hue and chroma follow the choices, so
  contrast stays the same whatever hue is picked; the tests check text against every surface,
  and the order of the surfaces, all round the circle. Chroma the screen cannot show is reduced
  rather than clipped per channel, which would shift hue and lightness.
* The lightnesses come from a willow-green mock-up, with accents and text a little brighter:
  dark, calm surfaces with a little green, a light accent for marks and focus, and a darker one
  for selection and the default button.
* Fixed on purpose: the gray around a picture (a tint would shift how its colours look),
  warnings and dangers, folders, annotation colours, and what tools draw on the image.
* The rules are in core, without Qt, as they are arithmetic worth testing; `ThemePalette`
  only hands their result to QML as a map that `Theme` binds to.

## The controls have a style of their own

* Buttons, check boxes, combo boxes, spin boxes, sliders and menus are drawn by a Qt Quick
  Controls style of the program's own, `ImageViewerStyle` in `src/ui/controls`, which takes
  every other control from Fusion. Pages keep writing `Button` and the like, so a control can be
  redone without touching them, and one at a time.
* The fallback is declared `QtQuick.Controls.Fusion/auto`, and the style's own version is 6.0
  for it: `auto` asks Fusion for the importing module's version, and Fusion has no 1.0. A
  plain import without a version is not taken as a fallback at all, and every control not
  redone came from Basic instead: tree rows 50 pixels high, white text fields, taller menu bars.
* Each control is a few rounded rectangles: a base (a soft gradient, or the colour of a field),
  a light line inside the top edge, a lighter or darker layer for hover and press, and a ring in
  the accent for keyboard focus. Every colour comes from `Theme`, so the palette's hue reaches
  them too; sizes too (`controlHeight` 28, a multiple of 4 so that it stays whole at 125%).
* Three kinds of button, by what pressing does: the default (`highlighted`, Apply and the like)
  filled with the accent, the ordinary one, and the quiet one (`flat`), text alone. Fusion's
  default button mixed the accent into the plain colour and could hardly be told apart.
* A check box shows when it is off, with a clear edge, which Fusion's hardly did on the dark
  palette. Menus keep a column on the left for a check box or an icon on every line, so the
  words of all lines start at one place; before, a checkable line pushed its words right. Plain
  `Action`s in an `AppMenu` get the same line, with their shortcut.
* Text fields, combo boxes, spin boxes and the path bar are one kind of thing, a field, and look
  alike: the colour of a field, an edge in `mid`, the same corners and the height of a button.
  In the file chooser each field has its button on its row (Name and Save, Type and Cancel), in
  one grid, so they line up across.
* Lists side by side stand on `ListFrame`, the colour of a field with a thin frame; in the file
  chooser the places and the folders as well as the files, which alone had one before. The gap
  between panes of a `SplitView` is the same across as down, of the window's colour, with a
  line only under the mouse: Fusion's was a bright line, thicker one way than the other where a
  frame stood beside it.
* A menu line of an exclusive group (Sort By) has a round mark with a dot, as a radio button
  does; a box with a tick says each line can be turned on alone. Disabled, a mark keeps its
  edge whether it is on or not. A tool (the probe) is not an option and has no mark: its action
  is not checkable, and only its toolbar button is down while it is at work.
* Qt's SpinBox counts in whole numbers; `DecimalSpinBox` counts in hundredths and shows the
  point. It is not named DoubleSpinBox, which newer Qt has.
* The tests run in this style whatever the environment says, so that they see what people
  see. A gallery of the controls in their states is part of the tests, and can be saved as a
  picture (`IV_GALLERY_PNG`).
* Written from scratch on Qt's templates; nothing is taken from Qt's own styles, which are
  under other licences.

