#include "core/viewport.h"

#include <algorithm>
#include <cmath>

namespace iv {

namespace {
constexpr double kMaxZoom = 64.0;
constexpr double kMaxResampleZoom = 2.0;
constexpr int kMaxResampleTexture = 8192;
}

void Viewport::setImageSize(Size size) {
    m_image = size;
    applyFit();
    clampOffset();
}

void Viewport::setViewSize(double width, double height) {
    m_viewWidth = width;
    m_viewHeight = height;
    applyFit();
    clampOffset();
}

void Viewport::setFitMode(FitMode mode) {
    m_fitMode = mode;
    applyFit();
    clampOffset();
}

void Viewport::applyFit() {
    if (m_fitMode == FitMode::Manual || m_image.width <= 0 || m_image.height <= 0 || m_viewWidth <= 0 ||
        m_viewHeight <= 0) {
        return;
    }
    const double fit = std::min(m_viewWidth / m_image.width, m_viewHeight / m_image.height);
    m_zoom = m_fitMode == FitMode::ShrinkToFit ? std::min(fit, 1.0) : fit;
}

void Viewport::zoomBy(double factor, double viewX, double viewY) {
    if (m_image.width <= 0 || m_image.height <= 0 || factor <= 0) {
        return;
    }
    const double fit = m_viewWidth > 0 && m_viewHeight > 0
                           ? std::min(m_viewWidth / m_image.width, m_viewHeight / m_image.height)
                           : 1.0;
    const double minZoom = std::min(fit, 1.0) / 4.0;

    double next = std::clamp(m_zoom * factor, minZoom, std::max(kMaxZoom, minZoom));
    if ((m_zoom < 1.0 && next > 1.0) || (m_zoom > 1.0 && next < 1.0)) {
        next = 1.0;
    }

    const double imageX = (viewX - m_offsetX) / m_zoom;
    const double imageY = (viewY - m_offsetY) / m_zoom;
    m_zoom = next;
    m_fitMode = FitMode::Manual;
    m_offsetX = viewX - imageX * m_zoom;
    m_offsetY = viewY - imageY * m_zoom;
    clampOffset();
}

void Viewport::panBy(double dx, double dy) {
    m_offsetX += dx;
    m_offsetY += dy;
    clampOffset();
}

void Viewport::clampOffset() {
    const auto clampAxis = [](double offset, double scaled, double view) {
        // Whole pixels keep the image crisp at 100%.
        return std::round(scaled <= view ? (view - scaled) / 2.0 : std::clamp(offset, view - scaled, 0.0));
    };
    m_offsetX = clampAxis(m_offsetX, m_image.width * m_zoom, m_viewWidth);
    m_offsetY = clampAxis(m_offsetY, m_image.height * m_zoom, m_viewHeight);
}

RectD Viewport::visibleRect() const {
    const double left = std::max(0.0, -m_offsetX / m_zoom);
    const double top = std::max(0.0, -m_offsetY / m_zoom);
    const double right = std::min<double>(m_image.width, (m_viewWidth - m_offsetX) / m_zoom);
    const double bottom = std::min<double>(m_image.height, (m_viewHeight - m_offsetY) / m_zoom);
    return {left, top, std::max(0.0, right - left), std::max(0.0, bottom - top)};
}

RectD Viewport::visibleFraction() const {
    if (m_image.width <= 0 || m_image.height <= 0) {
        return {0, 0, 1, 1};
    }
    const RectD visible = visibleRect();
    return {visible.x / m_image.width, visible.y / m_image.height, visible.width / m_image.width, visible.height / m_image.height};
}

int chooseLevel(std::span<const LevelInfo> levels, double zoom) {
    const int full = levels.front().size.width;
    int chosen = 0;
    for (int i = 0; i < std::ssize(levels); ++i) {
        const double scale = static_cast<double>(levels[static_cast<std::size_t>(i)].size.width) / full;
        // The tolerance absorbs rounding in the level's size.
        if (scale < zoom * 0.999) {
            break;
        }
        chosen = i;
    }
    return chosen;
}

int chooseBilinearLevel(std::span<const LevelInfo> levels, double zoom) { return chooseLevel(levels, zoom * 0.8); }

TileRange tilesInRect(const LevelInfo& level, Size fullSize, const RectD& rect) {
    if (rect.width <= 0 || rect.height <= 0) {
        return {};
    }
    const double scaleX = static_cast<double>(level.size.width) / fullSize.width;
    const double scaleY = static_cast<double>(level.size.height) / fullSize.height;
    const int cols = (level.size.width + level.tileSize.width - 1) / level.tileSize.width;
    const int rows = (level.size.height + level.tileSize.height - 1) / level.tileSize.height;

    const auto index = [](double position, double scale, int tile, int count) {
        return std::clamp(static_cast<int>(std::floor(position * scale / tile)), 0, count - 1);
    };
    return {index(rect.x, scaleX, level.tileSize.width, cols),
            index(rect.y, scaleY, level.tileSize.height, rows),
            // Stepping back a little stops a rectangle that ends exactly on a tile
            // boundary from selecting the next, invisible tile.
            index(rect.x + rect.width - 1e-6, scaleX, level.tileSize.width, cols),
            index(rect.y + rect.height - 1e-6, scaleY, level.tileSize.height, rows)};
}

RectD tileRect(const LevelInfo& level, Size fullSize, int col, int row) {
    const double scaleX = static_cast<double>(fullSize.width) / level.size.width;
    const double scaleY = static_cast<double>(fullSize.height) / level.size.height;
    const int left = col * level.tileSize.width;
    const int top = row * level.tileSize.height;
    const int right = std::min(left + level.tileSize.width, level.size.width);
    const int bottom = std::min(top + level.tileSize.height, level.size.height);
    return {left * scaleX, top * scaleY, (right - left) * scaleX, (bottom - top) * scaleY};
}

ResampleGeometry resampleGeometry(const Viewport& viewport, Size imageSize, double levelScale, double supportRadius) {
    ResampleGeometry g;
    const double zoom = viewport.zoom();
    if (zoom >= kMaxResampleZoom || std::abs(zoom / levelScale - 1.0) < 1e-6 || imageSize.width <= 0 || imageSize.height <= 0 ||
        viewport.viewWidth() <= 0 || viewport.viewHeight() <= 0) {
        return g;
    }
    g.ratio = levelScale / zoom;
    g.spacing = std::max(g.ratio, 1.0);
    g.scale = levelScale;
    const double margin = std::ceil(supportRadius * g.spacing) + 1;
    g.marginPixels = margin / levelScale;
    g.translateX = std::ceil(viewport.offsetX() * g.ratio) + margin;
    g.translateY = std::ceil(viewport.offsetY() * g.ratio) + margin;
    g.phaseX = g.translateX - viewport.offsetX() * g.ratio;
    g.phaseY = g.translateY - viewport.offsetY() * g.ratio;
    g.textureWidth = static_cast<int>(std::ceil(g.phaseX + viewport.viewWidth() * g.ratio + margin));
    g.textureHeight = static_cast<int>(std::ceil(g.phaseY + viewport.viewHeight() * g.ratio + margin));
    g.imageTexels = {g.translateX, g.translateY, std::round(imageSize.width * levelScale), std::round(imageSize.height * levelScale)};
    g.active = g.textureWidth <= kMaxResampleTexture && g.textureHeight <= kMaxResampleTexture;
    return g;
}

}  // namespace iv
