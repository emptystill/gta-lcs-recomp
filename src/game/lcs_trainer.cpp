// The trainer on Liberty City Stories (docs/LCS.md "Trainer"): game/vcs_trainer.h's interface, so the F5 menu
// (src/ui/trainer_menu.cpp) drives it. vcs_trainer.cpp (VCS's) is left out of the build: its addresses and script
// command numbers are VCS's.
//
// Two ways in, both from the frame boundary (the limiter hook, lcs_overrides.cpp), only while the game's clock runs:
//
// - **Weapons** are given the way the game's weapon cheats give them (FUN_0016C838 / 0016CA70 / 0016CC80): the
//   weapon's model requested (CStreaming::RequestModel 0x18B400, flags 1) and loaded (LoadAllRequestedModels
//   0x18F648), CPed::GiveWeapon (0x1E1180: ped, type, ammo, 1), then the model left to the streamer again
//   (SetModelIsDeletable 0x18FC38). One given alone is also put in his hand (CPed::SetCurrentWeapon(ped, type)
//   0x1E1470). Each is a guest call chained from the frame boundary, returning into the limiter, the next chained
//   then: a weapon set in one frame, as the cheats give it. Remove all weapons is CPed::ClearWeapons (0x1E16C8). The
//   vehicle repair's engine and tyres go the same way (the health cheat's calls, FUN_0016D0D8).
// - **Everything else** is a short script on the game's own script VM (CTheScripts, docs/LCS.md "The script VM"):
//   bytecode in the locals of an idle CRunningScript, started the way StartNewScript (0x10A210) starts one, run by
//   the game in its next frame, ending with TERMINATE_THIS_SCRIPT. The game applies the commands as its missions do
//   (proofs, vehicle creation, warps, the clock, the weather). A script still waiting when the game pauses is taken
//   back out, so none can end up in a save. Opcode numbers are LCS's (GTA3's, shifted by ~5 from 0xD6 on), each
//   checked against its handler.
//
// Values that must hold (health under god mode, ammo, the clip under no reload, the vehicle's health) are written into
// the player's ped / vehicle every frame while their toggle is on.
#include "game/lcs_trainer.h"
#include "game/vcs_trainer.h"

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_control.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    using vcs::trainer::Entry;
    using vcs::trainer::Toggle;
    namespace tr = vcs::trainer;

    constexpr uint32_t kRamMask = 0x01FFFFFFu;

    // ---- Game addresses (docs/LCS.md) ---------------------------------------------------------------------------
    constexpr uint32_t kPlayerInfo = 0x00408E40u;     // CWorld::Players[0]: +0 the ped, +0xBC money, +0x178 / +0x179 the
    constexpr uint32_t kPlayerPed = kPlayerInfo;      // most health / armour (bytes)
    constexpr uint32_t kMaxHealth = kPlayerInfo + 0x178u, kMaxArmour = kPlayerInfo + 0x179u;
    constexpr uint32_t kGameClock = 0x003D9B70u;      // CTimer::m_snTimeInMilliseconds: stands still while paused
    constexpr uint32_t kTimeScale = 0x003D9CE4u;      // CTimer::ms_fTimeScale (SET_TIME_SCALE's)
    constexpr uint32_t kClockHours = 0x003D9B74u;     // CClock (GET_TIME_OF_DAY's): hours, minutes
    constexpr uint32_t kClockMinutes = 0x003D9B80u;
    constexpr uint32_t kMissionRunning = 0x003D9C29u; // u8: cleared by TERMINATE_THIS_SCRIPT of the mission script
    constexpr uint32_t kMaxWanted = 0x003D829Cu;      // CWanted::MaximumWantedLevel: SetWantedLevel (0x2F4C48) caps at it
    constexpr uint32_t kWeaponInfoPtr = 0x003D9B48u;  // *(*(this) + 4) + type * 0x70: +0 fire type, +0x10 clip size,
    constexpr uint32_t kWeaponInfoSize = 0x70u;       // +0x60 / +0x64 the models (-1: none), +0x68 the slot
    constexpr uint32_t kPedPool = 0x003D5AE8u;        // -> CPool {entries, flags (u8 each), size}: peds 0xCD0 each
    constexpr uint32_t kVehiclePool = 0x003D5AECu;    // vehicles 0x780 each
    constexpr uint32_t kPedSize = 0xCD0u, kVehicleSize = 0x780u;
    constexpr uint32_t kModelInfos = 0x003D8720u;     // -> CModelInfo::ms_modelInfoPtrs; count at kModelCount
    constexpr uint32_t kModelCount = 0x003D9BA0u;
    constexpr uint32_t kStreamInfo = 0x003D0CC8u;     // -> the models' streaming info, 0x14 each: +0xC 1 = loaded
    constexpr uint32_t kText = 0x003D89B8u;           // -> CText: +0 entries {u32 UTF-16 text, char key[8]}, +4 u16 count
    // ped fields
    constexpr uint32_t kPos = 0x30u;
    constexpr uint32_t kPedHealth = 0x4B8u, kPedArmour = 0x4BCu; // floats
    constexpr uint32_t kPedHeading = 0x4E0u;                     // radians (SET_CHAR_HEADING's)
    constexpr uint32_t kPedVehicle = 0x534u, kPedDriving = 0x538u;
    constexpr uint32_t kPedWeapons = 0x594u, kSlotSize = 0x1Cu;  // type, state, clip +8, total +0xC
    constexpr uint32_t kPedWanted = 0x810u;                      // CWanted (+0x7F0) level
    constexpr int kSlots = 10;
    constexpr uint32_t kMaxAmmo = 9999u; // GiveWeapon's own cap
    // vehicle fields
    constexpr uint32_t kVehForward = 0x10u; // the matrix: right +0, forward +0x10, up +0x20, position +0x30
    constexpr uint32_t kVehHealth = 0x268u; // float, 1000 undamaged (below 250 it burns)
    constexpr uint32_t kVehSpeed = 0x70u;   // CPhysical: move speed +0x70, turn speed +0x80 (SET_CAR_FORWARD_SPEED's)
    constexpr uint32_t kVehClass = 0x344u;  // 0 automobile, 1 boat, 5 bike (the model's class)
    constexpr uint32_t kVehDamage = 0x350u; // CAutomobile's damage manager (the health cheat's)
    // model info fields
    constexpr uint32_t kMiType = 0x10u;  // u8, 6 = vehicle
    constexpr uint32_t kMiClass = 0x38u; // vehicle class (as kVehClass)

    // guest functions chained from the frame boundary (the weapon cheats' and the health cheat's)
    constexpr uint32_t kRequestModel = 0x0018B400u, kLoadRequested = 0x0018F648u, kModelDeletable = 0x0018FC38u;
    constexpr uint32_t kGiveWeapon = 0x001E1180u, kSetCurrentWeapon = 0x001E1470u, kClearWeapons = 0x001E16C8u;
    constexpr uint32_t kSetEngineStatus = 0x0032FCB8u, kSetWheelStatus = 0x0032FC50u; // (damage manager, ...)

    // ---- The script VM (docs/LCS.md "The script VM") -----------------------------------------------------------
    constexpr uint32_t kScriptSpacePtr = 0x003CF0F4u; // -> ScriptSpace (MAIN.SCM); an IP is an offset from it; 0 until
                                                      // the script engine runs
    constexpr uint32_t kActiveScripts = 0x003D9C00u;  // CRunningScript lists: +0 next, +4 prev, insert at the head
    constexpr uint32_t kIdleScripts = 0x003D9C04u;
    constexpr uint32_t kNextScriptId = 0x003CF100u;
    constexpr uint32_t kScriptSize = 0x21Cu;
    constexpr uint32_t kScriptId = 0x8u, kScriptName = 0x10u, kScriptIp = 0x18u, kScriptActive = 0x20Cu;
    constexpr uint32_t kScriptDeathArrest = 0x217u; // Init sets 1; SET_DEATHARREST_STATE (0x116)
    // the code lives in the script's own locals 16.. (+0xA0..+0x208): locals 0..15 stay free for outputs
    constexpr uint32_t kCodeOffset = 0x60u + 16u * 4u;
    constexpr uint32_t kCodeMax = 0x208u - kCodeOffset;

    // Script commands: LCS's numbers, each checked against its case in the handler blocks (docs/LCS.md).
    enum : uint16_t
    {
        TERMINATE = 0x004E,
        SET_CHAR_COORDINATES = 0x00A1,      // char, x, y, z (z <= -100: the ground); moves the vehicle it drives
        CREATE_CAR = 0x00A5,                // model, x, y, z, out handle (cars, bikes, boats)
        SET_CAR_COORDINATES = 0x00AB,
        SET_TIME_OF_DAY = 0x00C0,
        ADD_SCORE = 0x010E,                 // player, amount
        ALTER_WANTED_LEVEL = 0x0112,        // player, level
        CLEAR_WANTED_LEVEL = 0x0115,        // player
        SET_TIME_SCALE = 0x0162,
        SET_CAR_HEADING = 0x017A,           // car, degrees (the matrix rebuilt upright)
        FORCE_WEATHER_NOW = 0x01BB,
        RELEASE_WEATHER = 0x01BC,
        MARK_CAR_AS_NO_LONGER_NEEDED = 0x01C8,
        SET_POLICE_IGNORE_PLAYER = 0x01FC,  // player, on
        SET_CAR_HEALTH = 0x0229,
        CHANGE_CAR_COLOUR = 0x022E,
        REQUEST_MODEL = 0x024C,
        MARK_MODEL_AS_NO_LONGER_NEEDED = 0x024E,
        SET_CHAR_PROOFS = 0x02B0,           // char, bullet, fire, explosion, collision, melee
        SET_CAR_PROOFS = 0x02B1,
        RESTORE_CAMERA_JUMPCUT = 0x02F0,
        SET_PLAYER_NEVER_GETS_TIRED = 0x0335,
        SET_PLAYER_FAST_RELOAD = 0x0336,
        WARP_CHAR_FROM_CAR_TO_COORD = 0x0367,
        WARP_CHAR_INTO_CAR = 0x036F,
        SET_EVERYONE_IGNORE_PLAYER = 0x03C4,
        LOAD_SCENE = 0x03D0,
        SET_UPSIDEDOWN_CAR_NOT_DAMAGED = 0x03F2,
        SET_CAR_FORWARD_SPEED = 0x04BF,
    };

    // Bytecode for one script: arguments carry a type byte (FUN_001094C8: 1 zero, 7 int8, 8 int16, 6 int32, 9 float;
    // a byte >= 0x0C a variable, 0x0C + n the local n). The same encoding as VCS's.
    class Scm
    {
    public:
        Scm &op(uint16_t code) { return put(code & 0xFF).put(code >> 8); }
        Scm &i(int32_t v)
        {
            if (v == 0)
                return put(0x01);
            if (v >= -128 && v < 128)
                return put(0x07).put(static_cast<uint8_t>(v));
            if (v >= -32768 && v < 32768)
                return put(0x08).put(v & 0xFF).put((v >> 8) & 0xFF);
            put(0x06);
            for (int k = 0; k < 4; ++k)
                put((static_cast<uint32_t>(v) >> (8 * k)) & 0xFF);
            return *this;
        }
        Scm &f(float v)
        {
            uint32_t bits;
            std::memcpy(&bits, &v, 4);
            put(0x09);
            for (int k = 0; k < 4; ++k)
                put((bits >> (8 * k)) & 0xFF);
            return *this;
        }
        Scm &b(bool v) { return i(v ? 1 : 0); }
        Scm &player() { return i(0); } // the player commands take the player's index
        Scm &local(int n) { return put(static_cast<uint8_t>(0x0C + n)); }
        Scm &raw(uint8_t byte) { return put(byte); }
        bool empty() const { return code.empty(); }
        std::vector<uint8_t> finished() const
        {
            std::vector<uint8_t> out = code;
            out.push_back(TERMINATE & 0xFF);
            out.push_back(TERMINATE >> 8);
            return out;
        }

    private:
        Scm &put(uint32_t byte)
        {
            code.push_back(static_cast<uint8_t>(byte));
            return *this;
        }
        std::vector<uint8_t> code;
    };

    // ---- Static tables ----------------------------------------------------------------------------------------------
    struct WeaponDef
    {
        int type;
        const char *name;
        int ammo;
    };
    // LCS's weapon types (the weapon table: 37 entries, 0 fists; 16 the rocket, 34 the detonator, 35 the helicopter's
    // gun are no weapon to carry). Names by model and the cheats' sets (docs/LCS.md "Trainer").
    constexpr WeaponDef kWeapons[] = {
        {1, "Brass Knuckles", 1},    {2, "Screwdriver", 1},       {3, "Golf Club", 1},        {4, "Nightstick", 1},
        {5, "Knife", 1},             {6, "Baseball Bat", 1},      {7, "Hammer", 1},           {8, "Meat Cleaver", 1},
        {9, "Machete", 1},           {10, "Katana", 1},           {11, "Chainsaw", 1},        {12, "Grenades", 25},
        {13, "Remote Grenades", 25}, {14, "Tear Gas", 25},        {15, "Molotovs", 25},       {17, "Pistol", 200},
        {18, ".357 Magnum", 100},    {19, "Shotgun", 100},        {20, "Combat Shotgun", 100}, {21, "Stubby Shotgun", 100},
        {22, "Tec-9", 500},          {23, "Micro SMG", 500},      {24, "Silenced MAC-10", 500}, {25, "SMG", 500},
        {26, "M4", 500},             {27, "AK-47", 500},          {28, "Sniper Rifle", 50},   {29, "Laser Sniper", 50},
        {30, "Rocket Launcher", 25}, {31, "Flamethrower", 500},   {32, "M60", 500},           {33, "Minigun", 1000},
        {36, "Camera", 50},
    };
    const WeaponDef *weaponDef(int type)
    {
        for (const WeaponDef &w : kWeapons)
            if (w.type == type)
                return &w;
        return nullptr;
    }

    struct LoadoutDef
    {
        const char *name;
        std::array<std::pair<int, int>, 9> weapons; // type, ammo (the cheat's); type 0 ends the list
    };
    // The game's three weapon cheats, weapon for weapon and with their ammunition.
    constexpr LoadoutDef kLoadouts[] = {
        {"Set 1", {{{1, 1}, {5, 1}, {15, 10}, {17, 100}, {19, 50}, {22, 150}, {27, 120}, {28, 25}, {31, 250}}}},
        {"Set 2", {{{10, 1}, {13, 10}, {18, 40}, {21, 25}, {24, 100}, {26, 150}, {29, 21}, {30, 5}, {0, 0}}}},
        {"Set 3", {{{11, 1}, {12, 10}, {18, 40}, {20, 30}, {25, 100}, {26, 150}, {29, 21}, {33, 500}, {0, 0}}}},
    };

    struct LandmarkDef
    {
        const char *zoneKey; // the district's name in the game's text
        const char *fallback;
        float x, y, z;       // a street in it: a road node of the paths (z the road's)
    };
    // The districts of the zone table (CTheZones, *0x3D7750: 0x38 bytes each, the key first, then the box). Each spot is
    // the car path node nearest the district's centre, inside it and out of tunnels (the path nodes: *(*0x3D0314), 0x14
    // each, x / y / z int16 * 1/8 at +4; car nodes first, their count u16 at +0x18). The box centres put him on
    // rooftops, in the sea and in mid-air (docs/LCS.md "Trainer").
    constexpr LandmarkDef kLandmarks[] = {
        // Portland
        {"PORT_W", "Callahan Point", 904.4f, -1022.4f, 6.3f},     {"PORT_S", "Atlantic Quays", 1253.1f, -1098.6f, 10.6f},
        {"PORT_E", "Portland Harbor", 1528.6f, -823.1f, 10.8f},   {"PORT_I", "Trenton", 1197.8f, -879.0f, 13.8f},
        {"CHINA", "Chinatown", 900.8f, -686.3f, 13.8f},           {"REDLIGH", "Red Light District", 910.8f, -358.8f, 10.4f},
        {"TOWERS", "Hepburn Heights", 910.8f, -183.8f, 3.8f},     {"LITTLEI", "Saint Mark's", 1230.8f, -283.8f, 26.4f},
        {"HARWOOD", "Harwood", 1024.3f, 110.6f, 6.3f},            {"EASTBAY", "Portland Beach", 1426.3f, -183.6f, 49.9f},
        {"S_VIEW", "Portland View", 1225.0f, -619.4f, 18.8f},
        // Staunton Island
        {"CONSTRU", "Fort Staunton", 451.8f, -216.4f, 20.1f},     {"STADIUM", "Aspatria", -145.5f, 14.5f, 14.9f},
        {"YAKUSA", "Torrington", 406.8f, -1376.4f, 24.9f},        {"SHOPING", "Bedford Point", -3.1f, -1366.4f, 24.9f},
        {"COM_EAS", "Newport", 406.8f, -736.4f, 24.9f},           {"PARK", "Belleville Park", -8.1f, -691.3f, 25.0f},
        {"UNIVERS", "Liberty Campus", 179.5f, -207.0f, 16.6f},    {"HOSPI_2", "Rockford", 353.4f, 80.0f, 14.5f},
        // Shoreside Vale
        {"AIRPORT", "Francis International Airport", -1148.8f, -712.5f, 10.0f},
        {"PROJECT", "Wichita Gardens", -574.1f, -86.3f, 2.6f},    {"SWANKS", "Cedar Grove", -573.0f, 366.9f, 76.8f},
        {"SUB_IND", "Pike Creek", -1117.4f, -92.5f, 45.3f},       {"BIG_DAM", "Cochrane Dam", -1156.0f, 415.1f, 22.5f},
    };

    constexpr const char *kWeatherNames[] = {"Sunny", "Cloudy", "Rainy", "Foggy", "Extra sunny"};

    // ---- State ------------------------------------------------------------------------------------------------------
    uint8_t *g_rdram = nullptr;
    std::atomic<bool> g_toggles[tr::ToggleCount] = {};
    std::atomic<bool> g_ready{false}, g_inVehicle{false}, g_missionRunning{false};
    std::atomic<int> g_speed{100}, g_clockHour{12};
    uint32_t g_lastClock = 0;
    int g_stillFrames = 0;
    uint64_t g_runFrames = 0;

    std::mutex g_noticesMutex;
    std::vector<tr::Notice> g_notices;

    // the weapons' guest calls (game thread takes them at the frame boundary)
    struct Step
    {
        uint32_t fn = 0;
        bool ped = false; // a0 is the player's ped (taken when it runs)
        uint32_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
        std::string notice; // posted when it runs
    };
    std::mutex g_stepsMutex;
    std::deque<Step> g_steps;

    // the scripted actions
    struct Action
    {
        // game thread: fill `code` (or do the work itself and leave it empty) and set the notice; false refuses,
        // with the reason in the notice
        std::function<bool(uint8_t *ram, Scm &code, std::string &notice)> build;
        ps2control::Reply reply; // the control channel's, or empty
        std::vector<int> models; // loaded before build() runs
    };
    std::mutex g_actionsMutex;
    std::deque<Action> g_actions;

    struct Running // a trainer script started (game thread)
    {
        uint32_t slot = 0, id = 0;
        bool active(const uint8_t *ram) const;
    };
    struct Pending
    {
        Running script;
        Action action;
        std::string notice;
    };
    std::vector<Pending> g_pending;
    Running g_tickScript;
    // An action waiting for its models: requested with REQUEST_MODEL, then polled each frame (a synchronous load can
    // hang when the streaming memory is full - VCS, 2026-10-26).
    struct Loading
    {
        bool active = false, requested = false;
        Action action;
        uint64_t deadline = 0;
        Running script;
    };
    Loading g_loading;
    constexpr uint64_t kLoadFrames = 600; // ~10 s at 60 fps
    uint32_t g_protectedCar = 0;           // the vehicle VehicleInvincible last set its proofs on
    int g_frozenHour = 12, g_frozenMinute = 0;

    std::mutex g_namesMutex;
    uint32_t g_namesFrom = 0; // the text entries the names were built from
    std::vector<Entry> g_vehicleNames, g_landmarkNames;
    std::unordered_map<int, int> g_vehicleClass; // model -> class (0 car, 1 boat, 5 bike, ...)

    struct Saved
    {
        bool valid = false;
        float x = 0, y = 0, z = 0;
    };
    Saved g_saved; // game thread
    std::atomic<bool> g_hasSaved{false};

    std::string fmt(const char *format, ...)
    {
        char buffer[256];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        return buffer;
    }

    uint32_t rd32(const uint8_t *ram, uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, ram + (a & kRamMask), 4);
        return v;
    }
    void wr32(uint8_t *ram, uint32_t a, uint32_t v) { std::memcpy(ram + (a & kRamMask), &v, 4); }
    float rdf(const uint8_t *ram, uint32_t a)
    {
        float v;
        std::memcpy(&v, ram + (a & kRamMask), 4);
        return v;
    }
    void wrf(uint8_t *ram, uint32_t a, float v) { std::memcpy(ram + (a & kRamMask), &v, 4); }
    bool inRam(uint32_t a) { return a >= 0x00100000u && a < 0x02000000u; }

    uint32_t player(const uint8_t *ram)
    {
        const uint32_t ped = ram ? rd32(ram, kPlayerPed) : 0u;
        return inRam(ped) ? ped : 0u;
    }
    uint32_t vehicleOf(const uint8_t *ram, uint32_t ped)
    {
        const uint32_t car = rd32(ram, ped + kPedVehicle);
        return ped && (rd32(ram, ped + kPedDriving) & 0xFFu) && inRam(car) ? car : 0u;
    }
    bool alive(const uint8_t *ram, uint32_t ped) { return ped && rdf(ram, ped + kPedHealth) > 0.0f; }

    // A pool object's script handle: its index << 8 | its flags byte (CPool::GetIndex; GetAt 0x112068 checks it).
    int32_t handleOf(const uint8_t *ram, uint32_t poolPtr, uint32_t size, uint32_t object)
    {
        const uint32_t pool = rd32(ram, poolPtr);
        const uint32_t base = rd32(ram, pool), flags = rd32(ram, pool + 4);
        const uint32_t index = (object - base) / size;
        return static_cast<int32_t>(index << 8 | ram[(flags + index) & kRamMask]);
    }
    int32_t charHandle(const uint8_t *ram, uint32_t ped) { return handleOf(ram, kPedPool, kPedSize, ped); }
    int32_t carHandle(const uint8_t *ram, uint32_t car) { return handleOf(ram, kVehiclePool, kVehicleSize, car); }

    bool modelLoaded(const uint8_t *ram, int model)
    {
        const uint32_t info = rd32(ram, kStreamInfo);
        return inRam(info) && ram[(info + static_cast<uint32_t>(model) * 0x14u + 0xCu) & kRamMask] == 1;
    }

    void post(const std::string &text)
    {
        std::lock_guard<std::mutex> lock(g_noticesMutex);
        g_notices.push_back({text, tr::now(), false});
        if (g_notices.size() > 16)
            g_notices.erase(g_notices.begin());
        std::fprintf(stderr, "[trainer] %s\n", text.c_str());
    }

    void queue(std::function<bool(uint8_t *, Scm &, std::string &)> build, ps2control::Reply reply = {},
               std::vector<int> models = {})
    {
        std::lock_guard<std::mutex> lock(g_actionsMutex);
        g_actions.push_back({std::move(build), std::move(reply), std::move(models)});
    }

    void queueSteps(std::vector<Step> steps)
    {
        std::lock_guard<std::mutex> lock(g_stepsMutex);
        g_steps.insert(g_steps.end(), steps.begin(), steps.end());
    }

    // ---- Scripts on the game's VM -------------------------------------------------------------------------------
    bool Running::active(const uint8_t *ram) const
    {
        return slot && rd32(ram, slot + kScriptId) == id && ram[(slot + kScriptActive) & kRamMask] != 0;
    }

    void unlink(uint8_t *ram, uint32_t s, uint32_t head) // FUN_0010A1B8
    {
        const uint32_t next = rd32(ram, s), prev = rd32(ram, s + 4);
        if (prev)
            wr32(ram, prev, next);
        else
            wr32(ram, head, next);
        if (next)
            wr32(ram, next + 4, prev);
    }
    void linkAtHead(uint8_t *ram, uint32_t s, uint32_t head) // FUN_0010A1F0
    {
        const uint32_t first = rd32(ram, head);
        wr32(ram, s + 4, 0);
        wr32(ram, s, first);
        if (first)
            wr32(ram, first + 4, s);
        wr32(ram, head, s);
    }

    // CTheScripts::StartNewScript (0x10A210) done from the host, with the code in the script's own locals: take an
    // idle script, Init it (0x109A88: every field it sets is 0 but the death / arrest check), point its IP at the code
    // and put it at the head of the active list. Game thread, between frames only.
    Running startScript(uint8_t *ram, const std::vector<uint8_t> &code)
    {
        const uint32_t space = rd32(ram, kScriptSpacePtr);
        const uint32_t s = rd32(ram, kIdleScripts);
        if (!space || !inRam(s) || code.size() > kCodeMax)
            return {};
        unlink(ram, s, kIdleScripts);
        const uint32_t id = rd32(ram, kNextScriptId);
        wr32(ram, kNextScriptId, id + 1);
        std::memset(ram + (s & kRamMask), 0, kScriptSize);
        wr32(ram, s + kScriptId, id);
        std::memcpy(ram + ((s + kScriptName) & kRamMask), "trainer", 8);
        ram[(s + kScriptDeathArrest) & kRamMask] = 0; // not a mission: no death / arrest handling
        std::memcpy(ram + ((s + kCodeOffset) & kRamMask), code.data(), code.size());
        wr32(ram, s + kScriptIp, s + kCodeOffset - space); // (an offset below ScriptSpace: 32-bit wrap-around)
        linkAtHead(ram, s, kActiveScripts);
        ram[(s + kScriptActive) & kRamMask] = 1;
        return {s, id};
    }

    // A trainer script that has not run yet back to the idle list (the game paused before running it).
    void cancelScript(uint8_t *ram, const Running &r)
    {
        if (!r.active(ram))
            return;
        unlink(ram, r.slot, kActiveScripts);
        linkAtHead(ram, r.slot, kIdleScripts);
        ram[(r.slot + kScriptActive) & kRamMask] = 0;
    }

    // ---- Names from the game's text -----------------------------------------------------------------------------
    std::string keyAt(const uint8_t *ram, uint32_t a)
    {
        std::string key;
        for (uint32_t k = 0; k < 8; ++k)
        {
            const char c = static_cast<char>(ram[(a + k) & kRamMask]);
            if (!c)
                break;
            key += c;
        }
        return key;
    }

    // The game's text to UTF-8; its ~x~ formatting tokens dropped. Accented letters are its own codes: Latin-1 less
    // 0x40 (0x80..0xBF: 0xB3 o acute, 0xB1 n tilde, 0x81 A acute - the Spanish text, 2026-11-16); other codes above
    // 0x7F are the font's own symbols, left out.
    std::string textAt(const uint8_t *ram, uint32_t a)
    {
        std::string out;
        bool token = false;
        for (int n = 0; n < 64; ++n, a += 2)
        {
            uint16_t c = static_cast<uint16_t>(ram[a & kRamMask] | (ram[(a + 1) & kRamMask] << 8));
            if (c >= 0x80 && c < 0xC0)
                c = static_cast<uint16_t>(c + 0x40);
            else if (c >= 0xC0 && c < 0x100)
                continue;
            if (!c)
                break;
            if (c == '~')
            {
                token = !token;
                continue;
            }
            if (token)
                continue;
            if (c < 0x80)
                out += static_cast<char>(c);
            else if (c < 0x800)
            {
                out += static_cast<char>(0xC0 | (c >> 6));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
            else
            {
                out += static_cast<char>(0xE0 | (c >> 12));
                out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                out += static_cast<char>(0x80 | (c & 0x3F));
            }
        }
        return out;
    }

    std::string capitalised(std::string key) // PONTOON -> Pontoon
    {
        std::transform(key.begin() + (key.empty() ? 0 : 1), key.end(), key.begin() + (key.empty() ? 0 : 1),
                       [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
        return key;
    }

    constexpr uint32_t kMiGameName = 0x2Eu; // vehicle model info: the game name, a key of the text (8 chars)

    // Game thread: the name lists, once the game's text and models are in memory (again if the text is reloaded).
    void refreshNames(const uint8_t *ram)
    {
        const uint32_t text = rd32(ram, kText);
        if (!inRam(text))
            return;
        const uint32_t entries = rd32(ram, text);
        const uint32_t count = static_cast<uint16_t>(rd32(ram, text + 4));
        if (!inRam(entries) || !count || entries == g_namesFrom || g_runFrames % 60 != 0)
            return;
        const uint32_t infos = rd32(ram, kModelInfos), models = rd32(ram, kModelCount);
        if (!inRam(infos) || !models || models > 10000)
            return;
        std::unordered_map<std::string, std::string> names;
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint32_t e = entries + i * 12;
            names.emplace(keyAt(ram, e + 4), textAt(ram, rd32(ram, e)));
        }
        auto lookup = [&](const std::string &key, const std::string &fallback) {
            const auto it = names.find(key);
            return it != names.end() && !it->second.empty() ? it->second : fallback;
        };
        std::vector<Entry> vehicles;
        std::unordered_map<int, int> classes;
        for (uint32_t id = 0; id < models; ++id)
        {
            const uint32_t mi = rd32(ram, infos + id * 4);
            if (!inRam(mi) || ram[(mi + kMiType) & kRamMask] != 6)
                continue;
            const int cls = static_cast<int>(rd32(ram, mi + kMiClass));
            if (cls != 0 && cls != 1 && cls != 5) // CREATE_CAR makes cars, boats and bikes
                continue;
            // models 211..216 (RC planes, helicopters) are leftovers with no text: left out
            const std::string key = keyAt(ram, mi + kMiGameName);
            if (key.empty() || names.find(key) == names.end())
                continue;
            vehicles.push_back({lookup(key, capitalised(key)), static_cast<int>(id)});
            classes[static_cast<int>(id)] = cls;
        }
        std::sort(vehicles.begin(), vehicles.end(), [](const Entry &a, const Entry &b) { return a.name < b.name; });
        for (size_t k = 0; k < vehicles.size(); ++k) // models sharing a name (police and taxi variants): numbered
        {
            size_t end = k + 1;
            while (end < vehicles.size() && vehicles[end].name == vehicles[k].name)
                ++end;
            if (end - k > 1)
                for (size_t n = k; n < end; ++n)
                    vehicles[n].name += fmt(" (%zu)", n - k + 1);
            k = end - 1;
        }
        std::vector<Entry> landmarks;
        for (size_t k = 0; k < std::size(kLandmarks); ++k)
            landmarks.push_back({lookup(kLandmarks[k].zoneKey, kLandmarks[k].fallback), static_cast<int>(k)});
        std::sort(landmarks.begin(), landmarks.end(), [](const Entry &a, const Entry &b) { return a.name < b.name; });

        std::lock_guard<std::mutex> lock(g_namesMutex);
        g_vehicleNames = std::move(vehicles);
        g_vehicleClass = std::move(classes);
        g_landmarkNames = std::move(landmarks);
        if (!g_vehicleNames.empty()) // the text loads before the models register: until they do, again
            g_namesFrom = entries;
    }

    std::string nameOf(const std::vector<Entry> &list, int id, const std::string &fallback)
    {
        std::lock_guard<std::mutex> lock(g_namesMutex);
        for (const Entry &e : list)
            if (e.id == id)
                return e.name;
        return fallback;
    }

    // ---- Weapons (guest calls) ------------------------------------------------------------------------------------
    uint32_t weaponInfo(const uint8_t *ram, uint32_t type)
    {
        const uint32_t table = rd32(ram, kWeaponInfoPtr);
        if (!inRam(table) || !inRam(rd32(ram, table + 4)) || type > 36)
            return 0;
        return rd32(ram, table + 4) + type * kWeaponInfoSize;
    }

    // The weapons' models (one or two: the remote grenades' detonator, the minigun's second part), from the table.
    std::vector<uint32_t> weaponModels(int type)
    {
        std::vector<uint32_t> out;
        if (const uint32_t info = g_rdram ? weaponInfo(g_rdram, static_cast<uint32_t>(type)) : 0u)
            for (uint32_t off : {0x60u, 0x64u})
            {
                const int32_t m = static_cast<int32_t>(rd32(g_rdram, info + off));
                if (m > 0 && m < 0x4000)
                    out.push_back(static_cast<uint32_t>(m));
            }
        return out;
    }

    // Weapons given as the cheats give them: every model requested, one load, the weapons, the models released.
    void queueWeapons(const std::vector<std::pair<int, int>> &list, int select, const std::string &notice)
    {
        std::vector<uint32_t> all;
        for (const auto &[type, ammo] : list)
            for (uint32_t m : weaponModels(type))
                if (std::find(all.begin(), all.end(), m) == all.end())
                    all.push_back(m);
        std::vector<Step> steps;
        for (uint32_t m : all)
            steps.push_back({kRequestModel, false, m, 1u});
        steps.push_back({kLoadRequested, false, 0u});
        for (const auto &[type, ammo] : list)
            steps.push_back({kGiveWeapon, true, 0u, static_cast<uint32_t>(type), static_cast<uint32_t>(ammo), 1u});
        if (select > 0)
            steps.push_back({kSetCurrentWeapon, true, 0u, static_cast<uint32_t>(select)});
        for (uint32_t m : all)
            steps.push_back({kModelDeletable, false, m});
        steps.back().notice = notice;
        queueSteps(std::move(steps));
    }

    // ---- The held toggles ---------------------------------------------------------------------------------------
    // Every frame: values written directly. Every 10 running frames: one script for the commands that must be
    // repeated (a mission, death or arrest can reset them).
    void holdValues(uint8_t *ram, uint32_t ped)
    {
        if (tr::toggle(tr::GodMode))
        {
            const float most = static_cast<float>(ram[kMaxHealth]);
            if (rdf(ram, ped + kPedHealth) > 0.0f && rdf(ram, ped + kPedHealth) < most)
                wrf(ram, ped + kPedHealth, most);
        }
        const bool unlimited = tr::toggle(tr::UnlimitedAmmo), noReload = tr::toggle(tr::NoReload);
        if (unlimited || noReload)
            for (int s = 0; s < kSlots; ++s)
            {
                const uint32_t slot = ped + kPedWeapons + static_cast<uint32_t>(s) * kSlotSize;
                const uint32_t type = rd32(ram, slot);
                const uint32_t info = type ? weaponInfo(ram, type) : 0u;
                if (!info || rd32(ram, info) == 0u) // melee
                    continue;
                if (unlimited && rd32(ram, slot + 0xC) < kMaxAmmo)
                    wr32(ram, slot + 0xC, kMaxAmmo);
                const uint32_t clip = rd32(ram, info + 0x10), total = rd32(ram, slot + 0xC);
                if (noReload && clip > 1 && clip < 10000 && rd32(ram, slot + 8) < std::min(clip, total))
                    wr32(ram, slot + 8, std::min(clip, total));
            }
        if (tr::toggle(tr::VehicleInvincible))
            if (const uint32_t car = vehicleOf(ram, ped))
                if (rdf(ram, car + kVehHealth) < 1000.0f)
                    wrf(ram, car + kVehHealth, 1000.0f);
    }

    void tick(uint8_t *ram, uint32_t ped)
    {
        const bool refresh = g_runFrames % 120 == 0; // the commands that stick: every 2 s at 60 fps
        Scm code;
        if (tr::toggle(tr::GodMode) && refresh)
            code.op(SET_CHAR_PROOFS).i(charHandle(ram, ped)).b(true).b(true).b(true).b(true).b(true);
        if (tr::toggle(tr::NeverWanted) && rd32(ram, ped + kPedWanted) != 0)
            code.op(CLEAR_WANTED_LEVEL).player();
        if (refresh)
        {
            if (tr::toggle(tr::InfiniteSprint))
                code.op(SET_PLAYER_NEVER_GETS_TIRED).player().b(true);
            if (tr::toggle(tr::FastReload))
                code.op(SET_PLAYER_FAST_RELOAD).player().b(true);
            if (tr::toggle(tr::PoliceIgnore))
                code.op(SET_POLICE_IGNORE_PLAYER).player().b(true);
            if (tr::toggle(tr::EveryoneIgnores))
                code.op(SET_EVERYONE_IGNORE_PLAYER).player().b(true);
        }
        const uint32_t car = vehicleOf(ram, ped);
        if (tr::toggle(tr::VehicleInvincible) && car && (car != g_protectedCar || refresh))
        {
            const int32_t h = carHandle(ram, car);
            code.op(SET_CAR_PROOFS).i(h).b(true).b(true).b(true).b(true).b(true);
            code.op(SET_UPSIDEDOWN_CAR_NOT_DAMAGED).i(h).b(true);
            g_protectedCar = car;
        }
        if (tr::toggle(tr::FreezeTime))
            code.op(SET_TIME_OF_DAY).i(g_frozenHour).i(g_frozenMinute);
        if (!code.empty())
            g_tickScript = startScript(ram, code.finished());
    }

    // What turning a toggle on or off does at once.
    void applyToggle(Toggle t, bool on)
    {
        queue([t, on](uint8_t *ram, Scm &code, std::string &notice) {
            static const char *const names[tr::ToggleCount] = {
                "God mode", "Never wanted", "Unlimited ammo", "No reload", "Infinite sprint", "Fast reload",
                "Police ignore you", "Everyone ignores you", "Vehicle invincibility", "Get into spawned vehicles",
                "Freeze time", "Free camera", "Hide HUD"};
            notice = std::string(names[t]) + (on ? " on" : " off");
            const uint32_t ped = player(ram);
            switch (t)
            {
            case tr::GodMode:
                code.op(SET_CHAR_PROOFS).i(charHandle(ram, ped)).b(on).b(on).b(on).b(on).b(on);
                if (on)
                    wrf(ram, ped + kPedHealth, static_cast<float>(ram[kMaxHealth]));
                break;
            case tr::NeverWanted:
                if (on)
                    code.op(CLEAR_WANTED_LEVEL).player();
                break;
            case tr::InfiniteSprint: code.op(SET_PLAYER_NEVER_GETS_TIRED).player().b(on); break;
            case tr::FastReload: code.op(SET_PLAYER_FAST_RELOAD).player().b(on); break;
            case tr::PoliceIgnore: code.op(SET_POLICE_IGNORE_PLAYER).player().b(on); break;
            case tr::EveryoneIgnores: code.op(SET_EVERYONE_IGNORE_PLAYER).player().b(on); break;
            case tr::VehicleInvincible:
                g_protectedCar = 0;
                if (const uint32_t car = vehicleOf(ram, ped)) // the tick protects it; off gives it its damage back
                {
                    const int32_t h = carHandle(ram, car);
                    code.op(SET_CAR_PROOFS).i(h).b(on).b(on).b(on).b(on).b(on);
                    code.op(SET_UPSIDEDOWN_CAR_NOT_DAMAGED).i(h).b(on);
                }
                break;
            case tr::FreezeTime:
                g_frozenHour = static_cast<int>(rd32(ram, kClockHours) & 0xFF);
                g_frozenMinute = static_cast<int>(rd32(ram, kClockMinutes) & 0xFF);
                if (on)
                    notice += fmt(" at %02d:%02d", g_frozenHour, g_frozenMinute);
                break;
            default: break;
            }
            return true;
        });
    }

    // ---- The game thread's queue ----------------------------------------------------------------------------------
    void finish(Pending &p, bool ok, const std::string &why = {})
    {
        const std::string text = ok ? p.notice : why;
        if (!text.empty())
            post(text);
        if (p.action.reply)
            p.action.reply(ok ? "ok " + p.notice : "error " + why);
    }

    // In order, until one starts a script: the next waits until it has run (a frame), so an action sees what the ones
    // before it did and a control-channel answer queued last comes after all of them.
    void runActions(uint8_t *ram, uint32_t ped)
    {
        for (;;)
        {
            Action a;
            if (g_loading.active)
            {
                if (!g_loading.requested) // (again, after a pause took the request back out)
                {
                    Scm request;
                    for (int m : g_loading.action.models)
                        request.op(REQUEST_MODEL).i(m);
                    g_loading.script = startScript(ram, request.finished());
                    g_loading.requested = g_loading.script.slot != 0;
                    return;
                }
                const bool loaded = std::all_of(g_loading.action.models.begin(), g_loading.action.models.end(),
                                                [ram](int m) { return modelLoaded(ram, m); });
                if (!loaded && g_runFrames < g_loading.deadline)
                    return;
                a = std::move(g_loading.action);
                g_loading.active = false;
                if (!loaded)
                {
                    Scm release;
                    for (int m : a.models)
                        release.op(MARK_MODEL_AS_NO_LONGER_NEEDED).i(m);
                    startScript(ram, release.finished());
                    Pending p{{}, std::move(a), {}};
                    finish(p, false, "Could not load it: the game's memory for models is full here (leave the area or "
                                     "lose some spawned vehicles first)");
                    return;
                }
                a.models.clear();
            }
            else
            {
                std::lock_guard<std::mutex> lock(g_actionsMutex);
                if (g_actions.empty())
                    return;
                a = std::move(g_actions.front());
                g_actions.pop_front();
            }
            if (!a.models.empty())
            {
                g_loading = {true, false, std::move(a), g_runFrames + kLoadFrames, {}};
                continue;
            }
            Pending p{{}, std::move(a), {}};
            Scm code;
            std::string notice;
            if (!alive(ram, ped))
            {
                finish(p, false, "Not now: the player is down");
                continue;
            }
            if (!p.action.build(ram, code, notice))
            {
                finish(p, false, notice);
                continue;
            }
            p.notice = notice;
            if (code.empty())
            {
                finish(p, true);
                continue;
            }
            const std::vector<uint8_t> bytes = code.finished();
            p.script = startScript(ram, bytes);
            if (!p.script.slot)
            {
                finish(p, false, bytes.size() > kCodeMax ? "Internal error: script too long" : "The game's script slots are all in use");
                continue;
            }
            g_pending.push_back(std::move(p));
            return;
        }
    }

    // ---- Actions ----------------------------------------------------------------------------------------------------
    void notAvailable() { post("Not available in Liberty City Stories yet"); }

    // The vehicle commands need the player in one (a vehicle command on no vehicle stops the script for good).
    void vehicleAction(const char *done, std::function<void(uint8_t *, uint32_t, int32_t, Scm &)> body)
    {
        queue([done, body](uint8_t *ram, Scm &code, std::string &notice) {
            const uint32_t car = vehicleOf(ram, player(ram));
            if (!car)
                return (notice = "Not in a vehicle", false);
            body(ram, car, carHandle(ram, car), code);
            notice = done;
            return true;
        });
    }

    float headingDegrees(const uint8_t *ram, uint32_t car) // from the matrix's forward vector
    {
        const float fx = rdf(ram, car + kVehForward), fy = rdf(ram, car + kVehForward + 4);
        return std::atan2(-fx, fy) * 57.29578f;
    }

    // The scene loaded around the spot, then onto the ground there (z -100) or the height given. SET_CHAR_COORDINATES
    // moves the vehicle when the player drives one.
    void moveTo(uint8_t *ram, Scm &code, float x, float y, float z)
    {
        code.op(LOAD_SCENE).f(x).f(y).f(z < -99.0f ? 20.0f : z);
        code.op(SET_CHAR_COORDINATES).i(charHandle(ram, player(ram))).f(x).f(y).f(z);
    }

    std::vector<std::string> words(const std::string &args)
    {
        std::istringstream in(args);
        std::vector<std::string> out;
        for (std::string w; in >> w;)
            out.push_back(w);
        return out;
    }

    std::string listReply(const std::vector<Entry> &list)
    {
        std::string out = "ok";
        for (const Entry &e : list)
            out += fmt(" %d:", e.id) + e.name + ";";
        return out;
    }

    // Control channel: trainer <command> [arg]. Scripted actions answer once the game has run them.
    void command(const std::string &args, ps2control::Reply reply)
    {
        static const std::pair<const char *, Toggle> kToggleNames[] = {
            {"god", tr::GodMode},          {"wanted", tr::NeverWanted},      {"ammo", tr::UnlimitedAmmo},
            {"reload", tr::NoReload},      {"sprint", tr::InfiniteSprint},   {"fastreload", tr::FastReload},
            {"police", tr::PoliceIgnore},  {"ignore", tr::EveryoneIgnores},  {"vehicle", tr::VehicleInvincible},
            {"warp", tr::WarpIntoSpawned}, {"freeze", tr::FreezeTime},       {"hidehud", tr::HideHud},
        };
        const auto w = words(args);
        const std::string verb = w.empty() ? "" : w[0];
        const int arg = w.size() > 1 ? std::atoi(w[1].c_str()) : 0;
        auto answerWhenDone = [&reply] {
            queue([](uint8_t *, Scm &, std::string &) { return true; }, [reply](const std::string &) {
                const std::vector<tr::Notice> n = tr::notices(5.0);
                reply("ok " + (n.empty() ? std::string() : n.back().text));
            });
        };
        if (verb == "state")
        {
            size_t steps;
            {
                std::lock_guard<std::mutex> lock(g_stepsMutex);
                steps = g_steps.size();
            }
            std::string out = fmt("ok ready=%d vehicle=%d speed=%d mission=%d pending=%zu", tr::ready() ? 1 : 0,
                                  tr::inVehicle() ? 1 : 0, tr::gameSpeed(), tr::missionRunning() ? 1 : 0, steps);
            for (const auto &[name, t] : kToggleNames)
                out += fmt(" %s=%d", name, tr::toggle(t) ? 1 : 0);
            return reply(out);
        }
        if (verb == "toggle" && w.size() >= 3)
        {
            for (const auto &[name, t] : kToggleNames)
                if (w[1] == name)
                {
                    tr::setToggle(t, w[2] == "1" || w[2] == "on");
                    return answerWhenDone();
                }
            return reply("error unknown toggle");
        }
        if (verb == "ammo" || verb == "noreload") // (the first version's spelling)
        {
            tr::setToggle(verb == "ammo" ? tr::UnlimitedAmmo : tr::NoReload, arg != 0);
            return answerWhenDone();
        }
        if (verb == "list" && w.size() >= 2)
        {
            if (w[1] == "vehicles")
                return reply(listReply(tr::vehicles()));
            if (w[1] == "weapons")
                return reply(listReply(tr::weapons()));
            if (w[1] == "landmarks")
                return reply(listReply(tr::landmarks()));
            if (w[1] == "weathers")
                return reply(listReply(tr::weathers()));
            return reply("error usage: trainer list vehicles|weapons|landmarks|weathers");
        }
        if (verb == "notices")
        {
            std::string out = "ok";
            for (const tr::Notice &n : tr::notices(1e9))
                out += " " + n.text + ";";
            return reply(out);
        }
        if (verb == "scm" && w.size() >= 2) // raw bytecode as a trainer script (tests): scm <hex>
        {
            std::vector<uint8_t> bytes;
            for (size_t k = 0; k + 1 < w[1].size(); k += 2)
                bytes.push_back(static_cast<uint8_t>(std::strtoul(w[1].substr(k, 2).c_str(), nullptr, 16)));
            queue([bytes](uint8_t *, Scm &code, std::string &notice) {
                for (uint8_t b : bytes)
                    code.raw(b);
                notice = "script ran";
                return true;
            });
            return answerWhenDone();
        }
        // weapons answer at once (their guest calls run at the next frame boundary)
        if (verb == "weapon" || verb == "loadout" || verb == "noweapons")
        {
            if (verb == "weapon")
                tr::giveWeapon(arg);
            else if (verb == "loadout")
                tr::giveLoadout(arg);
            else
                tr::removeWeapons();
            return reply(fmt("ok ready=%d", tr::ready() ? 1 : 0));
        }
        if (!tr::ready())
            return reply("error not in game");
        using Fn = std::function<void()>;
        const std::pair<const char *, Fn> kActions[] = {
            {"heal", [] { tr::heal(); }},
            {"money", [arg] { tr::addMoney(arg); }},
            {"setwanted", [arg] { tr::setWanted(arg); }},
            {"spawn", [arg] { tr::spawnVehicle(arg); }},
            {"repair", [] { tr::repairVehicle(); }},
            {"flip", [] { tr::flipVehicle(); }},
            {"boost", [] { tr::boostVehicle(); }},
            {"paint", [] { tr::paintVehicle(); }},
            {"teleport", [arg] { tr::teleport(arg); }},
            {"savepos", [] { tr::savePosition(); }},
            {"returnpos", [] { tr::returnToSaved(); }},
            {"time", [arg] { tr::setTime(arg); }},
            {"weather", [arg] { tr::setWeather(arg); }},
            {"speed", [arg] { tr::setGameSpeed(arg); }},
        };
        for (const auto &[name, fn] : kActions)
            if (verb == name)
            {
                fn();
                return answerWhenDone();
            }
        reply("error usage: trainer state | toggle <name> 0|1 | list <what> | notices | heal | money N | setwanted N | "
              "weapon T | loadout N | noweapons | spawn MODEL | repair | flip | boost | paint | teleport N | savepos | "
              "returnpos | time H | weather W | speed PERCENT");
    }
}

namespace lcs::trainer
{
    void install(PS2Runtime &runtime)
    {
        g_rdram = runtime.memory().getRDRAM();
        g_toggles[tr::WarpIntoSpawned] = true; // a spawned vehicle is to be driven
        ps2control::addCommand("trainer", command);
    }

    void onFrame(uint8_t *rdram)
    {
        g_rdram = rdram;
        const uint32_t ped = player(rdram);
        const uint32_t clock = rd32(rdram, kGameClock);
        // the clock stands still in the pause menu and on loading screens: nothing then
        g_stillFrames = clock == g_lastClock ? g_stillFrames + 1 : 0;
        g_lastClock = clock;
        const bool inGame = ped && rd32(rdram, kScriptSpacePtr) != 0u;
        const bool running = inGame && g_stillFrames < 3;
        g_ready.store(running, std::memory_order_relaxed);
        g_inVehicle.store(inGame && vehicleOf(rdram, ped) != 0, std::memory_order_relaxed);
        g_missionRunning.store(inGame && rdram[kMissionRunning] != 0, std::memory_order_relaxed);
        g_clockHour.store(static_cast<int>(rd32(rdram, kClockHours) & 0xFF), std::memory_order_relaxed);
        if (inGame)
            refreshNames(rdram);

        // Scripts finish in the frame they start. One still waiting means the game paused before running it (its
        // menu, the save screen): take it back out so it cannot be saved, and run it once the game runs again.
        const bool clockRuns = g_stillFrames == 0;
        for (auto it = g_pending.begin(); it != g_pending.end();)
        {
            if (!it->script.active(rdram))
            {
                finish(*it, true);
                it = g_pending.erase(it);
            }
            else if (!clockRuns)
            {
                cancelScript(rdram, it->script);
                std::lock_guard<std::mutex> lock(g_actionsMutex);
                g_actions.push_front(std::move(it->action));
                it = g_pending.erase(it);
            }
            else
                ++it;
        }
        if (!clockRuns)
        {
            cancelScript(rdram, g_tickScript);
            if (g_loading.active && g_loading.script.active(rdram))
            {
                cancelScript(rdram, g_loading.script);
                g_loading.requested = false;
            }
        }
        if (!running || !clockRuns)
            return;
        ++g_runFrames;
        if (alive(rdram, ped))
            holdValues(rdram, ped);
        if (!g_pending.empty())
            return;
        runActions(rdram, ped);
        if (g_pending.empty() && g_runFrames % 10 == 0 && alive(rdram, ped) && !g_tickScript.active(rdram))
            tick(rdram, ped);
    }

    bool chainNext(uint8_t *rdram, R5900Context *ctx)
    {
        g_rdram = rdram;
        const uint32_t ped = player(rdram);
        if (!g_ready.load(std::memory_order_relaxed) || !ped)
            return false;
        Step step;
        {
            std::lock_guard<std::mutex> lock(g_stepsMutex);
            if (g_steps.empty())
                return false;
            step = std::move(g_steps.front());
            g_steps.pop_front();
        }
        if (!step.notice.empty())
            post(step.notice);
        SET_GPR_U32(ctx, 4, step.ped ? ped : step.a0);
        SET_GPR_U32(ctx, 5, step.a1);
        SET_GPR_U32(ctx, 6, step.a2);
        SET_GPR_U32(ctx, 7, step.a3);
        ctx->pc = step.fn; // returns into the limiter (lcs_overrides.cpp frameBoundary)
        return true;
    }
}

// ---- game/vcs_trainer.h's interface (the menu's) ------------------------------------------------------------------
namespace vcs::trainer
{
    void install(PS2Runtime &) {}
    void setMissionScript(const std::vector<uint8_t> &) {}
    void onGameFrame(uint8_t *) {}

    bool toggle(Toggle t) { return t >= 0 && t < ToggleCount && g_toggles[t].load(std::memory_order_relaxed); }
    void setToggle(Toggle t, bool on)
    {
        if (t < 0 || t >= ToggleCount || t == FreeCamera)
            return notAvailable();
        if (g_toggles[t].exchange(on) == on)
            return;
        if (t == UnlimitedAmmo || t == NoReload || t == HideHud || t == WarpIntoSpawned)
        {
            static const char *const names[] = {"Unlimited ammo", "No reload", "Hide HUD", "Get into spawned vehicles"};
            const int k = t == UnlimitedAmmo ? 0 : t == NoReload ? 1 : t == HideHud ? 2 : 3;
            return post(std::string(names[k]) + (on ? " on" : " off"));
        }
        applyToggle(t, on);
    }
    bool ready() { return g_ready.load(std::memory_order_relaxed); }
    bool inVehicle() { return g_inVehicle.load(std::memory_order_relaxed); }

    void heal()
    {
        queue([](uint8_t *ram, Scm &, std::string &notice) {
            const uint32_t ped = player(ram);
            wrf(ram, ped + kPedHealth, static_cast<float>(ram[kMaxHealth]));
            wrf(ram, ped + kPedArmour, static_cast<float>(ram[kMaxArmour]));
            notice = "Health and armour filled";
            return true;
        });
    }

    void addMoney(int amount)
    {
        queue([amount](uint8_t *, Scm &code, std::string &notice) {
            code.op(ADD_SCORE).player().i(amount);
            notice = fmt("$%d added", amount);
            return true;
        });
    }

    void setWanted(int stars)
    {
        stars = std::clamp(stars, 0, 6);
        queue([stars](uint8_t *ram, Scm &code, std::string &notice) {
            if (stars > 0 && toggle(NeverWanted))
                return (notice = "Never wanted is on", false);
            // a mission can hold the level down (the game's own cap: the first mission keeps it at 0)
            const int most = static_cast<int>(rd32(ram, kMaxWanted));
            if (stars > 0 && most < 1)
                return (notice = "This mission keeps the police away: no wanted level now", false);
            const int level = std::min(stars, most);
            if (level > 0)
                code.op(ALTER_WANTED_LEVEL).player().i(level);
            else
                code.op(CLEAR_WANTED_LEVEL).player();
            notice = level ? fmt("Wanted level %d", level) : "Wanted level cleared";
            if (level < stars)
                notice += fmt(" (this mission allows at most %d)", most);
            return true;
        });
        // a frame later (the scripts have all run): some missions clear it every frame (the first one does: even
        // the level written straight into CWanted reads 0 a frame later)
        if (stars > 0)
            queue([stars](uint8_t *ram, Scm &, std::string &notice) {
                const int level = static_cast<int>(rd32(ram, player(ram) + kPedWanted));
                if (level == 0 && !toggle(NeverWanted) && rd32(ram, kMaxWanted) > 0)
                    return (notice = "This mission keeps the wanted level down", false);
                return true;
            });
    }

    void giveWeapon(int type)
    {
        const WeaponDef *w = weaponDef(type);
        if (!w)
            return post("No such weapon");
        if (!ready())
            return post("Only while you play (not paused)");
        queueWeapons({{w->type, w->ammo}}, inVehicle() ? 0 : w->type, std::string(w->name) + " given");
    }
    void giveLoadout(int which)
    {
        if (which < 0 || which >= static_cast<int>(std::size(kLoadouts)))
            return post("No such loadout");
        if (!ready())
            return post("Only while you play (not paused)");
        std::vector<std::pair<int, int>> list;
        for (const auto &w : kLoadouts[which].weapons)
            if (w.first)
                list.push_back(w);
        queueWeapons(list, 0, std::string("Weapon ") + kLoadouts[which].name + " given");
    }
    void removeWeapons()
    {
        if (!ready())
            return post("Only while you play (not paused)");
        queueSteps({{kClearWeapons, true, 0u, 0u, 0u, 0u, "Weapons removed"}});
    }

    // Ahead of the player (of the vehicle when driving), facing the same way; the player put behind the wheel with
    // Get into spawned vehicles. Then handed back to the game: it stays while it is near or used, like any car.
    void spawnVehicle(int model)
    {
        int cls = -1;
        {
            std::lock_guard<std::mutex> lock(g_namesMutex);
            const auto it = g_vehicleClass.find(model);
            if (it != g_vehicleClass.end())
                cls = it->second;
        }
        if (cls < 0)
            return post("No such vehicle");
        const std::string name = nameOf(g_vehicleNames, model, fmt("Vehicle %d", model));
        queue([model, name](uint8_t *ram, Scm &code, std::string &notice) {
            const uint32_t ped = player(ram);
            const uint32_t car = vehicleOf(ram, ped);
            const bool warp = toggle(WarpIntoSpawned);
            float x, y, z, heading;
            if (car)
            {
                heading = headingDegrees(ram, car);
                const float fx = rdf(ram, car + kVehForward), fy = rdf(ram, car + kVehForward + 4);
                const float len = std::max(0.01f, std::sqrt(fx * fx + fy * fy));
                x = rdf(ram, car + kPos) + fx / len * 8.0f;
                y = rdf(ram, car + kPos + 4) + fy / len * 8.0f;
                z = rdf(ram, car + kPos + 8) + 0.5f;
            }
            else
            {
                const float h = rdf(ram, ped + kPedHeading);
                heading = h * 57.29578f;
                x = rdf(ram, ped + kPos) - std::sin(h) * 5.0f;
                y = rdf(ram, ped + kPos + 4) + std::cos(h) * 5.0f;
                z = rdf(ram, ped + kPos + 8);
            }
            const int32_t me = charHandle(ram, ped);
            code.op(REQUEST_MODEL).i(model);
            code.op(CREATE_CAR).i(model).f(x).f(y).f(z).local(0);
            code.op(SET_CAR_HEADING).local(0).f(heading);
            if (warp)
            {
                // the old vehicle stopped first: driverless, a moving one ran into the new one and knocked him off
                if (car)
                    std::memset(ram + ((car + kVehSpeed) & kRamMask), 0, 0x20);
                if (car)
                    code.op(WARP_CHAR_FROM_CAR_TO_COORD).i(me).f(x).f(y).f(z);
                code.op(WARP_CHAR_INTO_CAR).i(me).local(0);
                // from one vehicle straight into another the camera would keep following the old one; not while a
                // mission's script camera may hold it
                if (car && !ram[kMissionRunning])
                    code.op(RESTORE_CAMERA_JUMPCUT);
            }
            code.op(MARK_MODEL_AS_NO_LONGER_NEEDED).i(model);
            code.op(MARK_CAR_AS_NO_LONGER_NEEDED).local(0);
            g_protectedCar = 0;
            notice = name + " spawned";
            return true;
        }, {}, {model});
    }

    // Health back to full (the engine and fire); a car's engine and tyres too, as the health cheat does
    // (FUN_0016D0D8). The dents stay - a Pay 'n' Spray fixes those.
    void repairVehicle()
    {
        vehicleAction("Vehicle repaired", [](uint8_t *ram, uint32_t car, int32_t h, Scm &code) {
            code.op(SET_CAR_HEALTH).i(h).i(1000);
            if (rd32(ram, car + kVehClass) == 0)
            {
                std::vector<Step> steps{{kSetEngineStatus, false, car + kVehDamage, 0u}};
                for (uint32_t wheel = 0; wheel < 4; ++wheel)
                    steps.push_back({kSetWheelStatus, false, car + kVehDamage, wheel, 0u});
                queueSteps(std::move(steps));
            }
        });
    }

    // SET_CAR_HEADING rebuilds the matrix upright around its heading; SET_CAR_COORDINATES lifts it onto its wheels.
    void flipVehicle()
    {
        vehicleAction("Vehicle flipped upright", [](uint8_t *ram, uint32_t car, int32_t h, Scm &code) {
            code.op(SET_CAR_COORDINATES).i(h).f(rdf(ram, car + kPos)).f(rdf(ram, car + kPos + 4)).f(rdf(ram, car + kPos + 8));
            code.op(SET_CAR_HEADING).i(h).f(headingDegrees(ram, car));
        });
    }

    void boostVehicle()
    {
        vehicleAction("Boost!", [](uint8_t *, uint32_t, int32_t h, Scm &code) { code.op(SET_CAR_FORWARD_SPEED).i(h).f(45.0f); });
    }

    void paintVehicle()
    {
        static std::mt19937 rng{std::random_device{}()};
        const int a = static_cast<int>(rng() % 64), b = static_cast<int>(rng() % 64);
        vehicleAction("Vehicle painted", [a, b](uint8_t *, uint32_t, int32_t h, Scm &code) {
            code.op(CHANGE_CAR_COLOUR).i(h).i(a).i(b);
        });
    }

    void teleport(int landmark)
    {
        if (landmark < 0 || landmark >= static_cast<int>(std::size(kLandmarks)))
            return post("No such place");
        const LandmarkDef &l = kLandmarks[landmark];
        const std::string name = nameOf(g_landmarkNames, landmark, l.fallback);
        queue([&l, name](uint8_t *ram, Scm &code, std::string &notice) {
            moveTo(ram, code, l.x, l.y, l.z + 1.0f); // (a ped's position is about a metre above its feet)
            notice = "Teleported to " + name;
            return true;
        });
    }

    void savePosition()
    {
        queue([](uint8_t *ram, Scm &, std::string &notice) {
            const uint32_t ped = player(ram);
            const uint32_t car = vehicleOf(ram, ped);
            const uint32_t at = car ? car : ped;
            g_saved = {true, rdf(ram, at + kPos), rdf(ram, at + kPos + 4), rdf(ram, at + kPos + 8)};
            g_hasSaved = true;
            notice = fmt("Position saved (%.0f, %.0f, %.0f)", g_saved.x, g_saved.y, g_saved.z);
            return true;
        });
    }

    void returnToSaved()
    {
        queue([](uint8_t *ram, Scm &code, std::string &notice) {
            if (!g_saved.valid)
                return (notice = "No position saved", false);
            moveTo(ram, code, g_saved.x, g_saved.y, g_saved.z);
            notice = "Back at the saved position";
            return true;
        });
    }

    bool hasSavedPosition() { return g_hasSaved.load(std::memory_order_relaxed); }
    void teleportToCamera() { notAvailable(); }
    int freeCameraSpeed() { return 20; }
    void setFreeCameraSpeed(int) {}

    void setTime(int hour)
    {
        hour = ((hour % 24) + 24) % 24;
        queue([hour](uint8_t *, Scm &code, std::string &notice) {
            code.op(SET_TIME_OF_DAY).i(hour).i(0);
            g_frozenHour = hour;
            g_frozenMinute = 0;
            notice = fmt("Time %02d:00", hour);
            return true;
        });
    }

    void setWeather(int weather)
    {
        if (weather >= static_cast<int>(std::size(kWeatherNames)))
            return post("No such weather");
        queue([weather](uint8_t *, Scm &code, std::string &notice) {
            if (weather < 0)
            {
                code.op(RELEASE_WEATHER);
                notice = "Weather changes normally";
            }
            else
            {
                code.op(FORCE_WEATHER_NOW).i(weather);
                notice = std::string("Weather: ") + kWeatherNames[weather];
            }
            return true;
        });
    }

    void launchMission(int) { notAvailable(); }
    bool missionRunning() { return g_missionRunning.load(std::memory_order_relaxed); }

    void setGameSpeed(int percent)
    {
        percent = std::clamp(percent, 25, 200);
        g_speed = percent;
        queue([percent](uint8_t *, Scm &code, std::string &notice) {
            code.op(SET_TIME_SCALE).f(percent / 100.0f);
            notice = fmt("Game speed %d%%", percent);
            return true;
        });
    }
    int gameSpeed() { return g_speed.load(std::memory_order_relaxed); }
    int clockHour() { return g_clockHour.load(std::memory_order_relaxed); }

    std::vector<Entry> vehicles()
    {
        std::lock_guard<std::mutex> lock(g_namesMutex);
        return g_vehicleNames;
    }
    std::vector<Entry> weapons()
    {
        std::vector<Entry> out;
        for (const WeaponDef &w : kWeapons)
            out.push_back({w.name, w.type});
        return out;
    }
    std::vector<Entry> loadouts()
    {
        std::vector<Entry> out;
        for (size_t k = 0; k < std::size(kLoadouts); ++k)
            out.push_back({kLoadouts[k].name, static_cast<int>(k)});
        return out;
    }
    std::vector<Entry> landmarks()
    {
        std::lock_guard<std::mutex> lock(g_namesMutex);
        if (!g_landmarkNames.empty())
            return g_landmarkNames;
        std::vector<Entry> out;
        for (size_t k = 0; k < std::size(kLandmarks); ++k)
            out.push_back({kLandmarks[k].fallback, static_cast<int>(k)});
        return out;
    }
    std::vector<Entry> weathers()
    {
        std::vector<Entry> out{{"Normal", -1}};
        for (size_t k = 0; k < std::size(kWeatherNames); ++k)
            out.push_back({kWeatherNames[k], static_cast<int>(k)});
        return out;
    }
    std::vector<Entry> missions(bool) { return {}; }

    std::vector<Notice> notices(double maxAgeSeconds)
    {
        const double t = now();
        std::lock_guard<std::mutex> lock(g_noticesMutex);
        std::vector<Notice> out;
        for (const Notice &n : g_notices)
            if (t - n.time <= maxAgeSeconds)
                out.push_back(n);
        return out;
    }
    double now()
    {
        using namespace std::chrono;
        return duration<double>(steady_clock::now().time_since_epoch()).count();
    }
    void notifyScreenshot(const std::string &text)
    {
        std::lock_guard<std::mutex> lock(g_noticesMutex);
        g_notices.push_back({text, now(), true});
        if (g_notices.size() > 16)
            g_notices.erase(g_notices.begin());
    }
}
