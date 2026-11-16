// Bounds-checked little-endian binary reading, no alignment required.
#pragma once

#include <bit>
#include <cstdint>
#include <cstring>

static_assert(std::endian::native == std::endian::little,
              "LogHunter assumes a little-endian architecture (x86-64, ARM64).");

namespace lh::evtx {

template <typename T>
inline T readRaw(const uint8_t* p) noexcept
{
    T value;
    std::memcpy(&value, p, sizeof(T));
    return value;
}

inline uint16_t read16(const uint8_t* p) noexcept { return readRaw<uint16_t>(p); }
inline uint32_t read32(const uint8_t* p) noexcept { return readRaw<uint32_t>(p); }
inline uint64_t read64(const uint8_t* p) noexcept { return readRaw<uint64_t>(p); }

class Cursor
{
public:
    Cursor(const uint8_t* base, size_t size, size_t position = 0) noexcept
        : m_base(base), m_size(size), m_position(position)
    {
        if (m_position > m_size)
            m_error = true;
    }

    [[nodiscard]] bool ok() const noexcept { return !m_error; }
    [[nodiscard]] size_t position() const noexcept { return m_position; }
    [[nodiscard]] const uint8_t* pointer() const noexcept { return m_base + m_position; }

    bool available(size_t n) noexcept
    {
        if (m_error || n > m_size - m_position) {
            m_error = true;
            return false;
        }
        return true;
    }

    uint8_t u8() noexcept { return read<uint8_t>(); }
    uint16_t u16() noexcept { return read<uint16_t>(); }
    uint32_t u32() noexcept { return read<uint32_t>(); }
    uint64_t u64() noexcept { return read<uint64_t>(); }

    const uint8_t* take(size_t n) noexcept
    {
        if (!available(n))
            return nullptr;
        const uint8_t* p = m_base + m_position;
        m_position += n;
        return p;
    }

    bool skip(size_t n) noexcept { return take(n) != nullptr || n == 0; }

private:
    template <typename T>
    T read() noexcept
    {
        if (!available(sizeof(T)))
            return 0;
        T value = readRaw<T>(m_base + m_position);
        m_position += sizeof(T);
        return value;
    }

    const uint8_t* m_base;
    size_t m_size;
    size_t m_position;
    bool m_error = false;
};

} // namespace lh::evtx
