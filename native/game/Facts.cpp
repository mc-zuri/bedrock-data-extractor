#include "game/Blocks.h"

#include "bindings/Bindings.h"
#include "bindings/Names.h"
#include "core/Log.h"
#include "game/Registry.h"
#include "memory/Guard.h"
#include "memory/Mem.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <set>

// What a block type is, read from its BlockLegacy and its states' Blocks:
// - its numeric id: BlockLegacy::mID, the one uint16 offset where the blocks whose ids never changed read them (air 0,
//   stone 1, dirt 3, cobblestone 4, bedrock 7, sand 12, gravel 13, obsidian 49) and no two types read alike;
// - hardness, explosion resistance and light: the bindings' offsets, on the BlockLegacy or on each Block;
// - friction and the fire odds: the one offset of the Block (else of the BlockLegacy) where the blocks known for them
//   read them; the translucency, the BlockLegacy's;
// - whether it can be waterlogged: BlockLegacy::canContainLiquid(), at the bindings' slot, else (from 1.19.40) at the slot
//   before BlockLegacy::getRequiredMedium's, which is known by its code, where it answers as the known blocks; else (the
//   builds that keep it in each Block's liquid detection component) the one byte of the Block that does;
// - whether it drops only with the right tool: the binding's bit where the build has one; else the one bit of the
//   BlockLegacy set for a dozen blocks that need a pickaxe and clear for a dozen that do not; on builds without that
//   bit, the negation of its Material's mAlwaysDestroyable, which Player::canDestroy asked instead;
// - its tags and description id: the bindings' offsets; the tags, where the build binds none, the one vector of
//   HashedStrings where stone's names minecraft:stone and obsidian's minecraft:diamond_pick_diggable;
// - each state's data value: Block::mData (blockDataOffset).
// Each is checked on blocks whose values are known: a wrong offset fails the facts rather than giving wrong ones.

namespace bdg::game {

namespace n = bindings::names;
using memory::read;
using memory::readPtr;

namespace {

// the block types by name
class Known {
public:
    explicit Known(std::vector<BlockType> const& types) : mTypes(types) {
        for (std::size_t i = 0; i < types.size(); ++i) {
            mIndex.emplace(types[i].name, i);
        }
    }

    std::optional<std::size_t> index(char const* name) const {
        auto it = mIndex.find(name);
        return it == mIndex.end() ? std::nullopt : std::optional{it->second};
    }

    // the first of `names` the build has (a block renamed since: minecraft:log, minecraft:oak_log); 0 when none
    std::uintptr_t type(std::initializer_list<char const*> names) const {
        for (char const* name : names) {
            if (auto i = name ? index(name) : std::nullopt) {
                return mTypes[*i].address;
            }
        }
        return 0;
    }

    // the default state of the first of `names` the build has; 0 when none
    std::uintptr_t state(std::initializer_list<char const*> names) const {
        for (char const* name : names) {
            if (auto i = name ? index(name) : std::nullopt) {
                return mTypes[*i].states[mTypes[*i].defaultState];
            }
        }
        return 0;
    }

private:
    std::vector<BlockType> const&                   mTypes;
    std::map<std::string, std::size_t, std::less<>> mIndex;
};

// the one offset in [from, to) by `step` that fits; -1 when none or several do
template <class Fits>
std::int64_t onlyOffset(std::int64_t from, std::int64_t to, std::int64_t step, Fits fits, char const* what) {
    std::vector<std::int64_t> found;
    for (std::int64_t off = from; off < to; off += step) {
        if (fits(off)) {
            found.push_back(off);
        }
    }
    if (found.size() != 1) {
        log::info("block facts: {} offsets fit {}", found.size(), what);
    }
    return found.size() == 1 ? found[0] : -1;
}

std::int64_t idOffset(std::vector<BlockType> const& types, Known const& known) {
    std::pair<char const*, int> const ids[] = {{"minecraft:air", 0},     {"minecraft:stone", 1}, {"minecraft:dirt", 3},    {"minecraft:cobblestone", 4},
                                               {"minecraft:bedrock", 7}, {"minecraft:sand", 12}, {"minecraft:gravel", 13}, {"minecraft:obsidian", 49}};
    return onlyOffset(
        8, 0x400, 2,
        [&](std::int64_t off) {
            bool                    fits = std::all_of(std::begin(ids), std::end(ids), [&](auto const& id) {
                std::uintptr_t const t = known.type({id.first});
                return t && read<std::uint16_t>(t, off) == id.second;
            });
            std::set<std::uint16_t> seen;
            for (std::size_t i = 0; fits && i < types.size(); ++i) {
                fits = seen.insert(read<std::uint16_t>(types[i].address, off)).second;
            }
            return fits;
        },
        "a block id");
}

// What says whether a block drops only with the right tool: a bit of the BlockLegacy, or a flag of its Material
struct ToolRequirement {
    std::int64_t bit      = -1;                          // in bits from the BlockLegacy
    std::int64_t material = -1, alwaysDestroyable = -1;  // the BlockLegacy's Material*, and the byte of that flag

    bool operator()(std::uintptr_t type) const {
        if (bit >= 0) {
            return (read<std::uint8_t>(type, bit / 8) >> (bit % 8)) & 1;
        }
        return !read<std::uint8_t>(readPtr(type, material), alwaysDestroyable);
    }
};

std::optional<ToolRequirement> toolRequirement(Known const& known) {
    std::vector<std::uintptr_t> needs, needsNot;
    for (auto name : {"minecraft:stone", "minecraft:cobblestone", "minecraft:obsidian", "minecraft:iron_ore", "minecraft:coal_ore", "minecraft:gold_ore", "minecraft:diamond_ore",
                      "minecraft:lapis_ore", "minecraft:iron_block", "minecraft:furnace", "minecraft:netherrack", "minecraft:sandstone"}) {
        needs.push_back(known.type({name}));
    }
    for (auto name : {"minecraft:dirt", "minecraft:sand", "minecraft:gravel", "minecraft:glass", "minecraft:air", "minecraft:tnt", "minecraft:torch", "minecraft:crafting_table",
                      "minecraft:pumpkin", "minecraft:bookshelf", "minecraft:ice", "minecraft:cactus", "minecraft:reeds", "minecraft:sponge"}) {
        needsNot.push_back(known.type({name}));
    }
    needsNot.push_back(known.type({"minecraft:oak_log", "minecraft:log"}));
    needsNot.push_back(known.type({"minecraft:oak_planks", "minecraft:planks"}));
    if (std::ranges::count(needs, std::uintptr_t{0}) || std::ranges::count(needsNot, std::uintptr_t{0})) {
        return std::nullopt;
    }
    auto fits = [&](auto&& needsTool) { return std::ranges::all_of(needs, needsTool) && std::ranges::none_of(needsNot, needsTool); };

    // a bit of a word that holds a pointer in some block is none: the pointer's bits change from run to run, and may fit
    // (1.21.2: once in some runs, where the flag is the Material's)
    auto const inPointer = [&](std::int64_t bit) {
        auto const isPointer = [&](std::uintptr_t t) {
            std::uintptr_t const p = readPtr(t, bit / 64 * 8);
            std::uint8_t         byte;
            return p >= 0x10000 && !(p & 7) && memory::safeRead(reinterpret_cast<void const*>(p), &byte, 1);
        };
        return std::ranges::any_of(needs, isPointer) || std::ranges::any_of(needsNot, isPointer);
    };
    std::vector<std::int64_t> bits;
    for (std::int64_t bit = 64; bit < 0x400 * 8; ++bit) {
        if (fits([&](std::uintptr_t t) { return ToolRequirement{bit}(t); }) && !inPointer(bit)) {
            bits.push_back(bit);
        }
    }
    std::int64_t const bound = bindings::current().offset(n::BlockTypeRequiresCorrectTool);
    log::info("block facts: requires-correct-tool bit {} by the binding, {} bits fit", bound, bits.size());
    if (bound >= 0) {
        return std::ranges::count(bits, bound) ? std::optional{ToolRequirement{bound}} : std::nullopt;
    }
    if (bits.size() == 1) {
        return ToolRequirement{bits[0]};
    }
    if (!bits.empty()) {
        return std::nullopt;
    }

    std::vector<ToolRequirement> flags;
    for (std::int64_t ptr = 8; ptr < 0x400; ptr += 8) {
        for (std::int64_t byte = 0; byte < 16; ++byte) {
            ToolRequirement const flag{-1, ptr, byte};
            // a pointer to a Material, and a bool there
            auto isFlag = [&](std::uintptr_t t) {
                std::uintptr_t const m = readPtr(t, ptr);
                return m >= 0x10000 && !(m & 7) && read<std::uint8_t>(m, byte) <= 1;
            };
            if (std::ranges::all_of(needs, isFlag) && std::ranges::all_of(needsNot, isFlag) && fits(flag)) {
                flags.push_back(flag);
            }
        }
    }
    log::info("block facts: {} Material flags fit always-destroyable{}", flags.size(),
              flags.size() == 1 ? std::format(" (the byte +{} of the BlockLegacy's +0x{:X})", flags[0].alwaysDestroyable, flags[0].material) : "");
    return flags.size() == 1 ? std::optional{flags[0]} : std::nullopt;
}

// BlockLegacy::mTags: a std::vector<HashedString> (0x30 each with MSVC's STL, 0x28 libc++'s); nullopt when it does not read as one
std::optional<std::vector<std::string>> tagsOf(std::uintptr_t type, std::int64_t offset) {
    std::uintptr_t const     stride = memory::hashedStringSize();
    std::uintptr_t const     begin = readPtr(type, offset), end = readPtr(type, offset + 8);
    std::vector<std::string> tags;
    if (!begin && !end) {
        return tags;
    }
    if (begin < 0x10000 || end < begin || (end - begin) % stride || (end - begin) / stride > 200) {
        return std::nullopt;
    }
    for (std::uintptr_t at = begin; at < end; at += stride) {
        std::string tag = memory::readHashedString(at);
        if (tag.empty() || !std::ranges::all_of(tag, [](char c) { return c > 0x20 && c < 0x7f && c != '"' && c != '\\'; })) {
            return std::nullopt;
        }
        tags.push_back(tag.find(':') == std::string::npos ? "minecraft:" + tag : tag);
    }
    std::ranges::sort(tags);
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    return tags;
}

// BlockLegacy::mTags: the bound offset, else the one where stone and obsidian name their known tags; -1 when none or several
std::int64_t tagsAt(Known const& known) {
    if (std::int64_t const bound = bindings::current().offset(n::BlockTypeTags); bound >= 0) {
        return bound;
    }
    std::uintptr_t const stone = known.type({"minecraft:stone"}), obsidian = known.type({"minecraft:obsidian"});
    auto has = [](std::optional<std::vector<std::string>> const& tags, char const* tag) { return tags && std::ranges::find(*tags, std::string(tag)) != tags->end(); };
    if (!stone || !obsidian) {
        return -1;
    }
    return onlyOffset(
        8, 0x400, 8, [&](std::int64_t off) { return has(tagsOf(stone, off), "minecraft:stone") && has(tagsOf(obsidian, off), "minecraft:diamond_pick_diggable"); }, "the tags");
}

// A fact found by content: where it is, and whether on the BlockLegacy (the same for every state of the type)
struct Field {
    bool           onType = false;
    std::int64_t   offset = -1;
    int            width  = 4;  // the fire odds' bytes: 4, or 2 where the build keeps them in uint16s
    std::uintptr_t base(std::uintptr_t type, std::uintptr_t state) const { return onType ? type : state; }
};

// a block known for a fact's value, by its name (and its name before it was renamed)
struct Anchor {
    char const* name;
    float       value;
    char const* oldName = nullptr;
};

using Anchors = std::initializer_list<Anchor>;

// The one offset of the default states' Blocks (else, or with `typeOnly` only, of the BlockLegacies) where each anchor
// reads its value: floats, or ints of `width` bytes
Field fieldOf(Known const& known, Anchors anchors, char const* what, bool floats, int width = 4, bool typeOnly = false) {
    for (bool onType : {false, true}) {
        if (typeOnly && !onType) {
            continue;
        }
        auto at = [&](Anchor const& a) { return onType ? known.type({a.name, a.oldName}) : known.state({a.name, a.oldName}); };
        if (std::ranges::any_of(anchors, [&](Anchor const& a) { return !at(a); })) {
            continue;
        }
        std::int64_t const found = onlyOffset(
            8, onType ? 0x400 : 0x200, floats ? 4 : width,
            [&](std::int64_t off) {
                return std::ranges::all_of(anchors, [&](Anchor const& a) {
                    if (floats) {
                        return read<float>(at(a), off) == a.value;
                    }
                    return (width == 2 ? read<std::int16_t>(at(a), off) : read<std::int32_t>(at(a), off)) == static_cast<int>(a.value);
                });
            },
            what);
        if (found >= 0) {
            return {onType, found, width};
        }
    }
    return {};
}

// Where the build keeps the scalars of a state: the BlockLegacy where its key is set, else the Block (bindings), and the
// facts found by content
class Scalars {
public:
    explicit Scalars(Known const& known) {
        auto const& r  = bindings::current();
        auto        at = [&](char const* typeKey, char const* stateKey) {
            std::int64_t const onType = r.offset(typeKey);
            return onType >= 0 ? Field{true, onType} : Field{false, r.offset(stateKey)};
        };
        mHardness   = at(n::BlockTypeDestroySpeed, n::BlockDestroySpeed);
        mResistance = at(n::BlockTypeExplosionResistance, n::BlockExplosionResistance);
        mEmission   = at(n::BlockTypeLightEmission, n::BlockLightEmission);
        mDampening  = at(n::BlockTypeLightDampening, n::BlockLightDampening);
        mFriction   = fieldOf(known,
                              {{"minecraft:air", 0.9f},
                               {"minecraft:stone", 0.6f},
                               {"minecraft:ice", 0.98f},
                               {"minecraft:packed_ice", 0.98f},
                               {"minecraft:blue_ice", 0.989f},
                               {"minecraft:slime", 0.8f}},
                              "the friction", true);
        for (int width : {4, 2}) {
            if (mFlame.offset >= 0 && mBurn.offset >= 0) {
                break;
            }
            mFlame = fieldOf(known,
                             {{"minecraft:oak_planks", 5, "minecraft:planks"},
                              {"minecraft:oak_leaves", 30, "minecraft:leaves"},
                              {"minecraft:white_wool", 30, "minecraft:wool"},
                              {"minecraft:tnt", 15},
                              {"minecraft:bookshelf", 30},
                              {"minecraft:stone", 0}},
                             "the flame odds", false, width);
            mBurn  = fieldOf(known,
                             {{"minecraft:oak_planks", 20, "minecraft:planks"},
                              {"minecraft:oak_leaves", 60, "minecraft:leaves"},
                              {"minecraft:white_wool", 60, "minecraft:wool"},
                              {"minecraft:tnt", 100},
                              {"minecraft:bookshelf", 20},
                              {"minecraft:stone", 0}},
                             "the burn odds", false, width);
        }
        mTranslucency = fieldOf(known,
                                {{"minecraft:stone", 0.0f},
                                 {"minecraft:glass", 1.0f},
                                 {"minecraft:air", 1.0f},
                                 {"minecraft:chest", 0.5f},
                                 {"minecraft:oak_stairs", 0.8f},
                                 {"minecraft:oak_leaves", 0.5f, "minecraft:leaves"}},
                                "the translucency", true, 4, true)
                            .offset;
    }

    // why one is not known; "" when all are
    std::string missing() const {
        if (mHardness.offset < 0 || mResistance.offset < 0 || mEmission.offset < 0 || mDampening.offset < 0) {
            return "the bindings lack a block fact's offset";
        }
        if (mFriction.offset < 0 || mFlame.offset < 0 || mBurn.offset < 0 || mTranslucency < 0) {
            return std::format("no one offset reads the known friction (+{}), flame odds (+{}), burn odds (+{}) or translucency (+{})", mFriction.offset, mFlame.offset,
                               mBurn.offset, mTranslucency);
        }
        return "";
    }

    void log() const {
        auto where = [](Field const& f) { return std::format("{} +{}", f.onType ? "BlockLegacy" : "Block", f.offset); };
        log::info("block facts: friction at {}, flame odds at {}, burn odds at {} ({} bytes), translucency at BlockLegacy +{}", where(mFriction), where(mFlame), where(mBurn),
                  mFlame.width, mTranslucency);
    }

    StateFacts of(std::uintptr_t type, std::uintptr_t state, std::int64_t dataAt) const {
        // the BlockLegacy keeps the resistance times 5
        float const resistance = mResistance.onType ? read<float>(type, mResistance.offset) / 5 : read<float>(state, mResistance.offset);
        auto        odds       = [&](Field const& f) {
            std::uintptr_t const at = f.base(type, state);
            return f.width == 2 ? read<std::int16_t>(at, f.offset) : read<std::int32_t>(at, f.offset);
        };
        return {read<float>(mHardness.base(type, state), mHardness.offset),
                resistance,
                read<float>(mFriction.base(type, state), mFriction.offset),
                odds(mFlame),
                odds(mBurn),
                read<std::uint8_t>(mEmission.base(type, state), mEmission.offset),
                read<std::uint8_t>(mDampening.base(type, state), mDampening.offset),
                read<std::uint16_t>(state, dataAt),
                false};
    }

    float translucency(std::uintptr_t type) const { return read<float>(type, mTranslucency); }

private:
    Field        mHardness, mResistance, mEmission, mDampening, mFriction, mFlame, mBurn;
    std::int64_t mTranslucency = -1;
};

// whether `fn` is BlockLegacy::getRequiredMedium(), whose slot is right after canContainLiquid's from 1.19.40: it gives no
// medium, an empty std::optional<HashedString> through the hidden pointer (rdx with MSVC, rdi in the Itanium ABI), whose
// engaged byte is right after the HashedString, and returns that pointer, within its first 16 bytes and without a call
bool givesNoMedium(std::uintptr_t fn) {
    std::uint8_t b[16];
    if (!fn || !memory::safeRead(reinterpret_cast<void const*>(fn), b, sizeof b)) {
        return false;
    }
    auto const         ret     = std::find(b, b + sizeof b, std::uint8_t{0xC3});
    std::uint8_t const engaged = static_cast<std::uint8_t>(memory::hashedStringSize());
    bool               clears  = false;
    for (auto at = b; at + 4 <= ret; ++at) {
        if (*at == 0xE8 || *at == 0xE9) {
            return false;
        }
        // mov byte [rdx / rdi + engaged], 0
        clears |= at[0] == 0xC6 && (at[1] == 0x42 || at[1] == 0x47) && at[2] == engaged && at[3] == 0;
    }
    return ret != b + sizeof b && clears;
}

// -1 when the call faults
int callBool(std::uintptr_t fn, std::uintptr_t self) {
    bool answer = false;
    return memory::guard([&] { answer = reinterpret_cast<bool (*)(std::uintptr_t)>(fn)(self); }) ? int{answer} : -1;
}

// BlockLegacy::canContainLiquid()'s slot: the bound one, else the one before getRequiredMedium's (a slot of stone's whose
// function gives no medium; another virtual may look alike, so the one of those whose previous slot answers); -1 when
// none answers as the known blocks. Only the slots before those that give no medium are called.
int canContainLiquidSlot(Known const& known) {
    std::uintptr_t const stone      = known.type({"minecraft:stone"});
    std::uintptr_t const holds[]    = {known.type({"minecraft:oak_stairs"}), known.type({"minecraft:chest"}), known.type({"minecraft:oak_fence", "minecraft:fence"})};
    std::uintptr_t const holdsNot[] = {stone, known.type({"minecraft:dirt"}), known.type({"minecraft:glass"}), known.type({"minecraft:tnt"}), known.type({"minecraft:bookshelf"})};
    if (std::ranges::count(holds, std::uintptr_t{0}) || std::ranges::count(holdsNot, std::uintptr_t{0})) {
        return -1;
    }
    auto answers = [&](int slot) {
        return slot >= 0 && std::ranges::all_of(holds, [&](std::uintptr_t t) { return callBool(memory::virtualAt(t, slot), t) == 1; }) &&
               std::ranges::all_of(holdsNot, [&](std::uintptr_t t) { return callBool(memory::virtualAt(t, slot), t) == 0; });
    };
    if (int const bound = bindings::current().slot(n::BlockCanContainLiquid); bound >= 0) {
        return answers(bound) ? bound : -1;
    }
    std::vector<int> medium, found;
    for (int at = 1; at < 400; ++at) {
        std::uintptr_t const fn = memory::virtualAt(stone, at);
        if (!memory::inImage(fn)) {
            break;
        }
        if (givesNoMedium(fn)) {
            medium.push_back(at);
        }
    }
    for (int at : medium) {
        if (answers(at - 1)) {
            found.push_back(at - 1);
        }
    }
    log::info("block facts: {} slots give no medium (BlockLegacy::getRequiredMedium), {} before them answer as canContainLiquid", medium.size(), found.size());
    return found.size() == 1 ? found[0] : -1;
}

// The liquid detection component's can-contain-liquid of each Block: the one byte offset where the default states of the
// blocks known to be waterlogged read 1 and those known not to 0; -1 when none or several
std::int64_t liquidByteAt(Known const& known) {
    std::uintptr_t const holds[]    = {known.state({"minecraft:oak_stairs"}), known.state({"minecraft:chest"}),     known.state({"minecraft:oak_fence"}),
                                       known.state({"minecraft:ladder"}),     known.state({"minecraft:iron_bars"}), known.state({"minecraft:hopper"}),
                                       known.state({"minecraft:glass_pane"}), known.state({"minecraft:lantern"})};
    std::uintptr_t const holdsNot[] = {known.state({"minecraft:stone"}), known.state({"minecraft:dirt"}),     known.state({"minecraft:glass"}), known.state({"minecraft:tnt"}),
                                       known.state({"minecraft:sand"}),  known.state({"minecraft:obsidian"}), known.state({"minecraft:water"}), known.state({"minecraft:lava"}),
                                       known.state({"minecraft:torch"}), known.state({"minecraft:bookshelf"})};
    if (std::ranges::count(holds, std::uintptr_t{0}) || std::ranges::count(holdsNot, std::uintptr_t{0})) {
        return -1;
    }
    return onlyOffset(
        8, 0x200, 1,
        [&](std::int64_t off) {
            return std::ranges::all_of(holds, [&](std::uintptr_t s) { return read<std::uint8_t>(s, off) == 1; }) &&
                   std::ranges::all_of(holdsNot, [&](std::uintptr_t s) { return read<std::uint8_t>(s, off) == 0; });
        },
        "the liquid detection's can-contain-liquid");
}

// why the facts do not hold the values the game is known for; "" when they do
std::string wrongKnownFacts(std::vector<BlockType> const& types, Known const& known, std::vector<BlockFacts> const& facts) {
    auto const stone = known.index("minecraft:stone"), dirt = known.index("minecraft:dirt"), obsidian = known.index("minecraft:obsidian"),
               bedrock = known.index("minecraft:bedrock"), glowstone = known.index("minecraft:glowstone"), glass = known.index("minecraft:glass");
    if (!stone || !dirt || !obsidian || !bedrock || !glowstone || !glass) {
        return "a known block (stone, dirt, obsidian, bedrock, glowstone, glass) is missing";
    }
    auto       def    = [&](std::size_t i) { return facts[i].states[types[i].defaultState]; };
    auto       tagged = [&](std::size_t i, std::string const& tag) { return std::ranges::find(facts[i].tags, tag) != facts[i].tags.end(); };
    auto const s      = def(*stone);
    if (s.hardness != 1.5f || s.explosionResistance != 6.0f || s.lightEmission != 0 || s.lightDampening != 15 || def(*bedrock).hardness != -1.0f ||
        def(*glowstone).lightEmission != 15 || def(*glass).lightDampening != 0) {
        return std::format("stone reads {} / {} / {} / {}", s.hardness, s.explosionResistance, s.lightEmission, s.lightDampening);
    }
    if (!facts[*stone].requiresCorrectTool || !facts[*obsidian].requiresCorrectTool || facts[*dirt].requiresCorrectTool) {
        return "stone or obsidian drop without a tool, or dirt needs one";
    }
    if (!tagged(*stone, "minecraft:stone") || !tagged(*obsidian, "minecraft:diamond_pick_diggable")) {
        return "stone is not tagged minecraft:stone, or obsidian minecraft:diamond_pick_diggable";
    }
    if (!facts[*stone].descriptionId.starts_with("tile.stone") || !facts[*dirt].descriptionId.starts_with("tile.dirt")) {
        return std::format("stone's description id reads '{}'", facts[*stone].descriptionId);
    }
    if (std::ranges::count_if(facts, [](BlockFacts const& f) { return !f.tags.empty(); }) < 10) {
        return "fewer than 10 block types have tags";
    }
    return "";
}

}

std::int64_t blockDataOffset(std::vector<BlockType> const& types) {
    std::vector<BlockType const*> numbered;
    for (char const* name : {"minecraft:wheat", "minecraft:farmland", "minecraft:cake", "minecraft:composter", "minecraft:nether_wart"}) {
        auto const it = std::ranges::find(types, std::string(name), &BlockType::name);
        if (it == types.end()) {
            return -1;
        }
        numbered.push_back(&*it);
    }
    std::vector<std::int64_t> found;
    for (std::int64_t at = 8; at < 0x200; at += 2) {
        bool const fits = std::ranges::all_of(numbered, [&](BlockType const* type) {
            for (std::size_t i = 0; i < type->states.size(); ++i) {
                if (read<std::uint16_t>(type->states[i], at) != i) {
                    return false;
                }
            }
            return true;
        });
        if (fits) {
            found.push_back(at);
        }
    }
    return found.size() == 1 ? found[0] : -1;
}

AllBlockFacts blockFacts(std::vector<BlockType> const& types) {
    AllBlockFacts      facts;
    auto const&        r = bindings::current();
    Known const        known(types);
    Scalars const      scalars(known);
    std::int64_t const descriptionOffset = r.offset(n::BlockTypeDescriptionId), tagsOffset = tagsAt(known), idOff = idOffset(types, known), dataOff = blockDataOffset(types);
    auto const         needsTool   = toolRequirement(known);
    int const          liquidSlot  = canContainLiquidSlot(known);
    std::int64_t const liquidByte  = liquidSlot < 0 ? liquidByteAt(known) : -1;
    std::string const  missingFact = scalars.missing();
    if (!missingFact.empty()) {
        facts.error = missingFact;
    } else if (descriptionOffset < 0 || tagsOffset < 0) {
        facts.error = "the bindings lack the description id's or the tags' offset";
    } else if (idOff < 0) {
        facts.error = "no offset reads the known block ids";
    } else if (!needsTool) {
        facts.error = "nothing reads which blocks need the right tool";
    } else if (dataOff < 0) {
        facts.error = "no offset reads the numbered blocks' data values";
    } else if (liquidSlot < 0 && liquidByte < 0) {
        facts.error = "neither BlockLegacy::canContainLiquid nor a Block's liquid detection answers as the known blocks";
    }
    if (!facts.error.empty()) {
        return facts;
    }
    log::info("block facts: id at +{}, description id at +{}, tags at +{}, data at +{}, can contain liquid {}", idOff, descriptionOffset, tagsOffset, dataOff,
              liquidSlot >= 0 ? std::format("by slot {}", liquidSlot) : std::format("at Block +{}", liquidByte));
    scalars.log();

    for (BlockType const& type : types) {
        int const  liquid = liquidSlot >= 0 ? callBool(memory::virtualAt(type.address, liquidSlot), type.address) : 0;
        BlockFacts f{
            read<std::uint16_t>(type.address, idOff), memory::readString(type.address + descriptionOffset), (*needsTool)(type.address), {}, scalars.translucency(type.address), {}};
        auto tags = tagsOf(type.address, tagsOffset);
        if (!tags) {
            facts.error = std::format("the tags of {} are not a tag list", type.name);
        } else if (!std::ranges::all_of(f.descriptionId, [](char c) { return c >= 0x20 && c < 0x7f; })) {
            facts.error = std::format("{} has no readable description id", type.name);
        } else if (liquid < 0) {
            facts.error = std::format("BlockLegacy::canContainLiquid faults on {}", type.name);
        }
        if (!facts.error.empty()) {
            facts.types.clear();
            return facts;
        }
        f.tags = std::move(*tags);
        for (std::uintptr_t state : type.states) {
            StateFacts s       = scalars.of(type.address, state, dataOff);
            s.canContainLiquid = liquidSlot >= 0 ? liquid == 1 : read<std::uint8_t>(state, liquidByte) == 1;
            f.states.push_back(s);
        }
        facts.types.push_back(std::move(f));
    }
    facts.error = wrongKnownFacts(types, known, facts.types);
    if (!facts.error.empty()) {
        facts.types.clear();
    }
    return facts;
}

}
