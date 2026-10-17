#pragma once
// Linux: the Itanium ABI's run-time type information, which a server without symbols still has. A class's typeinfo
// names it ("11CompoundTag"), its vtable points at the typeinfo just before its address point, and every polymorphic
// object starts with its vtable: a class's vtable by its type name, an object's class by its vtable.

#include <cstdint>
#include <string>
#include <string_view>

namespace bdg::memory {

// the address point of the class's primary vtable (offset to top 0) whose typeinfo names `typeName`; 0 when none
std::uintptr_t vtableOf(std::string_view typeName);

// the type name of the object's dynamic class ("5Block"); "" when `object` has no vtable with RTTI in the image
std::string classOf(std::uintptr_t object);

}
