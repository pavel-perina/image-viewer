#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace iv {

// Reads numbers from a file's bytes and never past their end: a read beyond it gives 0 and
// marks the reader failed, so a parser checks once, after a group of reads, instead of
// before every one. For headers of formats that are parsed by hand.
class ByteReader {
public:
    explicit ByteReader(std::span<const std::uint8_t> bytes, std::size_t at = 0) : m_bytes(bytes), m_at(at) {
        if (at > bytes.size()) {
            m_failed = true;
            m_at = bytes.size();
        }
    }

    bool failed() const { return m_failed; }
    std::size_t position() const { return m_at; }
    std::size_t remaining() const { return m_bytes.size() - m_at; }

    // Moves to a position from the start, failing beyond the end.
    void seek(std::size_t at) {
        if (at > m_bytes.size()) {
            m_failed = true;
            m_at = m_bytes.size();
        } else {
            m_at = at;
        }
    }
    void skip(std::size_t count) { seek(count > remaining() ? m_bytes.size() + 1 : m_at + count); }

    std::uint8_t u8() { return take(1) ? m_bytes[m_at - 1] : 0; }
    std::uint16_t le16() { return static_cast<std::uint16_t>(le(2)); }
    std::uint32_t le32() { return static_cast<std::uint32_t>(le(4)); }
    std::int32_t le32signed() { return static_cast<std::int32_t>(le32()); }
    std::uint16_t be16() { return static_cast<std::uint16_t>(be(2)); }
    std::uint32_t be32() { return static_cast<std::uint32_t>(be(4)); }

    // The next count bytes, or nothing (and failed) if fewer are left.
    std::span<const std::uint8_t> bytes(std::size_t count) {
        if (!take(count)) {
            return {};
        }
        return m_bytes.subspan(m_at - count, count);
    }

private:
    bool take(std::size_t count) {
        if (m_failed || count > remaining()) {
            m_failed = true;
            m_at = m_bytes.size();
            return false;
        }
        m_at += count;
        return true;
    }
    std::uint64_t le(std::size_t count) {
        if (!take(count)) {
            return 0;
        }
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            value |= static_cast<std::uint64_t>(m_bytes[m_at - count + i]) << (8 * i);
        }
        return value;
    }
    std::uint64_t be(std::size_t count) {
        if (!take(count)) {
            return 0;
        }
        std::uint64_t value = 0;
        for (std::size_t i = 0; i < count; ++i) {
            value = value << 8 | m_bytes[m_at - count + i];
        }
        return value;
    }

    std::span<const std::uint8_t> m_bytes;
    std::size_t m_at = 0;
    bool m_failed = false;
};

}  // namespace iv
