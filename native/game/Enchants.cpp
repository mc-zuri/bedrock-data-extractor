#include "game/Enchants.h"

#include "core/Log.h"
#include "memory/Guard.h"
#include "memory/Mem.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <optional>
#include <string_view>

// Enchant::mEnchants is a static std::vector<std::unique_ptr<Enchant>>, entry i the Enchant of type i: the one whose
// entries name the known enchantments at one HashedString offset (mStringId). The fields below and the virtuals right
// after the destructor (isCompatibleWith, getMinCost, getMaxCost, getMinLevel, getMaxLevel) are where they are on every
// build, checked on known enchantments. isTreasureOnly and isDiscoverable move (12 and 13 in 1.16.201, 15 and 16 in
// 1.26.51): each is the one slot where every enchantment's function returns a constant matching the known answers, read
// from the code, not called.

namespace bdg::game {

using memory::readHashedString;
using memory::readPtr;

namespace {

constexpr std::int64_t TypeAt = 8, FrequencyAt = 12, TradeableAt = 16, PrimaryAt = 20, SecondaryAt = 24;
constexpr int          CompatibleSlot = memory::DestructorSlots, MinCostSlot = CompatibleSlot + 1, MaxCostSlot = CompatibleSlot + 2, MinLevelSlot = CompatibleSlot + 3,
              MaxLevelSlot = CompatibleSlot + 4;
constexpr int Faulted      = -0x7fffffff;

struct EnchantVector {
    std::uintptr_t begin  = 0;
    std::size_t    count  = 0;
    std::int64_t   nameAt = -1;
    std::uintptr_t at(std::size_t i) const { return readPtr(begin, static_cast<std::int64_t>(i * 8)); }
};

std::optional<EnchantVector> enchantVectorAt(std::uintptr_t at) {
    std::uintptr_t const b = readPtr(at), e = readPtr(at, 8);
    if (b < 0x10000 || e <= b || (e - b) % 8 || (e - b) / 8 < 20 || (e - b) / 8 > 100) {
        return std::nullopt;
    }
    EnchantVector v{b, (e - b) / 8};
    for (std::size_t i = 0; i < 18; ++i) {
        if (!memory::inImage(readPtr(v.at(i)))) {
            return std::nullopt;
        }
    }
    for (std::int64_t off = 16; off <= 0x100; off += 8) {
        if (readHashedString(v.at(0) + off) == "protection" && readHashedString(v.at(9) + off) == "sharpness" && readHashedString(v.at(15) + off) == "efficiency" &&
            readHashedString(v.at(17) + off) == "unbreaking") {
            v.nameAt = off;
            return v;
        }
    }
    return std::nullopt;
}

std::optional<EnchantVector> findEnchantVector() {
    for (auto [from, to] : memory::dataSections()) {
        for (std::uintptr_t at = from; at + 16 <= to; at += 8) {
            if (auto v = enchantVectorAt(at)) {
                return v;
            }
        }
    }
    return std::nullopt;
}

int callInt(std::uintptr_t fn, std::uintptr_t self) {
    if (!fn) {
        return Faulted;
    }
    int answer = Faulted;
    memory::guard([&] { answer = reinterpret_cast<int (*)(std::uintptr_t)>(fn)(self); });
    return answer;
}

int callIntOfInt(std::uintptr_t fn, std::uintptr_t self, int arg) {
    if (!fn) {
        return Faulted;
    }
    int answer = Faulted;
    memory::guard([&] { answer = reinterpret_cast<int (*)(std::uintptr_t, int)>(fn)(self, arg); });
    return answer;
}

// 1 or 0 as Enchant::isCompatibleWith answers, -1 when it faults
int callCompatible(std::uintptr_t fn, std::uintptr_t self, int type) {
    if (!fn) {
        return -1;
    }
    int answer = -1;
    memory::guard([&] { answer = reinterpret_cast<bool (*)(std::uintptr_t, int)>(fn)(self, type) ? 1 : 0; });
    return answer;
}

// 1 or 0 for a function that only returns that constant (mov al, 1 / xor al, al / xor eax, eax; ret), else -1
int constantBool(std::uintptr_t fn) {
    auto const b0 = memory::read<std::uint8_t>(fn), b1 = memory::read<std::uint8_t>(fn, 1), b2 = memory::read<std::uint8_t>(fn, 2);
    if (b0 == 0xB0 && b1 == 0x01 && b2 == 0xC3) {
        return 1;
    }
    if ((b0 == 0x32 || b0 == 0x30 || b0 == 0x31 || b0 == 0x33) && b1 == 0xC0 && b2 == 0xC3) {
        return 0;
    }
    // built without optimization (Linux 1.16.201 - 1.16.210): the frame, `this` spilled, the bool masked
    std::uint8_t const unoptimizedTrue[]  = {0x55, 0x48, 0x89, 0xE5, 0xB0, 0x01, 0x48, 0x89, 0x7D, 0xF8, 0x24, 0x01, 0x0F, 0xB6, 0xC0, 0x5D, 0xC3};
    std::uint8_t const unoptimizedFalse[] = {0x55, 0x48, 0x89, 0xE5, 0x31, 0xC0, 0x88, 0xC1, 0x48, 0x89, 0x7D, 0xF8, 0x80, 0xE1, 0x01, 0x0F, 0xB6, 0xC1, 0x5D, 0xC3};
    std::uint8_t       code[sizeof unoptimizedFalse]{};
    if (!memory::safeRead(reinterpret_cast<void const*>(fn), code, sizeof code)) {
        return -1;
    }
    if (std::memcmp(code, unoptimizedTrue, sizeof unoptimizedTrue) == 0) {
        return 1;
    }
    if (std::memcmp(code, unoptimizedFalse, sizeof unoptimizedFalse) == 0) {
        return 0;
    }
    return -1;
}

template <class Range, class Value>
bool contains(Range const& range, Value const& value) {
    return std::ranges::find(range, value) != std::ranges::end(range);
}

struct Known {
    std::vector<std::string_view> yes, no;
};

// the one virtual slot whose constant answers are those `known` gives, -1 when none or more than one is
int constantSlot(EnchantVector const& v, std::vector<std::string> const& names, Known const& known) {
    int found = -1;
    for (int slot = 1; slot < 32; ++slot) {
        bool fits = true;
        for (std::size_t i = 0; i < v.count && fits; ++i) {
            int const  answer = constantBool(memory::virtualAt(v.at(i), slot));
            bool const yes = contains(known.yes, names[i]), no = contains(known.no, names[i]);
            fits = answer >= 0 && (!yes || answer == 1) && (!no || answer == 0);
        }
        if (!fits) {
            continue;
        }
        if (found >= 0) {
            return -1;
        }
        found = slot;
    }
    return found;
}

}

Enchants enchants() {
    auto const found = findEnchantVector();
    if (!found) {
        return {{}, "no Enchant::mEnchants"};
    }
    EnchantVector const&     v = *found;
    std::vector<std::string> names;
    for (std::size_t i = 0; i < v.count; ++i) {
        if (memory::read<std::uint8_t>(v.at(i), TypeAt) != i) {
            return {{}, std::format("enchant {} has type {}", i, memory::read<std::uint8_t>(v.at(i), TypeAt))};
        }
        names.push_back(readHashedString(v.at(i) + v.nameAt));
    }
    auto byName = [&](std::string_view name) {
        auto at = std::ranges::find(names, name);
        return at == names.end() ? std::uintptr_t{0} : v.at(static_cast<std::size_t>(at - names.begin()));
    };
    auto typeOf     = [&](std::string_view name) { return static_cast<int>(std::ranges::find(names, name) - names.begin()); };
    auto minCost    = [](std::uintptr_t e, int level) { return callIntOfInt(memory::virtualAt(e, MinCostSlot), e, level); };
    auto maxCost    = [](std::uintptr_t e, int level) { return callIntOfInt(memory::virtualAt(e, MaxCostSlot), e, level); };
    auto minLevel   = [](std::uintptr_t e) { return callInt(memory::virtualAt(e, MinLevelSlot), e); };
    auto maxLevel   = [](std::uintptr_t e) { return callInt(memory::virtualAt(e, MaxLevelSlot), e); };
    auto compatible = [&](std::string_view a, std::string_view b) { return callCompatible(memory::virtualAt(byName(a), CompatibleSlot), byName(a), typeOf(b)); };

    std::uintptr_t const protection = byName("protection"), silkTouch = byName("silk_touch"), soulSpeed = byName("soul_speed");
    if (!soulSpeed || !byName("mending") || !byName("binding") || !byName("vanishing") || !byName("frost_walker") || !byName("riptide")) {
        return {{}, "a known enchantment is missing"};
    }
    if (memory::read<std::int32_t>(protection, FrequencyAt) != 30 || memory::read<std::int32_t>(silkTouch, FrequencyAt) != 1 ||
        memory::read<std::int32_t>(protection, PrimaryAt) != 0xF || memory::read<std::int32_t>(silkTouch, PrimaryAt) != 0xE40 ||
        memory::read<std::int32_t>(silkTouch, SecondaryAt) != 0x80 || memory::read<std::uint8_t>(protection, TradeableAt) != 1 ||
        memory::read<std::uint8_t>(soulSpeed, TradeableAt) != 0) {
        return {{}, "the Enchant fields do not read protection's, silk touch's and soul speed's known values"};
    }
    if (maxLevel(protection) != 4 || maxLevel(byName("sharpness")) != 5 || minLevel(protection) != 1 || minCost(protection, 1) != 1 || minCost(protection, 4) != 34 ||
        maxCost(protection, 1) != 21 || maxCost(protection, 4) != 54) {
        return {{}, "the Enchant virtuals do not give protection's known levels and costs"};
    }
    if (compatible("protection", "fire_protection") != 0 || compatible("sharpness", "smite") != 0 || compatible("riptide", "channeling") != 0 ||
        compatible("channeling", "riptide") != 0 || compatible("silk_touch", "fortune") != 0 || compatible("protection", "unbreaking") != 1 ||
        compatible("mending", "unbreaking") != 1 || compatible("riptide", "impaling") != 1) {
        return {{}, "Enchant::isCompatibleWith does not give the known answers"};
    }

    std::int64_t descriptionAt = -1;
    for (std::int64_t off = 16; off <= 0x100 && descriptionAt < 0; off += 8) {
        if (memory::readString(protection + off) == "enchantment.protect.all") {
            descriptionAt = off;
        }
    }
    if (descriptionAt < 0) {
        return {{}, "no description id reads enchantment.protect.all for protection"};
    }

    int const treasureSlot =
        constantSlot(v, names, {{"frost_walker", "mending", "binding", "vanishing", "soul_speed"}, {"protection", "sharpness", "efficiency", "unbreaking", "power"}});
    int const discoverableSlot = constantSlot(v, names, {{"protection", "sharpness", "mending", "binding"}, {"soul_speed"}});
    if (treasureSlot < 0 || discoverableSlot < 0) {
        return {{}, std::format("isTreasureOnly at slot {}, isDiscoverable at {}: not one slot each", treasureSlot, discoverableSlot)};
    }
    log::info("enchantments: {} at +0x{:X}, name at +{}, description id at +{}, isTreasureOnly slot {}, isDiscoverable slot {}", v.count, v.begin, v.nameAt, descriptionAt,
              treasureSlot, discoverableSlot);

    Enchants out;
    for (std::size_t i = 0; i < v.count; ++i) {
        std::uintptr_t const e = v.at(i);
        Enchant              enchant{static_cast<int>(i),
                        names[i],
                        memory::readString(e + descriptionAt),
                        memory::read<std::int32_t>(e, FrequencyAt),
                        memory::read<std::uint8_t>(e, TradeableAt) != 0,
                        memory::read<std::int32_t>(e, PrimaryAt),
                        memory::read<std::int32_t>(e, SecondaryAt),
                        minLevel(e),
                        maxLevel(e),
                        {},
                        {},
                        {},
                        constantBool(memory::virtualAt(e, treasureSlot)) == 1,
                        constantBool(memory::virtualAt(e, discoverableSlot)) == 1};
        if (enchant.minLevel == Faulted || enchant.maxLevel == Faulted) {
            return {{}, std::format("{}: its levels fault", names[i])};
        }
        // a cost is a line in the level: at least two levels say which
        for (int level = 1; level <= std::max(enchant.maxLevel, 2); ++level) {
            enchant.minCost.push_back(minCost(e, level));
            enchant.maxCost.push_back(maxCost(e, level));
        }
        if (contains(enchant.minCost, Faulted) || contains(enchant.maxCost, Faulted)) {
            return {{}, std::format("{}: its costs fault", names[i])};
        }
        for (std::size_t other = 0; other < v.count; ++other) {
            if (other == i) {
                continue;
            }
            int const answer = callCompatible(memory::virtualAt(e, CompatibleSlot), e, static_cast<int>(other));
            if (answer < 0) {
                return {{}, std::format("{}: isCompatibleWith({}) faults", names[i], names[other])};
            }
            if (!answer) {
                enchant.incompatible.push_back(static_cast<int>(other));
            }
        }
        out.enchants.push_back(std::move(enchant));
    }
    return out;
}

}
