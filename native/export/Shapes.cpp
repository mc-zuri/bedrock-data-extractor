#include "export/Exporter.h"

#include <format>

namespace bdg::exporter {

using namespace nbt;

namespace {

void writeBox(Buffer& out, game::AABB const& box) {
    out.list(Float, 6);
    for (float f : {box.minX, box.minY, box.minZ, box.maxX, box.maxY, box.maxZ}) {
        out.f32(f);
    }
}

void writeBoxes(Buffer& out, std::vector<game::AABB> const& boxes) {
    out.list(List, boxes.size());  // a list of lists even when empty, as prismarine-nbt writes it
    for (game::AABB const& box : boxes) {
        writeBox(out, box);
    }
}

}

Result exportShapes(Context const& context) {
    std::uintptr_t const inherited = game::inheritedOutline(context.blockTypes);
    bool const           camera    = game::hasCameraShape();
    Buffer               shapes;
    shapes.header(Compound, "");
    shapes.header(List, "blocks");
    shapes.list(Compound, context.blockTypes.size());
    for (auto const& type : context.blockTypes) {
        shapes.header(String, "name");
        shapes.str(type.name);
        shapes.header(List, "states");
        shapes.list(Compound, type.states.size());
        for (std::size_t i = 0; i < type.states.size(); ++i) {
            // a missing shape would be a wrong answer
            auto collision = game::collisionShape(context.world, type, type.states[i]);
            if (!collision) {
                return {false, std::format("no collision shape for {} #{}", type.name, i)};
            }
            auto visual = game::visualShape(type, type.states[i]);
            if (!visual) {
                return {false, std::format("no visual shape for {} #{}", type.name, i)};
            }
            auto ui = game::uiShape(type, type.states[i]);
            if (!ui) {
                return {false, std::format("no UI shape for {} #{}", type.name, i)};
            }
            auto outline = game::outlineShape(context.world, type, type.states[i], inherited);
            if (!outline) {
                return {false, std::format("no outline for {} #{}", type.name, i)};
            }
            shapes.header(List, "collision");
            writeBoxes(shapes, *collision);
            shapes.header(List, "visual");
            writeBox(shapes, *visual);
            shapes.header(List, "ui");
            writeBox(shapes, *ui);
            shapes.header(List, "outline");
            writeBox(shapes, *outline);
            if (camera) {
                auto box = game::cameraShape(context.world, type, type.states[i]);
                if (!box) {
                    return {false, std::format("no camera shape for {} #{}", type.name, i)};
                }
                // none or one box
                shapes.header(List, "camera");
                writeBoxes(shapes, *box ? std::vector<game::AABB>{**box} : std::vector<game::AABB>{});
            }
            shapes.u8(End);
        }
        shapes.u8(End);
    }
    shapes.u8(End);
    if (!writeFile(context, "block_shapes.nbt", shapes.data)) {
        return {false, "could not write block_shapes.nbt"};
    }
    return {true, std::format("shapes of {} states", game::stateCount(context.blockTypes))};
}

}
