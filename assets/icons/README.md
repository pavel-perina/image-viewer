# Icons

SVG on a 24 by 24 grid, with 2 pixel lines.

* **Where they come from:** `tabler/` holds icons from [Tabler Icons](https://tabler.io/icons)
  (MIT, `tabler/LICENSE`), copied one by one under their own names. Another set goes in a
  folder of its own with its licence, the same way. The `24x24_*.svg` files and
  `image-viewer-icons.af` are our own drawings, kept for icons no set has; they are not built in
  until listed.
* **Named by meaning, not by shape:** `zoom-in`, `search`, `go-up`, `view-refresh`, following
  the freedesktop icon naming spec where it has a name. The files keep their own names; the
  name the program uses is given in the resource list in `src/ui/CMakeLists.txt`. One drawing
  that serves two meanings is one file listed twice.
* **Colours are placeholders**, replaced by the theme's when the icon is drawn (`ThemedIcon`,
  `src/ui/themed_icon.h`), so that icons follow the palette. White or `currentColor` (what
  Tabler draws in) is the ink, the button's text colour, and pure green (`rgb(0,255,0)`) is the
  accent. Both turn grey when the button is disabled. Any other colour stays as drawn.
* A new icon goes in that resource list. A test draws every listed icon and fails on one that
  is left white, green or black, or draws nothing.
