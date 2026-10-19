#include "game/Dig.h"

#include "bindings/Bindings.h"
#include "bindings/Names.h"
#include "core/Log.h"
#include "game/Registry.h"
#include "game/Virtuals.h"
#include "memory/Guard.h"
#include "memory/Mem.h"

#include <cmath>
#include <format>
#include <map>

// The two answers are virtuals of the Item that ItemStack::getDestroySpeed and ItemStackBase::canDestroySpecial call:
//     float Item::getDestroySpeed(ItemStackBase const&, Block const&) const   (1 for most items)
//     bool  Item::canDestroySpecial(Block const&) const                       (false for most)
// Every override (DiggerItem: its tier's speed on the blocks it digs; WeaponItem, ShearsItem, a ComponentItem's
// minecraft:digger) reads the stack only for Item::destroySpeedBonus (efficiency), which a stack without enchantments
// leaves out. The slots move with every build and later builds have no symbols: each is the slot where a stick has the
// base Item's stub (return 1.0f / return false) and the tools each their own function, and whose calls give the answers
// the game is known for. A call at a wrong slot can end the process, so only slots whose code looks right are called.

namespace bdg::game {

namespace {

// false when the call faults
bool callSpeed(std::uintptr_t fn, std::uintptr_t item, void const* stack, std::uintptr_t state, float* out) {
    return memory::guard([&] { *out = reinterpret_cast<float (*)(std::uintptr_t, void const*, std::uintptr_t)>(fn)(item, stack, state); });
}

bool callSpecial(std::uintptr_t fn, std::uintptr_t item, std::uintptr_t state, bool* out) {
    return memory::guard([&] { *out = reinterpret_cast<bool (*)(std::uintptr_t, std::uintptr_t)>(fn)(item, state); });
}

struct Slots {
    int speed = -1, special = -1;
};

class Finder {
public:
    Finder(ItemTypes const& items, std::vector<BlockType> const& types) {
        for (ItemType const& t : items.types) {
            mItems.emplace(t.name, t.address);
        }
        for (BlockType const& t : types) {
            mBlocks.emplace(t.name, defaultState(t.address));
        }
    }

    Slots slots() {
        std::uintptr_t const stick = item("minecraft:stick"), woodenPickaxe = item("minecraft:wooden_pickaxe"), pickaxe = item("minecraft:diamond_pickaxe"),
                             axe = item("minecraft:diamond_axe"), sword = item("minecraft:diamond_sword"), shears = item("minecraft:shears");
        std::uintptr_t const stone = block("minecraft:stone"), obsidian = block("minecraft:obsidian"), web = block("minecraft:web"),
                             wool = block("minecraft:white_wool", "minecraft:wool"), log = block("minecraft:oak_log", "minecraft:log");
        if (!stick || !woodenPickaxe || !pickaxe || !axe || !sword || !shears || !stone || !obsidian || !web || !wool || !log) {
            return {};
        }
        auto fn = [](std::uintptr_t it, int slot) { return memory::virtualAt(it, slot); };
        // the stick's and each tool's function, all different: PickaxeItem, WeaponItem and ShearsItem each override
        // getDestroySpeed, PickaxeItem and WeaponItem canDestroySpecial (ShearsItem may share WeaponItem's)
        auto distinct = [&](int slot, std::initializer_list<std::uintptr_t> tools) {
            std::vector<std::uintptr_t> fns{fn(stick, slot)};
            for (std::uintptr_t t : tools) {
                fns.push_back(fn(t, slot));
            }
            for (std::size_t i = 0; i < fns.size(); ++i) {
                for (std::size_t j = i + 1; j < fns.size(); ++j) {
                    if (!fns[i] || fns[i] == fns[j]) {
                        return false;
                    }
                }
            }
            return true;
        };
        auto speed = [&](int slot, std::uintptr_t it, std::uintptr_t state) {
            float v = NAN;
            return callSpeed(fn(it, slot), it, mStack.of(it), state, &v) ? v : NAN;
        };
        auto special = [&](int slot, std::uintptr_t it, std::uintptr_t state) {
            bool v = false;
            return callSpecial(fn(it, slot), it, state, &v) ? int{v} : -1;
        };
        // where the bindings name a slot (Linux), it is the only one tried
        auto const& r          = bindings::current();
        int const   boundSpeed = r.slot(bindings::names::ItemGetDestroySpeed), boundSpecial = r.slot(bindings::names::ItemCanDestroySpecial);
        auto        speedLooks   = [&](int slot, std::uintptr_t base) { return boundSpeed >= 0 ? slot == boundSpeed : returnsOneFloat(base); };
        auto        specialLooks = [&](int slot, std::uintptr_t base) { return boundSpecial >= 0 ? slot == boundSpecial : returnsZero(base); };
        Slots       found;
        for (int slot = 1; slot < 400; ++slot) {
            std::uintptr_t const base = fn(stick, slot);
            if (!memory::inImage(base)) {
                break;
            }
            if (found.speed < 0 && speedLooks(slot, base) && distinct(slot, {pickaxe, sword, shears}) && speed(slot, pickaxe, stone) == 8.0f &&
                speed(slot, woodenPickaxe, stone) == 2.0f && speed(slot, stick, stone) == 1.0f && speed(slot, shears, wool) == 5.0f && speed(slot, sword, web) == 15.0f &&
                speed(slot, axe, log) == 8.0f) {
                found.speed = slot;
            }
            if (found.special < 0 && specialLooks(slot, base) && distinct(slot, {pickaxe, sword}) && fn(shears, slot) != base && special(slot, pickaxe, obsidian) == 1 &&
                special(slot, woodenPickaxe, obsidian) == 0 && special(slot, woodenPickaxe, stone) == 1 && special(slot, stick, stone) == 0 && special(slot, shears, web) == 1) {
                found.special = slot;
            }
        }
        return found;
    }

private:
    std::uintptr_t item(char const* name) const {
        auto it = mItems.find(name);
        return it == mItems.end() ? 0 : it->second;
    }
    std::uintptr_t block(char const* name, char const* oldName = nullptr) const {
        for (char const* n : {name, oldName}) {
            if (auto it = n ? mBlocks.find(n) : mBlocks.end(); it != mBlocks.end()) {
                return it->second;
            }
        }
        return 0;
    }

    std::map<std::string, std::uintptr_t> mItems, mBlocks;  // by name: Item*, the default state's Block*
    StackOfOne                            mStack;
};

// an item whose answers are its own: the functions at the slots where they are not the base Item's
struct Digger {
    std::string const* name;
    std::uintptr_t     item, speed, special;  // 0 where the item has the base Item's
};

}

Dig dig(ItemTypes const& items, std::vector<BlockType> const& types) {
    Dig         dig;
    Slots const slots = Finder(items, types).slots();
    if (slots.speed < 0 || slots.special < 0) {
        dig.error = std::format("no slot answers as Item::getDestroySpeed ({}) or Item::canDestroySpecial ({})", slots.speed, slots.special);
        return dig;
    }
    std::vector<Digger> diggers;
    std::uintptr_t      baseSpeed = 0, baseSpecial = 0;
    for (ItemType const& t : items.types) {
        if (t.name == "minecraft:stick") {
            baseSpeed = memory::virtualAt(t.address, slots.speed), baseSpecial = memory::virtualAt(t.address, slots.special);
        }
    }
    for (ItemType const& t : items.types) {
        std::uintptr_t const speed = memory::virtualAt(t.address, slots.speed), special = memory::virtualAt(t.address, slots.special);
        if (speed != baseSpeed || special != baseSpecial) {
            diggers.push_back({&t.name, t.address, speed != baseSpeed ? speed : 0, special != baseSpecial ? special : 0});
        }
    }
    log::info("dig: Item::getDestroySpeed at slot {}, Item::canDestroySpecial at slot {}, {} items answer for themselves", slots.speed, slots.special, diggers.size());

    StackOfOne  stack;
    std::size_t calls = 0;
    for (BlockType const& type : types) {
        auto& states = dig.types.emplace_back();
        for (std::uintptr_t state : type.states) {
            DigAnswer& answer = states.emplace_back();
            for (Digger const& d : diggers) {
                float      speed   = 1;
                bool       special = false;
                bool const ok      = (!d.speed || callSpeed(d.speed, d.item, stack.of(d.item), state, &speed)) && (!d.special || callSpecial(d.special, d.item, state, &special));
                calls += (d.speed ? 1 : 0) + (d.special ? 1 : 0);
                if (!ok || !std::isfinite(speed) || speed < 0) {
                    dig.error = std::format("{} on {}: {}", *d.name, type.name, ok ? std::format("speed {}", speed) : "the call faults");
                    dig.types.clear();
                    return dig;
                }
                if (speed != 1.0f) {
                    answer.speeds.emplace_back(*d.name, speed);
                }
                if (special) {
                    answer.harvest.push_back(*d.name);
                }
            }
        }
    }
    log::info("dig: {} block types, {} calls", types.size(), calls);
    return dig;
}

}
