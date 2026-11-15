# Icons

SVG, drawn on a 24 by 24 grid, aligned to whole pixels.

* **Named by meaning, not by shape:** `zoom-in`, `search`, `go-up`, `view-refresh`, following
  the freedesktop icon naming spec where it has a name. The files keep the names they are
  exported under (`24x24_zoom_in.svg`); the name the program uses is given in the resource list
  in `src/ui/CMakeLists.txt`. One drawing that serves two meanings is one file listed twice.
* **Colours are placeholders**, replaced by the theme's when the icon is drawn (`ThemedIcon`,
  `src/ui/themed_icon.h`), so that icons follow the palette. White is the ink, the button's text
  colour, and pure green (`rgb(0,255,0)`) is the accent. Both turn grey when the button is
  disabled. Any other colour stays as drawn.
* A new icon goes in that resource list. A test draws every listed icon and fails on one that
  is left white or green, or draws nothing.
