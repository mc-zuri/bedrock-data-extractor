#pragma once

#include <charconv>
#include <compare>
#include <string_view>

namespace bdg {

// Missing parts are 0, so "1.21.0" sorts before every 1.21.0.x build. Parsed without sscanf: built against a newer
// glibc, it would ask the server's for a symbol an older one has not (__isoc23_sscanf, glibc 2.38).
struct Version {
    int major = 0, minor = 0, patch = 0, revision = 0;

    static Version parse(std::string_view text) {
        Version     v;
        char const* at  = text.data();
        char const* end = text.data() + text.size();
        for (int* part : {&v.major, &v.minor, &v.patch, &v.revision}) {
            auto const [next, error] = std::from_chars(at, end, *part);
            if (error != std::errc{} || next == end || *next != '.') {
                break;
            }
            at = next + 1;
        }
        return v;
    }

    auto operator<=>(Version const&) const = default;
};

}
