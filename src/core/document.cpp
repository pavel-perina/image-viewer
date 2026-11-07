#include "core/document.h"

#include <limits>
#include <mutex>
#include <new>
#include <utility>

#include "core/exif.h"
#include "core/image_ops.h"
#include "core/overview_source.h"
#include "core/save_image.h"
#include "core/whole_image_source.h"
#include "core/xmp.h"

namespace iv {

namespace {

template <class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};

Size sizeAfter(Size /*before*/, const Edit& edit) {
    return std::visit(Overloaded{[](const CropEdit& crop) { return Size{crop.rect.width, crop.rect.height}; }}, edit);
}

ImageBuffer applyEdit(const ImageBuffer& pixels, const Edit& edit) {
    return std::visit(Overloaded{[&](const CropEdit& crop) {
                          return pixels.view(crop.rect.x, crop.rect.y, crop.rect.width, crop.rect.height);
                      }},
                      edit);
}

}  // namespace

// The full-size picture turned upright, decoded the first time it is needed and then shared by
// every source the document hands out.
struct Document::Upright {
    std::shared_ptr<ImageSource> base;
    int subimage = 0;
    std::mutex mutex;
    ImageBuffer pixels;

    Result<ImageBuffer> get(const std::stop_token& stop) {
        const std::scoped_lock lock(mutex);
        if (pixels.isNull()) {
            try {
                auto decoded = readLevel(*base, subimage, 0, stop);
                if (!decoded) {
                    return decoded;
                }
                pixels = applyOrientation(*decoded, base->subimages()[static_cast<std::size_t>(subimage)].orientation);
            } catch (const std::bad_alloc&) {
                return std::unexpected("not enough memory to edit the image");
            }
        }
        return pixels;
    }
};

Result<void> canEdit(const SubimageInfo& subimage) {
    if (!canSaveWhole(subimage)) {
        return std::unexpected("the image is too large to be edited");
    }
    return {};
}

Document::Document(std::shared_ptr<ImageSource> base, int subimage)
    : m_base(std::move(base)), m_subimage(subimage), m_upright(std::make_shared<Upright>()) {
    m_upright->base = m_base;
    m_upright->subimage = m_subimage;
}

void Document::apply(Edit edit) {
    m_edits.resize(m_position);
    if (m_savedPosition > m_position) {
        m_savedPosition = std::numeric_limits<std::size_t>::max();
    }
    m_edits.push_back(std::move(edit));
    ++m_position;
}

void Document::undo() {
    if (canUndo()) {
        --m_position;
    }
}

void Document::redo() {
    if (canRedo()) {
        ++m_position;
    }
}

Size Document::size() const {
    const SubimageInfo& info = m_base->subimages()[static_cast<std::size_t>(m_subimage)];
    Size size = displaySize(info.levels.front().size, info.orientation);
    for (const Edit& edit : edits()) {
        size = sizeAfter(size, edit);
    }
    return size;
}

std::shared_ptr<ImageSource> Document::source() const {
    SubimageInfo info = m_base->subimages()[static_cast<std::size_t>(m_subimage)];
    const Size edited = size();
    // The pixels are upright now, and the metadata says so, as the file written from it will.
    info.orientation = Orientation::Normal;
    info.exif.orientation = Orientation::Normal;
    resetExifForPixels(info.rawExif, edited.width, edited.height);
    resetXmpOrientation(info.xmp);
    info.stored.storedLevels = 1;

    std::vector<Edit> edits(this->edits().begin(), this->edits().end());
    auto decoder = [upright = m_upright, edits = std::move(edits)](const std::stop_token& stop) -> Result<ImageBuffer> {
        auto pixels = upright->get(stop);
        if (!pixels) {
            return pixels;
        }
        ImageBuffer result = *pixels;
        for (const Edit& edit : edits) {
            result = applyEdit(result, edit);
        }
        return result;
    };
    return withOverviews(makeWholeImageSource(std::move(info), edited, std::move(decoder)));
}

}  // namespace iv
