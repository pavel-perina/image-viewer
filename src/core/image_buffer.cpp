#include "core/image_buffer.h"

#include <cassert>

namespace iv {

int bytesPerPixel(PixelFormat format) {
    switch (format) {
    case PixelFormat::Bgrx8:   return 4;
    case PixelFormat::Gray8:   return 1;
    case PixelFormat::Gray16:  return 2;
    case PixelFormat::Rgba16:  return 8;
    case PixelFormat::Gray32F: return 4;
    case PixelFormat::Rgba32F: return 16;
    }
    return 0;
}

ImageBuffer ImageBuffer::create(Size size, PixelFormat format) {
    ImageBuffer buffer;
    buffer.m_stride = static_cast<std::size_t>(size.width) * bytesPerPixel(format);
    buffer.m_storage = std::make_shared_for_overwrite<std::byte[]>(buffer.m_stride * size.height);
    buffer.m_pixels = buffer.m_storage.get();
    buffer.m_size = size;
    buffer.m_format = format;
    return buffer;
}

ImageBuffer ImageBuffer::withAlpha(bool alpha) const {
    assert(!alpha || m_format == PixelFormat::Bgrx8 || m_format == PixelFormat::Rgba16 || m_format == PixelFormat::Rgba32F);
    ImageBuffer result = *this;
    result.m_alpha = alpha;
    return result;
}

ImageBuffer ImageBuffer::view(int x, int y, int width, int height) const {
    assert(x >= 0 && y >= 0 && x + width <= m_size.width && y + height <= m_size.height);
    ImageBuffer result = *this;
    result.m_pixels = row(y) + static_cast<std::size_t>(x) * bytesPerPixel(m_format);
    result.m_size = {width, height};
    return result;
}

}  // namespace iv
