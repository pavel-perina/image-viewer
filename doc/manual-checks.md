# Checks by hand

What the automated tests cannot cover: other applications, real disks and each system's own
behaviour. Tick what you have tried, and note on which system: L for Linux (with the desktop),
W for Windows.

## Clipboard

- [ ] L [ ] W  Ctrl+C on thumbnails, paste in the file manager (Dolphin, Nautilus, Explorer): copies.
- [ ] L [ ] W  Ctrl+C on a single image, paste in an image editor or a chat: arrives as a picture.
- [ ] L [ ] W  Copy files in the file manager, Ctrl+V here: copies into the folder shown.
- [ ] L [ ] W  Ctrl+X here, paste in the file manager: the file moves.
- [ ] L [ ] W  Cut in the file manager, Ctrl+V here: the file moves, and the clipboard empties.
- [ ] W        Explorer greys files that are cut; ours does not yet, which is expected.

## Drag and drop

- [ ] L [ ] W  Drag thumbnails to the file manager, plain: copies or moves? Note which.
- [ ] L [ ] W  The same with Shift (move) and with Ctrl (copy).
- [ ] L [ ] W  Drag from the file manager onto the thumbnails, plain and with Shift.
- [ ] L [ ] W  Drag from the file manager onto a folder of the tree: the folder is framed, files land there.
- [ ] L [ ] W  Drag a folder from the file manager onto the window: it opens.
- [ ] L [ ] W  Drag a file onto the large view: it opens.
- [ ] L [ ] W  Drag thumbnails into a mail or chat window: attached.
- [ ] Compare with FastStone: what a drop on its thumbnails does, and what its plain drag does.

## Keys

- [ ] Ctrl+L, Esc, then the arrows still move between thumbnails.
- [ ] Click the tree: Up and Down open folders without unfolding; Right unfolds, then goes in;
      Left folds, then goes to the parent; Backspace goes up; Enter goes to the thumbnails.
- [ ] F2 and Del on the tree act on the folder, not on the selected files.
- [ ] Tab moves between the tree and the thumbnails; the selection of the side without the
      keys turns paler, the folder in the tree as well as the thumbnails.
- [ ] L [ ] W  Start in a deep folder: the tree shows it, also once the folders above have loaded.
- [ ] Backspace on the thumbnails goes up a folder; in the large view, to the previous image.
- [ ] Enter opens the image, Enter again goes back.

## Look

- [ ] L [ ] W  In the browser and file chooser, focus the folder tree, Places (chooser),
      thumbnails, and brief/details lists in turn. Only the pane with keyboard focus has
      the accent frame; selected rows elsewhere remain dimly visible. No pane resizes.
- [ ] Using disposable test files only: Delete with browser tree focus asks about the
      folder; Cancel leaves it intact. Delete with thumbnail focus trashes selected files.
      With the path or file-name field focused it edits text. Places is navigation-only.
- [ ] L [ ] W  At 100%, 125% and 150% scaling, list contents do not cover the rounded
      pane corners or focus frames; scroll bars and splitter handles remain usable.

- [ ] On a light desktop theme: menus, dialogs and panels stay dark, with no black text on dark.
- [ ] Inter shows in menus and labels; no missing characters in file names with accents.
- [ ] W  To try: small text on a screen at 100% scaling is a little soft. Two likely causes,
      neither confirmed. Qt Quick draws text itself unless told otherwise, without the font's
      hinting or ClearType; one line in `main()` before the window is made hands it to Windows:
      `QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering)`. And the Inter that
      is bundled is the variable file, which has no hinting; the separate files per weight do.
      Try the first alone, then with the second, and compare menus, panel labels and the
      numbers of the probe. Text that is scaled or turned would blur with it, which nothing
      in the window is. Linux was judged on a denser screen, where this does not show.

## Display depth

Needs a monitor and cable that take 10 bits, set in the driver (NVIDIA Control Panel: Change
resolution, Use NVIDIA color settings, Output color depth 10 bpc). Make the image with
`python tools/gradient_test.py gradients.png` and open it at 100%: each gradient is drawn in
full 16 bits, rounded to 10 bits and rounded to 8 bits.

- [ ] L [x] W  Run with `IV_OUTPUT_BITS=10 IV_NO_DITHER=1`. About says 10 bits per channel, not
      dithered; the first two strips of each gradient are smooth and only the third bands.
- [ ] L [ ] W  The same with `IV_NO_DITHER=1` alone: About says 8 bits, all three strips band
      alike. This is the comparison.
- [ ] L [ ] W  `IV_OUTPUT_BITS=10` alone: smooth, with no grain to see, and About says dithered.

## Windows installer

- [x] W  After installing, right-click a JPEG > Open with: Image Viewer is listed, with its icon,
         and the default is still what it was.
- [x] W  Settings > Apps > Default apps > Image Viewer: lists its types; choosing it for .png
         opens PNGs here on a double-click.
- [x] W  The same for a .jxl and a .webp, which may have had no program before.
- [x] W  Uninstall: Image Viewer leaves Open with and Default apps, the defaults chosen for other
         programs stay, and its settings stay too (a reinstall remembers them).
- [ ] W  Installing with the Open with task unticked: Image Viewer is not offered.

## Disks

- [ ] A folder on a spinning disk or a USB stick: thumbnails come one file at a time, the image
      you open comes first. On Windows the detection has never been compiled before.
- [ ] A network folder and a OneDrive folder on Windows: does the window freeze? Does browsing
      download every file?

See also "Parts never run on Windows" in `building-windows.md`.
