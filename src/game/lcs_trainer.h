// The trainer on Liberty City Stories (docs/LCS.md "Trainer"): lcs_trainer.cpp implements game/vcs_trainer.h's
// interface for the F5 menu - so far the weapons: any weapon, the cheats' three sets, remove all, unlimited ammo, no
// reload. The rest of the menu's pages are hidden (src/ui/trainer_menu.cpp, kLcs).
#pragma once

#include <cstdint>

class PS2Runtime;
struct R5900Context;

namespace lcs::trainer
{
    // Before runtime.run() (main.cpp): the `trainer` control-channel command (tests).
    void install(PS2Runtime &runtime);

    // Game thread, once a frame at the frame boundary (the limiter hook): whether the game runs, the held toggles.
    void onFrame(uint8_t *rdram);
    // Game thread, at the frame boundary: the next queued guest call, if the game runs. True when it chained one (pc,
    // a0..a3 set); the hook makes it return into the limiter (lcs_overrides.cpp), and asks for the next one then - a
    // whole weapon set in one frame, as the game's cheats give it.
    bool chainNext(uint8_t *rdram, R5900Context *ctx);
}
