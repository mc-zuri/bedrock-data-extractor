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
    r.variant(n::SpawnResourcesKind, n::SpawnBlockLevelRandom);
    r.offset(n::LevelRandomSeed, 0x9E0);  // Level::getRandom in the PDB; Core::Random::_setSeed writes the seed first
    r.offset(n::BlockTypeDescriptionId, 8);
    r.offset(n::BlockTypeTags, 456);
    r.offset(n::BlockTypeDestroySpeed, 300);
    r.offset(n::BlockTypeExplosionResistance, 304);
    r.offset(n::BlockTypeLightDampening, 289);
    r.offset(n::BlockTypeLightEmission, 290);
    r.slot(n::BlockOnRemove, 152);
    r.function(n::BlockSpawnResources, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 48 89 7C 24 20 41 56 48 83 EC 70 4C 8B 72 10");
    r.function(n::SpawnerSpawnItem, "40 53 55 56 57 41 54 41 56 41 57 48 81 EC 00 01 00 00 48 C7 44 24 38 FE FF FF FF");
    r.function(n::BlockSourceSetBlock, "48 83 EC 58 89 54 24 40 48 8D 44 24 30 44 89 44 24 44");
    r.slot(n::BlockCanContainLiquid, 46);
    r.slot(n::ItemGetMaxDamage, 39);
    r.slot(n::ItemGetMaxStackSize, 92);
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
    r.offset(n::LevelRandomSeed, 0xA08);
    r.slot(n::BlockOnRemove, 157);
    r.function(n::BlockSpawnResources, "48 89 5C 24 10 48 89 6C 24 18 56 41 56 41 57 48 83 EC 50");
    r.function(n::SpawnerSpawnItem, "40 53 55 56 57 41 54 41 56 41 57 48 81 EC 00 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 F8 00 00 00 49 8B F1");
    r.function(n::BlockSourceSetBlock, "48 83 EC 58 89 54 24 40 48 8D 44 24 30 44 89 44 24 44 48 8D 54 24 40 4C 8B 84 24 80 00 00 00 0F 57 C0 44 89 4C 24 48 44 8B 8C 24 88 00 00 00");
    r.slot(n::BlockCanContainLiquid, 50);
}

BDG_BINDINGS("1.16.220.02") {
    r.slot(n::BlockGetVisualShape, 132);
    r.slot(n::BlockGetUIShape, 133);
    r.offset(n::LevelRandomSeed, 0xA18);  // the Random at +0xA08, its Core::Random (with a vtable now) at +8
    r.slot(n::BlockOnRemove, 160);
    r.slot(n::BlockCanContainLiquid, 52);
    r.slot(n::ItemGetMaxStackSize, 91);
}

BDG_BINDINGS("1.17.0.03") {
    r.offset(n::BlockSerializationId, 104);
    r.offset(n::BlockTypePermutations, 680);
    r.variant(n::SpawnResourcesKind, n::SpawnBlockLevelRandomTool);
    r.offset(n::BlockTypeTags, 464);
    r.slot(n::BlockOnRemove, 161);
    r.drop(n::LevelRandomSeed);
    r.slot(n::ItemGetMaxStackSize, 92);
}

BDG_BINDINGS("1.17.10.04") {
    r.slot(n::BlockGetVisualShape, 133);
    r.slot(n::BlockGetUIShape, 134);
    r.variant(n::SpawnResourcesKind, n::SpawnBlockRandomizeLootTool);
    r.slot(n::BlockOnRemove, 162);
    r.slot(n::BlockCanContainLiquid, 53);
    r.slot(n::ItemGetMaxDamage, 40);
    r.slot(n::ItemGetMaxStackSize, 93);
}

BDG_BINDINGS("1.17.30.04") {
    r.variant(n::AabbLayout, n::Aabb24);  // AABB lost its trailing bool mValid
    r.offset(n::BlockTypePermutations, 672);
    r.function(n::LevelTick, "48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F9");
    r.slot(n::BlockGetVisualShape, 134);
    r.slot(n::BlockGetUIShape, 135);
    r.function(n::SpawnerSpawnItem, "40 53 55 56 57 41 56 41 57 48 81 EC 28 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 18 01 00 00");
    r.slot(n::ItemGetMaxStackSize, 94);
}

BDG_BINDINGS("1.17.40.06") {
    r.function(n::LevelGetDimension, "4C 8B 81 ? ? ? ? 4C 8B 89 ? ? ? ? 48 63 C2");
    r.function(n::LevelCreateDimension, "48 89 5C 24 18 48 89 6C 24 20 89 54 24 10 56 57 41 54");
}

BDG_BINDINGS("1.18.0.02") {
    r.function(n::LevelTick, "48 89 5C 24 ? 48 89 74 24 ? 55 57 41 54 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F1");
    r.slot(n::BlockGetVisualShape, 135);
    r.slot(n::BlockGetUIShape, 136);
    r.slot(n::BlockOnRemove, 164);
    r.slot(n::ItemGetMaxDamage, 41);
    r.slot(n::ItemGetMaxStackSize, 95);
}

BDG_BINDINGS("1.18.11.01") {
    r.slot(n::LevelGetDimensionSlot, 5);
    r.data(n::BlockLookupMap, "48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8B 08 F0 FF 43 08", 3);
    r.slot(n::BlockGetVisualShape, 140);
    r.slot(n::BlockGetUIShape, 141);
    r.slot(n::BlockGetOutline, 13);
    r.function(n::BlockSourceSetBlock, "48 83 EC 48 48 8B 01 89 54 24 30 48 8D 54 24 30 44 89 44 24 34 4C 8B 44 24 70 44 89 4C 24 38 44 8B 4C 24 78");
    r.drop(n::BlockOnRemove);  // the skull spawns its item in spawnResources now
    r.slot(n::BlockCanContainLiquid, 56);
    r.slot(n::ItemGetMaxDamage, 43);
    r.slot(n::ItemGetMaxStackSize, 97);
}

BDG_BINDINGS("1.18.30.04") {
    r.variant(n::BlockMapKey, n::MapKeyHashedString);  // the block type map is keyed by HashedString
    r.offset(n::BlockSerializationId, 112);
    r.offset(n::DimensionBlockSource, 0xD0);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D AC 24 00 FE FF FF");
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 40", 3);
    r.slot(n::BlockGetVisualShape, 144);
    r.slot(n::BlockGetUIShape, 145);
    r.function(n::BlockSourceSetBlock, "48 83 EC 48 48 8B 01 89 54 24 30 48 8D 54 24 30");
    r.slot(n::BlockCanContainLiquid, 60);
    r.slot(n::ItemGetMaxStackSize, 96);
}

BDG_BINDINGS("1.19.1.01") {
    r.offset(n::DimensionBlockSource, 0xD8);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D AC 24 E0 FD FF FF 48 81 EC 20 03 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 10 02 00 00 48 8B F9");
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 28", 3);
    r.offset(n::BlockDestroySpeed, 28);
    r.offset(n::BlockLightDampening, 40);
    r.offset(n::BlockLightEmission, 41);
    r.function(n::BlockSpawnResources, "40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 88 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 78 4D 8B F9");
    r.drop(n::BlockTypeDestroySpeed);
    r.drop(n::BlockTypeLightDampening);
    r.drop(n::BlockTypeLightEmission);
    r.slot(n::ItemGetMaxDamage, 47);
    r.slot(n::ItemGetMaxStackSize, 100);
}

BDG_BINDINGS("1.19.10.03") {
    r.offset(n::DimensionBlockSource, 0xC0);
    r.slot(n::BlockCanContainLiquid, 61);
}

BDG_BINDINGS("1.19.20.02") {
    r.offset(n::BlockSerializationId, 128);
    r.function(n::LevelTick, "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B F1 45 33 ED 8B 81");
    r.offset(n::BlockDestroySpeed, 40);
    r.offset(n::BlockLightDampening, 52);
    r.offset(n::BlockLightEmission, 61);
    r.slot(n::ItemGetMaxDamage, 48);
    r.slot(n::ItemGetMaxStackSize, 101);
}

BDG_BINDINGS("1.19.30.04") {
    r.offset(n::BlockTypePermutations, 680);
    r.offset(n::BlockTypeTags, 472);
    r.offset(n::BlockTypeExplosionResistance, 308);
    r.slot(n::BlockCanContainLiquid, 62);
}

BDG_BINDINGS("1.19.40.02") {
    r.offset(n::BlockSerializationId, 136);
    r.function(n::LevelTick, "48 89 5C 24 ? 48 89 74 24 ? 48 89 7C 24 ? 55 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 4C 8B F1 8B 81");
    r.slot(n::BlockGetVisualShape, 143);
    r.slot(n::BlockGetUIShape, 144);
    r.variant(n::SpawnResourcesKind, n::SpawnBlockRandomizeTool);
    r.offset(n::BlockTypeExplosionResistance, 312);
    r.function(n::BlockSpawnResources, "40 53 55 56 57 41 54 41 55 41 56 41 57 48 81 EC 88 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 78 4D 8B E1");
    r.function(n::SpawnerSpawnItem, "40 53 55 56 57 41 56 41 57 48 81 EC 28 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 18 01 00 00 49 8B F1");
    r.function(n::BlockSourceSetBlock, "48 83 EC 58 48 8B 01 89 54 24 40 48 8B 94 24 90 00 00 00");
    r.drop(n::BlockCanContainLiquid);  // the slot before BlockLegacy::getRequiredMedium from here (game/Facts.cpp)
    r.slot(n::ItemGetMaxDamage, 47);
    r.slot(n::ItemGetMaxStackSize, 100);
}

BDG_BINDINGS("1.19.50.02") {
    r.variant(n::DimensionKind, n::DimensionByWeakRef);  // dimension getters return WeakRef<Dimension> by value
    r.offset(n::BlockSerializationId, 160);
    r.offset(n::DimensionBlockSource, 0xD0);
    r.function(n::LevelGetDimension, "48 83 EC ? 4C 8B 91 ? ? ? ? 4C 8B CA");
    r.function(n::LevelGetOrCreateDimension, "48 89 5C 24 20 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC B0 00 00 00");
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 50", 3);
    r.drop(n::LevelCreateDimension);
    r.variant(n::SpawnResourcesKind, n::SpawnRandomizeVectorTool);
    r.function(n::BlockSpawnResources, "40 53 55 56 57 41 54 41 56 41 57 48 81 EC 80 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 70 4C 8B A4 24 E0 00 00 00");
}

BDG_BINDINGS("1.19.60.04") {
    r.offset(n::BlockSerializationId, 168);
    r.function(n::LevelGetOrCreateDimension, "48 89 5C 24 20 44 89 44 24 18 55 56 57 41 54 41 55 41 56 41 57 48 8D 6C 24 D9 48 81 EC D0 00 00 00");
    r.variant(n::SpawnResourcesKind, n::SpawnRandomizeTool);
    r.function(n::BlockSpawnResources, "40 53 55 56 57 41 56 41 57 48 81 EC 88 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 70 48 8B 02");
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
    r.offset(n::BlockTypeTags, 520);
    r.offset(n::BlockTypeExplosionResistance, 360);
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
    r.offset(n::BlockTypeDescriptionId, 40);
    r.offset(n::BlockTypeTags, 552);
    r.offset(n::BlockDestroySpeed, 60);
    r.offset(n::BlockExplosionResistance, 120);
    r.offset(n::BlockLightDampening, 137);
    r.offset(n::BlockLightEmission, 136);
    r.drop(n::BlockTypeExplosionResistance);
}

BDG_BINDINGS("1.20.30.02") {
    r.offset(n::BlockSerializationId, 216);
    r.slot(n::BlockAddCollisionShapes, 8);
    r.variant(n::OutlineKind, n::OutlineByBlock);  // getAABB is gone: getOutline takes the Block
    r.drop(n::BlockGetAABB);
    r.slot(n::BlockGetCameraShape, 7);
    r.offset(n::BlockLightDampening, 153);
    r.offset(n::BlockLightEmission, 152);
}

BDG_BINDINGS("1.20.40.01") {
    r.offset(n::BlockSerializationId, 192);
    r.offset(n::BlockDestroySpeed, 128);
    r.offset(n::BlockExplosionResistance, 112);
    r.offset(n::BlockLightDampening, 108);
    r.offset(n::BlockLightEmission, 107);
}

BDG_BINDINGS("1.20.50.03") {
    r.offset(n::BlockSerializationId, 168);
    r.offset(n::BlockTypePermutations, 768);
    r.slot(n::BlockAddCollisionShapes, 7);
    r.slot(n::BlockGetVisualShape, 11);
    r.slot(n::BlockGetUIShape, 12);
    r.slot(n::BlockGetOutline, 9);
    r.slot(n::BlockGetCameraShape, 6);
    r.function(n::SpawnerSpawnItem, "40 53 55 56 57 41 56 41 57 48 81 EC 48 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 84 24 30 01 00 00 49 8B F1");
    r.slot(n::ItemGetMaxDamage, 33);
    r.slot(n::ItemGetMaxStackSize, 85);
}

BDG_BINDINGS("1.20.61.01") {
    r.variant(n::SpawnResourcesKind, n::SpawnRandomizeContext);
    r.offset(n::BlockLightDampening, 109);
    r.offset(n::BlockLightEmission, 108);
    r.function(n::BlockSpawnResources, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 40 48 8B 02 48 8B D9");
    r.slot(n::ItemGetMaxDamage, 34);
    r.slot(n::ItemGetMaxStackSize, 86);
}

BDG_BINDINGS("1.20.71.01") {
    r.drop(n::BlockSpawnResources);
    r.drop(n::SpawnerSpawnItem);
    r.drop(n::BlockSourceSetBlock);
    r.drop(n::SpawnResourcesKind);
    r.slot(n::ItemGetMaxStackSize, 85);
}

BDG_BINDINGS("1.20.80.05") {
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 5E 08 48 89 5C 24 50", 3);
    r.slot(n::ItemGetMaxStackSize, 86);
}

BDG_BINDINGS("1.21.0.03") {
    r.slot(n::ItemGetMaxStackSize, 87);
}

BDG_BINDINGS("1.21.20.03") {
    r.offset(n::BlockTypePermutations, 776);
    r.slot(n::LevelGetDimensionSlot, 4);
    r.drop(n::DimensionBlockSource);  // no PDB from here: the first BlockSource of the Dimension, then its getter
    r.offset(n::BlockTypeTags, 560);
    r.offset(n::BlockDestroySpeed, 136);
    r.offset(n::BlockExplosionResistance, 120);
    r.offset(n::BlockLightDampening, 117);
    r.offset(n::BlockLightEmission, 116);
    r.drop(n::ItemGetMaxStackSize);
    r.slot(n::ItemBuildDescriptionName, 81);
}

BDG_BINDINGS("1.21.30.03") {
    r.drop(n::ItemBuildDescriptionName);  // found by its code (game/ItemAnswers.cpp) where no step binds it
    r.offset(n::BlockSerializationId, 184);
    r.slot(n::LevelGetDimensionSlot, 5);
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 30", 3);
    r.slot(n::ItemGetMaxStackSize, 89);
}

BDG_BINDINGS("1.21.42.01") {
    r.offset(n::BlockTypePermutations, 768);
    r.slot(n::LevelGetDimensionSlot, 4);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 6C 24 18 56 57 41 56 48 83 EC 60 48 8B 05 ? ? ? ? 48 33 C4 48 89 44 24 50 48 8B D9");
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 50", 3);
    r.offset(n::BlockTypeTags, 552);
    r.slot(n::ItemGetMaxDamage, 35);
    r.slot(n::ItemGetMaxStackSize, 90);
}

BDG_BINDINGS("1.21.50.07") {
    r.slot(n::ItemBuildDescriptionName, 85);
    r.offset(n::BlockSerializationId, 264);
    r.offset(n::BlockTypePermutations, 832);
    r.data(n::BlockLookupMap, "48 8B 35 ? ? ? ? 48 8B 7E 08 48 89 7C 24 50 44 89 74 24 58", 3);
    r.offset(n::BlockTypeDescriptionId, 8);
    r.offset(n::BlockTypeTags, 616);
    r.offset(n::BlockDestroySpeed, 216);
    r.offset(n::BlockExplosionResistance, 200);
    r.offset(n::BlockLightDampening, 197);
    r.offset(n::BlockLightEmission, 196);
    r.slot(n::ItemGetMaxStackSize, 91);
}

BDG_BINDINGS("1.21.60.10") {
    r.offset(n::BlockTypePermutations, 728);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D AC 24 D0 FE FF FF 48 81 EC 30 02 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 20 01 00 00 4C 8B F1");
    r.data(n::BlockLookupMap, "4C 8B 2D ? ? ? ? 49 8B 5D 08 48 89 5C 24 40", 3);
    r.offset(n::BlockTypeTags, 512);
    r.drop(n::ItemGetMaxStackSize);
}

BDG_BINDINGS("1.21.70.04") {
    r.drop(n::ItemBuildDescriptionName);
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
    r.offset(n::BlockTypeTags, 504);
}

BDG_BINDINGS("1.21.111.1") {
    r.offset(n::BlockTypePermutations, 712);
    r.slot(n::LevelGetBlockTypeRegistry, 381);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 2F 4C 8B F9 45 33 F6");
    r.offset(n::BlockTypeTags, 496);
    r.offset(n::BlockDestroySpeed, 212);
    r.offset(n::BlockExplosionResistance, 204);
}

BDG_BINDINGS("1.21.120.4") {
    r.offset(n::BlockSerializationId, 272);
    r.slot(n::LevelGetBlockTypeRegistry, 380);
    r.variant(n::SetBlockChanger, n::ChangerContext);
}

BDG_BINDINGS("1.21.124.2") {
    r.slot(n::LevelGetDimensionSlot, 5);
    r.slot(n::ItemGetMaxStackSize, 94);
}

BDG_BINDINGS("1.21.130.3") {
    r.offset(n::BlockSerializationId, 264);
    r.slot(n::LevelGetBlockTypeRegistry, 382);
    r.slot(n::LevelGetDimensionSlot, 4);
    r.function(n::LevelTick, "48 89 5C 24 10 48 89 74 24 18 48 89 7C 24 20 55 41 54 41 55 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45 2F 4C 8B F1");
    r.offset(n::BlockDestroySpeed, 196);
    r.offset(n::BlockExplosionResistance, 188);
    r.offset(n::BlockLightDampening, 181);
    r.offset(n::BlockLightEmission, 180);
    r.slot(n::ItemGetMaxDamage, 36);
    r.drop(n::ItemGetMaxStackSize);
    r.slot(n::ItemBuildDescriptionName, 92);
}

BDG_BINDINGS("1.26.0.2") {
    r.offset(n::BlockSerializationId, 232);
    r.offset(n::BlockTypePermutations, 624);
    r.slot(n::LevelGetBlockTypeRegistry, 386);
    r.slot(n::LevelGetDimensionSlot, 5);
    r.slot(n::DimensionGetBlockSource, 12);
    r.offset(n::BlockTypeTags, 472);
    r.offset(n::BlockDestroySpeed, 172);
    r.offset(n::BlockExplosionResistance, 164);
    r.offset(n::BlockLightDampening, 157);
    r.offset(n::BlockLightEmission, 156);
    r.slot(n::ItemGetMaxStackSize, 100);
}

BDG_BINDINGS("1.26.10.4") {
    r.slot(n::LevelGetBlockTypeRegistry, 388);
    r.slot(n::LevelGetDimensionSlot, 4);
    r.offset(n::BlockDestroySpeed, 164);
    r.offset(n::BlockExplosionResistance, 156);
    r.offset(n::BlockLightDampening, 149);
    r.offset(n::BlockLightEmission, 148);
    r.drop(n::ItemGetMaxStackSize);
}

BDG_BINDINGS("1.26.20.5") {
    r.variant(n::RegistryGetterKind, n::RegistryInRax);  // clang-built server: getBlockTypeRegistry returns in rax
    r.offset(n::BlockSerializationId, 240);
    r.offset(n::BlockTypePermutations, 640);
    r.offset(n::LevelBlockTypeRegistry, 440);
    r.slot(n::LevelGetBlockTypeRegistry, 389);
    r.slot(n::LevelGetDimensionSlot, 6);
    r.function(n::LevelTick, "55 56 57 48 83 EC ? 48 8D 6C 24 ? 48 C7 45 ? FE FF FF FF 48 89 CE 48 8B 01 48 8B 80 38 06 00 00");
    r.offset(n::BlockTypeTags, 488);
    r.offset(n::BlockDestroySpeed, 180);
    r.offset(n::BlockExplosionResistance, 172);
    r.offset(n::BlockLightDampening, 165);
    r.offset(n::BlockLightEmission, 164);
    r.slot(n::ItemGetMaxStackSize, 100);
}

BDG_BINDINGS("1.26.30.5") {
    r.offset(n::BlockTypePermutations, 552);
    r.slot(n::LevelGetBlockTypeRegistry, 390);
    r.slot(n::BlockAddCollisionShapes, 6);
    r.slot(n::BlockGetVisualShape, 10);
    r.slot(n::BlockGetUIShape, 11);
    r.slot(n::BlockGetOutline, 8);
    r.slot(n::BlockGetCameraShape, 5);
    r.offset(n::BlockTypeTags, 416);
    r.offset(n::BlockTypeRequiresCorrectTool, 357 * 8 + 5);
}

BDG_BINDINGS("1.26.40.8") {
    r.function(n::LevelTick, "55 56 57 48 83 EC 70 48 8D 6C 24 70 48 C7 45 F8 FE FF FF FF 48 89 CE 48 8B 01 48 8B 80 48 06 00 00");
    r.offset(n::BlockTypeRequiresCorrectTool, 357 * 8 + 2);
}

BDG_BINDINGS("1.26.51.1") {
    r.slot(n::LevelGetBlockTypeRegistry, 391);
    r.function(n::LevelTick, "55 56 57 53 48 81 EC B8 00 00 00 48 8D AC 24 80 00 00 00 48 C7 45 30 FE FF FF FF 48 89 CF");
}

// previews (core/Version.h): a release of 1.26.60 comes out after them and runs these too
BDG_BINDINGS("1.26.60.21") {
    r.function(n::LevelTick, "55 41 57 41 56 56 57 53 48 81 EC B8 00 00 00 48 8D AC 24 80 00 00 00 48 C7 45 30 FE FF FF FF 48 89 CF 8B 05 ? ? ? ? 8B 0D ? ? ? ? 65 48 8B 14 25 58 00 00 00 48 8B 0C CA 3B 81 04 00 00 00 0F 8F ? ? ? ? 48 8D 0D ? ? ? ? 48 89 4D 10 C6 45 18 00 48 8D 75 20 48 C7 45 28 00 00 00 00 C6 45 08 00 48 8D 5D D0 48 89 F2 49 89 D8 E8 ? ? ? ? 88 45 18 48 8B 07 48 8B 80 ? ? 00 00 48 89 F9 FF 15 ? ? ? ? 48 8D 0D ? ? ? ? 48 89 4D D0 48 89 7D D8");
}

BDG_BINDINGS("1.26.60.25") {
    r.function(n::LevelTick, "55 41 57 41 56 56 57 53 48 81 EC B8 00 00 00 48 8D AC 24 80 00 00 00 48 C7 45 30 FE FF FF FF 48 89 CF 8B 05 ? ? ? ? 8B 0D ? ? ? ? 65 48 8B 14 25 58 00 00 00 48 8B 0C CA 3B 81 04 00 00 00 0F 8F ? ? ? ? 48 8D 0D ? ? ? ? 48 89 4D 10 C6 45 18 00 48 8D 75 20 48 C7 45 28 00 00 00 00 C6 45 08 00 48 8D 5D D0 48 89 F2 49 89 D8 E8 ? ? ? ? 88 45 18 48 8B 07 48 8B 80 ? ? 00 00 48 89 F9 FF 15 ? ? ? ? 90 48 8D 0D ? ? ? ? 48 89 4D D0 48 89 7D D8");  // a nop after each call
}

BDG_BINDINGS("1.26.60.28") {
    r.offset(n::LevelBlockTypeRegistry, 448);  // the Level's members from +0x1B0 on are a qword further
}

BDG_BINDINGS("1.26.60.6") {
    r.function(n::LevelTick, "55 56 57 53 48 81 EC B8 00 00 00 48 8D AC 24 80 00 00 00 48 C7 45 30 FE FF FF FF 48 89 CF 8B 05 ? ? ? ? 8B 0D ? ? ? ? 65 48 8B 14 25 58 00 00 00 48 8B 0C CA 3B 81 04 00 00 00 0F 8F ? ? ? ? 48 8D 0D ? ? ? ? 48 89 4D 10 C6 45 18 00 48 8D 75 20 48 C7 45 28 00 00 00 00 C6 45 08 00 48 8D 5D D0 48 89 F2 49 89 D8 E8 ? ? ? ? 88 45 18 48 8B 07 48 8B 80 ? ? 00 00 48 89 F9 FF 15 ? ? ? ? 90 48 8D 0D ? ? ? ? 48 89 4D D0 48 89 7D D8");  // r15 and r14 no longer saved
}

BDG_BINDINGS("1.26.70.20") {
    r.function(n::LevelTick, "55 41 57 41 56 56 57 53 48 81 EC B8 00 00 00 48 8D AC 24 80 00 00 00 48 C7 45 30 FE FF FF FF 48 89 CF 8B 05 ? ? ? ? 8B 0D ? ? ? ? 65 48 8B 14 25 58 00 00 00 48 8B 0C CA 3B 81 04 00 00 00 0F 8F ? ? ? ? 48 8D 0D ? ? ? ? 48 89 4D 10 C6 45 18 00 48 8D 75 20 48 C7 45 28 00 00 00 00 C6 45 08 00 48 8D 5D D0 48 89 F2 49 89 D8 E8 ? ? ? ? 88 45 18 48 8B 07 48 8B 80 ? ? 00 00 48 89 F9 FF 15 ? ? ? ? 90 48 8D 0D ? ? ? ? 48 89 4D D0 48 89 7D D8");  // 1.26.60.25's: r15 and r14 saved
    // Item has a virtual fewer, before slot 15
    r.slot(n::ItemGetMaxDamage, 35);
    r.slot(n::ItemGetMaxStackSize, 99);
    r.slot(n::ItemBuildDescriptionName, 91);
    r.drop(n::BlockTypeRequiresCorrectTool);  // one bit fits
}

// clang-format on
