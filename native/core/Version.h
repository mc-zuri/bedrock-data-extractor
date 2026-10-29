#pragma once

#include <charconv>
#include <compare>
#include <string_view>
#include <tuple>

namespace bdg {

// Bedrock's order: by the version (the first three numbers), then that version's previews (the fourth number from 20),
// then its releases (below 20), which come out after them. A version without a fourth number ("1.21.0") is where its
// builds begin. Parsed without sscanf: built against a newer glibc, it would ask the server's for a symbol an older one
// has not (__isoc23_sscanf, glibc 2.38).
struct Version {
    static constexpr int PreviewFrom = 20;

    int major = 0, minor = 0, patch = 0, revision = -1;

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

    auto operator<=>(Version const& other) const { return order() <=> other.order(); }
    bool operator==(Version const& other) const { return order() == other.order(); }

private:
    std::tuple<int, int, int, int, int> order() const { return std::tuple{major, minor, patch, revision < 0 ? 0 : revision >= PreviewFrom ? 1 : 2, revision}; }
};

}
