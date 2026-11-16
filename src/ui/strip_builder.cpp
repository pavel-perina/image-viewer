#include "ui/strip_builder.h"

#include <QDateTime>
#include <QFileInfo>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/canvas.h"
#include "core/geometry.h"
#include "core/image_source.h"
#include "core/save_image.h"
#include "core/strip_layout.h"
#include "core/thumbnail.h"
#include "core/whole_image_source.h"
#include "core/work_queue.h"
#include "ui/edited_image.h"
#include "ui/file_operations.h"
#include "ui/thumbnail_provider.h"

namespace iv {

namespace {

constexpr int kSizePriority = 100;

struct Picture {
    QString path;
    std::optional<Size> size;  // upright; none until read, or if it cannot be
    bool unreadable = false;
};

}  // namespace

struct StripBuilder::Impl {
    WorkQueue& queue = WorkQueue::global();
    WorkQueue::GroupId group = queue.createGroup();
    std::uint64_t generation = 0;
    std::vector<Picture> pictures;
    StripOptions options;
    bool transparent = false;
    int pending = 0;  // pictures whose size is still being read

    std::vector<Size> sizes() const {
        std::vector<Size> result;
        result.reserve(pictures.size());
        for (const Picture& picture : pictures) {
            result.push_back(picture.size.value_or(Size{1, 1}));
        }
        return result;
    }

    StripLayout layout() const { return layoutStrip(sizes(), options); }

    QString problem() const {
        if (pending > 0) {
            return {};
        }
        if (pictures.size() < 2) {
            return QObject::tr("Choose at least two pictures.");
        }
        for (const Picture& picture : pictures) {
            if (picture.unreadable) {
                return QObject::tr("%1 cannot be read.").arg(QFileInfo(picture.path).fileName());
            }
        }
        SubimageInfo info;
        info.levels = {{layout().canvas, {}}};
        if (!canSaveWhole(info)) {
            return QObject::tr("The strip would be too large to be saved.");
        }
        return {};
    }
};

StripBuilder::StripBuilder(QObject* parent) : QAbstractListModel(parent), m_impl(std::make_unique<Impl>()) {}

StripBuilder::~StripBuilder() {
    // A running task posts its result to this object, so it must finish first.
    m_impl->queue.cancelGroup(m_impl->group);
    m_impl->queue.waitGroup(m_impl->group);
}

int StripBuilder::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_impl->pictures.size());
}

QVariant StripBuilder::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount()) {
        return {};
    }
    const QString& path = m_impl->pictures[static_cast<std::size_t>(index.row())].path;
    switch (role) {
    case NameRole: return QFileInfo(path).fileName();
    case PathRole: return path;
    case ThumbnailRole: return ThumbnailProvider::urlForPath(path, QFileInfo(path).lastModified().toMSecsSinceEpoch());
    default: return {};
    }
}

QHash<int, QByteArray> StripBuilder::roleNames() const {
    return {{NameRole, "name"}, {PathRole, "path"}, {ThumbnailRole, "thumbnail"}};
}

QStringList StripBuilder::paths() const {
    QStringList result;
    for (const Picture& picture : m_impl->pictures) {
        result << picture.path;
    }
    return result;
}

void StripBuilder::setPaths(const QStringList& paths) {
    m_impl->queue.cancelGroup(m_impl->group);
    const std::uint64_t generation = ++m_impl->generation;
    beginResetModel();
    m_impl->pictures.clear();
    for (const QString& path : paths.first(std::min<qsizetype>(paths.size(), kMaxPictures))) {
        m_impl->pictures.push_back({path, std::nullopt, false});
    }
    endResetModel();
    m_impl->pending = static_cast<int>(m_impl->pictures.size());
    emit picturesChanged();
    emit resultChanged();
    for (const Picture& picture : m_impl->pictures) {
        m_impl->queue.submit(m_impl->group, kSizePriority, [this, generation, path = picture.path](std::stop_token stop) {
            std::optional<Size> size;
            if (const auto source = openImageSource(QFileInfo(path).filesystemFilePath(), ReadPriority::Background, stop)) {
                const SubimageInfo& info = (*source)->subimages()[0];
                size = displaySize(info.levels.front().size, info.orientation);
            }
            QMetaObject::invokeMethod(this, [this, generation, path, size] {
                if (generation != m_impl->generation) {
                    return;
                }
                for (Picture& picture : m_impl->pictures) {
                    if (picture.path == path && !picture.size && !picture.unreadable) {
                        picture.size = size;
                        picture.unreadable = !size;
                        break;
                    }
                }
                --m_impl->pending;
                emit resultChanged();
            });
        });
    }
}

int StripBuilder::count() const { return static_cast<int>(m_impl->pictures.size()); }

namespace {

// Sets an option and reports the change, for the setters below.
template <typename T>
void setOption(StripBuilder* builder, T& option, T value) {
    if (option != value) {
        option = value;
        emit builder->optionsChanged();
        emit builder->resultChanged();
    }
}

}  // namespace

bool StripBuilder::down() const { return m_impl->options.direction == StripDirection::Down; }
void StripBuilder::setDown(bool down) { setOption(this, m_impl->options.direction, down ? StripDirection::Down : StripDirection::Across); }
int StripBuilder::extent() const { return m_impl->options.extent; }
void StripBuilder::setExtent(int extent) { setOption(this, m_impl->options.extent, std::max(1, extent)); }
int StripBuilder::resize() const { return static_cast<int>(m_impl->options.resize); }
void StripBuilder::setResize(int resize) { setOption(this, m_impl->options.resize, static_cast<StripResize>(std::clamp(resize, 0, 2))); }
int StripBuilder::spacing() const { return m_impl->options.spacing; }
void StripBuilder::setSpacing(int spacing) { setOption(this, m_impl->options.spacing, std::max(0, spacing)); }
int StripBuilder::margin() const { return m_impl->options.margin; }
void StripBuilder::setMargin(int margin) { setOption(this, m_impl->options.margin, std::max(0, margin)); }
int StripBuilder::shadow() const { return m_impl->options.shadow; }
void StripBuilder::setShadow(int shadow) { setOption(this, m_impl->options.shadow, std::max(0, shadow)); }
bool StripBuilder::transparent() const { return m_impl->transparent; }
void StripBuilder::setTransparent(bool transparent) { setOption(this, m_impl->transparent, transparent); }
bool StripBuilder::reading() const { return m_impl->pending > 0; }

QSize StripBuilder::resultSize() const {
    if (m_impl->pending > 0 || m_impl->pictures.empty()) {
        return {};
    }
    const Size size = m_impl->layout().canvas;
    return {size.width, size.height};
}

QString StripBuilder::problem() const { return m_impl->problem(); }

void StripBuilder::move(int from, int to) {
    const int n = count();
    if (from < 0 || from >= n || to < 0 || to >= n || from == to) {
        return;
    }
    // Qt counts the place to move to before the row is taken out.
    beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
    const auto first = m_impl->pictures.begin();
    if (from < to) {
        std::rotate(first + from, first + from + 1, first + to + 1);
    } else {
        std::rotate(first + to, first + from, first + from + 1);
    }
    endMoveRows();
    emit picturesChanged();
    emit resultChanged();
}

void StripBuilder::remove(int index) {
    if (index < 0 || index >= count()) {
        return;
    }
    beginRemoveRows({}, index, index);
    m_impl->pictures.erase(m_impl->pictures.begin() + index);
    endRemoveRows();
    emit picturesChanged();
    emit resultChanged();
}

QVariant StripBuilder::strip() const {
    if (m_impl->pending > 0 || !m_impl->problem().isEmpty()) {
        return {};
    }
    const StripLayout layout = m_impl->layout();
    std::vector<std::filesystem::path> files;
    for (const Picture& picture : m_impl->pictures) {
        files.push_back(QFileInfo(picture.path).filesystemFilePath());
    }
    CanvasLook look;
    look.shadow = m_impl->options.shadow;
    if (m_impl->transparent) {
        look.background.reset();
    }
    SubimageInfo info;
    info.format = PixelFormat::Bgrx8;
    info.alpha = m_impl->transparent;
    info.stored.colorModel = "RGB";
    // Empty means sRGB, which every picture is converted to.
    auto decoder = [files = std::move(files), layout, look](const std::stop_token& stop) -> Result<ImageBuffer> {
        std::vector<PlacedPicture> placed;
        for (std::size_t i = 0; i < files.size(); ++i) {
            const PixelRect& rect = layout.pictures[i];
            auto pixels = makeSrgbCopy(files[i], {rect.width, rect.height}, stop);
            if (!pixels) {
                // Cancelling is told apart by its message, which must stay as it is.
                return pixels.error() == "cancelled" ? pixels : std::unexpected(files[i].filename().string() + ": " + pixels.error());
            }
            placed.push_back({*pixels, rect.x, rect.y});
        }
        return composeCanvas(layout.canvas, placed, look);
    };
    return QVariant::fromValue(makeWholeImageSource(std::move(info), layout.canvas, std::move(decoder)));
}

QString StripBuilder::suggestedName() const { return timeStampName(QDateTime::currentDateTime()) + QStringLiteral(".webp"); }

}  // namespace iv
