/*
  SystemPrefs.h - Core geiger system prefs module.

  Copyright (C) 2025 @steadramon
*/
#ifndef SYSTEMPREFS_H
#define SYSTEMPREFS_H

#include "../Module/EGModule.h"
#include "EGPrefs.h"
#include "../Counter/Counter.h"

extern Counter gcounter;

class SystemPrefs : public EGModule {
  public:
    const char* name() override { return "sys"; }
    uint8_t display_order() override { return 10; }
    const EGPrefGroup* prefs_group() override;
    void on_prefs_loaded() override;
    const EGLegacyAlias* legacy_aliases() override;  // LEGACY IMPORT (remove after v1.0.0)
};

extern SystemPrefs systemPrefs;

#endif
