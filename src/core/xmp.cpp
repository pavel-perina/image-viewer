#include "core/xmp.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

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

void removeXmpLocation(std::span<std::uint8_t> packet) {
    constexpr std::string_view kPrefix = "exif:GPS";
    auto* const data = reinterpret_cast<char*>(packet.data());
    const std::string_view text(data, packet.size());
    const auto blank = [&](std::size_t from, std::size_t to) { std::fill(data + from, data + to, ' '); };
    std::size_t at = text.find(kPrefix);
    while (at != std::string_view::npos && at > 0) {
        std::size_t nameEnd = at + kPrefix.size();
        while (nameEnd < text.size() && (std::isalnum(static_cast<unsigned char>(text[nameEnd])) != 0)) {
            ++nameEnd;
        }
        const std::string_view name = text.substr(at, nameEnd - at);
        std::size_t next = nameEnd;
        if (text[at - 1] == '<') {
            // An element: up to its end tag, or itself if it closes itself.
            const std::size_t close = text.find('>', nameEnd);
            if (close != std::string_view::npos && text[close - 1] == '/') {
                blank(at - 1, close + 1);
                next = close + 1;
            } else if (close != std::string_view::npos) {
                const std::string endTag = "</" + std::string(name) + ">";
                if (const std::size_t end = text.find(endTag, close); end != std::string_view::npos) {
                    blank(at - 1, end + endTag.size());
                    next = end + endTag.size();
                }
            }
        } else if (isSpace(text[at - 1])) {
            // An attribute: the name, the equals sign and the quoted value.
            std::size_t i = nameEnd;
            while (i < text.size() && isSpace(text[i])) {
                ++i;
            }
            if (i < text.size() && text[i] == '=') {
                ++i;
                while (i < text.size() && isSpace(text[i])) {
                    ++i;
                }
                if (i < text.size() && (text[i] == '"' || text[i] == '\'')) {
                    if (const std::size_t end = text.find(text[i], i + 1); end != std::string_view::npos) {
                        blank(at, end + 1);
                        next = end + 1;
                    }
                }
            }
        }
        at = text.find(kPrefix, next);
    }
}

}  // namespace iv
