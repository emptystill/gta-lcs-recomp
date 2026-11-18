// Particles on the game's time step instead of the frame (docs/vcs-reference/game-internals.md 4, "Effects that run per frame").
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

class PS2Runtime;

namespace vcs
{
    // Where the particle system is (VCS's by default; Liberty City Stories' set in lcs_overrides.cpp).
    struct ParticleLayout
    {
        uint32_t addParticle = 0x001E9078u;  // CParticle::AddParticle
        uint32_t addParticle2 = 0x001E9040u; // its overload without a colour (0: none)
        uint32_t update = 0x001E9C90u;       // CParticle::Update
        uint32_t addJet = 0x001EBD28u;       // CParticle::AddJetExplosion (0: none)
        uint32_t systems = 0x0070F318u;      // the word holding the systems' address
        uint32_t systemCount = 90u, systemStride = 0x84u;
        uint32_t listHead = 0x80u;           // a system's first live particle
        uint32_t period = 0x32u;             // a system's animation period (u16)
        uint32_t timeStep = 0x004CD110u, frameCounter = 0x004CD124u, clock = 0x004CD104u;
        std::vector<std::pair<uint32_t, uint32_t>> pauses = {{0x004CD2ACu, 1u}, {0x004CD11Cu, 1u}}; // (address, bytes)
        std::vector<uint32_t> throttled;     // the continuous emitters' return addresses (empty: VCS's own list)
        // Instead of a list: any emitter (call site, entity) that calls in a run - on frames at most 2 apart - for more
        // than 100 ms is a continuous one and goes at the PS2's rate, a frame's calls together; single events and short
        // bursts (an explosion's opening frames) pass. `throttled` then names the sites that spawn on a chance rolled
        // every frame (no runs to see), throttled per call. `never`: sites that read the particle back, and
        // the overload's own call of AddParticle (the overload's call decides).
        bool autoThrottle = false;
        std::vector<uint32_t> never;
    };

    // Puts the particle system's per-frame parts on the time step when frames are shorter than the PS2's 1/30 s: the
    // per-particle fades, growth, rotation and animation in CParticle::Update (FUN_001E9C90, scaled before it runs), and
    // the continuous emitters' AddParticle calls (FUN_001E9078 / FUN_001E9040, passed at the PS2's rate by return
    // address). At 30 fps and in original timing at 30 nothing changes. VCS_PARTICLE_TIMING=0 off; VCS_PARTICLE_LOG=1
    // prints the AddParticle calls per second by return address.
    void installParticleTimingHooks(PS2Runtime &runtime);
    void installParticleTimingHooks(PS2Runtime &runtime, const ParticleLayout &layout);
    // "Reduce Post Processing Effects" (vcs_effects_timing.h): on, particles fade and spawn once a frame as before
    // 2026-11-14 (the game's own rates are put back at once). Any thread.
    void setParticleTimingReduced(bool on);
}
