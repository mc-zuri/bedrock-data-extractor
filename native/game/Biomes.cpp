#include "game/Biomes.h"

#include "core/Log.h"
#include "memory/Guard.h"
#include "memory/Mem.h"

#include <algorithm>
#include <format>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <string_view>
#include <unordered_set>

// The biome registry keeps its Biomes in a std::vector of (owning) pointers, each Biome naming itself in a std::string or a
// HashedString, or in an unordered_map's node list ({ next, prev, name, ..., Biome* }): the container whose names
// include the Known ones; from 1.19.30, where it is neither, every object of the Biome class. The id is the one offset
// (a u16 or an i32) at which the biomes read the KnownIds.

namespace bdg::game {

using memory::readPtr;
using memory::readString;

namespace {

constexpr std::string_view Known[] = {"plains", "ocean", "desert", "forest", "river"};

// each name's Biomes: one in a container; from the Biome class every object named so, stale copies too
using ByName = std::map<std::string, std::vector<std::uintptr_t>>;

struct Registry {
    std::int64_t nameAt = -1;  // -1: a map's, whose names are its nodes'
    ByName       biomes;
};

std::string bare(std::string s) { return s.starts_with("minecraft:") ? s.substr(10) : s; }

bool isBiomeName(std::string const& s) {
    std::string const b = bare(s);
    return b.size() >= 3 && b.size() <= 48 && std::ranges::all_of(b, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

bool hasKnown(ByName const& byName) {
    return std::ranges::all_of(Known, [&](std::string_view k) { return byName.contains(std::string(k)); });
}

bool isObject(std::uintptr_t p) { return p >= 0x10000 && !(p & 7) && memory::inImage(readPtr(p)); }

// the biomes of a list of objects: the name offset at which all five known names are there, and each named one
std::optional<Registry> biomesOf(std::vector<std::uintptr_t> const& objects) {
    if (objects.size() < 20 || objects.size() > 1000) {
        return std::nullopt;
    }
    for (std::int64_t off = 8; off <= 0x800; off += 8) {
        // most offsets name nothing: the first objects say so before all are read
        if (std::count_if(objects.begin(), objects.begin() + 4, [&](std::uintptr_t o) { return isBiomeName(readString(o + off)); }) < 3) {
            continue;
        }
        ByName byName;
        for (std::uintptr_t o : objects) {
            if (std::string const s = readString(o + off); isBiomeName(s)) {
                byName[bare(s)].push_back(o);
            }
        }
        if (byName.size() * 10 >= objects.size() * 8 && hasKnown(byName)) {
            return Registry{off, std::move(byName)};
        }
    }
    return std::nullopt;
}

// a vector of Biome pointers (or owning pointers: 8 bytes each, or 16 for a shared_ptr) at `at`: the objects it holds
// (from 1.19.30 some of its entries are not Biomes)
std::vector<std::uintptr_t> vectorObjects(std::uintptr_t at, std::uintptr_t stride) {
    std::uintptr_t const b = readPtr(at), e = readPtr(at, 8);
    if (b < 0x10000 || e <= b || (e - b) % stride || (e - b) / stride < 20 || (e - b) / stride > 1000) {
        return {};
    }
    // older builds index the vector by id, with empty slots
    std::vector<std::uintptr_t> out;
    std::size_t                 others = 0, allowed = (e - b) / stride / 5;
    for (std::uintptr_t p = b; p < e; p += stride) {
        std::uintptr_t o = readPtr(p);
        if (!o) {
            continue;
        }
        if (!isObject(o)) {
            o = readPtr(o);
        }
        if (isObject(o)) {
            out.push_back(o);
        } else if (++others > allowed) {
            return {};
        }
    }
    return out;
}

// a list node holding a biome name at `keyAt`: the whole list's names and the Biome each points to
std::optional<Registry> listAt(std::uintptr_t node, std::int64_t keyAt) {
    ByName               byName;
    std::uintptr_t const start = node;
    for (int n = 0; n < 2000; ++n) {
        if (std::string const key = readString(node + keyAt); isBiomeName(key)) {
            // the value: the first pointer after the key to an object with a vtable
            for (std::int64_t off = keyAt + 32; off <= keyAt + 96; off += 8) {
                if (std::uintptr_t const v = readPtr(node, off); isObject(v)) {
                    byName[bare(key)].push_back(v);
                    break;
                }
            }
        }
        node = readPtr(node);
        if (node < 0x10000 || node == start) {
            break;
        }
    }
    if (byName.size() < 20 || !hasKnown(byName)) {
        return std::nullopt;
    }
    return Registry{-1, std::move(byName)};
}

std::optional<Registry> registryAt(std::uintptr_t at) {
    auto found = biomesOf(vectorObjects(at, 8));
    if (!found) {
        found = biomesOf(vectorObjects(at, 16));
    }
    // a map's list node: its name (a std::string, or a HashedString's) near the start
    for (std::int64_t keyAt = 16; !found && keyAt <= 32; keyAt += 8) {
        if (std::string const s = readString(at + keyAt); s == "plains" || s == "minecraft:plains") {
            found = listAt(at, keyAt);
        }
    }
    return found;
}

// Every object the image's data and the Level reach (or the Level alone: `fromData` false), breadth first, each asked
// `visit` until it answers true.
template <class Visit>
void walk(std::uintptr_t level, Visit visit, bool fromData = true) {
    std::unordered_set<std::uintptr_t> seen{level};
    std::queue<std::uintptr_t>         queue;
    auto                               enqueue = [&](std::uintptr_t child) {
        std::uint8_t probe;
        if (child >= 0x10000 && !(child & 7) && !memory::inImage(child) && seen.size() < 400000 && seen.insert(child).second &&
            memory::safeRead(reinterpret_cast<void const*>(child), &probe, 1)) {
            queue.push(child);
        }
    };
    queue.push(level);
    if (fromData) {
        for (auto [from, to] : memory::dataSections()) {
            for (std::uintptr_t at = from; at + 8 <= to; at += 8) {
                enqueue(readPtr(at));
            }
        }
    }
    for (int visited = 0; !queue.empty() && visited < 200000; ++visited) {
        std::uintptr_t const object = queue.front();
        queue.pop();
        if (visit(object)) {
            return;
        }
        for (std::int64_t off = 0; off <= 0x400; off += 8) {
            enqueue(readPtr(object, off));
        }
    }
}

std::optional<Registry> registryInData() {
    for (auto [from, to] : memory::dataSections()) {
        for (std::uintptr_t at = from; at + 16 <= to; at += 8) {
            if (auto found = registryAt(at)) {
                return found;
            }
        }
    }
    return std::nullopt;
}

// the first container reached that `accepts`: a container of objects named like biomes may hold copies, or other
// objects named after biomes, whose ids do not read; after a few such the search gives up (a walk is long)
constexpr int MaxRejected = 16;
template <class Accept>
std::optional<Registry> registryReached(std::uintptr_t level, Accept accepts, bool fromData = true) {
    std::optional<Registry> found;
    int                     rejected = 0;
    walk(
        level,
        [&](std::uintptr_t object) {
            for (std::int64_t off = 0; off <= 0x400 && !found && rejected < MaxRejected; off += 8) {
                auto candidate = registryAt(object + off);
                if (!candidate) {
                    continue;
                }
                if (accepts(*candidate)) {
                    found = std::move(candidate);
                } else {
                    ++rejected;
                }
            }
            return found.has_value() || rejected >= MaxRejected;
        },
        fromData);
    return found;
}

// the words in [from, to) equal to `value`, at most `cap` of them into `out`; their count, -1 if the range faults
std::int64_t wordsEqual(std::uintptr_t from, std::uintptr_t to, std::uintptr_t value, std::uintptr_t* out, std::size_t cap) {
    std::size_t n    = 0;
    bool const  read = memory::guard([&] {
        for (std::uintptr_t at = from; at + 8 <= to; at += 8) {
            if (*reinterpret_cast<std::uintptr_t const volatile*>(at) == value && n < cap) {
                out[n++] = at;
            }
        }
    });
    return read ? static_cast<std::int64_t>(n) : -1;
}

// every object in the process's committed read-write memory whose first word is `vtable`
std::vector<std::uintptr_t> objectsOfClass(std::uintptr_t vtable) {
    std::vector<std::uintptr_t> out;
    std::uintptr_t              found[4096];
    for (auto const& [from, to] : memory::readWriteRegions()) {
        if (std::int64_t const n = wordsEqual(from, to, vtable, found, std::size(found)); n > 0) {
            out.insert(out.end(), found, found + n);
        }
    }
    return out;
}

// From 1.19.30 the registry keeps its Biomes in containers the search above does not read. Every Biome is still an
// object of one class, named at +16 (a HashedString at +8): the class of an object reached that names plains there,
// then every object of that class in the process's memory (a walk reaches only some of them).
std::optional<Registry> biomesByClass(std::uintptr_t level) {
    std::set<std::uintptr_t> tried;
    std::optional<Registry>  found;
    walk(level, [&](std::uintptr_t object) {
        if (!isObject(object) || readString(object + 16) != "plains" || !tried.insert(readPtr(object)).second) {
            return false;
        }
        ByName byName;
        for (std::uintptr_t biome : objectsOfClass(readPtr(object))) {
            if (std::string const name = readString(biome + 16); isBiomeName(name)) {
                byName[bare(name)].push_back(biome);
            }
        }
        if (byName.size() >= 20 && hasKnown(byName)) {
            found = Registry{16, std::move(byName)};
        }
        return found.has_value();
    });
    return found;
}

// the ids Bedrock has given these biomes since they came (a biome a build lacks is not asked for, but every build has
// those up to 9), and two of the mutated ones, from 128: an index of the registry fits the low ids too
constexpr std::pair<std::string_view, int> KnownIds[] = {{"ocean", 0},
                                                         {"plains", 1},
                                                         {"desert", 2},
                                                         {"forest", 4},
                                                         {"river", 7},
                                                         {"hell", 8},
                                                         {"the_end", 9},
                                                         {"extreme_hills", 3},
                                                         {"taiga", 5},
                                                         {"swampland", 6},
                                                         {"legacy_frozen_ocean", 10},
                                                         {"frozen_ocean", 46},
                                                         {"frozen_river", 11},
                                                         {"ice_plains", 12},
                                                         {"mushroom_island", 14},
                                                         {"beach", 16},
                                                         {"jungle", 21},
                                                         {"birch_forest", 27},
                                                         {"roofed_forest", 29},
                                                         {"mega_taiga", 32},
                                                         {"savanna", 35},
                                                         {"mesa", 37},
                                                         {"flower_forest", 132},
                                                         {"ice_plains_spikes", 140}};

struct IdField {
    std::int64_t at;
    int          width;  // 2: a u16, 4: an i32
    int          read(std::uintptr_t biome) const { return width == 2 ? static_cast<int>(memory::read<std::uint16_t>(biome, at)) : memory::read<std::int32_t>(biome, at); }
    bool         operator==(IdField const&) const = default;
};

bool isId(int value) { return value >= 0 && value <= 1023; }

bool fitsKnownIds(ByName const& byName, IdField field) {
    return std::ranges::all_of(KnownIds, [&](auto const& known) {
        auto const it = byName.find(std::string(known.first));
        return it == byName.end() ? known.second > 9 : std::ranges::any_of(it->second, [&](std::uintptr_t b) { return field.read(b) == known.second; });
    });
}

// the Biomes' ids read by `id`: nullopt, with `why`, where a Biome reads none, or several, or another's
std::optional<Biomes> idsBy(ByName const& byName, IdField id, std::string& why) {
    Biomes                  out;
    std::unordered_set<int> ids;
    for (auto const& [name, candidates] : byName) {
        // a stale copy reads no id, or the live one's; a read of a Biome in the middle of a change gives another
        std::set<int> read;
        for (std::uintptr_t b : candidates) {
            if (isId(id.read(b))) {
                read.insert(id.read(b));
            }
        }
        if (read.size() != 1) {
            why = std::format("biome {}: {} ids among its {} objects", name, read.size(), candidates.size());
            return std::nullopt;
        }
        int const value = *read.begin();
        if (!ids.insert(value).second) {
            why = std::format("biome {} reads id {}, another biome's", name, value);
            return std::nullopt;
        }
        std::vector<std::uintptr_t> objects;
        for (std::uintptr_t b : candidates) {
            if (id.read(b) == value) {
                objects.push_back(b);
            }
        }
        out.biomes.push_back({name, value, std::move(objects)});
    }
    return out;
}

// the registry's Biomes by the one offset whose values are the ids the game is known to have given, each its own;
// nullopt, with `why`, where they do not read so
std::optional<Biomes> idsOf(Registry const& registry, std::string& why) {
    auto const& [nameAt, byName] = registry;
    std::vector<IdField> fields;
    for (std::int64_t off = 8; off <= 0x800; off += 2) {
        if (off >= nameAt && off < nameAt + 32) {
            continue;
        }
        if (off % 4 == 0 && fitsKnownIds(byName, {off, 4})) {
            fields.push_back({off, 4});
        }
        if (fitsKnownIds(byName, {off, 2})) {
            fields.push_back({off, 2});
        }
    }
    std::vector<std::int64_t> offsets;
    for (IdField f : fields) {
        if (std::ranges::find(offsets, f.at) == offsets.end()) {
            offsets.push_back(f.at);
        }
    }
    // the id kept twice (older builds): the same for every biome, so one field for this
    auto const sameAsFirst = [&](std::int64_t at) {
        IdField const first{offsets[0], 4}, other{at, 4};
        return std::ranges::all_of(
            byName, [&](auto const& biome) { return std::ranges::all_of(biome.second, [&](std::uintptr_t b) { return !isId(first.read(b)) || other.read(b) == first.read(b); }); });
    };
    if (offsets.size() > 1 && std::all_of(offsets.begin() + 1, offsets.end(), sameAsFirst)) {
        offsets.resize(1);
    }
    if (offsets.size() != 1) {
        why = std::format("{} offsets fit a biome id", offsets.size());
        return std::nullopt;
    }
    // an i32 first (a stale copy's reads out of range), then the u16 at its start where the i32 does not read (from
    // 1.26.30 the id is a u16 whose next two bytes are sometimes left as they were)
    for (IdField f : fields) {
        if (f.at != offsets[0]) {
            continue;
        }
        if (auto found = idsBy(byName, f, why)) {
            log::info("biomes: {} by name at +{}, id at +{} ({} bytes)", byName.size(), nameAt, f.at, f.width);
            return found;
        }
    }
    return std::nullopt;
}

}

Biomes biomes(World const& world) {
    // cheapest first; a route whose Biomes do not read their ids gives way to the next. Last, a container the Level alone
    // reaches: where the image's data holds so many pointers that the walk ends before it reaches the Level's own (Linux
    // 1.16.201, built unoptimized)
    std::string why = "no biome registry";
    for (int route = 0; route < 4; ++route) {
        std::optional<Biomes> found;
        auto                  accepts = [&](Registry const& candidate) { return (found = idsOf(candidate, why)).has_value(); };
        char const* const     names[] = {"a container in the image's data", "the Biome class", "a container reached", "a container the Level reaches"};
        if (route == 0) {
            if (auto registry = registryInData()) {
                accepts(*registry);
            }
        } else if (route == 1) {
            if (auto registry = biomesByClass(world.level)) {
                accepts(*registry);
            }
        } else if (route == 2) {
            registryReached(world.level, accepts);
        } else {
            registryReached(world.level, accepts, false);
        }
        if (found) {
            log::info("biomes: found by {}", names[route]);
            return *found;
        }
    }
    return {{}, why};
}

}
