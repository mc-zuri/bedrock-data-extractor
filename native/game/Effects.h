#pragma once
// The mob effects, the same on every build. They are found and read by content (Effects.cpp): no binding says where
// they are or how a MobEffect is laid out.

#include <string>
#include <vector>

namespace bdg::game {

struct Effect {
    int         id;             // the id the server sends a client: speed 1, fatal_poison 25
    std::string name;           // "speed"
    std::string descriptionId;  // "potion.moveSpeed"; the language file has it as it is
    bool        harmful;
};

struct Effects {
    std::vector<Effect> effects;  // by id, from 1
    std::string         error;    // why they do not read right; then there are none
};

Effects mobEffects();

}
