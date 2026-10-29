#include "export/Exporter.h"

#include "game/Dig.h"
#include "game/Drops.h"

#include <format>

namespace bdg::exporter {

using namespace nbt;

namespace {

void writeFacts(Buffer& out, game::StateFacts const& facts) {
    out.header(Int, "data");
    out.i32(facts.data);
    out.header(Float, "hardness");
    out.f32(facts.hardness);
    out.header(Float, "explosionResistance");
    out.f32(facts.explosionResistance);
    out.header(Float, "friction");
    out.f32(facts.friction);
    out.header(Int, "flameOdds");
    out.i32(facts.flameOdds);
    out.header(Int, "burnOdds");
    out.i32(facts.burnOdds);
    out.header(Int, "lightEmission");
    out.i32(facts.lightEmission);
    out.header(Int, "lightDampening");
    out.i32(facts.lightDampening);
    out.header(Byte, "canContainLiquid");
    out.u8(facts.canContainLiquid);
}

void writeDig(Buffer& out, game::DigAnswer const& dig) {
    out.header(List, "destroySpeeds");
    out.list(Compound, dig.speeds.size());
    for (auto const& [item, speed] : dig.speeds) {
        out.header(String, "item");
        out.str(item);
        out.header(Float, "speed");
        out.f32(speed);
        out.u8(End);
    }
    out.header(List, "harvestedBy");
    out.list(String, dig.harvest.size());
    for (std::string const& item : dig.harvest) {
        out.str(item);
    }
}

void writeDrops(Buffer& out, game::StateDrops const& state) {
    out.header(Int, "samples");
    out.i32(state.samples);
    out.header(List, "drops");
    out.list(Compound, state.drops.size());
    for (game::Drop const& drop : state.drops) {
        out.header(String, "item");
        out.str(drop.item);
        out.header(Int, "aux");
        out.i32(drop.aux);
        out.header(Int, "data");
        out.i32(drop.data);
        out.header(Int, "minCount");
        out.i32(drop.minCount);
        out.header(Int, "maxCount");
        out.i32(drop.maxCount);
        out.header(Int, "hits");
        out.i32(drop.hits);
        out.u8(End);
    }
}

}

Result exportBlocks(Context const& context) {
    auto const facts = game::blockFacts(context.blockTypes);
    if (!facts.error.empty()) {
        return {false, "block facts: " + facts.error};
    }
    // drops first: dig asks with stacks of one, which carry the vtable of the stacks the drop code returns
    auto const drops = game::drops(context.world, context.items, context.blockTypes);
    if (!drops.error.empty()) {
        return {false, "drops: " + drops.error, drops.notLoaded};
    }
    auto const dig = game::dig(context.items, context.blockTypes);
    if (!dig.error.empty()) {
        return {false, "dig: " + dig.error};
    }
    Buffer out;
    out.header(Compound, "");
    out.header(List, "blocks");
    out.list(Compound, facts.types.size());
    for (std::size_t i = 0; i < facts.types.size(); ++i) {
        game::BlockType const&  type = context.blockTypes[i];
        game::BlockFacts const& f    = facts.types[i];
        out.header(String, "name");
        out.str(type.name);
        out.header(Int, "id");
        out.i32(f.id);
        out.header(Int, "defaultState");
        out.i32(static_cast<std::int32_t>(type.defaultState));
        out.header(String, "descriptionId");
        out.str(f.descriptionId);
        out.header(Byte, "requiresCorrectTool");
        out.u8(f.requiresCorrectTool);
        out.header(List, "tags");
        out.list(String, f.tags.size());
        for (std::string const& tag : f.tags) {
            out.str(tag);
        }
        out.header(Float, "translucency");
        out.f32(f.translucency);
        out.header(List, "states");
        out.list(Compound, type.states.size());
        for (std::size_t s = 0; s < type.states.size(); ++s) {
            writeFacts(out, f.states[s]);
            writeDig(out, dig.types[i][s]);
            writeDrops(out, drops.types[i][s]);
            game::MapColor const& color = drops.mapColors[i][s];
            out.header(List, "mapColor");
            out.list(Float, 4);
            for (float c : {color.r, color.g, color.b, color.a}) {
                out.f32(c);
            }
            out.u8(End);
        }
        out.u8(End);
    }
    out.u8(End);
    if (!writeFile(context, "blocks.nbt", out.data)) {
        return {false, "could not write blocks.nbt"};
    }
    return {true, std::format("facts, dig and drops of {} block types", facts.types.size())};
}

Result exportItems(Context const& context) {
    auto const answers = game::itemAnswers(context.items, context.blockTypes);
    if (!answers.error.empty()) {
        return {false, "items: " + answers.error};
    }
    auto const& types = context.items.types;
    auto const  foods = game::foods(context.items);
    Buffer      out;
    out.header(Compound, "");
    out.header(List, "items");
    out.list(Compound, types.size());
    for (game::ItemType const& item : types) {
        out.header(String, "name");
        out.str(item.name);
        out.header(Int, "id");
        out.i32(item.id);
        out.header(String, "descriptionId");
        out.str(item.descriptionId);
        out.header(String, "descriptionName");
        out.str(answers.names.at(item.name));
        out.header(Int, "maxStackSize");
        out.i32(item.maxStackSize);
        out.header(Int, "maxDamage");
        out.i32(item.maxDamage);
        out.header(Int, "enchantSlots");
        out.i32(answers.enchantSlots.at(item.name));
        out.header(List, "tags");
        out.list(String, item.tags.size());
        for (std::string const& tag : item.tags) {
            out.str(tag);
        }
        auto const repair = answers.repairItems.find(item.name);
        out.header(List, "repairItems");
        out.list(String, repair == answers.repairItems.end() ? 0 : repair->second.size());
        if (repair != answers.repairItems.end()) {
            for (std::string const& with : repair->second) {
                out.str(with);
            }
        }
        if (auto const food = foods.find(item.name); food != foods.end()) {
            out.header(Int, "nutrition");
            out.i32(food->second.nutrition);
            out.header(Float, "saturationModifier");
            out.f32(food->second.saturationModifier);
        }
        out.u8(End);
    }
    out.header(List, "aliases");
    out.list(Compound, context.items.aliases.size());
    for (auto const& [name, item] : context.items.aliases) {
        out.header(String, "name");
        out.str(name);
        out.header(String, "item");
        out.str(item);
        out.u8(End);
    }
    out.header(List, "complexAliases");
    out.list(Compound, context.items.complexAliases.size());
    for (game::ComplexAlias const& alias : context.items.complexAliases) {
        out.header(String, "name");
        out.str(alias.name);
        out.header(List, "items");
        out.list(String, alias.items.size());
        for (std::string const& name : alias.items) {
            out.str(name);
        }
        out.u8(End);
    }
    out.u8(End);
    if (!writeFile(context, "items.nbt", out.data)) {
        return {false, "could not write items.nbt"};
    }
    return {true, std::format("{} items ({} repairable), {} aliases, {} complex aliases", types.size(), answers.repairItems.size(), context.items.aliases.size(),
                              context.items.complexAliases.size())};
}

}
