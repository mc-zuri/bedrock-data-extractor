#pragma once
// Big-endian NBT.

#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace bdg::nbt {

enum Tag : std::uint8_t { End = 0, Byte = 1, Short = 2, Int = 3, Long = 4, Float = 5, Double = 6, String = 8, List = 9, Compound = 10 };

struct Buffer {
    std::vector<std::uint8_t> data;

    void u8(std::uint8_t v) { data.push_back(v); }
    void raw(void const* p, std::size_t n) {
        auto const* b = static_cast<std::uint8_t const*>(p);
        data.insert(data.end(), b, b + n);
    }
    void ordered(std::uint64_t v, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            data.push_back(static_cast<std::uint8_t>(v >> (8 * (bytes - 1 - i))));
        }
    }
    void i16(std::int16_t v) { ordered(static_cast<std::uint16_t>(v), 2); }
    void i32(std::int32_t v) { ordered(static_cast<std::uint32_t>(v), 4); }
    void i64(std::int64_t v) { ordered(static_cast<std::uint64_t>(v), 8); }
    void f32(float v) {
        std::uint32_t b;
        std::memcpy(&b, &v, 4);
        ordered(b, 4);
    }
    void f64(double v) {
        std::uint64_t b;
        std::memcpy(&b, &v, 8);
        ordered(b, 8);
    }
    void str(std::string_view s) {
        i16(static_cast<std::int16_t>(s.size()));
        raw(s.data(), s.size());
    }
    void header(Tag t, std::string_view name) {
        u8(t);
        str(name);
    }
    // a list's element type and length; its elements follow
    void list(Tag element, std::size_t size) {
        u8(element);
        i32(static_cast<std::int32_t>(size));
    }
};

}
