#include "export/Exporter.h"

#include <format>

namespace bdg::exporter {

using namespace nbt;

Result exportEffects(Context const& context) {
    game::Effects const found = game::mobEffects();
    if (!found.error.empty()) {
        return {false, found.error};
    }
    Buffer out;
    out.header(Compound, "");
    out.header(List, "effects");
    out.list(Compound, found.effects.size());
    for (game::Effect const& effect : found.effects) {
        out.header(Int, "id");
        out.i32(effect.id);
        out.header(String, "name");
        out.str(effect.name);
        out.header(String, "descriptionId");
        out.str(effect.descriptionId);
        out.header(Byte, "harmful");
        out.u8(effect.harmful);
        out.u8(End);
    }
    out.u8(End);
    if (!writeFile(context, "effects.nbt", out.data)) {
        return {false, "could not write effects.nbt"};
    }
    return {true, std::format("{} effects", found.effects.size())};
}

}
