#include "export/Exporter.h"

#include <format>

namespace bdg::exporter {

using namespace nbt;

Result exportEnchantments(Context const& context) {
    game::Enchants const found = game::enchants();
    if (!found.error.empty()) {
        return {false, found.error};
    }
    Buffer out;
    out.header(Compound, "");
    out.header(List, "enchantments");
    out.list(Compound, found.enchants.size());
    auto ints = [&](std::string_view name, std::vector<int> const& values) {
        out.header(List, name);
        out.list(Int, values.size());
        for (int value : values) {
            out.i32(value);
        }
    };
    for (game::Enchant const& enchant : found.enchants) {
        out.header(Int, "id");
        out.i32(enchant.id);
        out.header(String, "name");
        out.str(enchant.name);
        out.header(String, "descriptionId");
        out.str(enchant.descriptionId);
        out.header(Int, "frequency");
        out.i32(enchant.frequency);
        out.header(Byte, "tradeable");
        out.u8(enchant.tradeable);
        out.header(Int, "primarySlots");
        out.i32(enchant.primarySlots);
        out.header(Int, "secondarySlots");
        out.i32(enchant.secondarySlots);
        out.header(Int, "minLevel");
        out.i32(enchant.minLevel);
        out.header(Int, "maxLevel");
        out.i32(enchant.maxLevel);
        ints("minCost", enchant.minCost);
        ints("maxCost", enchant.maxCost);
        ints("incompatible", enchant.incompatible);
        out.header(Byte, "treasureOnly");
        out.u8(enchant.treasureOnly);
        out.header(Byte, "discoverable");
        out.u8(enchant.discoverable);
        out.u8(End);
    }
    out.u8(End);
    if (!writeFile(context, "enchantments.nbt", out.data)) {
        return {false, "could not write enchantments.nbt"};
    }
    return {true, std::format("{} enchantments", found.enchants.size())};
}

}
