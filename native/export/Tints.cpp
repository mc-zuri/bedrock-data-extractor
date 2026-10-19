#include "export/Exporter.h"

#include <format>

namespace bdg::exporter {

using namespace nbt;

Result exportTints(Context const& context) {
    game::Biomes const biomes = game::biomes(context.world);
    // the Biomes are found among what the game keeps in memory, where a stale copy can stand in for a live one
    if (!biomes.error.empty()) {
        return {false, biomes.error, true};
    }
    game::Tints const tints = game::tints(biomes, context.blockTypes);
    if (!tints.error.empty()) {
        return {false, tints.error};
    }
    Buffer out;
    out.header(Compound, "");
    out.header(String, "route");
    out.str(tints.route);
    out.header(List, "biomes");
    out.list(Compound, tints.biomes.size());
    for (game::BiomeTint const& b : tints.biomes) {
        out.header(String, "name");
        out.str(b.name);
        out.header(Int, "grass");
        out.i32(b.grass);
        out.header(Int, "foliage");
        out.i32(b.foliage);
        out.header(Int, "water");
        out.i32(b.water);
        out.u8(End);
    }
    out.header(List, "redstone");
    out.list(Int, tints.redstone.size());
    for (int colour : tints.redstone) {
        out.i32(colour);
    }
    out.header(List, "constant");
    out.list(Compound, tints.constant.size());
    for (game::BlockTint const& t : tints.constant) {
        out.header(String, "name");
        out.str(t.name);
        out.header(Int, "color");
        out.i32(t.color);
        out.u8(End);
    }
    out.u8(End);
    if (!writeFile(context, "tints.nbt", out.data)) {
        return {false, "could not write tints.nbt"};
    }
    return {true, std::format("{} biomes, {} redstone states, {} blocks of one colour ({})", tints.biomes.size(), tints.redstone.size(), tints.constant.size(), tints.route)};
}

}
