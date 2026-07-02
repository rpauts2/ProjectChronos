#pragma once
#include "core/types.h"

class AntiDump {
    bool active = false;
    ULONG_PTR origEntryPoint = 0;
public:
    AntiDump() {}
    ~AntiDump();

    void Protect();
    void Unprotect();
    static void ErasePEHeaders();
    static void SelfDelete();
    static void NullOutSection(const char* sectionName);
    static void RandomizeImportOrder();
};
