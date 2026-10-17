#include "export/Exporter.h"

#include <format>

namespace bdg::exporter {

using namespace nbt;

Result exportPalette(Context const& context) {
    std::size_t const states = game::stateCount(context.blockTypes);
    Buffer            palette;
    palette.header(Compound, "");
    palette.header(List, "blocks");
    palette.list(Compound, states);
    for (auto const& type : context.blockTypes) {
        for (std::uintptr_t state : type.states) {
            game::writeStateTag(state, palette);
        }
    }
    palette.u8(End);
    if (!writeFile(context, "block_palette.nbt", palette.data)) {
        return {false, "could not write block_palette.nbt"};
    }
    return {true, std::format("{} states", states)};
}

}
