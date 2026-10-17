// What changed in each server build, oldest first: see Bindings.h. A new build needs a step only for what
// moved; `pnpm check <version>` shows what still resolves on its exe.
#include "bindings/Bindings.h"
#include "bindings/Names.h"

namespace n = bdg::bindings::names;

// a table: one binding a line, its pattern whole
// clang-format off

BDG_BINDINGS("1.16.201.02") {
    r.variant(n::BlockMapKey, n::MapKeyString);
    r.variant(n::RegistryGetterKind, n::RegistryBySret);
    r.variant(n::DimensionKind, n::DimensionByPointer);
    r.variant(n::AabbLayout, n::Aabb28WithValid);
    r.offset(n::BlockSerializationId, 32);
    r.offset(n::BlockTypePermutations, 672);
    r.offset(n::DimensionBlockSource, 0x58);  // Dimension::init / getBlockSourceFromMainChunkSource in the PDB
    r.slot(n::BlockAddCollisionShapes, 11);
    r.function(n::LevelTick, "48 8B C4 55 41 54 41 55 41 56 41 57 48 8D A8 08 FE FF FF 48 81 EC D0 02 00 00 48 C7 45 90 FE FF FF FF");
    r.function(n::LevelGetDimension, "4C 8B 89 50 08 00 00 4C 8B 81 38 08 00 00");
    r.function(n::LevelCreateDimension, "48 8B C4 89 50 10 56 57 41 56 48 81 EC");
    r.data(n::BlockLookupMap, "4C 8B 35 ? ? ? ? 49 8B EE 49 8B FE", 3);
    r.variant(n::VisualShapeKind, n::VisualShapeBlockBufferBool);
    r.slot(n::BlockGetVisualShape, 124);
    r.slot(n::BlockGetUIShape, 125);
    r.variant(n::OutlineKind, n::OutlineByAABB);
    r.slot(n::BlockGetAABB, 10);
    r.slot(n::BlockGetOutline, 12);
}

// Level::getDimension is virtual from here (the PDB has it in no vtable of 1.16.201)
BDG_BINDINGS("1.16.210.05") {
    r.offset(n::DimensionBlockSource, 0x60);
    r.slot(n::LevelGetDimensionSlot, 6);
    r.function(n::LevelTick, "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B E1 48 89 8D");
    r.function(n::LevelGetDimension, "4C 8B 81 ? ? ? ? 4C 8B 89");
    r.function(n::LevelCreateDimension, "48 89 5C 24 ? 48 89 6C 24 ? 89 54 24");
    r.data(n::BlockLookupMap, "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B 08 F0 FF 43 08 48 8B 79 40 48 8B 45 C7", 3);
    r.slot(n::BlockGetVisualShape, 129);
    r.slot(n::BlockGetUIShape, 130);
}

BDG_BINDINGS("1.16.220.02") {
    r.slot(n::BlockGetVisualShape, 132);
    r.slot(n::BlockGetUIShape, 133);
}

BDG_BINDINGS("1.17.0.03") {
    r.offset(n::BlockSerializationId, 104);
    r.offset(n::BlockTypePermutations, 680);
}

BDG_BINDINGS("1.17.10.04") {
    r.slot(n::BlockGetVisualShape, 133);
    r.slot(n::BlockGetUIShape, 134);
}

BDG_BINDINGS("1.17.30.04") {
    r.variant(n::AabbLayout, n::Aabb24);  // AABB lost its trailing bool mValid
    r.offset(n::BlockTypePermutations, 672);
    r.function(n::LevelTick, "48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F9");
    r.slot(n::BlockGetVisualShape, 134);
    r.slot(n::BlockGetUIShape, 135);
}

BDG_BINDINGS("1.17.40.06") {
    r.function(n::LevelGetDimension, "4C 8B 81 ? ? ? ? 4C 8B 89 ? ? ? ? 48 63 C2");
    r.function(n::LevelCreateDimension, "48 89 5C 24 18 48 89 6C 24 20 89 54 24 10 56 57 41 54");
}

BDG_BINDINGS("1.18.0.02") {
    r.function(n::LevelTick, "48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F1");
    r.slot(n::BlockGetVisualShape, 135);
    r.slot(n::BlockGetUIShape, 136);
}

BDG_BINDINGS("1.18.11.01") {
    r.slot(n::LevelGetDimensionSlot, 5);
    r.data(n::BlockLookupMap, "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B 08 F0 FF 43 08", 3);
    r.slot(n::BlockGetVisualShape, 140);
    r.slot(n::BlockGetUIShape, 141);
    r.slot(n::BlockGetOutline, 13);
}

BDG_BINDINGS("1.18.30.04") {
    r.variant(n::BlockMapKey, n::MapKeyHashedString);  // the block type map is keyed by HashedString
    r.offset(n::BlockSerializationId, 112);
    r.offset(n::DimensionBlockSource, 0xD0);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D AC 24 00 FE FF FF");
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 40", 3);
    r.slot(n::BlockGetVisualShape, 144);
    r.slot(n::BlockGetUIShape, 145);
}

BDG_BINDINGS("1.19.1.01") {
    r.offset(n::DimensionBlockSource, 0xD8);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 10 02 00 00 48 8B F9");
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 28", 3);
}

BDG_BINDINGS("1.19.10.03") {
    r.offset(n::DimensionBlockSource, 0xC0);
}

BDG_BINDINGS("1.19.20.02") {
    r.offset(n::BlockSerializationId, 128);
    r.function(n::LevelTick, "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B F1 45 33 ED 8B 81");
}

BDG_BINDINGS("1.19.30.04") {
    r.offset(n::BlockTypePermutations, 680);
}

BDG_BINDINGS("1.19.40.02") {
    r.offset(n::BlockSerializationId, 136);
    r.function(n::LevelTick, "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B F1 8B 81");
    r.slot(n::BlockGetVisualShape, 143);
    r.slot(n::BlockGetUIShape, 144);
}

BDG_BINDINGS("1.19.50.02") {
    r.variant(n::DimensionKind, n::DimensionByWeakRef);  // dimension getters return WeakRef<Dimension> by value
    r.offset(n::BlockSerializationId, 160);
    r.offset(n::DimensionBlockSource, 0xD0);
    r.function(n::LevelGetDimension, "48 83 EC ? 4C 8B 91 ? ? ? ? 4C 8B CA");
    r.function(n::LevelGetOrCreateDimension, "48 89 5C 24 20 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC B0 00 00 00");
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 50", 3);
    r.drop(n::LevelCreateDimension);
}

BDG_BINDINGS("1.19.60.04") {
    r.offset(n::BlockSerializationId, 168);
    r.function(n::LevelGetOrCreateDimension, "48 89 5C 24 20 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC D0 00 00 00");
}

BDG_BINDINGS("1.19.70.02") {
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 57 48 83 EC 40 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 30 48 8B D9 48 8B 01");
    // no unique pattern for the dimension getters from here on: the agent probes the Level vtable
    r.drop(n::LevelGetDimension);
    r.drop(n::LevelGetOrCreateDimension);
}

BDG_BINDINGS("1.19.80.02") {
    r.offset(n::BlockSerializationId, 176);
    r.offset(n::BlockTypePermutations, 728);
    r.slot(n::BlockGetVisualShape, 144);
    r.slot(n::BlockGetUIShape, 145);
}

BDG_BINDINGS("1.20.0.01") {
    r.offset(n::BlockSerializationId, 216);
    r.slot(n::BlockGetVisualShape, 142);
    r.slot(n::BlockGetUIShape, 143);
}

BDG_BINDINGS("1.20.11.01") {
    r.offset(n::BlockSerializationId, 200);
    r.offset(n::BlockTypePermutations, 760);
    r.slot(n::BlockAddCollisionShapes, 7);
    r.variant(n::VisualShapeKind, n::VisualShapeBlockBuffer);  // getVisualShape lost its trailing bool
    r.slot(n::BlockGetVisualShape, 12);
    r.slot(n::BlockGetUIShape, 13);
    r.slot(n::BlockGetAABB, 8);
    r.slot(n::BlockGetOutline, 10);
}

BDG_BINDINGS("1.20.30.02") {
    r.offset(n::BlockSerializationId, 216);
    r.slot(n::BlockAddCollisionShapes, 8);
    r.variant(n::OutlineKind, n::OutlineByBlock);  // getAABB is gone: getOutline takes the Block
    r.drop(n::BlockGetAABB);
    r.slot(n::BlockGetCameraShape, 7);
}

BDG_BINDINGS("1.20.40.01") {
    r.offset(n::BlockSerializationId, 192);
}

BDG_BINDINGS("1.20.50.03") {
    r.offset(n::BlockSerializationId, 168);
    r.offset(n::BlockTypePermutations, 768);
    r.slot(n::BlockAddCollisionShapes, 7);
    r.slot(n::BlockGetVisualShape, 11);
    r.slot(n::BlockGetUIShape, 12);
    r.slot(n::BlockGetOutline, 9);
    r.slot(n::BlockGetCameraShape, 6);
}

BDG_BINDINGS("1.20.80.05") {
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 5E 08 48 89 5C 24 50", 3);
}

BDG_BINDINGS("1.21.20.03") {
    r.offset(n::BlockTypePermutations, 776);
    r.slot(n::LevelGetDimensionSlot, 4);
    r.drop(n::DimensionBlockSource);  // no PDB from here: the first BlockSource of the Dimension, then its getter
}

BDG_BINDINGS("1.21.30.03") {
    r.offset(n::BlockSerializationId, 184);
    r.slot(n::LevelGetDimensionSlot, 5);
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 30", 3);
}

BDG_BINDINGS("1.21.42.01") {
    r.offset(n::BlockTypePermutations, 768);
    r.slot(n::LevelGetDimensionSlot, 4);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 60 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 50 48 8B D9");
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 50", 3);
}

BDG_BINDINGS("1.21.50.07") {
    r.offset(n::BlockSerializationId, 264);
    r.offset(n::BlockTypePermutations, 832);
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 50 44 89 74 24 58", 3);
}

BDG_BINDINGS("1.21.60.10") {
    r.offset(n::BlockTypePermutations, 728);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D AC 24 D0 FE FF FF 48 81 EC 30 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 20 01 00 00 4C 8B F1");
    r.data(n::BlockLookupMap, "4C 8B 2D ? ? ? ? 49 8B 5D 08 48 89 5C 24 40", 3);
}

BDG_BINDINGS("1.21.70.04") {
    r.slot(n::LevelGetDimensionSlot, 5);
}

BDG_BINDINGS("1.21.80.3") {
    r.slot(n::LevelGetDimensionSlot, 4);
}

BDG_BINDINGS("1.21.100.6") {
    // no static map from here on: the registry is reached through the Level
    r.offset(n::BlockTypePermutations, 720);
    r.slot(n::LevelGetBlockTypeRegistry, 380);
    r.drop(n::BlockLookupMap);
}

BDG_BINDINGS("1.21.111.1") {
    r.offset(n::BlockTypePermutations, 712);
    r.slot(n::LevelGetBlockTypeRegistry, 381);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 2F 4C 8B F9 45 33 F6");
}

BDG_BINDINGS("1.21.120.4") {
    r.offset(n::BlockSerializationId, 272);
    r.slot(n::LevelGetBlockTypeRegistry, 380);
}

BDG_BINDINGS("1.21.124.2") {
    r.slot(n::LevelGetDimensionSlot, 5);
}

BDG_BINDINGS("1.21.130.3") {
    r.offset(n::BlockSerializationId, 264);
    r.slot(n::LevelGetBlockTypeRegistry, 382);
    r.slot(n::LevelGetDimensionSlot, 4);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 2F 4C 8B F1");
}

BDG_BINDINGS("1.26.0.2") {
    r.offset(n::BlockSerializationId, 232);
    r.offset(n::BlockTypePermutations, 624);
    r.slot(n::LevelGetBlockTypeRegistry, 386);
    r.slot(n::LevelGetDimensionSlot, 5);
    r.slot(n::DimensionGetBlockSource, 12);
}

BDG_BINDINGS("1.26.10.4") {
    r.slot(n::LevelGetBlockTypeRegistry, 388);
    r.slot(n::LevelGetDimensionSlot, 4);
}

BDG_BINDINGS("1.26.20.5") {
    r.variant(n::RegistryGetterKind, n::RegistryInRax);  // clang-built server: getBlockTypeRegistry returns in rax
    r.offset(n::BlockSerializationId, 240);
    r.offset(n::BlockTypePermutations, 640);
    r.offset(n::LevelBlockTypeRegistry, 440);
    r.slot(n::LevelGetBlockTypeRegistry, 389);
    r.slot(n::LevelGetDimensionSlot, 6);
    r.function(n::LevelTick, "55 56 57 48 83 EC ? 48 8D 6C 24 ? 48 C7 45 ? FE FF FF FF 48 89 CE 48 8B 01 48 8B 80 38 06 00 00");
}

BDG_BINDINGS("1.26.30.5") {
    r.offset(n::BlockTypePermutations, 552);
    r.slot(n::LevelGetBlockTypeRegistry, 390);
    r.slot(n::BlockAddCollisionShapes, 6);
    r.slot(n::BlockGetVisualShape, 10);
    r.slot(n::BlockGetUIShape, 11);
    r.slot(n::BlockGetOutline, 8);
    r.slot(n::BlockGetCameraShape, 5);
}

BDG_BINDINGS("1.26.40.8") {
    r.function(n::LevelTick, "55 56 57 48 83 EC 70 48 8D 6C 24 70 48 C7 45 F8 FE FF FF FF 48 89 CE 48 8B 01 48 8B 80 48 06 00 00");
}

BDG_BINDINGS("1.26.51.1") {
    r.slot(n::LevelGetBlockTypeRegistry, 391);
    r.function(n::LevelTick, "55 56 57 53 48 81 EC B8 00 00 00 48 8D AC 24 80 00 00 00 48 C7 45 30 FE FF FF FF 48 89 CF");
}

// clang-format on
