#include "game/Blocks.h"

#include "bindings/Bindings.h"
#include "bindings/Names.h"
#include "memory/Guard.h"
#include "memory/Mem.h"

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace bdg::game {

namespace n = bindings::names;
using memory::inImage;
using memory::read;
using memory::readPtr;
using namespace nbt;

namespace {

#ifdef _WIN32
// The game's own CompoundTag::write: a stand-in IDataOutput whose vtable forwards each write into our buffer, handed to the live
// Tag::write(IDataOutput&) virtual (Tag vtable slot 2).
struct DataOutput {
    void**       vtable;
    Buffer*      out;
    static void* table[10];
    static bool  ready;

    explicit DataOutput(Buffer* b) : out(b) {
        if (!ready) {
            table[0] = reinterpret_cast<void*>(&dtor);
            table[1] = reinterpret_cast<void*>(&writeString);  // writeString(string_view)
            table[2] = reinterpret_cast<void*>(&writeString);  // writeLongString(string_view)
            table[3] = reinterpret_cast<void*>(&writeFloat);
            table[4] = reinterpret_cast<void*>(&writeDouble);
            table[5] = reinterpret_cast<void*>(&writeByte);
            table[6] = reinterpret_cast<void*>(&writeShort);
            table[7] = reinterpret_cast<void*>(&writeInt);
            table[8] = reinterpret_cast<void*>(&writeLong);
            table[9] = reinterpret_cast<void*>(&writeBytes);
            ready    = true;
        }
        vtable = table;
    }
    static void __fastcall dtor(DataOutput*) {}
    static void __fastcall writeString(DataOutput* self, void const* view) {  // string_view by hidden pointer
        char const* d = *static_cast<char const* const*>(view);
        std::size_t n = *(static_cast<std::size_t const*>(view) + 1);
        if (d && n > 0 && n < 65536) {
            self->out->str({d, n});
        } else {
            self->out->i16(0);
        }
    }
    static void __fastcall writeFloat(DataOutput* self, float v) { self->out->f32(v); }
    static void __fastcall writeDouble(DataOutput* self, double v) { self->out->f64(v); }
    static void __fastcall writeByte(DataOutput* self, std::uint8_t v) { self->out->u8(v); }
    static void __fastcall writeShort(DataOutput* self, std::int16_t v) { self->out->i16(v); }
    static void __fastcall writeInt(DataOutput* self, std::int32_t v) { self->out->i32(v); }
    static void __fastcall writeLong(DataOutput* self, std::int64_t v) { self->out->i64(v); }
    static void __fastcall writeBytes(DataOutput* self, void const* d, std::uint64_t n) {
        if (d && n > 0 && n < 0x1000000) {
            self->out->raw(d, n);
        }
    }
};
void* DataOutput::table[10] = {};
bool  DataOutput::ready     = false;

bool callWrite(std::uintptr_t fn, std::uintptr_t compound, DataOutput* out) {
    return memory::guard([&] { reinterpret_cast<void (*)(std::uintptr_t, DataOutput*)>(fn)(compound, out); });
}

// Before 1.21.45, the CompoundTag's std::map<std::string, CompoundTagVariant> walked:
// node: _Left @0, _Parent @8, _Right @0x10, _Isnil @0x19, key @0x20, value @0x40
// value (a std::variant): payload @+0 (or @+8 behind a Tag vtable on 1.21.45+), index (= tag type) @+40
constexpr std::int64_t Left = 0, Parent = 8, Right = 0x10, IsNil = 0x19, Key = 0x20, Value = 0x40;

void walk(std::uintptr_t node, std::uintptr_t head, Buffer& out, int depth) {
    if (!node || node == head || depth > 12 || read<std::uint8_t>(node, IsNil)) {
        return;
    }
    walk(readPtr(node, Left), head, out, depth);
    std::string key = memory::readString(node + Key);
    int         t   = read<std::uint8_t>(node, Value + 40);
    // only the types block states use; an unknown one would corrupt the stream
    bool handled = t == Byte || t == Short || t == Int || t == Long || t == Float || t == Double || t == String || t == Compound;
    if (!key.empty() && handled) {
        std::uintptr_t val = node + Value;
        std::uintptr_t v   = inImage(readPtr(val)) ? val + 8 : val;
        out.header(static_cast<Tag>(t), key);
        switch (t) {
        case Byte:
            out.u8(read<std::uint8_t>(v));
            break;
        case Short:
            out.i16(read<std::int16_t>(v));
            break;
        case Int:
            out.i32(read<std::int32_t>(v));
            break;
        case Long:
            out.i64(read<std::int64_t>(v));
            break;
        case Float:
            out.f32(read<float>(v));
            break;
        case Double:
            out.f64(read<double>(v));
            break;
        case String:
            out.str(memory::readString(v));
            break;
        case Compound: {
            std::uintptr_t h    = readPtr(v);
            std::uint64_t  size = readPtr(v, 8);
            if (h > 0x10000 && size > 0 && size <= 100) {
                walk(readPtr(h, Parent), h, out, depth + 1);
            }
            out.u8(End);
            break;
        }
        }
    }
    walk(readPtr(node, Right), head, out, depth);
}

// The live CompoundTag at `compound` as a compound body (entries + End).
void writeCompound(std::uintptr_t compound, Buffer& out) {
    std::uintptr_t first = readPtr(compound);
    if (inImage(first)) {
        // a Tag with its vtable: let the game write it
        std::uintptr_t fn = readPtr(first, 2 * 8);
        DataOutput     o(&out);
        if (fn < 0x10000 || !callWrite(fn, compound, &o)) {
            out.u8(End);
        }
        return;
    }
    std::uint64_t size = readPtr(compound, 8);
    if (first > 0x10000 && size >= 1 && size <= 64) {
        walk(readPtr(first, Parent), first, out, 0);
    }
    out.u8(End);
}

#else
// Linux: the game's own CompoundTag::write, by its symbol. A stand-in IDataOutput in the Itanium layout: the complete and the deleting destructor, then the writers in their
// declaration order (as BytesDataOutput's vtable holds them). A string comes in two registers: a std::string_view's
// data and size, or in older builds a gsl::basic_string_span's, size first; the size is the one a string has.
struct DataOutput {
    void**       vtable;
    Buffer*      out;
    static void* table[11];

    explicit DataOutput(Buffer* b) : vtable(table), out(b) {}
    static void dtor(DataOutput*) {}
    static void writeString(DataOutput* self, std::uintptr_t first, std::uintptr_t second) {
        auto const [data, size] = first < 65536 ? std::pair{second, first} : std::pair{first, second};
        if (data >= 0x10000 && size > 0 && size < 65536) {
            self->out->str({reinterpret_cast<char const*>(data), size});
        } else {
            self->out->i16(0);
        }
    }
    static void writeFloat(DataOutput* self, float v) { self->out->f32(v); }
    static void writeDouble(DataOutput* self, double v) { self->out->f64(v); }
    static void writeByte(DataOutput* self, char v) { self->out->u8(static_cast<std::uint8_t>(v)); }
    static void writeShort(DataOutput* self, std::int16_t v) { self->out->i16(v); }
    static void writeInt(DataOutput* self, std::int32_t v) { self->out->i32(v); }
    static void writeLong(DataOutput* self, std::int64_t v) { self->out->i64(v); }
    static void writeBytes(DataOutput* self, void const* d, std::size_t n) {
        if (d && n > 0 && n < 0x1000000) {
            self->out->raw(d, n);
        }
    }
};
void* DataOutput::table[11] = {
    reinterpret_cast<void*>(&DataOutput::dtor),        reinterpret_cast<void*>(&DataOutput::dtor),       reinterpret_cast<void*>(&DataOutput::writeString),
    reinterpret_cast<void*>(&DataOutput::writeString), reinterpret_cast<void*>(&DataOutput::writeFloat), reinterpret_cast<void*>(&DataOutput::writeDouble),
    reinterpret_cast<void*>(&DataOutput::writeByte),   reinterpret_cast<void*>(&DataOutput::writeShort), reinterpret_cast<void*>(&DataOutput::writeInt),
    reinterpret_cast<void*>(&DataOutput::writeLong),   reinterpret_cast<void*>(&DataOutput::writeBytes)};

// The live CompoundTag at `compound` as a compound body (entries + End), as CompoundTag::write writes it; only whole:
// a write that faults leaves nothing but End.
void writeCompound(std::uintptr_t compound, Buffer& out) {
    // the symbol, else the virtual Tag::write, right after the destructors (a build without symbols)
    std::uintptr_t const bound = bindings::current().address(n::CompoundTagWrite), write = bound ? bound : memory::virtualAt(compound, memory::DestructorSlots + 1);
    Buffer               body;
    DataOutput           o(&body);
    if (write && memory::guard([&] { reinterpret_cast<void (*)(std::uintptr_t, DataOutput*)>(write)(compound, &o); })) {
        out.raw(body.data.data(), body.data.size());
    } else {
        out.u8(End);
    }
}
#endif

struct Reader {
    std::vector<std::uint8_t> const& data;
    std::size_t                      at = 0;

    bool has(std::size_t n) const { return at + n <= data.size(); }

    // nullopt where the body is cut short
    std::optional<std::string_view> string() {
        if (!has(2)) {
            return std::nullopt;
        }
        std::size_t length = std::size_t{data[at]} << 8 | data[at + 1];
        at += 2;
        if (!has(length)) {
            return std::nullopt;
        }
        std::string_view text(reinterpret_cast<char const*>(data.data()) + at, length);
        at += length;
        return text;
    }

    // false where the body is cut short or holds a type a block state never has
    bool skip(Tag type) {
        static constexpr std::size_t sizes[] = {0, 1, 2, 4, 8, 4, 8};
        if (type >= Byte && type <= Double) {
            at += sizes[type];
            return has(0);
        }
        if (type == String) {
            return string().has_value();
        }
        if (type != Compound) {
            return false;
        }
        while (has(1)) {
            auto entry = static_cast<Tag>(data[at++]);
            if (entry == End) {
                return true;
            }
            if (!string() || !skip(entry)) {
                return false;
            }
        }
        return false;
    }

    // the string entry `key` of the body; "" when it has none
    std::string stringEntry(std::string_view key) {
        while (has(1)) {
            auto type = static_cast<Tag>(data[at++]);
            if (type == End) {
                return "";
            }
            auto name = string();
            if (!name) {
                return "";
            }
            if (type == String && *name == key) {
                return std::string(string().value_or(""));
            }
            if (!skip(type)) {
                return "";
            }
        }
        return "";
    }
};

}

void writeStateTag(std::uintptr_t state, Buffer& out) {
    std::int64_t const serialization = bindings::current().offset(n::BlockSerializationId);
    if (serialization < 0) {
        out.u8(End);
    } else {
        writeCompound(state + serialization, out);
    }
}

std::string stateName(std::uintptr_t state) {
    Buffer tag;
    writeStateTag(state, tag);
    return Reader{tag.data}.stringEntry("name");
}

}
