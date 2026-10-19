#pragma once
// The names the game gives some of its enums, read from the tables it keeps them in (Enums.cpp): found by content, the
// same on every build.

#include <string>
#include <vector>

namespace bdg::game {

struct EnumName {
    int         id;
    std::string name;
};

struct EnumNames {
    std::vector<EnumName> entries;  // by id
    std::string           error;    // why the table was not found; then there are no entries
};

// LevelSoundEventMap: every LevelSoundEvent's name ("item.use.on" 0, "hit" 1, ... "undefined" last), the ids with gaps
EnumNames soundEvents();

// ParticleTypeMap: every ParticleType's name ("bubble" 1, ...), the ids with gaps
EnumNames particleTypes();

// Container::containerTypeMap: the ContainerTypes the game names ("INVENTORY" -1, "CONTAINER" 0, ...)
EnumNames containerTypes();

// The note block instruments' sounds by instrument ("harp" or "note.harp" 0, "bd" or "note.bd" 1, ...); no entries
// and no error where the build keeps no such table (1.21.50 to 1.26.20, and Linux 1.26.30)
EnumNames noteInstruments();

// ContainerCollectionNameMap: the names of the containers an item stack request names its slots by ("hotbar_and_inventory",
// "crafting_input", "cursor", ...; 1.16.201's "combined_hotbar_and_inventory_items", "crafting_input_items", ...), by
// ContainerEnumName
EnumNames containerNames();

// The map decoration types exploration maps mark, by type, each named by the feature the map leads to ("monument" 15,
// "mansion" 14, ...; "minecraft:monument" from 1.21.20): ExplorationMapRegistry's entries in its order (from 1.21.20
// built by calling ExplorationMapRegistry::_getRegistry), before 1.20.40 the exploration map loot function's cases.
EnumNames explorationMarkers();

// MapDecoration::Type: the map decorations' names by type ("MarkerWhite" 0, "MarkerGreen" 1, ...), as the game's
// reflection registers them (from 1.26.40); no entries and no error where the build registers none
EnumNames mapDecorations();

}
