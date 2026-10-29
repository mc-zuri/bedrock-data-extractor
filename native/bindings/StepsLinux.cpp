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
    r.offsetOf(n::BlockTypeDescriptionId, "_ZNK11BlockLegacy16getDescriptionIdB5cxx11Ev");
    r.offsetOf(n::BlockTypeDestroySpeed, "_ZNK11BlockLegacy15getDestroySpeedEv");
    r.offsetOf(n::BlockTypeExplosionResistance, "_ZNK11BlockLegacy22getExplosionResistanceEP5Actor");
    r.offsetOf(n::BlockTypeLightDampening, "_ZNK11BlockLegacy8getLightEv");
    r.offsetOf(n::BlockTypeLightEmission, "_ZNK11BlockLegacy16getLightEmissionERK5Block");
    r.slotOf(n::BlockCanContainLiquid, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy16canContainLiquidEv");
    r.symbol(n::BlockSpawnResources, "_ZNK5Block14spawnResourcesER11BlockSourceRK8BlockPosRKS_PSt6vectorIPK4ItemSaISA_EEfib");
    r.symbol(n::SpawnerSpawnItem, "_ZN7Spawner9spawnItemER11BlockSourceRK9ItemStackP5ActorRK4Vec3i");
    r.symbol(n::BlockSourceSetBlockAt, "_ZN11BlockSource8setBlockERK8BlockPosRK5BlockiPK21ActorBlockSyncMessage");
    r.slotOf(n::BlockOnRemove, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy8onRemoveER11BlockSourceRK8BlockPos");
    r.slotOf(n::ItemGetDestroySpeed, "_ZTV4Item", "_ZNK4Item15getDestroySpeedERK13ItemStackBaseRK5Block");
    r.slotOf(n::ItemBuildDescriptionName, "_ZTV4Item", "_ZNK4Item20buildDescriptionNameB5cxx11ERK13ItemStackBase");
    r.slotOf(n::ItemCanDestroySpecial, "_ZTV4Item", "_ZNK4Item17canDestroySpecialERK5Block");
    r.symbol(n::BlockGetMapColor, "_ZNK5Block11getMapColorER11BlockSourceRK8BlockPos");
    r.slotOf(n::BlockGetMapColorSlot, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy11getMapColorER11BlockSourceRK8BlockPos");
    r.slotOf(n::ItemGetMaxDamage, "_ZTV4Item", "_ZNK4Item12getMaxDamageEv");
    r.slotOf(n::ItemGetMaxStackSize, "_ZTV4Item", "_ZNK4Item15getMaxStackSizeERK14ItemDescriptor");
    r.slotOf(n::ItemGetEnchantSlot, "_ZTV4Item", "_ZNK4Item14getEnchantSlotEv");
}

void randomSlots(bdg::bindings::Registry& r) {
    r.slotOf(n::RandomNextFloat, "_ZTV6Random", "_ZN6Random9nextFloatEv");
    r.slotOf(n::RandomNextDouble, "_ZTV6Random", "_ZN6Random10nextDoubleEv");
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
    r.variant(n::SpawnResourcesKind, n::SpawnBlockLevelRandom);
    r.offsetOf(n::LevelRandomSeed, "_ZNK5Level9getRandomEv");  // the Random has no vtable yet: its twister's seed first
    bySymbols(r);
}

// Level::getDimension is not virtual before (Level's first base is BlockSourceListener)
BDG_BINDINGS("1.16.210.05") {
    r.slotOf(n::LevelGetDimensionSlot, "_ZTV5Level", "_ZNK5Level12getDimensionE11AutomaticIDI9DimensioniE");
}

BDG_BINDINGS("1.16.220.02") {
    r.offsetOf(n::LevelRandomSeed, "_ZNK5Level9getRandomEv", 16);  // the Random's vtable, then its Core::Random's, then the seed
    randomSlots(r);
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
    r.variant(n::SpawnResourcesKind, n::SpawnBlockLevelRandomTool);
    r.drop(n::LevelRandomSeed);
    r.function(n::BlockSpawnResources, "55 41 57 41 56 41 55 41 54 53 48 83 EC 28 44 89 4C 24 14 F3 0F 11 44 24 10");
    r.function(n::SpawnerSpawnItem, "55 41 57 41 56 41 55 41 54 53 48 81 EC D8 00 00 00 44 89 4C 24 1C");
    r.function(n::BlockSourceSetBlockAt, "55 53 48 83 EC 18 4C 89 C0 0F 57 C0");
    r.offset(n::BlockTypeDescriptionId, 0x8);
    r.offset(n::BlockTypeDestroySpeed, 0x12C);
    r.offset(n::BlockTypeExplosionResistance, 0x130);
    r.offset(n::BlockTypeLightDampening, 0x121);
    r.offset(n::BlockTypeLightEmission, 0x122);
    r.slot(n::RandomNextFloat, 6);
    r.slot(n::RandomNextDouble, 7);
    r.slot(n::BlockOnRemove, 162);
    // found by their code, as on Windows
    r.drop(n::ItemGetDestroySpeed);
    r.drop(n::ItemCanDestroySpecial);
    r.drop(n::ItemBuildDescriptionName);
    // declared before the destructor: Windows' own (1.16.220's, as here), which no code tells apart from its siblings
    r.slot(n::BlockCanContainLiquid, 52);
    r.drop(n::BlockGetMapColor);
    r.drop(n::BlockGetMapColorSlot);
    r.slot(n::ItemGetMaxDamage, 40);
    r.slot(n::ItemGetMaxStackSize, 93);
    r.drop(n::ItemGetEnchantSlot);
}

BDG_BINDINGS("1.17.10.04") {
    bySymbols(r);
    r.slotOf(n::LevelGetDimensionSlot, "_ZTV5Level", "_ZNK5Level12getDimensionE11AutomaticIDI9DimensioniE");
    randomSlots(r);
    r.variant(n::SpawnResourcesKind, n::SpawnBlockRandomizeLootTool);
    r.symbol(n::BlockSpawnResources, "_ZNK5Block14spawnResourcesER11BlockSourceRK8BlockPosRKS_R9RandomizePSt6vectorIPK4ItemSaISC_EEfiRK9ItemStack");
}

BDG_BINDINGS("1.17.30.04") {
    r.variant(n::AabbLayout, n::Aabb24);
    r.slotOf(n::BlockAddCollisionShapes, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy18addCollisionShapesERK5BlockR11BlockSourceRK8BlockPosPK4AABBRSt6vectorIS8_SaIS8_EE12optional_refI19IActorMovementProxyE");
    r.slotOf(n::BlockGetAABB, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy7getAABBERK11BlockSourceRK8BlockPosRK5BlockR4AABBb");
}

BDG_BINDINGS("1.18.11.01") {
    r.symbol(n::DimensionBlockSourceGetter, "_ZNK9Dimension33getBlockSourceFromMainChunkSourceEv");
    r.drop(n::DimensionBlockSource);
    r.drop(n::BlockOnRemove);  // the skull spawns its item in spawnResources now
}

BDG_BINDINGS("1.18.30.04") {
    r.variant(n::BlockMapKey, n::MapKeyHashedString);
    r.symbol(n::BlockLookupMap, "_ZN17BlockTypeRegistry15mBlockLookupMapE");
    r.slotOf(n::BlockGetAABB, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy7getAABBERK17IConstBlockSourceRK8BlockPosRK5BlockR4AABBb");
    r.slotOf(n::BlockAddCollisionShapes, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy18addCollisionShapesERK5BlockR11BlockSourceRK8BlockPosPK4AABBRSt6vectorIS8_SaIS8_EE12optional_refIK26GetCollisionShapeInterfaceE");
    r.symbol(n::BlockSourceSetBlockAt, "_ZN11BlockSource8setBlockERK8BlockPosRK5BlockiPK21ActorBlockSyncMessageP5Actor");
}

BDG_BINDINGS("1.19.1.01") {
    r.slotOf(n::BlockAddCollisionShapes, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy18addCollisionShapesERK5BlockRK11BlockSourceRK8BlockPosPK4AABBRSt6vectorIS9_SaIS9_EE12optional_refIK26GetCollisionShapeInterfaceE");
    r.offsetOf(n::BlockDestroySpeed, "_ZNK5Block15getDestroySpeedEv");
    r.offsetOf(n::BlockLightDampening, "_ZNK5Block8getLightEv");
    r.offsetOf(n::BlockLightEmission, "_ZNK5Block16getLightEmissionEv");
    r.drop(n::BlockTypeDestroySpeed);
    r.drop(n::BlockTypeLightDampening);
    r.drop(n::BlockTypeLightEmission);
}

BDG_BINDINGS("1.19.40.02") {
    r.variant(n::SpawnResourcesKind, n::SpawnBlockRandomizeTool);
    r.symbol(n::BlockSpawnResources, "_ZNK5Block14spawnResourcesER11BlockSourceRK8BlockPosRKS_R9RandomizePSt6vectorIPK4ItemSaISC_EEfRK9ItemStack");
}

BDG_BINDINGS("1.19.50.02") {
    r.variant(n::DimensionKind, n::DimensionByWeakRef);
    r.symbol(n::LevelGetOrCreateDimension, "_ZN5Level20getOrCreateDimensionE11AutomaticIDI9DimensioniE");
    r.drop(n::LevelCreateDimension);
    r.variant(n::SpawnResourcesKind, n::SpawnRandomizeVectorTool);
    r.symbol(n::BlockSpawnResources, "_ZNK5Block14spawnResourcesER11BlockSourceRK8BlockPosR9RandomizePSt6vectorIPK4ItemSaISA_EEfRK9ItemStack");
    r.slotOf(n::BlockGetMapColorSlot, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy11getMapColorER11BlockSourceRK8BlockPosRK5Block");
}

BDG_BINDINGS("1.19.60.04") {
    r.variant(n::SpawnResourcesKind, n::SpawnRandomizeTool);
    r.symbol(n::BlockSpawnResources, "_ZNK5Block14spawnResourcesER11BlockSourceRK8BlockPosR9RandomizefRK9ItemStack");
    r.offsetOf(n::BlockTypeExplosionResistance, "_ZNK11BlockLegacy22getExplosionResistanceEv");
}

BDG_BINDINGS("1.20.11.01") {
    r.variant(n::VisualShapeKind, n::VisualShapeBlockBuffer);  // getVisualShape lost its trailing bool
    r.slotOf(n::BlockGetVisualShape, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy14getVisualShapeERK5BlockR4AABB");
    r.offsetOf(n::BlockExplosionResistance, "_ZNK5Block22getExplosionResistanceEv");
    r.drop(n::BlockTypeExplosionResistance);
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
    r.offsetOf(n::BlockTypeDescriptionId, "_ZNK11BlockLegacy16getDescriptionIdEv");
    r.slotOf(n::ItemBuildDescriptionName, "_ZTV4Item", "_ZNK4Item20buildDescriptionNameERK13ItemStackBase");
}

BDG_BINDINGS("1.20.61.01") {
    r.slotOf(n::BlockAddCollisionShapes, "_ZTV11BlockLegacy", "_ZNK11BlockLegacy18addCollisionShapesERK5BlockRK17IConstBlockSourceRK8BlockPosPK4AABBRNSt3__16vectorIS9_NSC_9allocatorIS9_EEEE12optional_refIK26GetCollisionShapeInterfaceE");
    r.variant(n::SpawnResourcesKind, n::SpawnRandomizeContext);
    r.symbol(n::BlockSpawnResources, "_ZNK5Block14spawnResourcesER11BlockSourceRK8BlockPosR9RandomizeRK20ResourceDropsContext");
}

BDG_BINDINGS("1.20.71.01") {
    // the drops are each block type's drop strategy's from here (game/Drops.cpp)
    r.drop(n::BlockSpawnResources);
    r.drop(n::SpawnerSpawnItem);
    r.drop(n::SpawnResourcesKind);
    r.offsetIn(n::BlockTypeDropStrategy, "_ZN11BlockLegacy24setResourceDropsStrategyENSt3__110unique_ptrI22IResourceDropsStrategyNS0_14default_deleteIS2_EEEE", "48 89 88");
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
    r.drop(n::BlockSourceSetBlockAt);
    r.drop(n::BlockTypeDescriptionId);
    r.drop(n::BlockDestroySpeed);
    r.drop(n::BlockExplosionResistance);
    r.drop(n::BlockLightEmission);
    r.drop(n::BlockLightDampening);
    r.drop(n::BlockTypeDropStrategy);
    r.drop(n::ItemGetDestroySpeed);
    r.slot(n::ItemBuildDescriptionName, 82);
    r.drop(n::ItemCanDestroySpecial);
    r.drop(n::BlockCanContainLiquid);
    r.drop(n::BlockGetMapColor);
    r.drop(n::BlockGetMapColorSlot);
    r.slot(n::RandomNextFloat, 6);
    r.slot(n::RandomNextDouble, 7);
    r.slot(n::BlockSourceSetBlockSlot, 31);
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 78 05 00 00");
    r.slot(n::LevelGetDimensionSlot, 6);
    r.slot(n::BlockAddCollisionShapes, 7);
    // as 1.21.2's symbols place them, and Windows' from 1.20.50
    r.slot(n::BlockGetVisualShape, 11);
    r.slot(n::BlockGetUIShape, 12);
    r.slot(n::BlockGetOutline, 9);
    r.slot(n::BlockGetCameraShape, 6);
    r.drop(n::ItemGetEnchantSlot);
    r.slot(n::ItemGetMaxDamage, 35);
    r.drop(n::ItemGetMaxStackSize);
}

BDG_BINDINGS("1.21.30.03") {
    r.slot(n::ItemGetMaxStackSize, 90);
    r.drop(n::ItemBuildDescriptionName);  // found by its code (game/ItemAnswers.cpp) where no step binds it
}

BDG_BINDINGS("1.21.42.01") {
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 F8 05 00 00");
    r.slot(n::ItemGetMaxDamage, 36);
    r.slot(n::ItemGetMaxStackSize, 91);
}

BDG_BINDINGS("1.21.50.10") {
    r.slot(n::ItemBuildDescriptionName, 86);
    r.slot(n::BlockSourceSetBlockSlot, 29);
    r.slot(n::ItemGetMaxStackSize, 92);
}

BDG_BINDINGS("1.21.60.10") {
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 00 06 00 00");
    r.slot(n::BlockSourceSetBlockSlot, 30);
    r.drop(n::ItemGetMaxStackSize);
}

BDG_BINDINGS("1.21.70.04") {
    r.drop(n::ItemBuildDescriptionName);
    r.slot(n::BlockSourceSetBlockSlot, 31);
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
    r.slot(n::BlockSourceSetBlockSlot, 30);
}

BDG_BINDINGS("1.21.120.4") {
    r.function(n::LevelTick, "53 48 83 EC 10 48 89 FB 48 8B 07 FF 90 18 06 00 00");
    r.slot(n::LevelGetDimensionSlot, 5);
    r.slot(n::BlockSourceSetBlockSlot, 31);
    r.variant(n::SetBlockChanger, n::ChangerContext);
}

BDG_BINDINGS("1.21.124.2") {
    r.slot(n::ItemGetMaxStackSize, 95);
}

BDG_BINDINGS("1.21.130.3") {
    r.slot(n::BlockSourceSetBlockSlot, 30);
    r.slot(n::ItemGetMaxDamage, 37);
    r.drop(n::ItemGetMaxStackSize);
    r.slot(n::ItemBuildDescriptionName, 93);
}

BDG_BINDINGS("1.26.0.2") {
    r.function(n::LevelTick, "41 56 53 48 83 EC 38 48 89 FB 48 8B 07 FF 90 28 06 00 00");
    r.variant(n::RandomizeAlive, n::AliveIsOne);
    r.slot(n::ItemGetMaxStackSize, 101);
}

BDG_BINDINGS("1.26.10.4") {
    r.function(n::LevelTick, "41 56 53 48 83 EC 38 48 89 FB 48 8B 07 FF 90 38 06 00 00");
    r.slot(n::LevelGetDimensionSlot, 6);
    r.drop(n::ItemGetMaxStackSize);
}

BDG_BINDINGS("1.26.20.5") {
    r.function(n::LevelTick, "41 56 53 48 83 EC 38 48 89 FB 48 8B 07 FF 90 40 06 00 00");
    r.slot(n::LevelGetDimensionSlot, 7);
    r.slot(n::ItemGetMaxStackSize, 101);
}

BDG_BINDINGS("1.26.30.5") {
    r.slot(n::BlockAddCollisionShapes, 6);
    r.slot(n::BlockGetVisualShape, 10);
    r.slot(n::BlockGetUIShape, 11);
    r.slot(n::BlockGetOutline, 8);
    r.slot(n::BlockGetCameraShape, 5);
    r.slot(n::BlockSourceSetBlockSlot, 31);
}

BDG_BINDINGS("1.26.40.8") {
    r.function(n::LevelTick, "41 56 53 48 83 EC 38 48 89 FB 48 8B 07 FF 90 50 06 00 00");
}

BDG_BINDINGS("1.26.51.1") {
    r.function(n::LevelTick, "55 41 57 41 56 53 48 83 EC 58 49 89 FE 0F B6 05 ? ? ? ?");
}

// previews (core/Version.h): a release of 1.26.70 comes out after them and runs this too
BDG_BINDINGS("1.26.70.20") {
    // Item has a virtual fewer, before slot 15
    r.slot(n::ItemGetMaxDamage, 36);
    r.slot(n::ItemGetMaxStackSize, 100);
    r.slot(n::ItemBuildDescriptionName, 92);
}

// clang-format on
