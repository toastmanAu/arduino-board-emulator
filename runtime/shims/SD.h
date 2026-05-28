#pragma once
#include "FS.h"

extern fs::FS SD;

// Many SD users also include this:
class SDClass {
public:
    bool begin(int /*cs*/ = -1) { return true; }
    void end() {}
};
extern SDClass SDLib;
