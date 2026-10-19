#include "game/Items.h"

#include "bindings/Bindings.h"
#include "bindings/Names.h"
#include "core/Log.h"
#include "game/Virtuals.h"
#include "memory/Flow.h"
#include "memory/Guard.h"
#include "memory/Image.h"
#include "memory/Mem.h"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <optional>

// Item virtuals no binding names, found by their code, not by calling candidates: a wrong slot called with the wrong
// arguments corrupts the heap or the stack, which no handler catches. Item::getEnchantSlot is the slot at which the
// diamond sword's function is `return 16` (WeaponItem: Enchant::Slot::GSword) and a stick's the base Item's
// `return 0`; Item::isValidRepairItem is the virtual declared right before it. Each is called only once its slot is
// found, checked first on items whose answers are known. They run on the server's own thread (Agent.cpp): the torso
// armor's answer reads thread-local data. Item::buildDescriptionName(ItemStackBase const&), where no binding names it,
// is the slot whose base function (a stick's) calls this item's next virtual, Item::buildDescriptionId, which a bed's,
// a banner's and a bucket's function overrides and not it.

namespace bdg::game {

using memory::readPtr;

namespace {

// false when the call faults
bool callInt(std::uintptr_t fn, std::uintptr_t item, int* out) {
    return memory::guard([&] { *out = reinterpret_cast<int (*)(std::uintptr_t)>(fn)(item); });
}

// isValidRepairItem(this, ItemStackBase const& source, ItemStackBase const& repair, BaseGameVersion const&): -1 when it faults
int callRepair(std::uintptr_t fn, std::uintptr_t item, void const* source, void const* repair, void const* version) {
    int answer = -1;
    memory::guard([&] { answer = reinterpret_cast<bool (*)(std::uintptr_t, void const*, void const*, void const*)>(fn)(item, source, repair, version) ? 1 : 0; });
    return answer;
}

// the vtable slot of Item::getEnchantSlot (see above), -1 when no slot or several fit
int enchantSlotSlot(std::uintptr_t stick, std::uintptr_t sword) {
    if (int const bound = bindings::current().slot(bindings::names::ItemGetEnchantSlot); bound >= 0) {
        return bound;
    }
    std::vector<int> found;
    for (int slot = 1; slot < 400; ++slot) {
        std::uintptr_t const base = memory::virtualAt(stick, slot);
        if (!memory::inImage(base)) {
            break;
        }
        if (returnsZero(base) && returnsSixteen(memory::virtualAt(sword, slot))) {
            found.push_back(slot);
        }
    }
    if (found.size() != 1) {
        log::info("items: {} slots fit Item::getEnchantSlot", found.size());
    }
    return found.size() == 1 ? found[0] : -1;
}

// buildDescriptionName(this, ItemStackBase const&), its std::string returned through a hidden pointer; none when it faults
std::optional<std::string> callName(std::uintptr_t fn, std::uintptr_t item, void const* stack) {
    alignas(16) std::uint8_t out[64]{};
    bool const               returned = memory::guard([&] {
#ifdef _WIN32
        reinterpret_cast<void* (*)(std::uintptr_t, void*, void const*)>(fn)(item, out, stack);
#else
        reinterpret_cast<void* (*)(void*, std::uintptr_t, void const*)>(fn)(out, item, stack);
#endif
    });
    if (!returned) {
        return std::nullopt;
    }
    return memory::readString(reinterpret_cast<std::uintptr_t>(out));
}

// the vtable slot of Item::buildDescriptionName (see above), -1 when no slot or several fit
int nameSlot(std::uintptr_t stick, std::array<std::uintptr_t, 3> const& overriders) {
    if (int const bound = bindings::current().slot(bindings::names::ItemBuildDescriptionName); bound >= 0) {
        return bound;
    }
    std::vector<int> found;
    for (int slot = 1; slot < 400; ++slot) {
        std::uintptr_t const base = memory::virtualAt(stick, slot), next = memory::virtualAt(stick, slot + 1);
        if (!memory::inImage(base) || !memory::inImage(next)) {
            break;
        }
        bool const overridden = std::ranges::all_of(overriders, [&](std::uintptr_t it) { return memory::virtualAt(it, slot) == base && memory::virtualAt(it, slot + 1) != next; });
        if (!overridden) {
            continue;
        }
        auto const [begin, end] = memory::functionAround(base);
        bool const calls        = begin == base && end - begin <= 0x1000 && memory::callsVirtualOfThisReturning(begin, end, {slot + 1});
        log::info("items: slot {} is the bed's, banner's and bucket's, slot {} theirs alone; its function (rva 0x{:X}, 0x{:X} bytes) {} it", slot, slot + 1,
                  base - memory::imageBase(), end - begin, calls ? "calls" : "does not call");
        if (calls) {
            found.push_back(slot);
        }
    }
    if (found.size() != 1) {
        log::info("items: {} slots fit Item::buildDescriptionName", found.size());
    }
    return found.size() == 1 ? found[0] : -1;
}

std::map<std::string, std::uintptr_t> byName(ItemTypes const& items) {
    std::map<std::string, std::uintptr_t> out;
    for (ItemType const& t : items.types) {
        out.emplace(t.name, t.address);
    }
    return out;
}

// Each item's enchant slots; empty when the call is not found or faults (`why` says which).
std::map<std::string, int> enchantSlots(ItemTypes const& items, std::string& why) {
    auto const items_ = byName(items);
    auto       item   = [&](char const* name) -> std::uintptr_t {
        auto it = items_.find(name);
        return it == items_.end() ? 0 : it->second;
    };
    // Enchant::Slot bits the game is known to give
    std::pair<char const*, int> const known[] = {
        {"minecraft:stick", 0},
        {"minecraft:apple", 0},
        {"minecraft:diamond_helmet", 1},
        {"minecraft:diamond_chestplate", 2},
        {"minecraft:diamond_boots", 4},
        {"minecraft:diamond_leggings", 8},
        {"minecraft:diamond_sword", 16},
        {"minecraft:bow", 32},
        {"minecraft:diamond_hoe", 64},
        {"minecraft:shears", 128},
        {"minecraft:flint_and_steel", 256},
        {"minecraft:diamond_axe", 512},
        {"minecraft:diamond_pickaxe", 1024},
        {"minecraft:diamond_shovel", 2048},
        {"minecraft:fishing_rod", 4096},
        {"minecraft:carrot_on_a_stick", 8192},
        {"minecraft:elytra", 16384},
        {"minecraft:trident", 32768},
        {"minecraft:crossbow", 65536},
    };
    int const slot = enchantSlotSlot(item("minecraft:stick"), item("minecraft:diamond_sword"));
    if (slot < 0) {
        why = "no slot fits Item::getEnchantSlot";
        return {};
    }
    for (auto [name, bits] : known) {
        std::uintptr_t const it  = item(name);
        int                  got = 0;
        if (it && (!callInt(memory::virtualAt(it, slot), it, &got) || got != bits)) {
            why = std::format("Item::getEnchantSlot (slot {}) gives {} {}, not {}", slot, name, got, bits);
            return {};
        }
    }
    std::map<std::string, int> out;
    for (ItemType const& t : items.types) {
        if (!callInt(memory::virtualAt(t.address, slot), t.address, &out[t.name])) {
            why = std::format("Item::getEnchantSlot faults on {}", t.name);
            return {};
        }
    }
    log::info("items: getEnchantSlot at slot {}", slot);
    return out;
}

// For each item that wears out, the items it is repaired with; empty with `why` when the call is not found or answers wrong.
std::map<std::string, std::vector<std::string>> repairItems(ItemTypes const& items, std::string& why) {
    auto const items_ = byName(items);
    auto       item   = [&](char const* name) -> std::uintptr_t {
        auto it = items_.find(name);
        return it == items_.end() ? 0 : it->second;
    };
    struct Known {
        char const* item;
        char const* repair;
        int         answer;
    };
    Known const known[] = {
        {"minecraft:diamond_sword", "minecraft:diamond", 1},  {"minecraft:diamond_sword", "minecraft:stick", 0},       {"minecraft:iron_pickaxe", "minecraft:iron_ingot", 1},
        {"minecraft:iron_pickaxe", "minecraft:diamond", 0},   {"minecraft:elytra", "minecraft:phantom_membrane", 1},   {"minecraft:elytra", "minecraft:diamond", 0},
        {"minecraft:diamond_helmet", "minecraft:diamond", 1}, {"minecraft:diamond_helmet", "minecraft:iron_ingot", 0},
    };
    int const enchant = enchantSlotSlot(item("minecraft:stick"), item("minecraft:diamond_sword"));
    if (enchant < 1) {
        why = "no slot fits Item::getEnchantSlot";
        return {};
    }
    int const                slot = enchant - 1;
    StackOfOne               source, repair;
    alignas(16) std::uint8_t version[0x80]{};
    for (Known const& k : known) {
        std::uintptr_t const it = item(k.item), with = item(k.repair);
        if (!it || !with) {
            continue;
        }
        int const got = callRepair(memory::virtualAt(it, slot), it, source.of(it), repair.of(with), version);
        if (got != k.answer) {
            why = std::format("Item::isValidRepairItem (slot {}) gives {} with {} {}, not {}", slot, k.item, k.repair, got, k.answer);
            return {};
        }
    }
    std::map<std::string, std::vector<std::string>> out;
    for (ItemType const& t : items.types) {
        if (t.maxDamage <= 0) {
            continue;
        }
        std::uintptr_t const fn = memory::virtualAt(t.address, slot);
        for (ItemType const& with : items.types) {
            if (callRepair(fn, t.address, source.of(t.address), repair.of(with.address), version) == 1) {
                out[t.name].push_back(with.name);
            }
        }
    }
    log::info("items: isValidRepairItem at slot {}, {} items repaired with something", slot, out.size());
    return out;
}

// Each item's name as the game builds it (ItemAnswers::names); empty with `why` when the call is not found or answers wrong.
std::map<std::string, std::string> names(ItemTypes const& items, std::vector<BlockType> const& blocks, std::string& why) {
    auto const items_ = byName(items);
    auto       item   = [&](char const* name) -> std::uintptr_t {
        auto it = items_.find(name);
        return it == items_.end() ? 0 : it->second;
    };
    std::map<std::string, std::uintptr_t> defaultStates;
    for (BlockType const& b : blocks) {
        defaultStates.emplace(b.name, b.states[b.defaultState]);
    }
    // a block item (an id below 256) holds its block's default state: the block of its name, or of <x> for item.<x>
    auto blockOf = [&](ItemType const& t) -> std::uintptr_t {
        if (t.id >= 256) {
            return 0;
        }
        std::string const name = t.name.starts_with("minecraft:item.") ? "minecraft:" + t.name.substr(15) : t.name;
        auto const        it   = defaultStates.find(name);
        return it == defaultStates.end() ? 0 : it->second;
    };
    int const slot = nameSlot(item("minecraft:stick"), {item("minecraft:bed"), item("minecraft:banner"), item("minecraft:bucket")});
    if (slot < 0) {
        why = "no slot fits Item::buildDescriptionName";
        return {};
    }
    // the name the server's en_US gives, or the key where it has none
    struct Known {
        char const* item;
        char const* name;
        char const* key;
    };
    Known const known[] = {
        {"minecraft:stick", "Stick", "item.stick.name"},       {"minecraft:diamond_sword", "Diamond Sword", "item.diamond_sword.name"},
        {"minecraft:bed", "White Bed", "item.bed.white.name"}, {"minecraft:cod_bucket", "Bucket of Cod", "item.bucketFish.name"},
        {"minecraft:stone", "Stone", "tile.stone.stone.name"},
    };
    StackOfOne stack;
    for (Known const& k : known) {
        auto const it = std::ranges::find(items.types, std::string_view(k.item), &ItemType::name);
        if (it == items.types.end()) {
            continue;
        }
        auto const got = callName(memory::virtualAt(it->address, slot), it->address, stack.of(it->address, blockOf(*it)));
        if (got != k.name && got != k.key) {
            why = std::format("Item::buildDescriptionName (slot {}) gives {} '{}', not '{}'", slot, k.item, got.value_or("(a fault)"), k.name);
            return {};
        }
    }
    std::map<std::string, std::string> out;
    int                                nameless = 0;
    for (ItemType const& t : items.types) {
        auto name = callName(memory::virtualAt(t.address, slot), t.address, stack.of(t.address, blockOf(t)));
        if (!name) {
            auto const fault = memory::lastFault();
            why = std::format("Item::buildDescriptionName (slot {}) faults on {} (exception 0x{:X} at {} 0x{:X})", slot, t.name, fault.code, fault.inImage ? "rva" : "address",
                              fault.rva);
            return {};
        }
        nameless += name->empty();
        out.emplace(t.name, std::move(*name));
    }
    log::info("items: buildDescriptionName at slot {}, {} items it names nothing", slot, nameless);
    return out;
}

}

ItemAnswers itemAnswers(ItemTypes const& items, std::vector<BlockType> const& blocks) {
    ItemAnswers answers;
    answers.enchantSlots = enchantSlots(items, answers.error);
    if (answers.error.empty()) {
        answers.repairItems = repairItems(items, answers.error);
    }
    if (answers.error.empty()) {
        answers.names = names(items, blocks, answers.error);
    }
    return answers;
}
}
