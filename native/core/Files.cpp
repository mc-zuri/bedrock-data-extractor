#include "core/Files.h"

#include <cstdio>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace bdg::files {

std::string join(std::string const& dir, std::string const& name) {
#ifdef _WIN32
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

bool writeWhole(std::string const& path, void const* data, std::size_t size) {
    std::string const tmp = path + ".tmp";
    FILE*             f   = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        return false;
    }
    bool const ok = std::fwrite(data, 1, size, f) == size;
    if (std::fclose(f) != 0 || !ok) {
        return false;
    }
#ifdef _WIN32
    return MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
#else
    return std::rename(tmp.c_str(), path.c_str()) == 0;
#endif
}

void appendLine(std::string const& path, std::string const& text) {
    FILE* f = std::fopen(path.c_str(), "a");
    if (!f) {
        return;
    }
    std::fputs(text.c_str(), f);
    std::fputc('\n', f);
    std::fclose(f);
}

bool exists(std::string const& path) {
    std::error_code e;
    return std::filesystem::exists(path, e);
}

}
