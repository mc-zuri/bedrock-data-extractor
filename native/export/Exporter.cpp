#include "export/Exporter.h"

#include "core/Files.h"

namespace bdg::exporter {

bool writeFile(Context const& context, std::string const& name, std::vector<std::uint8_t> const& bytes) {
    return files::writeWhole(files::join(context.out, name), bytes.data(), bytes.size());
}

}
