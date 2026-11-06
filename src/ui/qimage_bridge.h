#pragma once

#include <QImage>

#include "core/image_buffer.h"

namespace iv {

// Wraps an 8-bit buffer without copying; the QImage keeps the pixels alive.
inline QImage toQImage(const ImageBuffer& buffer) {
    auto* keepAlive = new ImageBuffer(buffer);
    return QImage(
        reinterpret_cast<const uchar*>(buffer.data()), buffer.width(), buffer.height(),
        static_cast<qsizetype>(buffer.stride()), QImage::Format_RGB32,
        [](void* info) { delete static_cast<ImageBuffer*>(info); }, keepAlive);
}

}  // namespace iv
