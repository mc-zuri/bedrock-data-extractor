#include "game/Effects.h"

#include "core/Log.h"
#include "memory/Mem.h"

#include <algorithm>
#include <format>

// MobEffect::mMobEffects is a static array indexed by effect id (0 is empty), each MobEffect's mId (a u32 at +8) its
// index: the longest such run from id 1, not the first, as the image also holds the effects in registration order, which
// agrees with the ids only partway (1.26.20 registers weakness last). Each field is the one offset where known effects
// read their known values.

namespace bdg::game {

using memory::readPtr;
using memory::readString;

namespace {

bool isEffect(std::uintptr_t e, std::uint32_t id) { return e >= 0x10000 && memory::inImage(readPtr(e)) && memory::read<std::uint32_t>(e, 8) == id; }

std::size_t effectsFromOne(std::uintptr_t array) {
    std::size_t n = 0;
    while (n < 120 && isEffect(readPtr(array, (n + 1) * 8), static_cast<std::uint32_t>(n + 1))) {
        ++n;
    }
    return n;
}

struct EffectArray {
    std::uintptr_t at    = 0;
    std::size_t    count = 0;
    std::uintptr_t effect(std::size_t id) const { return readPtr(at, static_cast<std::int64_t>(id * 8)); }
};

EffectArray findEffectArray() {
    EffectArray best;
    for (auto [from, to] : memory::dataSections()) {
        for (std::uintptr_t at = from; at + 16 <= to; at += 8) {
            if (isEffect(readPtr(at, 8), 1)) {
                if (std::size_t n = effectsFromOne(at); n > best.count) {
                    best = {at, n};
                }
            }
        }
    }
    return best;
}

}

Effects mobEffects() {
    EffectArray const array = findEffectArray();
    if (array.count < 20) {
        return {{}, std::format("no MobEffect array (the longest run of effects by id has {})", array.count)};
    }

    // the effects whose harm every build agrees on: slowness, mining fatigue, instant damage, nausea, blindness, hunger,
    // weakness, poison and wither; speed, haste, strength, instant health, jump boost, regeneration, resistance, fire
    // resistance, water breathing, invisibility, night vision, health boost, absorption and saturation. A byte of a word
    // that holds a pointer is none: it changes from run to run, and may fit (1.26.20: in some runs).
    constexpr std::size_t harms[]   = {2, 4, 7, 9, 15, 17, 18, 19, 20};
    constexpr std::size_t helps[]   = {1, 3, 5, 6, 8, 10, 11, 12, 13, 14, 16, 21, 22, 23};
    auto const            isPointer = [&](std::size_t id, std::int64_t off) {
        std::uintptr_t const p = readPtr(array.effect(id), off / 8 * 8);
        std::uint8_t         byte;
        return p >= 0x10000 && !(p & 7) && memory::safeRead(reinterpret_cast<void const*>(p), &byte, 1);
    };
    std::vector<std::int64_t> harmful, descriptionId, name;
    for (std::int64_t off = 9; off < 0x100; ++off) {
        auto       flag = [&](std::size_t id) { return memory::read<std::uint8_t>(array.effect(id), off); };
        bool const fits = std::ranges::all_of(harms, [&](std::size_t id) { return flag(id) == 1 && !isPointer(id, off); }) &&
                          std::ranges::all_of(helps, [&](std::size_t id) { return flag(id) == 0 && !isPointer(id, off); });
        if (fits) {
            harmful.push_back(off);
        }
    }
    for (std::int64_t off = 16; off < 0x200; off += 8) {
        auto text = [&](std::size_t id) { return readString(array.effect(id) + off); };
        if (text(1) == "potion.moveSpeed" && text(2) == "potion.moveSlowdown") {
            descriptionId.push_back(off);
        }
        if (text(1) == "speed" && text(2) == "slowness") {
            name.push_back(off);
        }
    }
    if (harmful.size() != 1 || descriptionId.size() != 1 || name.size() != 1) {
        return {{}, std::format("{} harmful, {} description id and {} name offsets fit", harmful.size(), descriptionId.size(), name.size())};
    }
    log::info("effects: {} at +0x{:X} (image), harmful at +{}, description id at +{}, name at +{}", array.count, array.at - memory::imageBase(), harmful[0], descriptionId[0],
              name[0]);

    Effects out;
    for (std::size_t id = 1; id <= array.count; ++id) {
        std::uintptr_t const e = array.effect(id);
        out.effects.push_back({static_cast<int>(id), readString(e + name[0]), readString(e + descriptionId[0]), memory::read<std::uint8_t>(e, harmful[0]) != 0});
    }
    return out;
}

}
