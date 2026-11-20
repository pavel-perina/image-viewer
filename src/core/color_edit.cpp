#include "core/color_edit.h"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>

#include "core/bands.h"
#include "core/oklab.h"

namespace iv {

namespace {

// Rows handled together, so the Oklab planes of a band stay small and in cache.
constexpr int kRowsPerChunk = 16;
// Enough points that joining them by straight lines stays within a 16-bit step of the curve.
constexpr int kCurveSamples = 16385;
constexpr int kLargestCube = 129;
constexpr int kLargest1d = 65536;
constexpr int kCubeFrom1d = 33;

bool isGray(PixelFormat format) {
    return format == PixelFormat::Gray8 || format == PixelFormat::Gray16 || format == PixelFormat::Gray32F;
}

int channelsOf(PixelFormat format) { return isGray(format) ? 1 : 3; }

// Samples as stored, from 0 to 1 for integers, as R, G, B or one gray value per pixel.
void readRow(const ImageBuffer& image, int y, float* out) {
    const int width = image.width();
    const std::byte* row = image.row(y);
    switch (image.format()) {
    case PixelFormat::Bgrx8: {
        const auto* p = reinterpret_cast<const std::uint8_t*>(row);
        for (int x = 0; x < width; ++x, p += 4, out += 3) {
            out[0] = p[2] / 255.0F;
            out[1] = p[1] / 255.0F;
            out[2] = p[0] / 255.0F;
        }
        break;
    }
    case PixelFormat::Gray8: {
        const auto* p = reinterpret_cast<const std::uint8_t*>(row);
        for (int x = 0; x < width; ++x) {
            out[x] = p[x] / 255.0F;
        }
        break;
    }
    case PixelFormat::Rgba16: {
        const auto* p = reinterpret_cast<const std::uint16_t*>(row);
        for (int x = 0; x < width; ++x, p += 4, out += 3) {
            out[0] = p[0] / 65535.0F;
            out[1] = p[1] / 65535.0F;
            out[2] = p[2] / 65535.0F;
        }
        break;
    }
    case PixelFormat::Gray16: {
        const auto* p = reinterpret_cast<const std::uint16_t*>(row);
        for (int x = 0; x < width; ++x) {
            out[x] = p[x] / 65535.0F;
        }
        break;
    }
    case PixelFormat::Rgba32F: {
        const auto* p = reinterpret_cast<const float*>(row);
        for (int x = 0; x < width; ++x, p += 4, out += 3) {
            out[0] = p[0];
            out[1] = p[1];
            out[2] = p[2];
        }
        break;
    }
    case PixelFormat::Gray32F:
        std::copy_n(reinterpret_cast<const float*>(row), width, out);
        break;
    }
}

template <typename Sample>
Sample toInteger(float value) {
    constexpr float kMax = std::numeric_limits<Sample>::max();
    return static_cast<Sample>(std::clamp(value, 0.0F, 1.0F) * kMax + 0.5F);
}

// The other way, keeping the unused or alpha channel of the source.
void writeRow(const ImageBuffer& source, const ImageBuffer& target, int y, const float* in) {
    const int width = source.width();
    switch (source.format()) {
    case PixelFormat::Bgrx8: {
        const auto* from = reinterpret_cast<const std::uint8_t*>(source.row(y));
        auto* p = reinterpret_cast<std::uint8_t*>(target.row(y));
        for (int x = 0; x < width; ++x, p += 4, from += 4, in += 3) {
            p[2] = toInteger<std::uint8_t>(in[0]);
            p[1] = toInteger<std::uint8_t>(in[1]);
            p[0] = toInteger<std::uint8_t>(in[2]);
            p[3] = from[3];
        }
        break;
    }
    case PixelFormat::Gray8: {
        auto* p = reinterpret_cast<std::uint8_t*>(target.row(y));
        for (int x = 0; x < width; ++x) {
            p[x] = toInteger<std::uint8_t>(in[x]);
        }
        break;
    }
    case PixelFormat::Rgba16: {
        const auto* from = reinterpret_cast<const std::uint16_t*>(source.row(y));
        auto* p = reinterpret_cast<std::uint16_t*>(target.row(y));
        for (int x = 0; x < width; ++x, p += 4, from += 4, in += 3) {
            p[0] = toInteger<std::uint16_t>(in[0]);
            p[1] = toInteger<std::uint16_t>(in[1]);
            p[2] = toInteger<std::uint16_t>(in[2]);
            p[3] = from[3];
        }
        break;
    }
    case PixelFormat::Gray16: {
        auto* p = reinterpret_cast<std::uint16_t*>(target.row(y));
        for (int x = 0; x < width; ++x) {
            p[x] = toInteger<std::uint16_t>(in[x]);
        }
        break;
    }
    case PixelFormat::Rgba32F: {
        const auto* from = reinterpret_cast<const float*>(source.row(y));
        auto* p = reinterpret_cast<float*>(target.row(y));
        for (int x = 0; x < width; ++x, p += 4, from += 4, in += 3) {
            p[0] = in[0];
            p[1] = in[1];
            p[2] = in[2];
            p[3] = from[3];
        }
        break;
    }
    case PixelFormat::Gray32F:
        std::copy_n(in, width, reinterpret_cast<float*>(target.row(y)));
        break;
    }
}

// Lightness, green to red and blue to yellow of a chunk of rows, one plane each.
struct OklabPlanes {
    std::vector<float> L;
    std::vector<float> a;
    std::vector<float> b;
};

// The stages that look at one pixel at a time. Ones that look at neighbours would run here too,
// on planes of a chunk with rows of margin around it.
void applyOklabStages(OklabPlanes& planes, const ColorEdit& edit) {
    if (!edit.lightness.isIdentity()) {
        for (float& L : planes.L) {
            L = edit.lightness(L);
        }
    }
    if (edit.chroma != 1) {
        const auto gain = static_cast<float>(edit.chroma);
        for (float& a : planes.a) {
            a *= gain;
        }
        for (float& b : planes.b) {
            b *= gain;
        }
    }
}

bool inGamut(const LinearRgb& c) {
    constexpr double kSlack = 1e-6;
    return c.r >= -kSlack && c.g >= -kSlack && c.b >= -kSlack && c.r <= 1 + kSlack && c.g <= 1 + kSlack &&
           c.b <= 1 + kSlack;
}

// The colour at the same hue with lightness clamped and as much of its chroma as fits, found by
// halving the steps; the shader takes the same steps.
constexpr int kGamutSteps = 12;

LinearRgb intoGamut(Oklab lab) {
    lab.L = std::clamp(lab.L, 0.0, 1.0);
    LinearRgb rgb = oklabToLinear(lab);
    if (inGamut(rgb)) {
        return rgb;
    }
    double low = 0;
    double high = 1;
    for (int i = 0; i < kGamutSteps; ++i) {
        const double middle = (low + high) / 2;
        (inGamut(oklabToLinear({lab.L, lab.a * middle, lab.b * middle})) ? low : high) = middle;
    }
    return oklabToLinear({lab.L, lab.a * low, lab.b * low});
}

// Through Oklab and back, in samples as stored. Integer formats are clipped to their range in
// linear light; floats keep what lies beyond it.
void throughOklab(std::vector<float>& samples, int channels, bool clip, OklabPlanes& planes, const ColorEdit& edit) {
    const std::size_t pixels = samples.size() / static_cast<std::size_t>(channels);
    planes.L.resize(pixels);
    planes.a.assign(pixels, 0);
    planes.b.assign(pixels, 0);
    for (std::size_t i = 0; i < pixels; ++i) {
        if (channels == 1) {
            planes.L[i] = static_cast<float>(std::cbrt(srgbDecode(samples[i])));
        } else {
            const float* p = &samples[i * 3];
            const Oklab lab = linearToOklab(srgbDecode(p[0]), srgbDecode(p[1]), srgbDecode(p[2]));
            planes.L[i] = static_cast<float>(lab.L);
            planes.a[i] = static_cast<float>(lab.a);
            planes.b[i] = static_cast<float>(lab.b);
        }
    }
    applyOklabStages(planes, edit);
    const auto encode = [clip](double linear) {
        return static_cast<float>(srgbEncode(clip ? std::clamp(linear, 0.0, 1.0) : linear));
    };
    for (std::size_t i = 0; i < pixels; ++i) {
        if (channels == 1) {
            const double L = planes.L[i];
            samples[i] = encode(L * L * L);
        } else {
            const Oklab lab{planes.L[i], planes.a[i], planes.b[i]};
            const LinearRgb rgb = clip && edit.gamutMap ? intoGamut(lab) : oklabToLinear(lab);
            float* p = &samples[i * 3];
            p[0] = encode(rgb.r);
            p[1] = encode(rgb.g);
            p[2] = encode(rgb.b);
        }
    }
}

// White balance on R, G, B samples as stored: decoded, through the matrix, and encoded again.
// Integer formats have what leaves the range fitted by chroma when gamutMap is on, and clipped
// in linear light otherwise, as the preview does; floats keep it.
void balanceWhite(std::vector<float>& samples, const Matrix3& matrix, const std::array<double, 3>& luminance, bool clip,
                  bool gamutMap) {
    for (std::size_t i = 0; i < samples.size(); i += 3) {
        float* p = &samples[i];
        std::array<double, 3> rgb = matrix * std::array<double, 3>{srgbDecode(p[0]), srgbDecode(p[1]), srgbDecode(p[2])};
        if (clip && gamutMap) {
            rgb = fitByChroma(rgb, luminance);
        }
        if (clip) {
            for (double& c : rgb) {
                c = std::clamp(c, 0.0, 1.0);
            }
        }
        p[0] = static_cast<float>(srgbEncode(rgb[0]));
        p[1] = static_cast<float>(srgbEncode(rgb[1]));
        p[2] = static_cast<float>(srgbEncode(rgb[2]));
    }
}

std::string_view trimmed(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r") - first + 1);
}

// Numbers separated by spaces, as many as asked for and no more.
bool readNumbers(std::string_view text, float* out, int count) {
    const char* p = text.data();
    const char* end = text.data() + text.size();
    for (int i = 0; i < count; ++i) {
        while (p < end && (*p == ' ' || *p == '\t')) {
            ++p;
        }
        const auto [next, error] = std::from_chars(p, end, out[i]);
        if (error != std::errc()) {
            return false;
        }
        p = next;
    }
    while (p < end && (*p == ' ' || *p == '\t')) {
        ++p;
    }
    return p == end;
}

}  // namespace

float Curve::operator()(float x) const {
    if (samples.empty()) {
        return x;
    }
    const float position = std::clamp(x, 0.0F, 1.0F) * static_cast<float>(samples.size() - 1);
    const std::size_t i = std::min(static_cast<std::size_t>(position), samples.size() - 2);
    const float fraction = position - static_cast<float>(i);
    return samples[i] + (samples[i + 1] - samples[i]) * fraction;
}

namespace {

template <typename Map>
Curve sampledCurve(const Map& map) {
    Curve curve;
    curve.samples.resize(kCurveSamples);
    for (int i = 0; i < kCurveSamples; ++i) {
        curve.samples[static_cast<std::size_t>(i)] = static_cast<float>(map(static_cast<double>(i) / (kCurveSamples - 1)));
    }
    return curve;
}

}  // namespace

Curve levelsCurve(double black, double white, double gamma, double outBlack, double outWhite) {
    if (black == 0 && white == 1 && gamma == 1 && outBlack == 0 && outWhite == 1) {
        return {};
    }
    const double span = std::max(white - black, 1e-6);
    return sampledCurve([&](double x) {
        const double t = std::clamp((x - black) / span, 0.0, 1.0);
        return outBlack + (outWhite - outBlack) * std::pow(t, 1 / gamma);
    });
}

Curve lightnessCurve(double brightness, double contrast, double gamma) {
    if (brightness == 0 && contrast == 1 && gamma == 1) {
        return {};
    }
    return sampledCurve([&](double x) {
        return std::clamp((std::pow(x, 1 / gamma) - 0.5) * contrast + 0.5 + brightness, 0.0, 1.0);
    });
}

std::array<float, 3> ColorCube::operator()(float r, float g, float b) const {
    const int n = size;
    const auto grid = [&](float value, int c) {
        const float t = (value - domainMin[static_cast<std::size_t>(c)]) /
                        (domainMax[static_cast<std::size_t>(c)] - domainMin[static_cast<std::size_t>(c)]);
        return std::clamp(t, 0.0F, 1.0F) * static_cast<float>(n - 1);
    };
    float fr = grid(r, 0);
    float fg = grid(g, 1);
    float fb = grid(b, 2);
    const int ir = std::min(static_cast<int>(fr), n - 2);
    const int ig = std::min(static_cast<int>(fg), n - 2);
    const int ib = std::min(static_cast<int>(fb), n - 2);
    fr -= static_cast<float>(ir);
    fg -= static_cast<float>(ig);
    fb -= static_cast<float>(ib);
    const auto at = [&](int i, int j, int k) {
        return &rgb[((static_cast<std::size_t>(ib + k) * static_cast<std::size_t>(n) + static_cast<std::size_t>(ig + j)) *
                         static_cast<std::size_t>(n) +
                     static_cast<std::size_t>(ir + i)) *
                    3];
    };
    const float* c000 = at(0, 0, 0);
    const float* c111 = at(1, 1, 1);
    // The tetrahedron of the cell that holds the point, by the order of its three fractions.
    const float* first = nullptr;
    const float* second = nullptr;
    float w1 = 0;
    float w2 = 0;
    float w3 = 0;
    if (fr >= fg) {
        if (fg >= fb) {
            first = at(1, 0, 0), second = at(1, 1, 0), w1 = fr, w2 = fg, w3 = fb;
        } else if (fr >= fb) {
            first = at(1, 0, 0), second = at(1, 0, 1), w1 = fr, w2 = fb, w3 = fg;
        } else {
            first = at(0, 0, 1), second = at(1, 0, 1), w1 = fb, w2 = fr, w3 = fg;
        }
    } else {
        if (fb >= fg) {
            first = at(0, 0, 1), second = at(0, 1, 1), w1 = fb, w2 = fg, w3 = fr;
        } else if (fb >= fr) {
            first = at(0, 1, 0), second = at(0, 1, 1), w1 = fg, w2 = fb, w3 = fr;
        } else {
            first = at(0, 1, 0), second = at(1, 1, 0), w1 = fg, w2 = fr, w3 = fb;
        }
    }
    std::array<float, 3> out{};
    for (std::size_t c = 0; c < 3; ++c) {
        out[c] = c000[c] + w1 * (first[c] - c000[c]) + w2 * (second[c] - first[c]) + w3 * (c111[c] - second[c]);
    }
    return out;
}

ColorCube identityCube(int size) {
    ColorCube cube;
    cube.size = size;
    cube.rgb.resize(static_cast<std::size_t>(size) * size * size * 3);
    std::size_t at = 0;
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                cube.rgb[at++] = static_cast<float>(r) / static_cast<float>(size - 1);
                cube.rgb[at++] = static_cast<float>(g) / static_cast<float>(size - 1);
                cube.rgb[at++] = static_cast<float>(b) / static_cast<float>(size - 1);
            }
        }
    }
    return cube;
}

Result<ColorCube> parseCube(std::string_view text) {
    int size3d = 0;
    int size1d = 0;
    std::array<float, 3> domainMin{0, 0, 0};
    std::array<float, 3> domainMax{1, 1, 1};
    std::vector<float> values;
    int lineNumber = 0;
    while (!text.empty()) {
        const auto newline = text.find('\n');
        const std::string_view line = trimmed(text.substr(0, newline));
        text = newline == std::string_view::npos ? std::string_view() : text.substr(newline + 1);
        ++lineNumber;
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto space = line.find_first_of(" \t");
        const std::string_view keyword = line.substr(0, space);
        const std::string_view rest = space == std::string_view::npos ? std::string_view() : trimmed(line.substr(space));
        const auto bad = [&] { return std::unexpected("line " + std::to_string(lineNumber) + " of the .cube file is not understood"); };
        if (keyword == "TITLE") {
            continue;
        }
        if (keyword == "LUT_3D_SIZE" || keyword == "LUT_1D_SIZE") {
            float n = 0;
            if (!readNumbers(rest, &n, 1) || n != std::floor(n)) {
                return bad();
            }
            (keyword == "LUT_3D_SIZE" ? size3d : size1d) = static_cast<int>(n);
            continue;
        }
        if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX") {
            if (!readNumbers(rest, (keyword == "DOMAIN_MIN" ? domainMin : domainMax).data(), 3)) {
                return bad();
            }
            continue;
        }
        if (keyword == "LUT_3D_INPUT_RANGE" || keyword == "LUT_1D_INPUT_RANGE") {
            std::array<float, 2> range{};
            if (!readNumbers(rest, range.data(), 2)) {
                return bad();
            }
            domainMin.fill(range[0]);
            domainMax.fill(range[1]);
            continue;
        }
        std::array<float, 3> rgb{};
        if (!readNumbers(line, rgb.data(), 3)) {
            return bad();
        }
        values.insert(values.end(), rgb.begin(), rgb.end());
    }
    for (std::size_t c = 0; c < 3; ++c) {
        if (!(domainMax[c] > domainMin[c])) {
            return std::unexpected("the .cube file has an empty domain");
        }
    }
    if ((size3d > 0) == (size1d > 0)) {
        return std::unexpected("the .cube file gives no size, or two");
    }
    if (size3d > 0) {
        if (size3d < 2 || size3d > kLargestCube) {
            return std::unexpected("a .cube of " + std::to_string(size3d) + " points per side is not supported");
        }
        if (values.size() != static_cast<std::size_t>(size3d) * size3d * size3d * 3) {
            return std::unexpected("the .cube file has the wrong number of entries");
        }
        return ColorCube{size3d, domainMin, domainMax, std::move(values)};
    }
    if (size1d < 2 || size1d > kLargest1d) {
        return std::unexpected("a 1D .cube of " + std::to_string(size1d) + " points is not supported");
    }
    if (values.size() != static_cast<std::size_t>(size1d) * 3) {
        return std::unexpected("the .cube file has the wrong number of entries");
    }
    // Each channel through its own curve, sampled on a 3D grid.
    const auto curve = [&](float t, std::size_t c) {
        const float position = t * static_cast<float>(size1d - 1);
        const auto i = std::min(static_cast<std::size_t>(position), static_cast<std::size_t>(size1d - 2));
        const float fraction = position - static_cast<float>(i);
        return values[i * 3 + c] + (values[(i + 1) * 3 + c] - values[i * 3 + c]) * fraction;
    };
    ColorCube cube = identityCube(kCubeFrom1d);
    cube.domainMin = domainMin;
    cube.domainMax = domainMax;
    for (std::size_t i = 0; i < cube.rgb.size(); ++i) {
        cube.rgb[i] = curve(cube.rgb[i], i % 3);
    }
    return cube;
}

bool ColorEdit::operator==(const ColorEdit& other) const {
    const bool sameCube = cube == other.cube || (cube && other.cube && *cube == *other.cube);
    return sameCube && cubeStrength == other.cubeStrength && whiteBalance == other.whiteBalance && workingSpace == other.workingSpace && tone == other.tone && lightness == other.lightness && chroma == other.chroma &&
           gamutMap == other.gamutMap;
}

ImageBuffer applyColor(const ImageBuffer& source, const ColorEdit& edit, const std::stop_token& stop) {
    const PixelFormat format = source.format();
    const bool gray = isGray(format);
    if (edit.isIdentity() || (gray && edit.tone.isIdentity() && edit.lightness.isIdentity())) {
        return source;
    }
    const bool balance = !gray && !edit.whiteBalance.isIdentity();
    const int channels = channelsOf(format);
    const bool clip = format != PixelFormat::Rgba32F && format != PixelFormat::Gray32F;
    const bool oklab = gray ? !edit.lightness.isIdentity() : edit.usesOklab();
    const ColorCube* cube = gray ? nullptr : edit.usedCube();
    const auto strength = static_cast<float>(std::min(edit.cubeStrength, 1.0));
    const ImageBuffer target = ImageBuffer::create(source.size(), format).withAlpha(source.hasAlpha());  // alpha is copied as it is
    const Matrix3 white = balance ? whiteBalanceMatrix(edit.whiteBalance, edit.workingSpace) : Matrix3{};
    const std::array<double, 3> luminance{edit.workingSpace.m[3], edit.workingSpace.m[4], edit.workingSpace.m[5]};
    const int width = source.width();
    std::atomic<bool> stopped = false;
    inBands(source.height(), width, [&](int firstRow, int endRow) {
        std::vector<float> samples;
        OklabPlanes planes;
        for (int y = firstRow; y < endRow; y += kRowsPerChunk) {
            if (stop.stop_requested()) {
                stopped = true;
                return;
            }
            const int rows = std::min(kRowsPerChunk, endRow - y);
            const std::size_t rowSamples = static_cast<std::size_t>(width) * static_cast<std::size_t>(channels);
            samples.resize(rowSamples * static_cast<std::size_t>(rows));
            for (int r = 0; r < rows; ++r) {
                readRow(source, y + r, samples.data() + rowSamples * static_cast<std::size_t>(r));
            }
            if (balance) {
                balanceWhite(samples, white, luminance, clip, edit.gamutMap);
            }
            if (!edit.tone.isIdentity()) {
                for (float& sample : samples) {
                    sample = edit.tone(sample);
                }
            }
            if (oklab) {
                throughOklab(samples, channels, clip, planes, edit);
            }
            if (cube) {
                for (std::size_t i = 0; i < samples.size(); i += 3) {
                    const auto out = (*cube)(samples[i], samples[i + 1], samples[i + 2]);
                    for (std::size_t c = 0; c < 3; ++c) {
                        samples[i + c] += strength * (out[c] - samples[i + c]);
                    }
                }
            }
            for (int r = 0; r < rows; ++r) {
                writeRow(source, target, y + r, samples.data() + rowSamples * static_cast<std::size_t>(r));
            }
        }
    });
    return stopped ? ImageBuffer{} : target;
}

}  // namespace iv
