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

// ---- member offsets ----------------------------------------------------------------------------
inline constexpr char BlockSerializationId[]   = "Block::mSerializationId";          // the state's CompoundTag
inline constexpr char BlockTypePermutations[]  = "BlockLegacy::mBlockPermutations";  // std::vector<Block*> begin; end is +8
inline constexpr char LevelBlockTypeRegistry[] = "Level::mBlockTypeRegistry";
// where the Dimension keeps the BlockSource addCollisionShapes reads, on builds without the getter slot
// (Dimension::getBlockSourceFromMainChunkSource / Dimension::init in the PDB)
inline constexpr char DimensionBlockSource[] = "Dimension::mBlockSource";

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

}
