// The Linux servers' bindings, by name (bindings/Bindings.h): each build's symbols (bedrock_server_symbols.debug, or
// the server's own .dynsym before 1.16.220) say where every function, global and vtable is, and a getter's code where
// a member is. Steps as on Windows: each states only what changed since the one before, and the eras are Windows' own.
// The builds without symbols (1.17.0, and every one from 1.21.20) are bound as Windows is: patterns, slots, and the
// members found by content (game/Layout.cpp).
#include "bindings/Bindings.h"
#include "bindings/Names.h"

namespace n = bdg::bindings::names;

namespace {

// The bindings by name of the first builds, until the steps below change them: 1.17.0, which has no symbols, binds them
// otherwise, and 1.17.10 by name again.
// clang-format off
void bySymbols(bdg::bindings::Registry& r) {
    r.symbol(n::LevelTick, "_ZN5Level4tickEv");
    r.symbol(n::LevelGetDimension, "_ZNK5Level12getDimensionE11AutomaticIDI9DimensioniE");
    r.symbol(n::LevelCreateDimension, "_ZN5Level15createDimensionE11AutomaticIDI9DimensioniE");
    r.symbol(n::BlockLookupMap, "_ZN17BlockTypeRegistry15mBlockLookupMapB5cxx11E");
    r.offsetOf(n::DimensionBlockSource, "_ZNK9Dimension46getBlockSourceDEPRECATEDUSEPLAYERREGIONINSTEADEv");
    r.offsetOf(n::BlockSerializationId, "_ZNK5Block18getSerializationIdEv");
    r.offsetOf(n::BlockTypePermutations, "_ZNK11BlockLegacy15getDefaultStateEv", -24);  // mDefaultState is right after the vector
    r.symbol(n::CompoundTagWrite, "_ZNK11CompoundTag5writeER11IDataOutput");
    r.symbol(n::OperatorDelete, "_ZdlPv");
    r.slotOf(n::BlockAddCollisionShapes, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy18addCollisionShapesERK5BlockR11BlockSourceRK8BlockPosPK4AABBRSt6vectorIS8_SaIS8_EEP5Actor");
    r.slotOf(n::BlockGetVisualShape, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy14getVisualShapeERK5BlockR4AABBb");
    r.slotOf(n::BlockGetUIShape, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy10getUIShapeERK5BlockR4AABB");
    r.slotOf(n::BlockGetAABB, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy7getAABBER11BlockSourceRK8BlockPosRK5BlockR4AABBb");
    r.slotOf(n::BlockGetOutline, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy10getOutlineER11BlockSourceRK8BlockPosR4AABB");
}
// clang-format on

}

// clang-format off
BDG_BINDINGS("1.16.201.02") {
    r.variant(n::StlKind, n::StlLibstdcxx);
    r.variant(n::BlockMapKey, n::MapKeyString);
    r.variant(n::DimensionKind, n::DimensionByPointer);
    r.variant(n::AabbLayout, n::Aabb28WithValid);
    r.variant(n::VisualShapeKind, n::VisualShapeBlockBufferBool);
    r.variant(n::OutlineKind, n::OutlineByAABB);
    bySymbols(r);
}

// Level::getDimension is not virtual before (Level's first base is BlockSourceListener)
BDG_BINDINGS("1.16.210.05") {
    r.slotOf(n::LevelGetDimensionSlot, "_ZTV5Level", "_ZNK5Level12getDimensionE11AutomaticIDI9DimensioniE");
}

BDG_BINDINGS("1.17.0.03") {
    r.function(n::LevelTick, "55 41 57 41 56 41 54 53 48 81 EC 20 01 00 00 49 89 FC 8A 05 ? ? ? ? 84 C0 0F 84 ? ? ? ? 48 8D BC 24 88 00 00 00");
    r.function(n::LevelCreateDimension, "41 57 41 56 53 48 83 EC 30 48 89 FB 89 74 24 08");
    r.drop(n::LevelGetDimension);
    r.drop(n::BlockLookupMap);
    r.drop(n::CompoundTagWrite);
    r.drop(n::OperatorDelete);  // libstdc++'s, imported: the agent leaves what the game allocated it
    r.offset(n::DimensionBlockSource, 0x60);
    r.offset(n::BlockSerializationId, 0x60);
    r.offset(n::BlockTypePermutations, 0x2B8);
    r.slot(n::LevelGetDimensionSlot, 7);
    r.slot(n::BlockAddCollisionShapes, 11);
    // as the symbol builds around it (1.16.220, 1.17.10) have them: the virtuals declared after the destructor are each
    // Windows' + 1 (the Itanium ABI's two destructor slots), those before it Windows' own
    r.slot(n::BlockGetVisualShape, 133);
    r.slot(n::BlockGetUIShape, 134);
    r.slot(n::BlockGetAABB, 10);
    r.slot(n::BlockGetOutline, 12);
}

BDG_BINDINGS("1.17.10.04") {
    bySymbols(r);
    r.slotOf(n::LevelGetDimensionSlot, "_ZTV5Level", "_ZNK5Level12getDimensionE11AutomaticIDI9DimensioniE");
}

BDG_BINDINGS("1.17.30.04") {
    r.variant(n::AabbLayout, n::Aabb24);
    r.slotOf(n::BlockAddCollisionShapes, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy18addCollisionShapesERK5BlockR11BlockSourceRK8BlockPosPK4AABBRSt6vectorIS8_SaIS8_EE12optional_refI19IActorMovementProxyE");
    r.slotOf(n::BlockGetAABB, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy7getAABBERK11BlockSourceRK8BlockPosRK5BlockR4AABBb");
}

BDG_BINDINGS("1.18.11.01") {
    r.symbol(n::DimensionBlockSourceGetter, "_ZNK9Dimension33getBlockSourceFromMainChunkSourceEv");
    r.drop(n::DimensionBlockSource);
}

BDG_BINDINGS("1.18.30.04") {
    r.variant(n::BlockMapKey, n::MapKeyHashedString);
    r.symbol(n::BlockLookupMap, "_ZN17BlockTypeRegistry15mBlockLookupMapE");
    r.slotOf(n::BlockGetAABB, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy7getAABBERK17IConstBlockSourceRK8BlockPosRK5BlockR4AABBb");
    r.slotOf(n::BlockAddCollisionShapes, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy18addCollisionShapesERK5BlockR11BlockSourceRK8BlockPosPK4AABBRSt6vectorIS8_SaIS8_EE12optional_refIK26GetCollisionShapeInterfaceE");
}

BDG_BINDINGS("1.19.1.01") {
    r.slotOf(n::BlockAddCollisionShapes, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy18addCollisionShapesERK5BlockRK11BlockSourceRK8BlockPosPK4AABBRSt6vectorIS9_SaIS9_EE12optional_refIK26GetCollisionShapeInterfaceE");
}

BDG_BINDINGS("1.19.50.02") {
    r.variant(n::DimensionKind, n::DimensionByWeakRef);
    r.symbol(n::LevelGetOrCreateDimension, "_ZN5Level20getOrCreateDimensionE11AutomaticIDI9DimensioniE");
    r.drop(n::LevelCreateDimension);
}

BDG_BINDINGS("1.20.11.01") {
    r.variant(n::VisualShapeKind, n::VisualShapeBlockBuffer);  // getVisualShape lost its trailing bool
    r.slotOf(n::BlockGetVisualShape, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy14getVisualShapeERK5BlockR4AABB");
}

BDG_BINDINGS("1.20.30.02") {
    r.variant(n::OutlineKind, n::OutlineByBlock);  // getAABB is gone: getOutline takes the Block
    r.drop(n::BlockGetAABB);
    r.slotOf(n::BlockGetOutline, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy10getOutlineERK5BlockRK17IConstBlockSourceRK8BlockPosR4AABB");
    r.slotOf(n::BlockGetCameraShape, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy26getCollisionShapeForCameraER4AABBRK5BlockRK17IConstBlockSourceRK8BlockPos");
}

BDG_BINDINGS("1.20.40.01") {
    r.variant(n::StlKind, n::StlLibcxx);
    r.slotOf(n::BlockAddCollisionShapes, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy18addCollisionShapesERK5BlockRK11BlockSourceRK8BlockPosPK4AABBRNSt3__16vectorIS9_NSC_9allocatorIS9_EEEE12optional_refIK26GetCollisionShapeInterfaceE");
}

BDG_BINDINGS("1.20.61.01") {
    r.slotOf(n::BlockAddCollisionShapes, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy18addCollisionShapesERK5BlockRK17IConstBlockSourceRK8BlockPosPK4AABBRNSt3__16vectorIS9_NSC_9allocatorIS9_EEEE12optional_refIK26GetCollisionShapeInterfaceE");
}

BDG_BINDINGS("1.21.20.03") {
    r.variant(n::LayoutByContent, 1);
    r.drop(n::LevelGetDimension);
    r.drop(n::LevelGetOrCreateDimension);
    r.drop(n::BlockLookupMap);
    r.drop(n::DimensionBlockSourceGetter);
    r.drop(n::CompoundTagWrite);
    r.drop(n::OperatorDelete);
    r.drop(n::BlockSerializationId);
    r.drop(n::BlockTypePermutations);
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 78 05 00 00");
    r.slot(n::LevelGetDimensionSlot, 6);
    r.slot(n::BlockAddCollisionShapes, 7);
    // as 1.21.2's symbols place them, and Windows' from 1.20.50
    r.slot(n::BlockGetVisualShape, 11);
    r.slot(n::BlockGetUIShape, 12);
    r.slot(n::BlockGetOutline, 9);
    r.slot(n::BlockGetCameraShape, 6);
}

BDG_BINDINGS("1.21.42.01") {
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 F8 05 00 00");
}

BDG_BINDINGS("1.21.60.10") {
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 00 06 00 00");
}

BDG_BINDINGS("1.21.80.3") {
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 08 06 00 00");
}

BDG_BINDINGS("1.21.90.3") {
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 F8 05 00 00");
}

BDG_BINDINGS("1.21.100.6") {
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 08 06 00 00");
}

BDG_BINDINGS("1.21.111.1") {
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 10 06 00 00");
}

BDG_BINDINGS("1.21.120.4") {
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 18 06 00 00");
    r.slot(n::LevelGetDimensionSlot, 5);
}

BDG_BINDINGS("1.26.0.2") {
    r.function(n::LevelTick, "41 56 53 48 83 EC 38 48 89 FB 48 8B 07 FF 90 28 06 00 00");
}

BDG_BINDINGS("1.26.10.4") {
    r.function(n::LevelTick, "41 56 53 48 83 EC 38 48 89 FB 48 8B 07 FF 90 38 06 00 00");
    r.slot(n::LevelGetDimensionSlot, 6);
}

BDG_BINDINGS("1.26.20.5") {
    r.function(n::LevelTick, "41 56 53 48 83 EC 38 48 89 FB 48 8B 07 FF 90 40 06 00 00");
    r.slot(n::LevelGetDimensionSlot, 7);
}

BDG_BINDINGS("1.26.30.5") {
    r.slot(n::BlockAddCollisionShapes, 6);
    r.slot(n::BlockGetVisualShape, 10);
    r.slot(n::BlockGetUIShape, 11);
    r.slot(n::BlockGetOutline, 8);
    r.slot(n::BlockGetCameraShape, 5);
}

BDG_BINDINGS("1.26.40.8") {
    r.function(n::LevelTick, "41 56 53 48 83 EC 38 48 89 FB 48 8B 07 FF 90 50 06 00 00");
}

BDG_BINDINGS("1.26.51.1") {
    r.function(n::LevelTick, "55 41 57 41 56 53 48 83 EC 58 49 89 FE 0F B6 05 ? ? ? ?");
}

// clang-format on
