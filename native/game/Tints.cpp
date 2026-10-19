#include "game/Tints.h"

#include "core/Log.h"
#include "memory/Code.h"
#ifndef _WIN32
#include "memory/Elf.h"
#endif
#include "memory/Guard.h"
#include "memory/Mem.h"
#include "memory/Scanner.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

#ifndef _WIN32
#include <unistd.h>

#include <climits>
#endif

// Biome::getMapGrassColor is the one function that holds the swamp's darker grass, 0x4C763C (its noise picks that or
// 0x6A7039), in every build. What else tints is found from it:
// - to 1.21.50 ("biome"), Biome::getMapFoliageColor is the function before it on Windows; on Linux the one before
//   Biome::getMapBirchFoliageColor (getters and thunks between passed over), the first of the functions just before the
//   grass one that go to a function returning a constant (the birch's, then the evergreen's). The water's map colour is a
//   Color member of the Biome (setMapWaterColor's: each channel a byte times 1/255);
// - from 1.21.60 ("samplers"), one function makes a sampler for each tint method, taking their functions' addresses in
//   the methods' order: the default foliage, the birch, the evergreen (constants), the dry foliage's (a constant, from
//   1.21.100), the grass (the grass function, or a thunk to it), the water (a member of the Biome). Its callers read a
//   block's tint method as a byte of what one function returns for the Block.
// Each takes the Biome and a BlockPos, which only the swamp's noise reads (at the origin it gives 0x6A7039).
// The blocks tinted with one colour are those whose tint method's sampler returns a constant (samplers), or whose
// virtuals go to a Biome function near the grass one that returns a constant, and to no other (biome: the birch and the
// spruce leaves, from 1.20.71, where they are block types of their own).
// The redstone wire's colour is its BlockLegacy::getColor(Block const&): of the two getColor slots whose base returns
// 0xFFFFFFFF, the one the wire overrides taking the Block (MSVC orders the overloads the other way round from the
// Itanium ABI).

namespace bdg::game {

using memory::Function;
using memory::readPtr;

namespace {

struct BlockPos {
    int x, y, z;
};
BlockPos const Origin{0, 0, 0};

using BiomeColor = int (*)(std::uintptr_t biome, BlockPos const* pos);
using Constant   = int (*)();
// getColor(Block const&), called as the other overload too would read it: (this, BlockSource&, BlockPos const&, Block const&)
using BlockColor = int (*)(std::uintptr_t legacy, std::uintptr_t block, BlockPos const* pos, std::uintptr_t block2);
using BlockFn    = std::uintptr_t (*)(std::uintptr_t block);

std::optional<int> callBiome(std::uintptr_t fn, std::uintptr_t biome) {
    int        out = 0;
    bool const ok  = memory::guard([&] { out = reinterpret_cast<BiomeColor>(fn)(biome, &Origin); });
    return ok ? std::optional<int>(out) : std::nullopt;
}

std::optional<int> callConstant(std::uintptr_t fn) {
    int        out = 0;
    bool const ok  = memory::guard([&] { out = reinterpret_cast<Constant>(fn)(); });
    return ok ? std::optional<int>(out) : std::nullopt;
}

struct Found {
    std::uintptr_t grass = 0, foliage = 0;
    std::uintptr_t water = 0;  // samplers: its function; biome: none, a member
    // samplers: their functions by tint method from 1 (the default foliage); and where a block's method is read
    std::vector<std::uintptr_t> samplers;
    std::uintptr_t              methodSource = 0;
    std::int32_t                methodOffset = 0;
    std::vector<std::int32_t>   sourceArguments;  // what the callers add to the Block (or its BlockLegacy) they pass it
    Function                    grassFn;
    std::string                 route, error;
};

Found failed(std::string why) {
    Found f;
    f.error = std::move(why);
    return f;
}

// the jmp thunks to `fn` (a function begins 16-aligned)
std::vector<std::uintptr_t> thunksTo(std::uintptr_t fn) {
    std::vector<std::uintptr_t> out;
    for (memory::Reference const& r : memory::referencesTo(fn)) {
        if (r.kind == memory::Ref::Jump && r.at % 16 == 0) {
            out.push_back(r.at);
        }
    }
    return out;
}

std::vector<std::uintptr_t> leasOf(std::uintptr_t fn) {
    std::vector<std::uintptr_t> out;
    for (memory::Reference const& r : memory::referencesTo(fn)) {
        if (r.kind == memory::Ref::Lea) {
            out.push_back(r.at);
        }
    }
    return out;
}

bool samplersFound(Found& f, std::uintptr_t lea) {
    Function const maker = memory::functionAt(lea);
    if (!maker) {
        return false;
    }
    std::vector<std::uintptr_t> targets;
    for (std::uintptr_t t : memory::leaTargets(maker)) {
        targets.push_back(memory::throughThunk(t));
    }
    auto const grass = std::ranges::find(targets, f.grass);
    log::info("tints: sampler maker +0x{:X}, {} lea targets, grass at {}", maker.begin - memory::imageBase(), targets.size(), grass - targets.begin());
    if (grass == targets.end() || grass + 1 == targets.end()) {
        return false;
    }
    auto first = grass;
    while (first != targets.begin() && memory::returnsConstant(*(first - 1))) {
        --first;
    }
    if (grass - first < 2 || first == targets.begin() || !memory::returnsMember(*(grass + 1))) {
        return false;
    }
    f.foliage = *(first - 1);
    f.water   = *(grass + 1);
    f.samplers.assign(first - 1, grass + 2);
    // the byte a caller of the maker passes it, read from what one function returns for the Block
    std::map<std::pair<std::uintptr_t, std::int32_t>, int>                    votes;
    std::map<std::pair<std::uintptr_t, std::int32_t>, std::set<std::int32_t>> arguments;
    for (memory::Reference const& r : memory::referencesTo(maker.begin)) {
        if (r.kind == memory::Ref::Call) {
            if (Function const caller = memory::functionAt(r.at)) {
                for (memory::ByteArgument const& a : memory::byteArgumentsTo(maker.begin, caller)) {
                    ++votes[{a.source, a.offset}];
                    arguments[{a.source, a.offset}].insert(a.sourceArguments.begin(), a.sourceArguments.end());
                }
            }
        }
    }
    for (auto const& [k, n] : votes) {
        log::info("tints: tint method byte +0x{:X} of +0x{:X}: {} callers", k.second, k.first - memory::imageBase(), n);
    }
    if (votes.empty()) {
        return false;
    }
    auto const best = std::ranges::max_element(votes, {}, [](auto const& v) { return v.second; });
    f.methodSource  = best->first.first;
    f.methodOffset  = best->first.second;
    f.sourceArguments.assign(arguments[best->first].begin(), arguments[best->first].end());
    f.route = "samplers";
    return true;
}

bool biomeFunctionsFound(Found& f) {
#ifdef _WIN32
    Function const before = memory::functionBefore(f.grassFn);
    if (!before) {
        return false;
    }
    f.foliage = before.begin;
#else
    // the first function shortly before the grass one that goes to a constant: Biome::getMapBirchFoliageColor
    Function birch;
    for (Function const& fn : memory::functionsIn(f.grassFn.begin - 0x3000, f.grassFn.begin)) {
        if (fn.end - fn.begin > 0x100) {
            continue;
        }
        auto const targets = memory::branchTargets(fn.begin, fn.end);
        if (std::ranges::any_of(targets, memory::returnsConstant)) {
            birch = fn;
            break;
        }
    }
    if (!birch) {
        return false;
    }
    Function fn = memory::functionBefore(birch);
    while (fn && memory::isTrivial(fn.begin)) {
        fn = memory::functionBefore(fn);
    }
    if (!fn) {
        return false;
    }
    f.foliage      = fn.begin;
#endif
    f.route = "biome";
    return true;
}

#ifndef _WIN32
// Biome::getMapGrassColor by its symbol, where the build has its symbols (to 1.21.2, but 1.17.0): some of those keep
// Biome::getGrassColor too, which holds the swamp's colours as well
std::uintptr_t grassBySymbol() {
    char          path[PATH_MAX];
    ssize_t const n = readlink("/proc/self/exe", path, sizeof path - 1);
    if (n <= 0) {
        return 0;
    }
    memory::ElfFile file;
    if (!file.open(memory::symbolsFileOf(std::string(path, static_cast<std::size_t>(n))))) {
        return 0;
    }
    auto const found = file.symbols({"_ZNK5Biome16getMapGrassColorERK8BlockPos"});
    return found.empty() ? 0 : memory::imageBase() + found.begin()->second;
}
#endif

Found findFunctions() {
    auto const& game = memory::Module::game();
    auto const  hits = memory::Pattern::parse("3C 76 4C 00")->find(game.textBegin, game.textEnd, 3);
    Found       f;
#ifndef _WIN32
    if (hits.size() > 1) {
        if (std::uintptr_t const bySymbol = grassBySymbol(); bySymbol && std::ranges::any_of(hits, [&](std::uintptr_t h) { return memory::functionAt(h).begin == bySymbol; })) {
            f.grassFn = memory::functionAt(bySymbol);
        }
    }
#endif
    if (!f.grassFn && hits.size() != 1) {
        return failed(std::format("the swamp's grass colour is in {} places of the code", hits.size()));
    }
    if (!f.grassFn) {
        f.grassFn = memory::functionAt(hits[0]);
    }
    if (!f.grassFn) {
        return failed("no function holds the swamp's grass colour");
    }
    f.grass                          = f.grassFn.begin;
    std::vector<std::uintptr_t> leas = leasOf(f.grass);
    for (std::uintptr_t thunk : thunksTo(f.grass)) {
        for (std::uintptr_t lea : leasOf(thunk)) {
            leas.push_back(lea);
        }
    }
    if (!leas.empty()) {
        if (std::ranges::none_of(leas, [&](std::uintptr_t lea) { return samplersFound(f, lea); })) {
            return failed("the grass function's address is taken, but by no sampler maker");
        }
    } else if (!biomeFunctionsFound(f)) {
        return failed("no foliage function by the grass one");
    }
    log::info("tints: {}, grass +0x{:X}, foliage +0x{:X}, water {}", f.route, f.grass - memory::imageBase(), f.foliage - memory::imageBase(),
              f.water ? std::format("+0x{:X}", f.water - memory::imageBase()) : "a member");
    return f;
}

// what each of a biome's objects answers; nullopt where none answers or they disagree
std::optional<int> askBiome(std::uintptr_t fn, Biome const& biome) {
    std::set<int> answers;
    for (std::uintptr_t object : biome.objects) {
        if (auto const a = callBiome(fn, object)) {
            answers.insert(*a & 0xFFFFFF);
        }
    }
    if (answers.size() != 1) {
        return std::nullopt;
    }
    return *answers.begin();
}

// Where a Biome keeps its map water colour as a Color (setMapWaterColor's: each channel a byte times 1/255, then an alpha
// in (0, 1]): the one offset where every biome reads so and the biomes do not all read the same. From about 1.21.20 it is
// an int, which nothing in the Biome tells from its others: there the water is the packets' (biome_definition_list).
int channelOf(float f) {
    float const n = std::round(f * 255.0f);
    return n >= 0 && n <= 255 && std::abs(f * 255.0f - n) < 1e-3f ? static_cast<int>(n) : -1;
}

std::optional<int> waterAt(std::uintptr_t biome, std::int64_t at) {
    int const   r = channelOf(memory::read<float>(biome, at)), g = channelOf(memory::read<float>(biome, at + 4)), b = channelOf(memory::read<float>(biome, at + 8));
    float const a = memory::read<float>(biome, at + 12);
    if (r < 0 || g < 0 || b < 0 || !(a > 0 && a <= 1)) {
        return std::nullopt;
    }
    return r << 16 | g << 8 | b;
}

std::optional<std::int64_t> waterMember(Biomes const& biomes) {
    std::vector<std::int64_t> fits;
    for (std::int64_t off = 8; off <= 0x400; off += 4) {
        std::set<int> colours;
        bool const    all = std::ranges::all_of(biomes.biomes, [&](Biome const& b) {
            auto const c = waterAt(b.objects.front(), off);
            if (c) {
                colours.insert(*c);
            }
            return c.has_value();
        });
        if (all && colours.size() >= 5) {
            fits.push_back(off);
        }
    }
    std::string at;
    for (std::int64_t off : fits) {
        at += std::format(" +0x{:X}", off);
    }
    log::info("tints: {} Biome members read as a water Color:{}", fits.size(), at);
    if (fits.size() != 1) {
        return std::nullopt;
    }
    return fits[0];
}

// a vtable's code pointers, up to the first word that is none (Windows has no RTTI between vtables: it runs on into the
// next one, which the base's length cuts off)
std::vector<std::uintptr_t> vtableOf(std::uintptr_t object) {
    std::vector<std::uintptr_t> out;
    std::uintptr_t const        vt  = readPtr(object);
    auto const [codeBegin, codeEnd] = memory::imageCode();
    for (int slot = 0; slot < 800; ++slot) {
        std::uintptr_t const fn = readPtr(vt, slot * 8);
        if (fn < codeBegin || fn >= codeEnd) {
            break;
        }
        out.push_back(fn);
    }
    return out;
}

// each slot's most common function among the block types: what BlockLegacy (or a near base) has there, up to its last
// slot that most of the types share (past BlockLegacy's own, the words are other vtables')
std::vector<std::uintptr_t> baseVtable(std::vector<std::vector<std::uintptr_t>> const& vtables) {
    std::vector<std::uintptr_t> base;
    std::size_t                 shared = 0;
    for (std::size_t slot = 0;; ++slot) {
        std::unordered_map<std::uintptr_t, std::size_t> counts;
        for (auto const& vt : vtables) {
            if (slot < vt.size()) {
                ++counts[vt[slot]];
            }
        }
        if (counts.empty()) {
            break;
        }
        auto const common = std::ranges::max_element(counts, {}, [](auto const& c) { return c.second; });
        base.push_back(common->first);
        if (common->second * 2 > vtables.size()) {
            shared = slot + 1;
        }
    }
    base.resize(shared);
    // Windows: the block classes' vtables, all of BlockLegacy's length, lie one after another, so the base repeats with
    // that period; its length is the shortest shift under which most slots repeat
    for (std::size_t period = 32; period * 2 <= base.size(); ++period) {
        std::size_t same = 0;
        for (std::size_t slot = 0; slot < period; ++slot) {
            same += base[slot] == base[slot + period];
        }
        if (same * 10 >= period * 9) {
            base.resize(period);
            break;
        }
    }
    log::info("tints: BlockLegacy's vtable has {} slots", base.size());
    return base;
}

// The redstone wire's colour in each state; none where BlockLegacy has no getColor returning 0xFFFFFFFF (from 1.21.20 the
// server keeps no block colour: the client tints the wire by its tint method).
std::optional<std::vector<int>> redstoneColours(std::vector<BlockType> const& types, std::vector<std::vector<std::uintptr_t>> const& vtables,
                                                std::vector<std::uintptr_t> const& base, std::string& why) {
    auto const wire = std::ranges::find(types, std::string("minecraft:redstone_wire"), &BlockType::name);
    if (wire == types.end()) {
        return why = "no redstone_wire", std::nullopt;
    }
    auto const&      vt    = vtables[wire - types.begin()];
    auto const       white = [&](std::size_t slot) { return memory::returnsConstant(base[slot]) && callConstant(base[slot]) == -1; };
    std::vector<int> whites, slots;
    for (std::size_t slot = 0; slot < base.size(); ++slot) {
        if (white(slot)) {
            whites.push_back(static_cast<int>(slot));
            if (slot < vt.size() && vt[slot] != base[slot]) {
                slots.push_back(static_cast<int>(slot));
            }
        }
    }
    if (whites.empty()) {
        log::info("tints: BlockLegacy has no getColor, the redstone wire no colour");
        return std::vector<int>{};
    }
    // both overloads, or the one taking the Block alone (an unoptimized build's other one calls it): either answers called
    // as both would be
    if (slots.empty() || slots.size() > 2) {
        return why = std::format("the redstone wire overrides {} of the {} slots whose base returns 0xFFFFFFFF", slots.size(), whites.size()), std::nullopt;
    }
#ifdef _WIN32
    int const slot = slots.back();
#else
    int const slot = slots.front();
#endif
    std::vector<int> out;
    for (std::uintptr_t state : wire->states) {
        int        colour = 0;
        bool const ok     = memory::guard([&] { colour = reinterpret_cast<BlockColor>(vt[slot])(wire->address, state, &Origin, state); });
        if (!ok) {
            return why = "the redstone wire's getColor faults", std::nullopt;
        }
        out.push_back(colour & 0xFFFFFF);
    }
    if (std::set<int>(out.begin(), out.end()).size() < 2) {
        return why = "the redstone wire has one colour", std::nullopt;
    }
    log::info("tints: redstone wire getColor at slot {}", slot);
    return out;
}

// samplers: a state's tint method is a byte of the map colour component its Block, else its BlockLegacy, holds (the Script
// API's BlockMapColorComponent.tintMethod), which one function finds in the object's component storage. Where each keeps
// it is the pair of the callers' offsets under which the known blocks read their known methods; the types all of whose
// states read one method, whose sampler returns a constant, are tinted with that.
struct MethodRoute {
    std::optional<std::int32_t> block, legacy;  // the storage's offset in the Block, then in the BlockLegacy
};

int methodOf(Found const& f, MethodRoute route, BlockType const& t, std::uintptr_t state) {
    for (auto [object, at] : {std::pair{state, route.block}, std::pair{t.address, route.legacy}}) {
        if (!at) {
            continue;
        }
        std::uintptr_t component = 0;
        if (!memory::guard([&] { component = reinterpret_cast<BlockFn>(f.methodSource)(object + *at); })) {
            return -1;
        }
        if (component) {
            return memory::read<std::uint8_t>(component, f.methodOffset);
        }
    }
    return 0;
}

std::optional<std::vector<BlockTint>> constantsBySampler(Found const& f, std::vector<BlockType> const& types, std::string& why) {
    auto const grassMethod = static_cast<int>(std::ranges::find(f.samplers, f.grass) - f.samplers.begin()) + 1;
    // the default foliage is method 1, the birch 2, the evergreen 3
    std::pair<char const*, int> const known[] = {
        {"minecraft:grass_block", grassMethod}, {"minecraft:oak_leaves", 1}, {"minecraft:birch_leaves", 2}, {"minecraft:spruce_leaves", 3}};
    std::vector<MethodRoute>                 routes;
    std::vector<std::optional<std::int32_t>> offsets{std::nullopt};
    for (std::int32_t a : f.sourceArguments) {
        offsets.push_back(a);
    }
    for (auto const& b : offsets) {
        for (auto const& l : offsets) {
            if (b || l) {
                routes.push_back({b, l});
            }
        }
    }
    std::optional<MethodRoute> route;
    std::size_t                tinted = 0;
    for (MethodRoute const& r : routes) {
        bool const fits = std::ranges::all_of(known, [&](auto const& k) {
            auto const t = std::ranges::find(types, std::string(k.first), &BlockType::name);
            return t != types.end() && methodOf(f, r, *t, t->states[t->defaultState]) == k.second;
        });
        if (!fits) {
            continue;
        }
        std::size_t const n = std::ranges::count_if(types, [&](BlockType const& t) { return methodOf(f, r, t, t.states[t.defaultState]) > 0; });
        if (n > tinted) {
            route = r, tinted = n;
        }
    }
    if (!route) {
        return why = "no storage of a block's map colour reads the known blocks' tint methods", std::nullopt;
    }
    log::info("tints: tint method of the Block at {}, of its BlockLegacy at {}: {} types tinted", route->block ? std::format("+0x{:X}", *route->block) : "-",
              route->legacy ? std::format("+0x{:X}", *route->legacy) : "-", tinted);
    std::vector<BlockTint> out;
    for (BlockType const& t : types) {
        std::set<int> methods;
        for (std::uintptr_t state : t.states) {
            methods.insert(methodOf(f, *route, t, state));
        }
        if (methods.size() != 1 || *methods.begin() <= 0 || *methods.begin() > static_cast<int>(f.samplers.size())) {
            continue;
        }
        std::uintptr_t const sampler = f.samplers[*methods.begin() - 1];
        if (!memory::returnsConstant(sampler)) {
            continue;
        }
        if (auto const colour = callConstant(sampler)) {
            out.push_back({t.name, *colour & 0xFFFFFF});
        }
    }
    return out;
}

// biome: the block types whose colour virtuals go to a Biome function near the grass one that returns a constant, and to
// no other colour function. The colour virtuals (getMapColor, getColor) are the slots whose functions reach the foliage
// function: call it, or (1.21.50) take the address of a sampler's vtable whose method hands its argument on to it.
// One getMapColor for several types picks among them by a value of the type: a member of its BlockLegacy
// (mBlockTintTypeID, 1.21.20 - 1.21.42) or what a getter returns for the Block (1.21.50); the agent follows the code's
// branches on that value to the one it takes.
std::vector<BlockTint> constantsByCode(Found const& f, std::vector<BlockType> const& types, std::vector<std::vector<std::uintptr_t>> const& vtables,
                                       std::vector<std::uintptr_t> const& base) {
    std::uintptr_t const near0 = f.grassFn.begin - 0x4000, near1 = f.grassFn.end + 0x4000;
    auto const           isColourFn = [&](std::uintptr_t t) { return t == f.grass || t == f.foliage || (t >= near0 && t < near1); };
    // a sampler vtable's colour function: the first of its methods that hands its argument on to one
    std::unordered_map<std::uintptr_t, std::uintptr_t> samplerOf;
    auto                                               sampler = [&](std::uintptr_t vt) -> std::uintptr_t {
        auto [it, fresh] = samplerOf.try_emplace(vt, 0);
        if (fresh) {
            for (int slot = 0; slot < 3 && !it->second; ++slot) {
                if (std::uintptr_t const to = memory::forwardedTo(readPtr(vt, slot * 8)); to && isColourFn(to)) {
                    it->second = to;
                }
            }
        }
        return it->second;
    };
    // what each function reaches: its calls' and jumps' targets, and the colour functions of the samplers it takes
    std::unordered_map<std::uintptr_t, std::vector<std::uintptr_t>> reachOf;
    auto                                                            wholeOf = [](std::uintptr_t fn) {
        Function const whole = memory::functionAt(fn);
        return whole.begin == fn ? whole : Function{fn, fn + 0x800};
    };
    auto reach = [&](std::uintptr_t fn) -> std::vector<std::uintptr_t> const& {
        auto [it, fresh] = reachOf.try_emplace(fn);
        if (fresh) {
            Function const whole = wholeOf(fn);
            it->second           = memory::branchTargets(fn, whole.end);
            for (std::uintptr_t vt : memory::leaTargets(whole, true)) {
                if (std::uintptr_t const to = sampler(vt)) {
                    it->second.push_back(to);
                }
            }
        }
        return it->second;
    };
    std::map<std::size_t, int> foliageCallers;
    for (auto const& vt : vtables) {
        for (std::size_t slot = 0; slot < std::min(vt.size(), base.size()); ++slot) {
            if (std::ranges::count(reach(vt[slot]), f.foliage)) {
                ++foliageCallers[slot];
            }
        }
    }
    std::string slots;
    for (auto const& [slot, n] : foliageCallers) {
        slots += std::format(" {} ({})", slot, n);
    }
    log::info("tints: the slots whose functions reach the foliage function:{}", slots);
    auto constantOf = [&](std::uintptr_t t) -> std::optional<int> {
        if (t < near0 || t >= near1) {
            return std::nullopt;
        }
        for (std::uintptr_t c : memory::branchTargets(t, t + 0x100, 40, true)) {
            if (memory::returnsConstant(c)) {
                if (auto const colour = callConstant(c)) {
                    return *colour & 0xFFFFFF;
                }
            }
        }
        return std::nullopt;
    };
    std::vector<BlockTint> out;
    for (std::size_t i = 0; i < types.size(); ++i) {
        auto const&   vt = vtables[i];
        std::set<int> answers;
        bool          biomeDependent = false;
        for (auto const& [slot, n] : foliageCallers) {
            if (slot >= vt.size()) {
                continue;
            }
            for (std::uintptr_t t : reach(vt[slot])) {
                if (t == f.grass || t == f.foliage) {
                    biomeDependent = true;
                }
                if (auto const c = constantOf(t)) {
                    answers.insert(*c);
                }
            }
        }
        if (answers.size() == 1 && !biomeDependent) {
            out.push_back({types[i].name, *answers.begin()});
            continue;
        }
        if (!biomeDependent || answers.empty()) {
            continue;
        }
        for (auto const& [slot, n] : foliageCallers) {
            if (slot >= vt.size()) {
                continue;
            }
            Function const              whole = wholeOf(vt[slot]);
            std::vector<std::uintptr_t> stops{f.grass, f.foliage};
            for (std::uintptr_t t : memory::branchTargets(vt[slot], whole.end)) {
                if (isColourFn(t)) {
                    stops.push_back(t);
                }
            }
            for (std::uintptr_t v : memory::leaTargets(whole, true)) {
                if (sampler(v)) {
                    stops.push_back(v);
                }
            }
            // where the branches on the type's value go, for each state; the first switch that leads every state to one
            std::uintptr_t chosen = 0;
            std::string    how;
            auto           take = [&](memory::MemberSwitch const& sw, auto valueOf) {
                std::set<std::uintptr_t> to;
                for (std::uintptr_t state : types[i].states) {
                    auto const value = valueOf(state);
                    if (!value) {
                        return false;
                    }
                    std::uintptr_t t = memory::followSwitch(sw, *value, stops);
                    if (std::uintptr_t const s = sampler(t)) {
                        t = s;
                    }
                    to.insert(t);
                }
                if (to.size() != 1 || !*to.begin()) {
                    return false;
                }
                chosen = *to.begin();
                return true;
            };
            for (memory::MemberSwitch const& sw : memory::memberSwitches(whole)) {
                if (take(sw, [&](std::uintptr_t) { return std::optional<std::int64_t>(memory::read<std::int32_t>(types[i].address, sw.offset)); })) {
                    how = std::format("its BlockLegacy's +0x{:X}", sw.offset);
                    break;
                }
            }
            if (!chosen) {
                for (memory::MemberSwitch const& sw : memory::resultSwitches(whole)) {
                    if (memory::onlyReads(sw.source) && take(sw, [&](std::uintptr_t state) -> std::optional<std::int64_t> {
                            int        value = 0;
                            bool const ok    = memory::guard([&] { value = reinterpret_cast<int (*)(std::uintptr_t)>(sw.source)(state); });
                            return ok ? std::optional<std::int64_t>(value) : std::nullopt;
                        })) {
                        how = std::format("+0x{:X} for its Block", sw.source - memory::imageBase());
                        break;
                    }
                }
            }
            if (!chosen || chosen == f.grass || chosen == f.foliage) {
                continue;
            }
            if (auto const c = constantOf(chosen)) {
                out.push_back({types[i].name, *c});
                log::info("tints: {}: {} picks a constant", types[i].name, how);
            }
            break;
        }
    }
    return out;
}

}

Tints tints(Biomes const& biomes, std::vector<BlockType> const& types) {
    Tints       out;
    Found const f = findFunctions();
    if (!f.error.empty()) {
        return out.error = f.error, out;
    }
    out.route                                = f.route;
    std::optional<std::int64_t> const member = f.water ? std::nullopt : waterMember(biomes);
    for (Biome const& b : biomes.biomes) {
        auto const grass = askBiome(f.grass, b), foliage = askBiome(f.foliage, b);
        auto const water = f.water ? askBiome(f.water, b) : member ? waterAt(b.objects.front(), *member) : std::optional<int>(-1);
        if (!grass || !foliage || !water) {
            return out.error = std::format("biome {}: its colours do not answer", b.name), out;
        }
        out.biomes.push_back({b.name, *grass, *foliage, *water});
    }
    std::vector<std::vector<std::uintptr_t>> vtables;
    for (BlockType const& t : types) {
        vtables.push_back(vtableOf(t.address));
    }
    std::vector<std::uintptr_t> const base     = baseVtable(vtables);
    auto                              redstone = redstoneColours(types, vtables, base, out.error);
    if (!redstone) {
        return out;
    }
    out.redstone = std::move(*redstone);
    if (f.route == "samplers") {
        auto constant = constantsBySampler(f, types, out.error);
        if (!constant) {
            return out;
        }
        out.constant = std::move(*constant);
    } else {
        out.constant = constantsByCode(f, types, vtables, base);
    }
    return out;
}

}
