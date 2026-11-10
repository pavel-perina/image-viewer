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
- [ ] Tab moves between the tree and the thumbnails; the selection turns paler on the tree.
- [ ] Backspace on the thumbnails goes up a folder; in the large view, to the previous image.
- [ ] Enter opens the image, Enter again goes back.

## Look

- [ ] On a light desktop theme: menus, dialogs and panels stay dark, with no black text on dark.
- [ ] Inter shows in menus and labels; no missing characters in file names with accents.

## Disks

- [ ] A folder on a spinning disk or a USB stick: thumbnails come one file at a time, the image
      you open comes first. On Windows the detection has never been compiled before.
- [ ] A network folder and a OneDrive folder on Windows: does the window freeze? Does browsing
      download every file?

## Known problems to watch, not to tick

- The tree often fails to scroll to the open folder at startup and shows the top of the disk.
- Four tree tests fail at random, here and on Windows.

See also "Parts never run on Windows" in `building-windows.md`.
