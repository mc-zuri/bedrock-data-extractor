// Linux: the server numbers what it finds in its packs' folders in the order readdir lists them (1.21.0's trial keys).
// An ext4 folder lists in an order of its filesystem's own, other on every machine; the Windows server's NTFS folder
// lists by name. The agent's opendir reads the whole folder and its readdir gives it back in NTFS's order.
#include <dirent.h>
#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <climits>
#include <cstring>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

struct Listing {
    std::vector<dirent64> entries;
    std::size_t           next = 0;
};

// built on first use: the agent's own constructor may list a folder before this file's statics are
struct Listings {
    std::mutex                        mutex;
    std::unordered_map<DIR*, Listing> byDir;
};

Listings& listings() {
    static Listings l;
    return l;
}

template <class Fn>
Fn real(char const* name) {
    return reinterpret_cast<Fn>(dlsym(RTLD_NEXT, name));
}

// a process the agent was preloaded into by mistake lists as its filesystem does
bool inServer() {
    static bool const server = [] {
        char          path[PATH_MAX];
        ssize_t const n = readlink("/proc/self/exe", path, sizeof path);
        return n > 0 && std::string_view(path, static_cast<std::size_t>(n)).ends_with("/bedrock_server");
    }();
    return server;
}

// "." and ".." first, then by name with ASCII letters upper-cased ('_' after the letters), as NTFS orders a folder
bool beforeOnNtfs(dirent64 const& a, dirent64 const& b) {
    auto const dots = [](std::string_view name) { return name == "." ? 0 : name == ".." ? 1 : 2; };
    if (dots(a.d_name) != dots(b.d_name)) {
        return dots(a.d_name) < dots(b.d_name);
    }
    for (std::size_t i = 0;; ++i) {
        auto const ca = static_cast<unsigned char>(a.d_name[i]);
        auto const cb = static_cast<unsigned char>(b.d_name[i]);
        auto const ua = ca >= 'a' && ca <= 'z' ? ca - 32 : ca;
        auto const ub = cb >= 'a' && cb <= 'z' ? cb - 32 : cb;
        if (ua != ub || ca == 0) {
            return ua < ub;
        }
    }
}

// the entry's fields, not the whole struct: a record ends with its name, maybe where readdir's buffer ends
dirent64 copyOf(dirent64 const& e) {
    dirent64 out{};
    out.d_ino    = e.d_ino;
    out.d_off    = e.d_off;
    out.d_reclen = sizeof out;
    out.d_type   = e.d_type;
    std::strncpy(out.d_name, e.d_name, sizeof out.d_name - 1);
    return out;
}

// whether the agent listed `dir`, and if so its next entry (null after the last)
bool listed(DIR* dir, dirent64*& next) {
    auto&                 l = listings();
    std::lock_guard const lock(l.mutex);
    auto const            it = l.byDir.find(dir);
    if (it == l.byDir.end()) {
        return false;
    }
    Listing& listing = it->second;
    next             = listing.next < listing.entries.size() ? &listing.entries[listing.next++] : nullptr;
    return true;
}

}

extern "C" {

DIR* opendir(char const* path) {
    static auto const realOpendir = real<DIR* (*)(char const*)>("opendir");
    static auto const realReaddir = real<dirent64* (*)(DIR*)>("readdir64");
    DIR* const        dir         = realOpendir(path);
    if (!dir || !inServer()) {
        return dir;
    }
    Listing listing;
    while (dirent64 const* e = realReaddir(dir)) {
        listing.entries.push_back(copyOf(*e));
    }
    std::stable_sort(listing.entries.begin(), listing.entries.end(), beforeOnNtfs);
    auto&                 l = listings();
    std::lock_guard const lock(l.mutex);
    l.byDir[dir] = std::move(listing);
    return dir;
}

// struct dirent and dirent64 are one layout on x86-64
dirent* readdir(DIR* dir) {
    static auto const realReaddir = real<dirent* (*)(DIR*)>("readdir");
    dirent64*         next        = nullptr;
    return listed(dir, next) ? reinterpret_cast<dirent*>(next) : realReaddir(dir);
}

dirent64* readdir64(DIR* dir) {
    static auto const realReaddir = real<dirent64* (*)(DIR*)>("readdir64");
    dirent64*         next        = nullptr;
    return listed(dir, next) ? next : realReaddir(dir);
}

void rewinddir(DIR* dir) {
    static auto const realRewinddir = real<void (*)(DIR*)>("rewinddir");
    {
        auto&                 l = listings();
        std::lock_guard const lock(l.mutex);
        if (auto const it = l.byDir.find(dir); it != l.byDir.end()) {
            it->second.next = 0;
        }
    }
    realRewinddir(dir);
}

int closedir(DIR* dir) {
    static auto const realClosedir = real<int (*)(DIR*)>("closedir");
    {
        auto&                 l = listings();
        std::lock_guard const lock(l.mutex);
        l.byDir.erase(dir);
    }
    return realClosedir(dir);
}
}
