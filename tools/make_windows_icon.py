"""Makes the Windows icon and resource file from the AppImage's SVG icon.

    python -m pip install skia-python pillow
    python tools/make_windows_icon.py

Writes src/app/windows/imageviewer.ico and imageviewer.rc, which are committed, so builds need
neither this script nor its packages. Run it again when installer/linux/imageviewer.svg changes.

Each size is drawn from the SVG at that size, not scaled down from the largest, so that the small
ones stay sharp. Skia draws the SVG; Pillow packs the pictures into the .ico.
"""
import io
from pathlib import Path

import skia
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SVG = ROOT / "installer" / "linux" / "imageviewer.svg"
OUT = ROOT / "src" / "app" / "windows"

# What Explorer, the taskbar and the Start menu ask for, at 100% to 200% scaling.
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]

RC = """\
// Written by tools/make_windows_icon.py; do not edit.
// IDI_ICON1 is the name Qt looks for, so the windows take the same icon as the executable.
IDI_ICON1 ICON "imageviewer.ico"
"""


def render(svg: bytes, size: int) -> Image.Image:
    dom = skia.SVGDOM.MakeFromStream(skia.MemoryStream(svg))
    if dom is None:
        raise SystemExit(f"{SVG} is not an SVG Skia can read")
    natural = dom.containerSize()
    surface = skia.Surface(size, size)  # premultiplied RGBA, cleared to transparent
    with surface as canvas:
        canvas.scale(size / natural.width(), size / natural.height())
        dom.render(canvas)
    png = surface.makeImageSnapshot().encodeToData(skia.EncodedImageFormat.kPNG, 100)
    return Image.open(io.BytesIO(bytes(png))).convert("RGBA")


def main() -> None:
    svg = SVG.read_bytes()
    images = [render(svg, size) for size in SIZES]
    OUT.mkdir(parents=True, exist_ok=True)
    largest = images[-1]
    largest.save(OUT / "imageviewer.ico", format="ICO", sizes=[(s, s) for s in SIZES],
                 append_images=images[:-1])
    (OUT / "imageviewer.rc").write_text(RC, encoding="utf-8", newline="\n")
    print(f"Wrote {OUT / 'imageviewer.ico'} ({', '.join(map(str, SIZES))} px) and imageviewer.rc")


if __name__ == "__main__":
    main()
