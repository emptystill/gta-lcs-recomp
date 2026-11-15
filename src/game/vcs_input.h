// The player's controls (docs/vcs-reference/CONTROLS.md): keyboard / mouse bindings, the gamepad's button map, stick options, the
// mouse camera and vibration - all turned into the one DualShock 2 the game reads.
#pragma once

class PS2Runtime;

namespace vcs::input
{
    // The settings of the "Keyboard & mouse" and "Gamepad" pages (settings/settings.h), the menu keys included.
    void registerSettings();

    // Where the pad reads look in the game (the defaults: VCS, docs/vcs-reference/game-internals.md 12, 12b). A game
    // sets its own before install(); an address left 0 switches off what needs it: no player -> no Driving keys, no
    // mouse camera on foot (the mouse is the right stick then), no radio on the wheel; no camera -> no scope / foot
    // camera; no frame counter -> the mouse is taken once per read. `extras`: the VCS-only modules that work with the
    // input (free aim, the car camera, the pause menu's mouse) are installed.
    struct GameLayout
    {
        // the player (CWorld::Players[0].m_pPed) and its fields (pedStateDriving 0: driving is pedState != 0, a flag)
        unsigned playerPed = 0x004E4910u, pedVehicle = 0x480u, pedState = 0x8C4u, pedStateDriving = 0x37u;
        unsigned pedPos = 0x30u, pedAttachedTo = 0x858u, vehicleRadio = 0x2BAu, radioPositions = 10u;
        // TheCamera: the active CCam's index, the CCam array and its stride, "looks at the player" (0 under a script
        // camera); a CCam's mode, pitch / heading, FOV, its source points and its target point
        unsigned theCamera = 0x006F44D0u, camActive = 0x50u, cams = 0x70u, camStride = 0x270u, looksAtPlayer = 0x888u;
        unsigned camAlpha = 0x78u, camBeta = 0x7Cu, camFov = 0x134u, camSource = 0x220u, camSource2 = 0x230u,
                 camTarget = 0x1A0u;
        unsigned camModeFollowPed = 4u;
        unsigned scopeZoom = 0x00487E2Cu, camStickLocked = 0x00488EB4u, camTable = 0x00487B30u,
                 camIndoors = 0x00489F7Cu, camWallSkip = 0x00487E58u;
        // CPad 0 and its DisablePlayerControls (s16)
        unsigned pad0 = 0x005147A8u, padDisabled = 0x96u;
        // CTimer: m_FrameCounter, m_UserPause / m_CodePause (bytes), m_snTimeInMilliseconds
        unsigned frameCounter = 0x004CD124u, userPause = 0x004CD2ACu, codePause = 0x004CD11Cu, gameClock = 0x004CD104u;
        bool extras = true;
        // A simpler direct mouse camera (Liberty City Stories; 0 = off): the active camera's mode at TheCamera +
        // directMode; when it is directFootMode or directCarMode the mouse writes the heading (directBeta and its target
        // directBetaTarget) and the pitch (directAlpha, and directAlphaNeg = -pitch), pitch kept in directAlphaMin..Max.
        // The game keeps a heading / pitch written to both of each pair.
        unsigned directMode = 0, directFootMode = 4u, directCarMode = 18u;
        unsigned directBeta = 0, directBetaTarget = 0, directAlpha = 0, directAlphaNeg = 0;
        float directAlphaMin = -0.5f, directAlphaMax = 1.46f;
    };
    void setGameLayout(const GameLayout &layout);

    // Feeds the game's pad reads from the bindings (PSPadBackend::setReader) and its motor levels to the gamepad's
    // vibration (setActuatorSink). VCS_INPUT_MAP=0 keeps the runtime's built-in fixed mapping (the reference). The pad
    // reads look at the game's memory: driving or on foot, and the on-foot camera the mouse turns.
    void install(PS2Runtime &runtime);

    // Main thread, once per host frame: the mouse camera captures the mouse while the game has it (no menu open, the
    // window in front); with the window in the background the sound is muted or the game paused (the Game page's "In
    // the background", game.background: 0 keep running, 1 mute, 2 pause).
    void onHostFrame(bool menuOpen);

    // Game thread, at each game frame: holds the game while it is paused in the background.
    void holdInBackground(PS2Runtime *runtime);

    // Stops any vibration now (the game paused in the background).
    void stopVibration();

    // The device the player used last, for the button prompts (game/vcs_prompts.h): a bound key, a menu key or a mouse
    // button pressed, or the mouse moved, against a gamepad button, or a stick / trigger past half way. Taken at the
    // game's pad reads; the keyboard until the gamepad is used. Any thread.
    enum class Device
    {
        Keyboard,
        Gamepad,
    };
    Device lastDevice();

    // The free camera's controls (game/vcs_freecam.h; fixed keys, docs/vcs-reference/CONTROLS.md), taken once per camera update on
    // the game thread. While the free camera flies the game's pad reads give it nothing, and the mouse is left for this.
    struct FreeCamInput
    {
        float strafe = 0, forward = 0, rise = 0; // -1..1: right, ahead, up
        float turn = 0, tilt = 0;                // the mouse: radians since the last update (left +, up +)
        float turnRate = 0, tiltRate = 0;        // the right stick: -1..1 (left +, up +)
        float zoom = 0;                          // -1..1: narrower (+) or wider
        bool fast = false, slow = false;
    };
    FreeCamInput takeFreeCameraInput();
}
