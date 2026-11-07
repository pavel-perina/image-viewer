#include "core/xmp.h"

#include <cstddef>

namespace iv {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

void resetXmpOrientation(std::span<std::uint8_t> packet) {
    constexpr std::string_view kName = "tiff:Orientation";
    auto* const data = reinterpret_cast<char*>(packet.data());
    const std::string_view text(data, packet.size());
    for (std::size_t at = text.find(kName); at != std::string_view::npos; at = text.find(kName, at + kName.size())) {
        if (at == 0) {
            continue;
        }
        std::size_t i = at + kName.size();
        auto skipSpaces = [&] {
            while (i < text.size() && isSpace(text[i])) {
                ++i;
            }
        };
        const char before = text[at - 1];
        char end = 0;  // what must follow the digit
        skipSpaces();
        if (isSpace(before) && i < text.size() && text[i] == '=') {
            ++i;
            skipSpaces();
            if (i < text.size() && (text[i] == '"' || text[i] == '\'')) {
                end = text[i++];
            }
        } else if (before == '<' && i < text.size() && text[i] == '>') {
            ++i;
            end = '<';
        }
        if (end == 0) {
            continue;
        }
        skipSpaces();
        const std::size_t digit = i;
        if (digit >= text.size() || !isDigit(text[digit])) {
            continue;
        }
        i = digit + 1;
        skipSpaces();
        if (i < text.size() && text[i] == end) {
            data[digit] = '1';
        }
    }
}

}  // namespace iv
