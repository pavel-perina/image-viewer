#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <variant>
#include <vector>

#include "core/color_edit.h"
#include "core/crop.h"
#include "core/image_layer.h"
#include "core/image_ops.h"
#include "core/image_source.h"
#include "core/perspective.h"

namespace iv {

// Keeps a rectangle, in pixels of the upright image as the edits before it left it.
struct CropEdit {
    PixelRect rect;
    bool operator==(const CropEdit&) const = default;
};

// Scales to exactly the given size. With fill, the largest centred part of the ratio of that
// size is kept first, so nothing is distorted; without it, a different ratio stretches.
struct ResizeEdit {
    Size size;
    bool fill = false;
    bool operator==(const ResizeEdit&) const = default;
};

// Straightens as if the camera had been turned, then keeps the largest rectangle with no empty
// corners, of the picture's ratio or of any. The focal length is in pixels of the picture the
// edit is applied to. Without crop it keeps the whole turned picture instead, and the empty
// corners are transparent: the picture gets alpha if it had none, and a gray one becomes a
// colour one, as gray pictures have no alpha.
struct TiltEdit {
    Tilt tilt;
    double focalPixels = 0;
    bool keepRatio = true;
    bool crop = true;
    bool operator==(const TiltEdit&) const = default;
};

// Turns the picture gray, keeping its size and format.
struct GrayEdit {
    GrayMethod method = GrayMethod::Luma;
    bool operator==(const GrayEdit&) const = default;
};

// Draws on the picture what annotations were turned into: first rectangles are pixelated,
// then a layer of markers darkens what is under it, then a layer of everything else covers.
// Both layers are as blendLayer takes them, with their top left corner at x, y, and either
// may be missing. With colour set, a gray picture becomes a colour one first, so that what
// is drawn keeps its colours; its gray colour profile then no longer fits and is dropped.
struct DrawEdit {
    struct Pixelated {
        PixelRect rect;
        int block = 8;
    };
    std::vector<Pixelated> pixelated;
    ImageBuffer markers;
    ImageBuffer drawing;
    int x = 0;
    int y = 0;
    DisplayRange range;
    bool colour = false;
};

using Edit = std::variant<CropEdit, ResizeEdit, TiltEdit, GrayEdit, ColorEdit, DrawEdit>;

// Whether a subimage can be edited, and if not, why. It is held whole in memory, so it has to
// be small enough to be saved whole.
Result<void> canEdit(const SubimageInfo& subimage);

// One subimage of an opened file and the edits made to it, with undo. Edits are kept as
// operations with their values, applied to the upright full-size picture, which is decoded once
// and kept. Use from one thread; the sources it hands out may be read from any.
class Document {
public:
    // The subimage must be one that canEdit accepts.
    Document(std::shared_ptr<ImageSource> base, int subimage);

    const std::shared_ptr<ImageSource>& base() const { return m_base; }
    int subimage() const { return m_subimage; }

    // Adds an edit after the current one, dropping any that were undone.
    void apply(Edit edit);
    bool canUndo() const { return m_position > 0; }
    bool canRedo() const { return m_position < m_edits.size(); }
    void undo();
    void redo();

    // The edits in effect, oldest first.
    std::span<const Edit> edits() const { return std::span(m_edits).first(m_position); }

    // Whether it differs from what was last saved, or from the file if never saved.
    bool modified() const { return m_position != m_savedPosition; }
    void markSaved() { m_savedPosition = m_position; }

    // The size of the picture with the edits in effect, upright.
    Size size() const;

    // How many pixels of the picture now one pixel of the original became, through resizing.
    double scale() const;

    // Whether the picture can be resized to a size: it has to stay small enough to be saved.
    Result<void> canResizeTo(Size size) const;

    // The edited picture as a source of one upright subimage, with the metadata of the original.
    // Without edits it is still upright. Each call gives a new source reflecting the edits now.
    std::shared_ptr<ImageSource> source() const;

private:
    struct Upright;

    std::shared_ptr<ImageSource> m_base;
    int m_subimage;
    std::shared_ptr<Upright> m_upright;
    std::vector<Edit> m_edits;
    std::size_t m_position = 0;
    std::size_t m_savedPosition = 0;  // past the end when what was saved has been dropped
};

}  // namespace iv
