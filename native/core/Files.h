#pragma once
// Files the agent writes into BDG_OUT, the same on every OS: a path in the OS's own form, a file written whole or not
// at all, a log line appended.

#include <cstddef>
#include <string>

namespace bdg::files {

// `dir` and `name` joined with the OS's separator
std::string join(std::string const& dir, std::string const& name);

// Through a temporary file moved over `path`: never half a file.
bool writeWhole(std::string const& path, void const* data, std::size_t size);

// open-append-close: the server may be killed at any moment, and nothing written may be lost
void appendLine(std::string const& path, std::string const& text);

bool exists(std::string const& path);

}
