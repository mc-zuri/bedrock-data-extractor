#include "core/Log.h"

#include "core/Files.h"

#include <mutex>

namespace bdg::log {

namespace {
std::mutex  gLock;
std::string gPath;
}

void open(std::string const& path) {
    std::lock_guard lock(gLock);
    gPath = path;
}

void line(std::string const& text) {
    std::lock_guard lock(gLock);
    if (!gPath.empty()) {
        files::appendLine(gPath, text);
    }
}

}
