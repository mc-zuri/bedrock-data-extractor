#pragma once
// Every key bindings/Steps.cpp may set. Consumers use these, never raw strings.

namespace bdg::bindings::names {

// ---- functions (patterns) ----------------------------------------------------------------------
// Hooked: the export runs on the server's own tick, with the game quiescent.
inline constexpr char LevelTick[] = "Level::tick";
// Overworld lookup. getDimension is also how a Level is recognised by its vtable; either creator is
// preferred over it so the overworld is created rather than waited for.
inline constexpr char LevelGetDimension[]         = "Level::getDimension";
inline constexpr char LevelCreateDimension[]      = "Level::createDimension";       // DimensionByPointer era
inline constexpr char LevelGetOrCreateDimension[] = "Level::getOrCreateDimension";  // DimensionByWeakRef era
// data: the static std::map<HashedString, BlockType owner> of every block type (up to 1.21.93)
inline constexpr char BlockLookupMap[] = "BlockTypeRegistry::mBlockLookupMap";
// A block's drops up to 1.20.61, where block types spawn them themselves: Block::spawnResources (its overload is
// SpawnResourcesKind), and Spawner::spawnItem, which every item they spawn goes through.
inline constexpr char BlockSpawnResources[] = "Block::spawnResources";
inline constexpr char SpawnerSpawnItem[]    = "Spawner::spawnItem";  // ItemActor* (BlockSource&, ItemStack const&, Actor*, Vec3 const&, int)
// bool BlockSource::setBlock(int x, int y, int z, Block const&, int update flags[, Actor*]), up to 1.20.61; from 1.20.71
// the agent finds the virtual setBlock(BlockPos const&, ...) by its code
inline constexpr char BlockSourceSetBlock[] = "BlockSource::setBlock(x, y, z)";
// Linux: BlockSource::setBlock(BlockPos const&, Block const&, int, ActorBlockSyncMessage const*[, Actor*]), by its symbol
inline constexpr char BlockSourceSetBlockAt[] = "BlockSource::setBlock(BlockPos)";
// mce::Color Block::getMapColor(BlockSource&, BlockPos const&) const, by its symbol where the build has one (else found by
// what it calls: game/Drops.cpp)
inline constexpr char BlockGetMapColor[] = "Block::getMapColor";

// Linux: CompoundTag::write(IDataOutput&) const, which writes a state's tag (Windows walks it, or calls its vtable's)
inline constexpr char CompoundTagWrite[] = "CompoundTag::write";
// Linux: Dimension::getBlockSourceFromMainChunkSource() const, not virtual there: called by its symbol
inline constexpr char DimensionBlockSourceGetter[] = "Dimension::getBlockSourceFromMainChunkSource()";
// Linux: the game's own operator delete(void*), for what it allocated into a vector the agent handed it
inline constexpr char OperatorDelete[] = "operator delete";

// ---- vtable slots ------------------------------------------------------------------------------
inline constexpr char LevelGetBlockTypeRegistry[] = "ILevel::getBlockTypeRegistry";
inline constexpr char LevelGetDimensionSlot[]     = "Level::getDimension (slot)";
inline constexpr char DimensionGetBlockSource[]   = "Dimension::getBlockSourceFromMainChunkSource";
inline constexpr char BlockAddCollisionShapes[]   = "BlockLegacy::addCollisionShapes";
// AABB const& (Block const&, AABB& buffer[, bool]) const: the box a state is drawn in, and its outline's (VisualShapeKind)
inline constexpr char BlockGetVisualShape[] = "BlockLegacy::getVisualShape";
inline constexpr char BlockGetUIShape[]     = "BlockLegacy::getUIShape";
// the outline (OutlineKind): getOutline, and before 1.20.30 getAABB, which the getOutline types inherit gives
inline constexpr char BlockGetOutline[] = "BlockLegacy::getOutline";
inline constexpr char BlockGetAABB[]    = "BlockLegacy::getAABB";
// bool (AABB& out, Block const&, IConstBlockSource const&, BlockPos const&) const, from 1.20.30
inline constexpr char BlockGetCameraShape[] = "BlockLegacy::getCollisionShapeForCamera";
// Linux: the virtuals Windows finds by their code (game/Dig.cpp, game/Facts.cpp), which the unoptimized builds (1.16.201,
// 1.16.210) do not have; their answers are checked the same
inline constexpr char ItemGetDestroySpeed[]   = "Item::getDestroySpeed";
inline constexpr char ItemCanDestroySpecial[] = "Item::canDestroySpecial";
inline constexpr char ItemGetEnchantSlot[]    = "Item::getEnchantSlot";
inline constexpr char BlockCanContainLiquid[] = "BlockLegacy::canContainLiquid";
inline constexpr char BlockGetMapColorSlot[]  = "BlockLegacy::getMapColor";
// int () const and uint8 (ItemDescriptor const&) const; where a build binds neither, each is the slot whose base function
// reads the item's field (game/Items.cpp)
inline constexpr char ItemGetMaxDamage[]    = "Item::getMaxDamage";
inline constexpr char ItemGetMaxStackSize[] = "Item::getMaxStackSize";
// std::string (ItemStackBase const&) const: the item's name, as the server's language gives the key it builds; where a
// build binds none, the slot whose base function calls Item::buildDescriptionId (game/ItemAnswers.cpp)
inline constexpr char ItemBuildDescriptionName[] = "Item::buildDescriptionName";
// void (BlockSource&, BlockPos const&) const: up to 1.18.0 a skull spawns its item here (from its block actor), not in
// spawnResources
inline constexpr char BlockOnRemove[] = "BlockLegacy::onRemove";
// Linux builds without symbols: the virtual BlockSource::setBlock(BlockPos const&, Block const&, int, ActorBlockSyncMessage
// const*, Actor*) (Windows finds it by its code, which passes its last arguments on the stack)
inline constexpr char BlockSourceSetBlockSlot[] = "BlockSource::setBlock (slot)";
// Linux: Random's IRandom::nextFloat and nextDouble (Windows: slots 5 and 6), what tells a Random by its code
inline constexpr char RandomNextFloat[]  = "Random::nextFloat";
inline constexpr char RandomNextDouble[] = "Random::nextDouble";

// ---- member offsets ----------------------------------------------------------------------------
inline constexpr char BlockSerializationId[]   = "Block::mSerializationId";          // the state's CompoundTag
inline constexpr char BlockTypePermutations[]  = "BlockLegacy::mBlockPermutations";  // std::vector<Block*> begin; end is +8
inline constexpr char LevelBlockTypeRegistry[] = "Level::mBlockTypeRegistry";
// where the Dimension keeps the BlockSource addCollisionShapes reads, on builds without the getter slot
// (Dimension::getBlockSourceFromMainChunkSource / Dimension::init in the PDB)
inline constexpr char DimensionBlockSource[]   = "Dimension::mBlockSource";
inline constexpr char BlockTypeTags[]          = "BlockLegacy::mTags";  // std::vector<HashedString> begin; end is +8
inline constexpr char BlockTypeDescriptionId[] = "BlockLegacy::mDescriptionId";
// in bits from the BlockLegacy, where a build binds it (else found by content: game/Facts.cpp)
inline constexpr char BlockTypeRequiresCorrectTool[] = "BlockLegacy::mRequiresCorrectToolForDrops (bit)";
// a state's scalars: on each Block, or (before 1.19.1; the resistance before 1.20.11) on the BlockLegacy, where the
// BlockLegacy's key is set
inline constexpr char BlockDestroySpeed[]            = "Block::mDestroySpeed";
inline constexpr char BlockExplosionResistance[]     = "Block::mExplosionResistance";
inline constexpr char BlockLightDampening[]          = "Block::mLightBlock";
inline constexpr char BlockLightEmission[]           = "Block::mLightEmission";
inline constexpr char BlockTypeDestroySpeed[]        = "BlockLegacy::mDestroySpeed";
inline constexpr char BlockTypeExplosionResistance[] = "BlockLegacy::mExplosionResistance (x5)";
inline constexpr char BlockTypeLightDampening[]      = "BlockLegacy::mLightBlock";
inline constexpr char BlockTypeLightEmission[]       = "BlockLegacy::mLightEmission";
// the IResourceDropsStrategy a BlockLegacy owns, from 1.20.71 (else 0x88 after mBlockPermutations)
inline constexpr char BlockTypeDropStrategy[] = "BlockLegacy::mResourceDropsStrategy";
// before 1.17.0 the Level's Random has no vtable to tell it by: where its twister's seed is
inline constexpr char LevelRandomSeed[] = "Level::mRandom's twister seed";

// ---- variants ----------------------------------------------------------------------------------
// the server's standard library, whose containers the agent walks (memory/Stl.h): MSVC's on Windows, libstdc++ on
// Linux up to the builds that switched to libc++
inline constexpr char StlKind[] = "standard library";
// Linux builds without symbols: the members no symbol says are found by content in the running server (game/Layout.cpp)
inline constexpr char LayoutByContent[] = "members found by content";
enum StlKindValue { StlMsvc = 0, StlLibstdcxx = 1, StlLibcxx = 2 };

inline constexpr char RegistryGetterKind[] = "getBlockTypeRegistry kind";
enum RegistryGetterKindValue { RegistryBySret = 0, RegistryInRax = 1 };  // clang-built servers (1.26.20+) return it in rax

inline constexpr char BlockMapKey[] = "block type map key";
enum BlockMapKeyValue { MapKeyString = 0, MapKeyHashedString = 1 };  // bare-name std::string keys (value @0x40) before 1.18.30

inline constexpr char DimensionKind[] = "Level dimension getters";
enum DimensionKindValue { DimensionByPointer = 0, DimensionByWeakRef = 1 };  // WeakRef<Dimension> by value from 1.19.50

inline constexpr char AabbLayout[] = "AABB layout";
enum AabbLayoutValue { Aabb24 = 0, Aabb28WithValid = 1 };  // the AABB carries `bool mValid` before 1.17.30

inline constexpr char VisualShapeKind[] = "getVisualShape kind";
enum VisualShapeKindValue { VisualShapeBlockBuffer = 0, VisualShapeBlockBufferBool = 1 };  // a trailing bool before 1.20.11

inline constexpr char OutlineKind[] = "getOutline kind";
enum OutlineKindValue { OutlineByAABB = 0, OutlineByBlock = 1 };  // getOutline takes the Block from 1.20.30

// what the virtual BlockSource::setBlock takes last: the Actor* that sets the block (none: null), or from 1.21.120 a
// BlockChangeContext const& (a variant, its index a byte at +0x10; 0: none)
inline constexpr char SetBlockChanger[] = "BlockSource::setBlock changer";
enum SetBlockChangerValue { ChangerActor = 0, ChangerContext = 1 };

// how a Randomize's NonOwnerPointer says its Random is alive: by a byte that is not 0, or (Linux from 1.26.0, whose
// inlined Randomize::chanceOneIn compares it with 1) that is 1
inline constexpr char RandomizeAlive[] = "Randomize alive byte";
enum RandomizeAliveValue { AliveNotZero = 0, AliveIsOne = 1 };

// Block::spawnResources(BlockSource&, BlockPos const&, ...): what follows the position. Where it takes the Block (the
// block's in the world, whose drops are drawn), the overload without asks the BlockSource for it.
inline constexpr char SpawnResourcesKind[] = "Block::spawnResources kind";
enum SpawnResourcesKindValue {
    SpawnBlockLevelRandom       = 0,  // Block const&, std::vector<Item const*>*, float explosion radius, int bonus loot, bool allow randomness: the Level's Random
    SpawnBlockLevelRandomTool   = 1,  // as SpawnBlockLevelRandom, then ItemStack const& tool (1.17.0)
    SpawnBlockRandomizeLootTool = 2,  // Block const&, Randomize&, std::vector<Item const*>*, float explosion radius, int bonus loot, ItemStack const& tool (1.17.10)
    SpawnBlockRandomizeTool     = 3,  // Block const&, Randomize&, std::vector<Item const*>*, float explosion radius, ItemStack const& tool (1.19.40)
    SpawnRandomizeVectorTool    = 4,  // Randomize&, std::vector<Item const*>*, float explosion radius, ItemStack const& tool (1.19.50)
    SpawnRandomizeTool          = 5,  // Randomize&, float explosion radius, ItemStack const& tool (1.19.60)
    SpawnRandomizeContext       = 6,  // Randomize&, ResourceDropsContext const& (1.20.61)
};

}
