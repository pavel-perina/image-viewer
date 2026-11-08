#pragma once

#include <cstddef>
#include <memory>

#include "core/geometry.h"

namespace iv {

enum class PixelFormat {
    Bgrx8,    // bytes B, G, R, unused; 8-bit colour, and 8-bit gray drawn on screen
    Gray8,    // one 8-bit channel; 8-bit gray as decoded and edited
    Gray16,   // one 16-bit unsigned channel
    Rgba16,   // four 16-bit unsigned channels
    Gray32F,  // one 32-bit float channel
    Rgba32F,  // four 32-bit float channels
};

int bytesPerPixel(PixelFormat format);

// Pixels in shared storage. Copies and views are cheap and alias the same pixels,
// so one decoded image can be handed out as many tiles without copying.
class ImageBuffer {
public:
    ImageBuffer() = default;

    // The pixels are uninitialized. Throws std::bad_alloc if memory runs out.
    static ImageBuffer create(Size size, PixelFormat format);

    // A view of part of this buffer; the rectangle must lie inside it.
    ImageBuffer view(int x, int y, int width, int height) const;

    bool isNull() const { return !m_pixels; }
    Size size() const { return m_size; }
    int width() const { return m_size.width; }
    int height() const { return m_size.height; }
    PixelFormat format() const { return m_format; }
    std::size_t stride() const { return m_stride; }
    std::byte* data() const { return m_pixels; }
    std::byte* row(int y) const { return m_pixels + static_cast<std::size_t>(y) * m_stride; }

private:
    std::shared_ptr<std::byte[]> m_storage;
    std::byte* m_pixels = nullptr;
    Size m_size;
    std::size_t m_stride = 0;
    PixelFormat m_format = PixelFormat::Bgrx8;
};

}  // namespace iv
