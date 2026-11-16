// The trainer on Liberty City Stories (docs/LCS.md "Trainer"): game/vcs_trainer.h's interface, so the F5 menu
// (src/ui/trainer_menu.cpp) drives it, with what LCS has so far - the weapons. vcs_trainer.cpp (VCS's, through the
// game's script VM) is left out of the build: its addresses and script commands are VCS's.
//
// Weapons are given the way the game's weapon cheats give them (FUN_0016C838 / 0016CA70 / 0016CC80): the weapon's
// model requested (CStreaming::RequestModel 0x18B400, flags 1) and loaded (LoadAllRequestedModels 0x18F648),
// CPed::GiveWeapon (0x1E1180: ped, type, ammo, 1), then the model left to the streamer again (SetModelIsDeletable
// 0x18FC38). One given alone is also put in his hand (CPed::SetCurrentWeapon(ped, type) 0x1E1470). Each of those is a
// guest call chained from the frame boundary (the limiter hook, lcs_overrides.cpp), only while
// the game's clock runs (not in the pause menu), each returning into the limiter and the next chained then: a
// weapon set in one frame, as the cheats give it. Remove all weapons is
// CPed::ClearWeapons (0x1E16C8). Unlimited ammo / no reload write the slots (+8 clip, +0xC total) every frame.
#include "game/lcs_trainer.h"
#include "game/vcs_trainer.h"

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ps2_control.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kRamMask = 0x01FFFFFFu;
    constexpr uint32_t kPlayerPed = 0x00408E40u;      // CWorld::Players[0].m_pPed
    constexpr uint32_t kScriptSpacePtr = 0x003CF0F4u; // 0 until the script engine runs
    constexpr uint32_t kGameClock = 0x003D9B70u;      // CTimer::m_snTimeInMilliseconds: stands still while paused
    constexpr uint32_t kWeaponInfoPtr = 0x003D9B48u;  // *(*(this) + 4) + type * 0x70: +0 fire type, +0x10 clip size,
    constexpr uint32_t kWeaponInfoSize = 0x70u;       // +0x60 / +0x64 the models (-1: none), +0x68 the slot
    constexpr uint32_t kPedVehicle = 0x534u, kPedDriving = 0x538u;
    constexpr uint32_t kPedWeapons = 0x594u, kSlotSize = 0x1Cu; // type, state, clip +8, total +0xC
    constexpr int kSlots = 10;
    constexpr uint32_t kMaxAmmo = 9999u; // GiveWeapon's own cap

    constexpr uint32_t kRequestModel = 0x0018B400u, kLoadRequested = 0x0018F648u, kModelDeletable = 0x0018FC38u;
    constexpr uint32_t kGiveWeapon = 0x001E1180u, kSetCurrentWeapon = 0x001E1470u, kClearWeapons = 0x001E16C8u;

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

    // One guest call: a0 is the player's ped when ped is set (taken when it runs).
    struct Step
    {
        uint32_t fn = 0;
        bool ped = false;
        uint32_t a0 = 0, a1 = 0, a2 = 0, a3 = 0;
        std::string notice; // posted when it runs
    };

    uint8_t *g_rdram = nullptr;
    std::mutex g_mutex; // the steps and the notices
    std::deque<Step> g_steps;
    std::vector<vcs::trainer::Notice> g_notices;
    std::atomic<bool> g_toggles[vcs::trainer::ToggleCount] = {};
    std::atomic<bool> g_ready{false}, g_inVehicle{false};
    uint32_t g_lastClock = 0;
    int g_stillFrames = 0;

    uint32_t rd32(uint32_t a)
    {
        uint32_t v;
        std::memcpy(&v, g_rdram + (a & kRamMask), 4);
        return v;
    }
    void wr32(uint32_t a, uint32_t v) { std::memcpy(g_rdram + (a & kRamMask), &v, 4); }
    bool inRam(uint32_t a) { return a >= 0x00100000u && a < 0x02000000u; }

    uint32_t player()
    {
        const uint32_t ped = g_rdram ? rd32(kPlayerPed) : 0u;
        return inRam(ped) ? ped : 0u;
    }

    uint32_t weaponInfo(uint32_t type)
    {
        const uint32_t table = rd32(kWeaponInfoPtr);
        if (!inRam(table) || !inRam(rd32(table + 4)) || type > 36)
            return 0;
        return rd32(table + 4) + type * kWeaponInfoSize;
    }

    void post(const std::string &text)
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_notices.push_back({text, vcs::trainer::now(), false});
        if (g_notices.size() > 16)
            g_notices.erase(g_notices.begin());
        std::fprintf(stderr, "[trainer] %s\n", text.c_str());
    }

    void notAvailable() { post("Not available in Liberty City Stories yet"); }

    // The weapons' models (one or two: the remote grenades' detonator, the minigun's second part), from the table.
    std::vector<uint32_t> models(int type)
    {
        std::vector<uint32_t> out;
        if (const uint32_t info = weaponInfo(static_cast<uint32_t>(type)))
            for (uint32_t off : {0x60u, 0x64u})
            {
                const int32_t m = static_cast<int32_t>(rd32(info + off));
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
            for (uint32_t m : models(type))
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
        std::lock_guard<std::mutex> lock(g_mutex);
        g_steps.insert(g_steps.end(), steps.begin(), steps.end());
    }

    // The held toggles: ammo kept full in every slot with a firearm or thrown weapon, the clip full.
    void holdAmmo(uint32_t ped)
    {
        const bool unlimited = g_toggles[vcs::trainer::UnlimitedAmmo].load(std::memory_order_relaxed);
        const bool noReload = g_toggles[vcs::trainer::NoReload].load(std::memory_order_relaxed);
        if (!unlimited && !noReload)
            return;
        for (int s = 0; s < kSlots; ++s)
        {
            const uint32_t slot = ped + kPedWeapons + static_cast<uint32_t>(s) * kSlotSize;
            const uint32_t type = rd32(slot);
            const uint32_t info = type ? weaponInfo(type) : 0u;
            if (!info || rd32(info) == 0u) // melee
                continue;
            if (unlimited && rd32(slot + 0xC) < kMaxAmmo)
                wr32(slot + 0xC, kMaxAmmo);
            const uint32_t clip = rd32(info + 0x10), total = rd32(slot + 0xC);
            if (noReload && clip > 1 && clip < 10000 && rd32(slot + 8) < std::min(clip, total))
                wr32(slot + 8, std::min(clip, total));
        }
    }
}

namespace lcs::trainer
{
    void install(PS2Runtime &runtime)
    {
        g_rdram = runtime.memory().getRDRAM();
        // trainer weapon T | loadout N | noweapons | ammo 0|1 | noreload 0|1 | state -> ok ... (tests)
        ps2control::addCommand("trainer", [](const std::string &args, ps2control::Reply reply) {
            char cmd[32] = {};
            int arg = 0;
            std::sscanf(args.c_str(), "%31s %d", cmd, &arg);
            const std::string c = cmd;
            if (c == "weapon")
                vcs::trainer::giveWeapon(arg);
            else if (c == "loadout")
                vcs::trainer::giveLoadout(arg);
            else if (c == "noweapons")
                vcs::trainer::removeWeapons();
            else if (c == "ammo")
                vcs::trainer::setToggle(vcs::trainer::UnlimitedAmmo, arg != 0);
            else if (c == "noreload")
                vcs::trainer::setToggle(vcs::trainer::NoReload, arg != 0);
            else if (c != "state")
                return reply("error usage: trainer weapon T | loadout N | noweapons | ammo 0|1 | noreload 0|1 | state");
            size_t pending;
            {
                std::lock_guard<std::mutex> lock(g_mutex);
                pending = g_steps.size();
            }
            char text[96];
            std::snprintf(text, sizeof(text), "ok ready=%d pending=%zu", vcs::trainer::ready() ? 1 : 0, pending);
            reply(text);
        });
    }

    void onFrame(uint8_t *rdram)
    {
        g_rdram = rdram;
        const uint32_t ped = player();
        const uint32_t clock = rd32(kGameClock);
        // the clock stands still in the pause menu and on loading screens: nothing then
        g_stillFrames = clock == g_lastClock ? g_stillFrames + 1 : 0;
        g_lastClock = clock;
        const bool running = ped && rd32(kScriptSpacePtr) != 0u && g_stillFrames < 3;
        g_ready.store(running, std::memory_order_relaxed);
        g_inVehicle.store(ped && ((rd32(ped + kPedDriving) & 0xFFu) || inRam(rd32(ped + kPedVehicle))), std::memory_order_relaxed);
        if (running)
            holdAmmo(ped);
    }

    bool chainNext(uint8_t *rdram, R5900Context *ctx)
    {
        g_rdram = rdram;
        const uint32_t ped = player();
        if (!g_ready.load(std::memory_order_relaxed) || !ped)
            return false;
        Step step;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
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
        if (t != UnlimitedAmmo && t != NoReload)
            return notAvailable();
        g_toggles[t].store(on, std::memory_order_relaxed);
        post(std::string(t == UnlimitedAmmo ? "Unlimited ammo " : "No reload ") + (on ? "on" : "off"));
    }
    bool ready() { return g_ready.load(std::memory_order_relaxed); }
    bool inVehicle() { return g_inVehicle.load(std::memory_order_relaxed); }

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
        std::lock_guard<std::mutex> lock(g_mutex);
        g_steps.push_back({kClearWeapons, true, 0u, 0u, 0u, 0u, "Weapons removed"});
    }

    void heal() { notAvailable(); }
    void addMoney(int) { notAvailable(); }
    void setWanted(int) { notAvailable(); }
    void spawnVehicle(int) { notAvailable(); }
    void repairVehicle() { notAvailable(); }
    void flipVehicle() { notAvailable(); }
    void boostVehicle() { notAvailable(); }
    void paintVehicle() { notAvailable(); }
    void teleport(int) { notAvailable(); }
    void savePosition() { notAvailable(); }
    void returnToSaved() { notAvailable(); }
    bool hasSavedPosition() { return false; }
    void teleportToCamera() { notAvailable(); }
    int freeCameraSpeed() { return 20; }
    void setFreeCameraSpeed(int) {}
    void setTime(int) { notAvailable(); }
    void setWeather(int) { notAvailable(); }
    void launchMission(int) { notAvailable(); }
    bool missionRunning() { return false; }
    void setGameSpeed(int) { notAvailable(); }
    int gameSpeed() { return 100; }
    int clockHour() { return 12; }

    std::vector<Entry> vehicles() { return {}; }
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
    std::vector<Entry> landmarks() { return {}; }
    std::vector<Entry> weathers() { return {{"Normal", -1}}; }
    std::vector<Entry> missions(bool) { return {}; }

    std::vector<Notice> notices(double maxAgeSeconds)
    {
        const double t = now();
        std::lock_guard<std::mutex> lock(g_mutex);
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
        std::lock_guard<std::mutex> lock(g_mutex);
        g_notices.push_back({text, now(), true});
        if (g_notices.size() > 16)
            g_notices.erase(g_notices.begin());
    }
}
