#include "export/Exporter.h"

#include <format>

namespace bdg::exporter {

using namespace nbt;

Result exportBiomes(Context const& context) {
    game::Biomes const found = game::biomes(context.world);
    // the Biomes are found among what the game keeps in memory, where a stale copy can stand in for a live one
    if (!found.error.empty()) {
        return {false, found.error, true};
    }
    Buffer out;
    out.header(Compound, "");
    out.header(List, "biomes");
    out.list(Compound, found.biomes.size());
    for (game::Biome const& biome : found.biomes) {
        out.header(String, "name");
        out.str(biome.name);
        out.header(Int, "id");
        out.i32(biome.id);
        out.u8(End);
    }
    out.u8(End);
    if (!writeFile(context, "biomes.nbt", out.data)) {
        return {false, "could not write biomes.nbt"};
    }
    return {true, std::format("{} biomes", found.biomes.size())};
}

}
