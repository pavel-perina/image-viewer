#include "ui/histogram_view.h"

#include <QImage>
#include <QPainter>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "ui/histogram_handle.h"

namespace iv {

namespace {

constexpr QRgb kBackground = qRgb(16, 16, 16);

// Indexed by which channels cover a pixel: bit 0 red, bit 1 green, bit 2 blue. A gray
// histogram covers all three. The base colours share one LCH lightness and chroma, and the
// mixes share a lighter one, so no channel looks darker than the others.
constexpr std::array<QRgb, 8> kCoverageColors{
    kBackground,
    qRgb(0xE2, 0x20, 0x23),  // red
    qRgb(0x00, 0x98, 0x00),  // green
    qRgb(0xD3, 0xD1, 0x6B),  // red + green: yellowish
    qRgb(0x68, 0x68, 0xE7),  // blue
    qRgb(0xFF, 0xA5, 0xEF),  // red + blue: pink
    qRgb(0x00, 0xE7, 0xE7),  // green + blue: teal
    qRgb(0xCC, 0xCC, 0xCC),  // all: light gray
};

struct BinRange {
    int first = 0;
    int last = 0;
};

BinRange usedBins(const Histogram& histogram) {
    int first = histogram.binCount() - 1;
    int last = 0;
    for (const auto& channel : histogram.channels) {
        for (int i = 0; i < std::ssize(channel); ++i) {
            if (channel[static_cast<std::size_t>(i)] != 0) {
                first = std::min(first, i);
                last = std::max(last, i);
            }
        }
    }
    if (first >= last) {  // empty, or a single value that still needs some width
        first = std::max(0, std::min(first, last) - 1);
        last = std::min(histogram.binCount() - 1, first + 2);
    }
    return {first, last};
}

// Adds up the bins that fall into each column of pixels.
std::vector<std::uint64_t> toColumns(const std::vector<std::uint64_t>& channel, BinRange bins, int width) {
    std::vector<std::uint64_t> columns(static_cast<std::size_t>(width), 0);
    const std::int64_t count = bins.last - bins.first + 1;
    for (int x = 0; x < width; ++x) {
        const std::int64_t begin = bins.first + x * count / width;
        const std::int64_t end = std::max(begin + 1, bins.first + (x + 1) * count / width);
        for (std::int64_t bin = begin; bin < end; ++bin) {
            columns[static_cast<std::size_t>(x)] += channel[static_cast<std::size_t>(bin)];
        }
    }
    return columns;
}

}  // namespace

struct HistogramView::Impl {
    std::shared_ptr<const Histogram> histogram;
    QVariant value;
    bool logScale = false;
    bool fitRange = false;

    BinRange visibleBins() const {
        if (!histogram || histogram->binCount() == 0) {
            return {};
        }
        return fitRange ? usedBins(*histogram) : BinRange{0, histogram->binCount() - 1};
    }

    double binValue(int bin) const {
        const int last = std::max(1, histogram->binCount() - 1);
        return histogram->low + (histogram->high - histogram->low) * bin / last;
    }

    QImage render(int width, int height) const {
        QImage image(width, height, QImage::Format_RGB32);
        image.fill(kBackground);
        if (!histogram || histogram->binCount() == 0) {
            return image;
        }
        const BinRange bins = visibleBins();
        const int channelCount = histogram->gray ? 1 : 3;

        std::array<std::vector<std::uint64_t>, 3> columns;
        for (int c = 0; c < channelCount; ++c) {
            columns[static_cast<std::size_t>(c)] = toColumns(histogram->channels[static_cast<std::size_t>(c)], bins, width);
        }

        // In a linear scale a clipped end would flatten everything else, so the two ends do
        // not set the height and may run off the top.
        std::uint64_t peak = 0;
        std::uint64_t peakWithEnds = 0;
        for (int c = 0; c < channelCount; ++c) {
            const auto& channel = columns[static_cast<std::size_t>(c)];
            for (int x = 0; x < width; ++x) {
                const std::uint64_t value = channel[static_cast<std::size_t>(x)];
                peakWithEnds = std::max(peakWithEnds, value);
                if (x > 0 && x < width - 1) {
                    peak = std::max(peak, value);
                }
            }
        }
        peak = peak > 0 ? peak : peakWithEnds;
        if (peak == 0) {
            return image;
        }

        const auto barHeight = [&](std::uint64_t value) {
            const double fraction = logScale ? std::log1p(static_cast<double>(value)) / std::log1p(static_cast<double>(peakWithEnds))
                                             : static_cast<double>(value) / static_cast<double>(peak);
            const int bar = static_cast<int>(std::lround(std::min(fraction, 1.0) * height));
            return value > 0 ? std::max(bar, 1) : 0;
        };

        // Each pixel is coloured by which channels reach it, so overlaps stay readable.
        for (int x = 0; x < width; ++x) {
            std::array<int, 3> bars{};
            for (int c = 0; c < 3; ++c) {
                const int source = histogram->gray ? 0 : c;
                bars[static_cast<std::size_t>(c)] = barHeight(columns[static_cast<std::size_t>(source)][static_cast<std::size_t>(x)]);
            }
            const int tallest = std::ranges::max(bars);
            for (int fromBottom = 0; fromBottom < tallest; ++fromBottom) {
                const std::size_t mask = (fromBottom < bars[0] ? 1u : 0u) | (fromBottom < bars[1] ? 2u : 0u) |
                                         (fromBottom < bars[2] ? 4u : 0u);
                reinterpret_cast<QRgb*>(image.scanLine(height - 1 - fromBottom))[x] = kCoverageColors[mask];
            }
        }
        return image;
    }
};

HistogramView::HistogramView(QQuickItem* parent) : QQuickPaintedItem(parent), m_impl(std::make_unique<Impl>()) {
    setOpaquePainting(true);
}

HistogramView::~HistogramView() = default;

QVariant HistogramView::histogram() const { return m_impl->value; }

void HistogramView::setHistogram(const QVariant& histogram) {
    m_impl->value = histogram;
    m_impl->histogram = histogram.value<std::shared_ptr<const Histogram>>();
    emit histogramChanged();
    emit displayChanged();
    update();
}

bool HistogramView::logScale() const { return m_impl->logScale; }

void HistogramView::setLogScale(bool enabled) {
    if (enabled != m_impl->logScale) {
        m_impl->logScale = enabled;
        emit displayChanged();
        update();
    }
}

bool HistogramView::fitRange() const { return m_impl->fitRange; }

void HistogramView::setFitRange(bool enabled) {
    if (enabled != m_impl->fitRange) {
        m_impl->fitRange = enabled;
        emit displayChanged();
        update();
    }
}

QString HistogramView::description() const {
    const auto& histogram = m_impl->histogram;
    if (!histogram || histogram->binCount() == 0) {
        return {};
    }
    const BinRange bins = m_impl->visibleBins();
    const bool isFloat = histogram->bitsPerBin == 0;
    const auto text = [&](int bin) {
        const double value = m_impl->binValue(bin);
        return isFloat ? QString::number(value, 'g', 4) : QString::number(static_cast<qint64>(std::lround(value)));
    };
    return QStringLiteral("%1 to %2, measured at %3 x %4")
        .arg(text(bins.first), text(bins.last))
        .arg(histogram->measuredSize.width)
        .arg(histogram->measuredSize.height);
}

void HistogramView::paint(QPainter* painter) {
    const qreal scale = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
    const int width = std::max(1, static_cast<int>(std::lround(this->width() * scale)));
    const int height = std::max(1, static_cast<int>(std::lround(this->height() * scale)));
    painter->drawImage(QRectF(0, 0, this->width(), this->height()), m_impl->render(width, height));
}

}  // namespace iv
