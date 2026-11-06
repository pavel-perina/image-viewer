#pragma once

#include <span>

#include "core/geometry.h"
#include "core/image_source.h"

namespace iv {

enum class FitMode {
    Manual,        // the user chose the zoom
    ShrinkToFit,   // fit the view, but never enlarge a smaller image
    Fit,           // fit the view, enlarging if necessary
};

// Zoom and pan state, with lengths in device pixels. "Image" here means the image as
// displayed, after orientation has been applied.
class Viewport {
public:
    void setImageSize(Size size);
    void setViewSize(double width, double height);
    void setFitMode(FitMode mode);

    // Multiplies the zoom, keeping the given view position over the same image point. A
    // step that would cross 100% stops there, so that scale is easy to land on.
    void zoomBy(double factor, double viewX, double viewY);
    void panBy(double dx, double dy);

    FitMode fitMode() const { return m_fitMode; }
    double zoom() const { return m_zoom; }
    double offsetX() const { return m_offsetX; }
    double offsetY() const { return m_offsetY; }
    double viewWidth() const { return m_viewWidth; }
    double viewHeight() const { return m_viewHeight; }

    // The part of the image that is visible, in image coordinates.
    RectD visibleRect() const;
    // The same as fractions of the image, from 0 to 1, or all of it if there is no image.
    RectD visibleFraction() const;

private:
    void applyFit();
    void clampOffset();

    Size m_image;
    double m_viewWidth = 0;
    double m_viewHeight = 0;
    FitMode m_fitMode = FitMode::ShrinkToFit;
    double m_zoom = 1;
    double m_offsetX = 0;
    double m_offsetY = 0;
};

struct TileRange {
    int firstCol = 0;
    int firstRow = 0;
    int lastCol = -1;
    int lastRow = -1;
};

// The lowest resolution level that still has at least as many pixels as the zoom shows.
int chooseLevel(std::span<const LevelInfo> levels, double zoom);

// Tiles of a level overlapping a rectangle given in level 0 coordinates.
TileRange tilesInRect(const LevelInfo& level, Size fullSize, const RectD& rect);

// The area a tile covers, in level 0 coordinates.
RectD tileRect(const LevelInfo& level, Size fullSize, int col, int row);

// How to draw through a resampling kernel: a level's tiles go into a texture at one texel per
// pixel of the level, aligned to it, and the kernel takes that texture to the view. A
// "texel" position below is in that texture, where a texel's centre is at +0.5.
struct ResampleGeometry {
    bool active = false;
    double ratio = 1;    // texels per device pixel
    double spacing = 1;  // how far the kernel is stretched, in texels; only when minifying
    int textureWidth = 0;
    int textureHeight = 0;
    double scale = 1;       // texels per image pixel
    double translateX = 0;  // texel = image * scale + translate; whole numbers, for alignment
    double translateY = 0;
    double phaseX = 0;  // the texel position of the view's corner
    double phaseY = 0;
    RectD imageTexels;     // the image in the texture
    double marginPixels = 0;  // how far beyond the view the kernel reads, in image pixels
};

// Inactive at exactly the level's scale, where the kernel would sharpen pixels that are shown
// as they are; from 200%, where individual pixels are shown; and if the texture would be
// unreasonably large for the GPU. The kernel reaches supportRadius of its own units.
ResampleGeometry resampleGeometry(const Viewport& viewport, Size imageSize, double levelScale, double supportRadius);

}  // namespace iv
