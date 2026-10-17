#include "game/Blocks.h"

#include "bindings/Bindings.h"
#include "bindings/Names.h"
#include "memory/Guard.h"
#include "memory/Mem.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

namespace bdg::game {

namespace n = bindings::names;

namespace {

struct BlockPos {
    int x = 0, y = 0, z = 0;
};

// Before 1.17.30 the AABB is `Vec3 min, max; bool mValid` (28 bytes): the game fills a vector of those.
struct AABB28 {
    float         c[6];
    std::uint32_t valid;
};

// (this = BlockLegacy, Block const&, IConstBlockSource const&, BlockPos const&, AABB const* intersectTestBox,
//  std::vector<AABB>& out, optional_ref<GetCollisionShapeInterface const>)
using AddCollisionShapes = bool (*)(void const*, void const*, void const*, void const*, void const*, void*, void const*);
// getVisualShape / getUIShape: AABB const& (this = BlockLegacy, Block const&, AABB& buffer [, bool]); getVisualShape's
// trailing bool (before 1.20.11) is passed false
using GetShape     = void const* (*)(void const*, void const*, void*);
using GetShapeBool = void const* (*)(void const*, void const*, void*, bool);
// getOutline: AABB const& (this, Block const&, IConstBlockSource const&, BlockPos const&, AABB& buffer) from 1.20.30;
// before, AABB const& (this, BlockSource&, BlockPos const&, AABB& buffer), and getAABB: AABB const& (this,
// IConstBlockSource const&, BlockPos const&, Block const&, AABB& buffer, bool)
using GetOutline    = void const* (*)(void const*, void const*, void const*, void const*, void*);
using GetOutlineOld = void const* (*)(void const*, void const*, void const*, void*);
using GetAABB       = void const* (*)(void const*, void const*, void const*, void const*, void*, bool);
// getCollisionShapeForCamera: bool (this, AABB& out, Block const&, IConstBlockSource const&, BlockPos const&)
using GetCameraShape = bool (*)(void const*, void*, void const*, void const*, void const*);

// The calls run on one worker thread, each with a deadline: a call that hangs is abandoned, and three in a row give
// the export up instead of wedging it.
#ifdef _WIN32
HANDLE gRequest = nullptr, gDone = nullptr, gThread = nullptr;
void (*volatile gJob)(void*) = nullptr;
void* volatile gArg          = nullptr;
int gHangs                   = 0;

DWORD WINAPI worker(LPVOID) {
    for (;;) {
        WaitForSingleObject(gRequest, INFINITE);
        auto job = gJob;
        if (!job) {
            return 0;
        }
        __try {
            job(gArg);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
        SetEvent(gDone);
    }
}

// false when the call hung or was given up. A fault inside the job is the job's to report.
bool run(void (*job)(void*), void* arg, DWORD ms) {
    if (gHangs >= 3) {
        return false;
    }
    if (!gThread) {
        gRequest = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        gDone    = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        gThread  = CreateThread(nullptr, 0, worker, nullptr, 0, nullptr);
        if (!gThread) {
            return false;
        }
    }
    gJob = job;
    gArg = arg;
    SetEvent(gRequest);
    if (WaitForSingleObject(gDone, ms) == WAIT_OBJECT_0) {
        gHangs = 0;
        return true;
    }
    // hung: abandon that worker (and its events) for good; the next call starts a fresh one
    CloseHandle(gThread);
    gThread = gRequest = gDone = nullptr;
    ++gHangs;
    return false;
}
#else
// A worker's state outlives it: a hung worker is abandoned, never freed.
struct Worker {
    std::mutex              m;
    std::condition_variable cv;
    void (*job)(void*) = nullptr;
    void* arg          = nullptr;
    bool  pending = false, finished = false;
};
Worker* gWorker = nullptr;
int     gHangs  = 0;

void loop(Worker* w) {
    for (;;) {
        std::unique_lock lock(w->m);
        w->cv.wait(lock, [&] { return w->pending; });
        w->pending = false;
        auto job   = w->job;
        auto arg   = w->arg;
        lock.unlock();
        memory::guard([&] { job(arg); });
        lock.lock();
        w->finished = true;
        w->cv.notify_all();
    }
}

bool run(void (*job)(void*), void* arg, int ms) {
    if (gHangs >= 3) {
        return false;
    }
    if (!gWorker) {
        gWorker = new Worker;
        std::thread(loop, gWorker).detach();
    }
    std::unique_lock lock(gWorker->m);
    gWorker->job      = job;
    gWorker->arg      = arg;
    gWorker->pending  = true;
    gWorker->finished = false;
    gWorker->cv.notify_all();
    if (gWorker->cv.wait_for(lock, std::chrono::milliseconds(ms), [&] { return gWorker->finished; })) {
        gHangs = 0;
        return true;
    }
    lock.unlock();
    gWorker = nullptr;
    ++gHangs;
    return false;
}
#endif

enum class InWorld { Outline, OutlineOld, AABBOf, Camera };

struct Call {
    std::uintptr_t fn, blockType, block, blockSource;
    bool           flag;  // addCollisionShapes: the AABB is AABB28; getVisualShape: it takes the trailing bool
    BlockPos       pos;
    InWorld        inWorld = InWorld::Outline;
    bool           ok      = false;
    int            count   = 0;
    AABB           boxes[64]{};
};

// On the worker, for at most `ms`. The call is the worker's while it may still run: a hung one's is never freed.
std::optional<std::vector<AABB>> onWorker(void (*job)(void*), Call const& call) {
    auto* c = new Call(call);
    if (!run(job, c, 3000)) {
        return std::nullopt;
    }
    std::optional<std::vector<AABB>> boxes;
    if (c->ok) {
        boxes.emplace(c->boxes, c->boxes + c->count);
    }
    delete c;
    return boxes;
}

// The single-box getters return the AABB, or fill the buffer: it is oversized, so a 28-byte AABB cannot overrun it.
struct Scratch {
    float         c[6];
    std::uint32_t pad[2];
};

void getShapeJob(void* p) {
    auto*       c = static_cast<Call*>(p);
    Scratch     s{};
    auto        bt = reinterpret_cast<void*>(c->blockType), blk = reinterpret_cast<void*>(c->block);
    void const* r = c->flag ? reinterpret_cast<GetShapeBool>(c->fn)(bt, blk, &s, false) : reinterpret_cast<GetShape>(c->fn)(bt, blk, &s);
    float       f[6];
    std::memcpy(f, reinterpret_cast<std::uintptr_t>(r) > 0x10000 ? r : static_cast<void const*>(s.c), sizeof f);
    c->boxes[0] = {f[0], f[1], f[2], f[3], f[4], f[5]};
    c->count    = 1;
    c->ok       = true;
}

std::optional<AABB> single(BlockType const& type, std::uintptr_t state, char const* slot, bool withBool) {
    std::uintptr_t fn = memory::virtualAt(type.address, bindings::current().slot(slot));
    if (!fn) {
        return std::nullopt;
    }
    auto box = onWorker(getShapeJob, Call{fn, type.address, state, 0, withBool, {}});
    if (!box) {
        return std::nullopt;
    }
    return box->front();
}

// A single box the game answers where the block is, asked at the origin, where it is the block's own. The camera's may
// be none (count 0).
void inWorldJob(void* p) {
    auto*       c   = static_cast<Call*>(p);
    BlockPos    pos = c->pos;
    Scratch     s{};
    auto        bt = reinterpret_cast<void*>(c->blockType), blk = reinterpret_cast<void*>(c->block), region = reinterpret_cast<void*>(c->blockSource);
    void const* r = nullptr;
    switch (c->inWorld) {
    case InWorld::Outline:
        r = reinterpret_cast<GetOutline>(c->fn)(bt, blk, region, &pos, &s);
        break;
    case InWorld::OutlineOld:
        r = reinterpret_cast<GetOutlineOld>(c->fn)(bt, region, &pos, &s);
        break;
    case InWorld::AABBOf:
        r = reinterpret_cast<GetAABB>(c->fn)(bt, region, &pos, blk, &s, true);
        break;
    case InWorld::Camera:
        if (!reinterpret_cast<GetCameraShape>(c->fn)(bt, &s, blk, region, &pos)) {
            c->ok = true;
            return;
        }
        break;
    }
    float f[6];
    std::memcpy(f, reinterpret_cast<std::uintptr_t>(r) > 0x10000 ? r : static_cast<void const*>(s.c), sizeof f);
    c->boxes[0] = {f[0], f[1], f[2], f[3], f[4], f[5]};
    c->count    = 1;
    c->ok       = true;
}

std::optional<std::vector<AABB>> inWorld(World const& world, BlockType const& type, std::uintptr_t state, char const* slot, InWorld kind) {
    std::uintptr_t fn = memory::virtualAt(type.address, bindings::current().slot(slot));
    if (!fn) {
        return std::nullopt;
    }
    Call call{fn, type.address, state, world.overworld, false, {}};
    call.inWorld = kind;
    return onWorker(inWorldJob, call);
}

template <class Box>
int copyOut(std::vector<Box> const& v, AABB* out) {
    int n = static_cast<int>(v.size() > 64 ? 64 : v.size());
    for (int i = 0; i < n; ++i) {
        float const* f = reinterpret_cast<float const*>(&v[i]);
        out[i]         = {f[0], f[1], f[2], f[3], f[4], f[5]};
    }
    return n;
}

#ifdef _WIN32
// The game appends to our std::vector, so this DLL shares the game's CRT heap (/MD).
void addCollisionShapesJob(void* p) {
    auto*    c   = static_cast<Call*>(p);
    BlockPos pos = c->pos;
    auto     fn  = reinterpret_cast<AddCollisionShapes>(c->fn);
    auto     bt = reinterpret_cast<void*>(c->blockType), blk = reinterpret_cast<void*>(c->block), region = reinterpret_cast<void*>(c->blockSource);
    if (c->flag) {
        std::vector<AABB28> v;
        fn(bt, blk, region, &pos, nullptr, &v, nullptr);
        c->count = copyOut(v, c->boxes);
    } else {
        std::vector<AABB> v;
        fn(bt, blk, region, &pos, nullptr, &v, nullptr);
        c->count = copyOut(v, c->boxes);
    }
    c->ok = true;
}
#else
// The game appends to a vector of its own standard library (begin, end, capacity in every one), allocating with its
// own operator new: the agent hands it an empty one and gives the buffer back to the game's operator delete.
struct RawVector {
    std::uint8_t *begin = nullptr, *end = nullptr, *capacity = nullptr;
};

void addCollisionShapesJob(void* p) {
    auto*     c   = static_cast<Call*>(p);
    BlockPos  pos = c->pos;
    auto      fn  = reinterpret_cast<AddCollisionShapes>(c->fn);
    RawVector v;
    fn(reinterpret_cast<void*>(c->blockType), reinterpret_cast<void*>(c->block), reinterpret_cast<void*>(c->blockSource), &pos, nullptr, &v, nullptr);
    std::size_t const stride = c->flag ? sizeof(AABB28) : sizeof(AABB);
    std::size_t const n      = v.begin && v.end > v.begin ? static_cast<std::size_t>(v.end - v.begin) / stride : 0;
    c->count                 = static_cast<int>(n > 64 ? 64 : n);
    for (int i = 0; i < c->count; ++i) {
        float f[6];
        std::memcpy(f, v.begin + i * stride, sizeof f);
        c->boxes[i] = {f[0], f[1], f[2], f[3], f[4], f[5]};
    }
    if (std::uintptr_t const free = bindings::current().address(n::OperatorDelete); free && v.begin) {
        reinterpret_cast<void (*)(void*)>(free)(v.begin);
    }
    c->ok = true;
}
#endif

// In world coordinates. nullopt when the call is unbound, faulted or hung.
std::optional<std::vector<AABB>> boxesAt(World const& world, BlockType const& type, std::uintptr_t state, BlockPos pos) {
    auto const&    r  = bindings::current();
    std::uintptr_t fn = memory::virtualAt(type.address, r.slot(n::BlockAddCollisionShapes));
    if (!fn) {
        return std::nullopt;
    }
    return onWorker(addCollisionShapesJob, Call{fn, type.address, state, world.overworld, r.variant(n::AabbLayout) == n::Aabb28WithValid, pos});
}

}

// BlockSource::fetchCollisionShapes calls addCollisionShapes for every block an entity overlaps and keeps the boxes
// that intersect it. The boxes come back in world coordinates, so the call is made at the origin, where they are the
// block's own exact floats, and once more one chunk away. A box that stays at the same world place wherever the block
// is does not belong to the block: it sits at the world origin and collides with nothing near the block. The game
// makes such boxes when a getCollisionShape override answers false before 1.18.11 (addCollisionShapes then pushes its
// unit-cube default: buttons, fire, scaffolding, thin snow), or returns a box it never offsets by the position
// (pressure plates before 1.19.50, the end portal frame's eye).
std::optional<std::vector<AABB>> collisionShape(World const& world, BlockType const& type, std::uintptr_t state) {
    constexpr int Away = 16;
    auto          here = boxesAt(world, type, state, {});
    if (!here) {
        return std::nullopt;
    }
    auto away = boxesAt(world, type, state, {0, 0, Away});
    if (!away) {
        return std::nullopt;
    }
    auto same = [](AABB const& a, AABB const& b, float dz) {
        return a.minX == b.minX && a.minY == b.minY && a.maxX == b.maxX && a.maxY == b.maxY && std::abs(a.minZ + dz - b.minZ) < 1e-4f && std::abs(a.maxZ + dz - b.maxZ) < 1e-4f;
    };
    std::vector<AABB> own;
    for (AABB const& box : *here) {
        bool const stays = std::ranges::any_of(*away, [&](AABB const& a) { return same(box, a, 0); });
        bool const moves = std::ranges::any_of(*away, [&](AABB const& a) { return same(box, a, Away); });
        if (!stays || moves) {
            own.push_back(box);
        }
    }
    return own;
}

std::optional<AABB> visualShape(BlockType const& type, std::uintptr_t state) {
    return single(type, state, n::BlockGetVisualShape, bindings::current().variant(n::VisualShapeKind) == n::VisualShapeBlockBufferBool);
}

std::optional<AABB> uiShape(BlockType const& type, std::uintptr_t state) { return single(type, state, n::BlockGetUIShape, false); }

std::uintptr_t inheritedOutline(std::vector<BlockType> const& types) {
    std::map<std::uintptr_t, int> count;
    int const                     slot = bindings::current().slot(n::BlockGetOutline);
    for (auto const& type : types) {
        ++count[memory::virtualAt(type.address, slot)];
    }
    auto const most = std::ranges::max_element(count, {}, [](auto const& entry) { return entry.second; });
    return most == count.end() ? 0 : most->first;
}

// Before 1.20.30 getOutline takes no state: the one types inherit asks the world for the block (no chunk is loaded at
// the origin) and gives what getAABB(region, pos, block, buffer, true) gives, so getAABB is asked with the state. A type
// that overrides it (the stairs: the full block) is asked itself.
std::optional<AABB> outlineShape(World const& world, BlockType const& type, std::uintptr_t state, std::uintptr_t inherited) {
    auto const&                      r = bindings::current();
    std::optional<std::vector<AABB>> box;
    if (r.variant(n::OutlineKind) == n::OutlineByBlock) {
        box = inWorld(world, type, state, n::BlockGetOutline, InWorld::Outline);
    } else if (memory::virtualAt(type.address, r.slot(n::BlockGetOutline)) != inherited) {
        box = inWorld(world, type, state, n::BlockGetOutline, InWorld::OutlineOld);
    } else {
        box = inWorld(world, type, state, n::BlockGetAABB, InWorld::AABBOf);
    }
    if (!box || box->empty()) {
        return std::nullopt;
    }
    return box->front();
}

bool hasCameraShape() { return bindings::current().slot(n::BlockGetCameraShape) >= 0; }

std::optional<std::optional<AABB>> cameraShape(World const& world, BlockType const& type, std::uintptr_t state) {
    auto box = inWorld(world, type, state, n::BlockGetCameraShape, InWorld::Camera);
    if (!box) {
        return std::nullopt;
    }
    return box->empty() ? std::optional<AABB>{} : std::optional<AABB>{box->front()};
}

}
