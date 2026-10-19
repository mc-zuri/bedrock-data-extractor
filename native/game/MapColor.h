#pragma once
// Block::getMapColor, called: the function, or, where the compiler put it inline in its callers, the parts of it they
// call, read from their code and called as they call them.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bdg::game {

struct Color4 {
    float c[4];
};

// mce::Color Block::getMapColor(BlockSource&, BlockPos const&) const; false when it faults
bool callMapColor(std::uintptr_t fn, std::uintptr_t state, std::uintptr_t region, void const* pos, Color4* out);

// What an inline Block::getMapColor calls: where the block has a map colour component, the colour it keeps tinted by the
// sampler its tint method picks (each component times the sample's); else its type's own
// BlockLegacy::getMapColor(BlockSource&, BlockPos const&, Block const&).
struct MapColorParts {
    // what the component is looked up in: the block, or its BlockLegacy (at `legacy`), plus an offset
    struct Lookup {
        bool         ofLegacy;
        std::int64_t offset;
        bool         operator==(Lookup const&) const = default;
    };

    std::uintptr_t      component = 0;                               // the map colour component of what a lookup gives, or null: (void const*) -> component*
    std::vector<Lookup> lookups;                                     // asked in turn, while they answer null
    std::int64_t        tintAt                                 = 0;  // the component's tint method, a byte
    std::uintptr_t      sampler                                = 0;  // the tint method's sampler: (byte) -> sampler*
    int                 sampleSlot                             = 0;  // the sampler's virtual: mce::Color (BlockSource&, BlockPos const&)
    std::int64_t        colorAt                                = 0;  // the component's mce::Color
    std::int64_t        legacy                                 = 0;  // Block::mLegacy
    int                 slot                                   = 0;  // BlockLegacy::getMapColor
    bool                operator==(MapColorParts const&) const = default;
};

// The parts each place in the image that has Block::getMapColor inline calls (a call of `slot` of the BlockLegacy at
// `legacy`), read from its code: nullopt (`why`) when no place has it, or two places call different parts
std::optional<MapColorParts> inlinedMapColor(std::int64_t legacy, int slot, std::string& why);

// the block's map colour component, as the parts ask it; 0 when it has none (or the call faults)
std::uintptr_t mapColorComponent(MapColorParts const& parts, std::uintptr_t state);

// mce::Color Block::getMapColor(BlockSource&, BlockPos const&) const as its parts answer it; false when a call faults
bool askMapColor(MapColorParts const& parts, std::uintptr_t state, std::uintptr_t region, void const* pos, Color4* out);

}
