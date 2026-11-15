// VCS frame rate and game timing (docs/vcs-reference/NATIVE_PORT_ROADMAP.md milestone 4, docs/vcs-reference/game-internals.md 4).
#pragma once

class PS2Runtime;

namespace vcs
{
    // Where the frame-rate hooks go (VCS's by default, vcs_framerate.cpp). 0: the game has no such function / it is
    // not known (timerEnsure: the step's minimum clamp is then patched in the game's code instead - LCS's is inline
    // in CTimer::Update; twoDPass: the HUD's flashing stays on the frame counter).
    struct FrameLayout
    {
        unsigned countVBlank = 0x00317AC8u;   // the VBlank callback that counts VBlanks in the graphics object (+4)
        unsigned timerTicks = 0x00451C00u;    // the kernel timer read, CTimer::Update's only caller
        unsigned timerEnsure = 0x002F47C0u;   // CTimer's "create the timer" (its 2nd call in Update follows the clamp)
        unsigned afterMinClamp = 0x002F4C04u; // ... that call's return address
        unsigned waitVBlank = 0x003703B8u;
        unsigned limiterWait = 0x00317A50u;   // return address of the limiter's VBlank wait
        unsigned limiterObjectReg = 18u;      // the register holding the graphics object there ($s2)
        unsigned timerUpdate = 0x002F4960u;   // CTimer::Update
        unsigned twoDPass = 0x0021F348u;      // the frame's 2D pass (HUD)
        unsigned frameCounter = 0x004CD124u, timeMs = 0x004CD104u;
        unsigned prevTicks = 0x00488298u, frameTicks = 0x004CD564u; // the last Update's tick count (64-bit), this frame's
        unsigned timeScale = 0x004CD168u, timeStep = 0x004CD110u, timeStepNonClipped = 0x004CD300u;
    };
    void installFrameRateHooks(PS2Runtime &runtime);
    void installFrameRateHooks(PS2Runtime &runtime, const FrameLayout &layout);

    // Game timing: Original = paced by the emulated VBlank as on the PS2 (30, or 60 with a cap of 60 or more);
    // Independent = paced by the host clock to the frame-rate cap, game time from the host clock. Set before the game
    // runs (a switch while it runs would jump the game's clock).
    enum class Timing
    {
        Original = 0,
        Independent = 1,
    };
    void setTiming(Timing timing);
    Timing timing();

    // Frames per second: 30, 60 ... 240 (the most, vcs_framerate.cpp kMaxFps); kCapRefresh = the monitor's refresh rate; kCapUnlimited = no limit
    // (independent timing). Any thread, takes effect at the next frame.
    constexpr int kCapRefresh = -1;
    constexpr int kCapUnlimited = 0;
    void setFrameCap(int fps);
    int frameCap();

    // The pre-independence switch: 60 = setFrameCap(60), else 30.
    void setFrameRate60(bool on);
    bool frameRate60();
}
