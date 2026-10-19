#include "export/Exporter.h"

#include <format>

namespace bdg::exporter {

using namespace nbt;

namespace {

void writeList(Buffer& out, char const* name, std::vector<game::EnumName> const& entries) {
    out.header(List, name);
    out.list(Compound, entries.size());
    for (game::EnumName const& e : entries) {
        out.header(Int, "id");
        out.i32(e.id);
        out.header(String, "name");
        out.str(e.name);
        out.u8(End);
    }
}

}

Result exportEnums(Context const& context) {
    game::EnumNames const sounds = game::soundEvents(), particles = game::particleTypes(), containers = game::containerTypes(), instruments = game::noteInstruments(),
                          decorations = game::mapDecorations(), containerNames = game::containerNames(), explorationMarkers = game::explorationMarkers();
    for (game::EnumNames const* names : {&sounds, &particles, &containers, &instruments, &decorations, &containerNames, &explorationMarkers}) {
        if (!names->error.empty()) {
            return {false, names->error};
        }
    }
    Buffer out;
    out.header(Compound, "");
    writeList(out, "sounds", sounds.entries);
    writeList(out, "particles", particles.entries);
    writeList(out, "containers", containers.entries);
    writeList(out, "instruments", instruments.entries);
    writeList(out, "mapDecorations", decorations.entries);
    writeList(out, "containerNames", containerNames.entries);
    writeList(out, "explorationMarkers", explorationMarkers.entries);
    out.u8(End);
    if (!writeFile(context, "enums.nbt", out.data)) {
        return {false, "could not write enums.nbt"};
    }
    return {true, std::format("{} sound events, {} particle types, {} container types, {} instruments, {} map decorations, {} container names", sounds.entries.size(),
                              particles.entries.size(), containers.entries.size(), instruments.entries.size(), decorations.entries.size(), containerNames.entries.size())};
}

}
