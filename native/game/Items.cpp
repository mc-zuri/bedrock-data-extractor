#include "game/Items.h"

#include "bindings/Bindings.h"
#include "bindings/Names.h"
#include "core/Log.h"
#include "memory/Flow.h"
#include "memory/Guard.h"
#include "memory/Image.h"
#include "memory/Mem.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string_view>
#include <unordered_set>

// 1. The registry is the vector of Items (each entry an Item*, or a SharedPtr counter whose first word is one) whose
//    entries carry a "minecraft:*" name at one offset, among them minecraft:diamond_sword and minecraft:stone:
//    ItemRegistry::mItemRegistry. Older builds keep it in a static (the image's writable data), newer ones on the heap
//    (a bounded breadth-first walk from the Level and the image's data); the longest such vector wins.
// 2. The fields are the one offset each where the known items read their known values: the stack size (stone 64,
//    diamond sword 1, egg / ender pearl / snowball 16), the max damage (diamond sword 1561, bow 384, elytra 432),
//    the description id ("item.diamond_sword") and the runtime id (air -158, stone 1, dirt 3, cobblestone 4 on every
//    build, and no two items alike).
// 3. Some items answer otherwise than their fields (a fishing rod's durability, a bucket's stack size): the max damage
//    is Item::getMaxDamage() and the stack size Item::getMaxStackSize(an empty descriptor), at the slots the Linux
//    symbols name, else at the one slot whose base function (a stick's) reads the field and returns it, once they give
//    the known items' values. An item's stack size function is called only where it calls nothing (the base, a
//    bucket's); a data-driven item's (1.21.93's egg) builds on the descriptor, which an empty one breaks: the field.
// 4. ItemRegistry::mItemAliasLookupMap, beside the vector (or among the statics around it): the old names the game
//    still reads (loot tables name minecraft:fish for cod).
// 5. Item::mTags, a std::vector of ItemTags (HashedStrings: 0x30 bytes with MSVC's STL, 0x28 libc++'s): the one offset at which the diamond
//    sword's names minecraft:is_sword and minecraft:diamond_tier, and neither stone's nor the bow's names
//    minecraft:is_sword (both valid vectors, empty or not).
// 6. ItemRegistry::mComplexAliasLookupMap, beside the aliases (or among the statics around the vector): an
//    std::unordered_map<HashedString, ComplexAlias>, the list whose keys include minecraft:coal (the first a recipe
//    names, 1.19.70's torch; minecraft:log only from 1.19.80). Each alias keeps the names it splits into, by data value,
//    in a vector of strings in its node or behind a pointer there: the one place where minecraft:coal's reads coal,
//    charcoal.

namespace bdg::game {

namespace n = bindings::names;
using memory::dataSections;
using memory::readPtr;
using memory::readString;

namespace {

bool isObject(std::uintptr_t p) { return p >= 0x10000 && !(p & 7) && memory::inImage(readPtr(p)); }

// a vector entry: an Item* or a SharedPtr counter pointing at one
std::uintptr_t entryItem(std::uintptr_t entry) {
    if (isObject(entry)) {
        return entry;
    }
    std::uintptr_t inner = readPtr(entry);
    return isObject(inner) ? inner : 0;
}

struct ItemVector {
    std::uintptr_t holder = 0;  // where its begin / end pointers are (the ItemRegistry, or a static)
    std::uintptr_t begin  = 0;
    std::size_t    count = 0, stride = 8;
    std::int64_t   nameOffset = -1;
    std::uintptr_t at(std::size_t i) const { return entryItem(readPtr(begin + i * stride)); }
};

bool isGameName(std::string const& s) { return s.size() > 10 && s.starts_with("minecraft:"); }

// the offset at which (nearly) every sampled item has a "minecraft:*" name, -1 when none
std::int64_t nameOffset(ItemVector const& v) {
    std::size_t const sample = std::min<std::size_t>(v.count, 64);
    for (std::int64_t off = 8; off <= 0x200; off += 8) {
        std::size_t named = 0;
        for (std::size_t i = 0; i < sample; ++i) {
            if (std::uintptr_t it = v.at(i); it && isGameName(readString(it + off))) {
                ++named;
            }
        }
        if (named * 10 >= sample * 9) {
            return off;
        }
    }
    return -1;
}

bool hasItem(ItemVector const& v, std::string_view name) {
    for (std::size_t i = 0; i < v.count; ++i) {
        if (std::uintptr_t it = v.at(i); it && readString(it + v.nameOffset) == name) {
            return true;
        }
    }
    return false;
}

std::optional<ItemVector> vectorAt(std::uintptr_t obj, std::int64_t off) {
    std::uintptr_t const b = readPtr(obj, off), e = readPtr(obj, off + 8);
    if (b < 0x10000 || e <= b) {
        return std::nullopt;
    }
    for (std::size_t stride : {8u, 16u}) {
        if ((e - b) % stride) {
            continue;
        }
        ItemVector v{static_cast<std::uintptr_t>(obj + off), b, (e - b) / stride, stride};
        if (v.count < 400 || v.count > 20000) {
            continue;
        }
        bool items = true;
        for (std::size_t i = 0; i < 8 && items; ++i) {
            items = v.at(i) != 0;
        }
        if (!items) {
            continue;
        }
        v.nameOffset = nameOffset(v);
        if (v.nameOffset >= 0 && hasItem(v, "minecraft:diamond_sword") && hasItem(v, "minecraft:stone")) {
            return v;
        }
    }
    return std::nullopt;
}

std::optional<ItemVector> findItems(std::uintptr_t level) {
    std::optional<ItemVector> best;
    for (auto [from, to] : dataSections()) {
        for (std::uintptr_t at = from; at + 16 <= to; at += 8) {
            if (auto v = vectorAt(at, 0); v && (!best || v->count > best->count)) {
                best = v;
            }
        }
    }
    if (best) {
        return best;
    }
    // the first ring that holds the registry is enough, but every object in it is looked at (the longest wins)
    std::unordered_set<std::uintptr_t> seen{level};
    std::queue<std::uintptr_t>         queue;
    queue.push(level);
    auto enqueue = [&](std::uintptr_t child) {
        std::uint8_t probe;
        if (child >= 0x10000 && !(child & 7) && !memory::inImage(child) && seen.size() < 400000 && seen.insert(child).second &&
            memory::safeRead(reinterpret_cast<void const*>(child), &probe, 1)) {
            queue.push(child);
        }
    };
    for (auto [from, to] : dataSections()) {
        for (std::uintptr_t at = from; at + 8 <= to; at += 8) {
            enqueue(readPtr(at));
        }
    }
    for (int visited = 0; !queue.empty() && visited < 200000; ++visited) {
        std::uintptr_t obj = queue.front();
        queue.pop();
        for (std::int64_t off = 0; off <= 0x400; off += 8) {
            if (auto v = vectorAt(obj, off); v && (!best || v->count > best->count)) {
                best = v;
            }
            enqueue(readPtr(obj, off));
        }
        if (best && best->count >= 900) {
            break;
        }
    }
    return best;
}

// Whether `fn` reads the member at `offset` of `this` (rcx with MSVC, rdi in the Itanium ABI) and returns it: a mov,
// movzx or movsx of it, then ret
bool returnsMember(std::uintptr_t fn, std::int64_t offset) {
    std::uint8_t b[16];
    if (!fn || !memory::safeRead(reinterpret_cast<void const*>(fn), b, sizeof b)) {
        return false;
    }
    std::size_t at = 0;
    if (b[0] == 0x0F && (b[1] == 0xB6 || b[1] == 0xB7 || b[1] == 0xBE || b[1] == 0xBF)) {
        at = 2;
    } else if (b[0] == 0x8A || b[0] == 0x8B) {
        at = 1;
    } else if (b[0] == 0x66 && b[1] == 0x8B) {
        at = 2;
    } else {
        return false;
    }
    std::uint8_t const base = b[at] & 0xC7;
    std::int64_t       disp = 0;
    std::size_t        end  = 0;
    if (base == 0x81 || base == 0x87) {
        std::int32_t d;
        std::memcpy(&d, b + at + 1, 4);
        disp = d;
        end  = at + 5;
    } else if (base == 0x41 || base == 0x47) {
        disp = static_cast<std::int8_t>(b[at + 1]);
        end  = at + 2;
    } else {
        return false;
    }
    return disp == offset && b[end] == 0xC3;
}

// The slot whose base function (the stick's) returns the member at `offset`, `overriddenBy` overrides, and `answers`
// holds of: the bound one, else the only one; -1 when none or several
template <class Answers>
int slotReturning(std::uintptr_t stick, std::uintptr_t overriddenBy, std::int64_t offset, char const* bound, Answers answers) {
    if (int const slot = bindings::current().slot(bound); slot >= 0) {
        return answers(slot) ? slot : -1;
    }
    std::vector<int> found;
    for (int slot = 1; slot < 400; ++slot) {
        std::uintptr_t const fn = memory::virtualAt(stick, slot);
        if (!memory::inImage(fn)) {
            break;
        }
        if (returnsMember(fn, offset) && memory::virtualAt(overriddenBy, slot) != fn && answers(slot)) {
            found.push_back(slot);
        }
    }
    log::info("items: {} slots return the member at +{} and answer", found.size(), offset);
    return found.size() == 1 ? found[0] : -1;
}

// -1 when the call faults
int callInt(std::uintptr_t fn, std::uintptr_t self) {
    int answer = -1;
    memory::guard([&] { answer = reinterpret_cast<int (*)(std::uintptr_t)>(fn)(self); });
    return answer;
}

int callStackSize(std::uintptr_t fn, std::uintptr_t self) {
    alignas(16) std::uint8_t descriptor[128] = {};
    int                      answer          = -1;
    memory::guard([&] { answer = reinterpret_cast<std::uint8_t (*)(std::uintptr_t, void const*)>(fn)(self, descriptor); });
    return answer;
}

// Where a list may start at `at` (memory::hashLayout): { its head, its first node }; with a null-terminated list the head
// is 0, which is also where it ends.
std::pair<std::uintptr_t, std::uintptr_t> listAt(std::uintptr_t at, memory::HashLayout const& layout) {
    if (!layout.throughHead) {
        return {0, readPtr(at)};
    }
    std::uintptr_t const head = readPtr(at);
    if (head < 0x10000 || (head & 7)) {
        return {0, 0};
    }
    return {head, readPtr(head)};
}

// ItemRegistry::mItemAliasLookupMap, an std::unordered_map<HashedString, ItemAlias { HashedString name }>: the list of
// nodes (memory::hashLayout) whose keys are minecraft:* names, among them minecraft:clownfish, and whose values are names
// (without their namespace), among the words between `from` and `to`.
std::vector<std::pair<std::string, std::string>> aliasesNear(std::uintptr_t from, std::uintptr_t to) {
    auto const         layout = memory::hashLayout();
    std::int64_t const keyAt = layout.value + 8, mappedAt = layout.value + static_cast<std::int64_t>(memory::hashedStringSize()) + 8;
    for (std::uintptr_t at = from; at + 8 <= to; at += 8) {
        auto [head, node] = listAt(at, layout);
        if (node < 0x10000 || node == head || !readString(node + keyAt).starts_with("minecraft:")) {
            continue;
        }
        std::vector<std::pair<std::string, std::string>> pairs;
        bool                                             clownfish = false, ok = true;
        for (int i = 0; node && node != head && i < 20000; ++i, node = readPtr(node)) {
            std::string key = readString(node + keyAt), value = readString(node + mappedAt);
            if (!key.starts_with("minecraft:") || value.empty() ||
                !std::all_of(value.begin(), value.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == ':' || c == '.'; })) {
                ok = false;
                break;
            }
            if (value.find(':') == std::string::npos) {
                value = "minecraft:" + value;
            }
            clownfish |= key == "minecraft:clownfish";
            pairs.emplace_back(std::move(key), std::move(value));
        }
        if (ok && clownfish && node == head) {
            return pairs;
        }
    }
    return {};
}

// a vector at `at` of strings (`stride` 8: pointers to HashedStrings; a std::string's size: std::strings; a HashedString's), each
// with its namespace; empty where it is none
std::vector<std::string> stringVector(std::uintptr_t at, std::uintptr_t stride) {
    std::vector<std::string> out;
    std::uintptr_t const     b = readPtr(at), e = readPtr(at, 8);
    if (b < 0x10000 || e <= b || (e - b) % stride || (e - b) / stride > 256) {
        return out;
    }
    for (std::uintptr_t p = b; p < e; p += stride) {
        std::string name = stride == 8 ? memory::readHashedString(readPtr(p)) : stride == memory::hashedStringSize() ? memory::readHashedString(p) : readString(p);
        if (name.empty() || name.size() > 64) {
            return {};
        }
        out.push_back(name.find(':') == std::string::npos ? "minecraft:" + name : name);
    }
    return out;
}

// The complex alias map's std::list of nodes { next, prev, HashedString key (+16), ... }, among the words between
// `from` and `to`: the list of minecraft:* keys among them minecraft:coal, its names where minecraft:coal's are coal
// and charcoal.
std::vector<ComplexAlias> complexAliasesNear(std::uintptr_t from, std::uintptr_t to) {
    std::vector<std::string> const coals  = {"minecraft:coal", "minecraft:charcoal"};
    auto const                     layout = memory::hashLayout();
    std::int64_t const             keyAt = layout.value + 8, mappedAt = layout.value + static_cast<std::int64_t>(memory::hashedStringSize());
    for (std::uintptr_t at = from; at + 8 <= to; at += 8) {
        auto [head, node] = listAt(at, layout);
        if (node < 0x10000 || node == head || !readString(node + keyAt).starts_with("minecraft:")) {
            continue;
        }
        std::vector<std::uintptr_t> nodes;
        std::uintptr_t              coal = 0;
        bool                        ok   = true;
        for (int i = 0; node && node != head && i < 2000; ++i, node = readPtr(node)) {
            std::string const key = readString(node + keyAt);
            if (!key.starts_with("minecraft:")) {
                ok = false;
                break;
            }
            if (key == "minecraft:coal") {
                coal = node;
            }
            nodes.push_back(node);
        }
        if (!ok || node != head || !coal) {
            continue;
        }
        for (std::int64_t off = mappedAt; off <= mappedAt + 0x200; off += 8) {
            for (std::int64_t inner : {-1, 8, 16, 24, 32, 40, 48}) {
                for (std::uintptr_t stride : {std::size_t{8}, memory::stringSize(), memory::hashedStringSize()}) {
                    auto names = [&](std::uintptr_t n) { return inner < 0 ? n + off : readPtr(n, off) + inner; };
                    if (inner >= 0 && !isObject(readPtr(coal, off))) {
                        continue;
                    }
                    if (stringVector(names(coal), stride) != coals) {
                        continue;
                    }
                    log::info("items: complex alias names at +{}{} (stride {:#x})", off, inner < 0 ? "" : std::format(" -> +{}", inner), stride);
                    std::vector<ComplexAlias> out;
                    for (std::uintptr_t n : nodes) {
                        out.push_back({readString(n + keyAt), stringVector(names(n), stride)});
                    }
                    return out;
                }
            }
        }
    }
    return {};
}

// the one offset (step `step`) at which `fits` holds, -1 when none or several (`why` says which)
template <class Fits>
std::int64_t onlyOffset(std::int64_t step, Fits fits, std::string& why, char const* what) {
    std::vector<std::int64_t> found;
    for (std::int64_t off = 8; off <= 0x400; off += step) {
        if (fits(off)) {
            found.push_back(off);
        }
    }
    if (found.size() == 1) {
        return found[0];
    }
    why = std::format("{} offsets fit {}", found.size(), what);
    return -1;
}

}

ItemTypes itemTypes(World const& world) {
    ItemTypes list;
    auto      items = findItems(world.level);
    if (!items) {
        list.error = "no item registry found from the Level";
        return list;
    }
    std::map<std::string, std::uintptr_t> byName;
    for (std::size_t i = 0; i < items->count; ++i) {
        if (std::uintptr_t it = items->at(i)) {
            std::string name = readString(it + items->nameOffset);
            if (isGameName(name)) {
                byName.emplace(name, it);
            }
        }
    }
    log::info("item registry: {} entries (stride {}), {} named, name at +{}", items->count, items->stride, byName.size(), items->nameOffset);

    auto item = [&](char const* name) -> std::uintptr_t {
        auto it = byName.find(name);
        return it == byName.end() ? 0 : it->second;
    };
    std::uintptr_t const air = item("minecraft:air"), stone = item("minecraft:stone"), dirt = item("minecraft:dirt"), cobblestone = item("minecraft:cobblestone"),
                         sword = item("minecraft:diamond_sword"), egg = item("minecraft:egg"), pearl = item("minecraft:ender_pearl"), snowball = item("minecraft:snowball"),
                         bow = item("minecraft:bow"), elytra = item("minecraft:elytra"), rod = item("minecraft:fishing_rod"), bucket = item("minecraft:bucket"),
                         waterBucket = item("minecraft:water_bucket");
    if (!stone || !dirt || !cobblestone || !sword || !egg || !pearl || !snowball || !bow || !elytra || !rod || !bucket || !waterBucket || !item("minecraft:stick")) {
        list.error = "the item registry lacks one of the known items (stone, dirt, cobblestone, diamond_sword, egg, ender_pearl, snowball, bow, elytra, fishing_rod, buckets)";
        return list;
    }
    auto u8  = [](std::uintptr_t it, std::int64_t off) { return memory::read<std::uint8_t>(it, off); };
    auto i16 = [](std::uintptr_t it, std::int64_t off) { return memory::read<std::int16_t>(it, off); };

    std::int64_t const stackOff = onlyOffset(
        1, [&](std::int64_t o) { return u8(stone, o) == 64 && u8(sword, o) == 1 && u8(egg, o) == 16 && u8(pearl, o) == 16 && u8(snowball, o) == 16; }, list.error,
        "a max stack size");
    std::int64_t const damageOff = onlyOffset(
        2, [&](std::int64_t o) { return i16(sword, o) == 1561 && i16(bow, o) == 384 && i16(elytra, o) == 432 && i16(stone, o) == 0 && i16(egg, o) == 0; }, list.error,
        "a max damage");
    std::int64_t const descriptionOff = onlyOffset(
        8, [&](std::int64_t o) { return readString(sword + o) == "item.diamond_sword" && readString(bow + o) == "item.bow"; }, list.error, "a description id");
    std::int64_t const idOff = onlyOffset(
        2,
        [&](std::int64_t o) {
            if (i16(stone, o) != 1 || i16(dirt, o) != 3 || i16(cobblestone, o) != 4 || (air && i16(air, o) != -158)) {
                return false;
            }
            std::set<std::int16_t> ids;
            for (auto const& [name, it] : byName) {
                if (!ids.insert(i16(it, o)).second) {
                    return false;
                }
            }
            return true;
        },
        list.error, "a runtime id");
    // "?": not a vector of tags
    auto tagsAt = [](std::uintptr_t it, std::int64_t off) {
        std::vector<std::string> out;
        std::uintptr_t const     b = readPtr(it, off), e = readPtr(it, off + 8);
        if (b == 0 && e == 0) {
            return out;
        }
        std::size_t const size = memory::hashedStringSize();
        if (b < 0x10000 || e < b || (e - b) % size || (e - b) / size > 64) {
            return std::vector<std::string>{"?"};
        }
        for (std::uintptr_t p = b; p < e; p += size) {
            out.push_back(memory::readHashedString(p));
        }
        return out;
    };
    auto               has     = [](std::vector<std::string> const& tags, char const* tag) { return std::ranges::find(tags, tag) != tags.end(); };
    std::int64_t const tagsOff = onlyOffset(
        8,
        [&](std::int64_t o) {
            auto const sw = tagsAt(sword, o), st = tagsAt(stone, o), bw = tagsAt(bow, o);
            return has(sw, "minecraft:is_sword") && has(sw, "minecraft:diamond_tier") && !has(st, "minecraft:is_sword") && !has(st, "?") && !has(bw, "minecraft:is_sword") &&
                   !has(bw, "?");
        },
        list.error, "the item tags");
    if (stackOff < 0 || damageOff < 0 || descriptionOff < 0 || idOff < 0 || tagsOff < 0) {
        return list;
    }
    log::info("items: stack size at +{}, max damage at +{}, description id at +{}, runtime id at +{}, tags at +{}", stackOff, damageOff, descriptionOff, idOff, tagsOff);

    std::uintptr_t const stick    = item("minecraft:stick");
    auto                 damageAt = [&](int slot, std::uintptr_t it) { return callInt(memory::virtualAt(it, slot), it); };
    auto                 stackAt  = [&](int slot, std::uintptr_t it) {
        std::uintptr_t const fn = memory::virtualAt(it, slot);
        return memory::callsNothing(fn) ? callStackSize(fn, it) : int{u8(it, stackOff)};
    };
    auto damageKnown = [&](int s) { return damageAt(s, sword) == 1561 && damageAt(s, bow) == 384 && damageAt(s, rod) == 384 && damageAt(s, stone) == 0; };
    auto stackKnown = [&](int s) { return stackAt(s, stone) == 64 && stackAt(s, sword) == 1 && stackAt(s, egg) == 16 && stackAt(s, bucket) == 16 && stackAt(s, waterBucket) == 1; };
    // the fishing rod's durability is its own (FishingRodItem), a bucket's stack size by what it holds (BucketItem)
    int const damageSlot = slotReturning(stick, rod, damageOff, n::ItemGetMaxDamage, damageKnown);
    int const stackSlot  = slotReturning(stick, bucket, stackOff, n::ItemGetMaxStackSize, stackKnown);
    auto      damageOf   = [&](std::uintptr_t it) { return damageAt(damageSlot, it); };
    auto      stackOf    = [&](std::uintptr_t it) { return stackAt(stackSlot, it); };
    if (damageSlot < 0) {
        list.error = "no slot gives the known items' durability (Item::getMaxDamage)";
        return list;
    }
    if (stackSlot < 0) {
        list.error = "no slot gives the known items' stack sizes (Item::getMaxStackSize)";
        return list;
    }
    log::info("items: Item::getMaxDamage at slot {}, Item::getMaxStackSize at slot {}", damageSlot, stackSlot);

    for (auto const& [name, it] : byName) {
        std::string description = readString(it + descriptionOff);
        if (!std::all_of(description.begin(), description.end(), [](char c) { return c >= 0x20 && c < 0x7f; })) {
            description.clear();
        }
        int const stackSize = stackOf(it), damage = damageOf(it);
        if (stackSize < 1 || stackSize > 64 || damage < 0) {
            auto const fault = memory::lastFault();
            list.error       = std::format("{}: stack size {} (slot {} at rva 0x{:X}), max damage {} (the last call that faulted: exception 0x{:X} at {} 0x{:X})", name, stackSize,
                                           stackSlot, memory::virtualAt(it, stackSlot) - memory::imageBase(), damage, fault.code, fault.inImage ? "rva" : "address", fault.rva);
            list.types.clear();
            return list;
        }
        std::vector<std::string> tags = tagsAt(it, tagsOff);
        if (!std::ranges::all_of(tags, [](std::string const& t) { return t.find(':') != std::string::npos && t.size() < 128; })) {
            list.error = std::format("{}: a tag that is no name", name);
            list.types.clear();
            return list;
        }
        std::ranges::sort(tags);
        list.types.push_back({name, it, i16(it, idOff), std::move(description), stackSize, damage, std::move(tags)});
    }

    list.aliases = aliasesNear(items->holder - 0x400, items->holder + 0x400);
    if (list.aliases.empty()) {
        for (auto [from, to] : dataSections()) {
            if (items->holder >= from && items->holder < to && (list.aliases = aliasesNear(from, to)).size()) {
                break;
            }
        }
    }
    if (list.aliases.empty()) {
        list.error = "no item alias map (with minecraft:clownfish) found near the item registry";
        list.types.clear();
        return list;
    }
    std::sort(list.aliases.begin(), list.aliases.end());
    list.complexAliases = complexAliasesNear(items->holder - 0x400, items->holder + 0x400);
    if (list.complexAliases.empty()) {
        for (auto [from, to] : dataSections()) {
            if (items->holder >= from && items->holder < to && (list.complexAliases = complexAliasesNear(from, to)).size()) {
                break;
            }
        }
    }
    std::ranges::sort(list.complexAliases, {}, &ComplexAlias::name);
    log::info("items: {} items, {} aliases, {} complex aliases", list.types.size(), list.aliases.size(), list.complexAliases.size());
    return list;
}

}
