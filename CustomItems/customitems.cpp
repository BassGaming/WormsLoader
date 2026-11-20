// CustomItems: new weapons. Assets: bundles\CustomItems.bdl (built by assets\make_bundle.py) and script
// overrides under files\ are served by the loader. Code: none yet (stage 1 = asset pipeline test).
#include "wormsloader.h"

static const WL_ModInfo kInfo = {
    "CustomItems", "0.1.0", "WormsLoader",
    "New weapons. Test build: cluster bomb fragments use a model from this mod's own bundle.",
    false, "WormsClanWars", // changes gameplay data: every player needs it
};

extern "C" __declspec(dllexport) const WL_ModInfo* WL_GetModInfo() { return &kInfo; }
extern "C" __declspec(dllexport) void WL_ModInit(const WL_Api*) {}
