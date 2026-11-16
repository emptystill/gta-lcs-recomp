// See lcs_weapons.h and docs/LCS.md "The detonator".
//
// CPlayerPed::ProcessWeaponSwitch (FUN_0034BE08, a0 the player's ped, a1 his CPad) cycles the weapons only when the
// one in hand is not the detonator (type 0x22): with it, nothing until the grenades go off. The cycle buttons are the
// pad's L2 (+0x0C, previous: CPad 0x171E40) and R2 (+0x10, next: 0x171EB0), "just down" against the last frame's state
// 0x30 further on, and only with the controls enabled (+0x82 zero) and a control mode +0x7E of 0..4. With the setting
// on, the detonator in hand and one of them just down, the hook replaces this call of the switch by
// CPed::SetCurrentWeapon(ped, slot) (FUN_001E13A0: the model swapped, the player's chosen slot +0xB64 set) for the
// next / previous slot holding a weapon (fists, a melee weapon, or one with ammunition) - a guest tail call, returning
// to the switch's caller (AGENTS.md rule 2).
#include "game/lcs_weapons.h"

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "settings/settings.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace lcs::weapons
{
    namespace
    {
        constexpr uint32_t kRamMask = 0x01FFFFFFu;
        constexpr uint32_t kProcessWeaponSwitch = 0x0034BE08u, kSetCurrentWeaponSlot = 0x001E13A0u;
        constexpr uint32_t kPedWeapons = 0x594u, kSlotSize = 0x1Cu, kPedSlot = 0x6B8u;
        constexpr uint32_t kDetonator = 0x22u;
        constexpr uint32_t kWeaponInfoPtr = 0x003D9B48u, kWeaponInfoSize = 0x70u; // +0 the fire type (0 melee)
        constexpr int kSlots = 10;

        std::atomic<bool> g_on{false};
        PS2Runtime::RecompiledFunction g_orig = nullptr;

        uint32_t rd32(const uint8_t *ram, uint32_t a)
        {
            uint32_t v;
            std::memcpy(&v, ram + (a & kRamMask), 4);
            return v;
        }
        uint16_t rd16(const uint8_t *ram, uint32_t a)
        {
            uint16_t v;
            std::memcpy(&v, ram + (a & kRamMask), 2);
            return v;
        }
        bool inRam(uint32_t a) { return a >= 0x00100000u && a < 0x02000000u; }

        // a slot he can switch to: fists (slot 0), a melee weapon, or a weapon with ammunition; not the detonator
        bool usable(const uint8_t *ram, uint32_t ped, int s)
        {
            const uint32_t slot = ped + kPedWeapons + static_cast<uint32_t>(s) * kSlotSize;
            const uint32_t type = rd32(ram, slot);
            if (s == 0)
                return true;
            if (type == 0 || type == kDetonator || type > 36)
                return false;
            const uint32_t table = rd32(ram, kWeaponInfoPtr);
            const bool melee = inRam(table) && inRam(rd32(ram, table + 4)) && rd32(ram, rd32(ram, table + 4) + type * kWeaponInfoSize) == 0;
            return melee || static_cast<int32_t>(rd32(ram, slot + 0xC)) > 0;
        }

        PS2_GUEST_CC void processWeaponSwitch(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t ped = GPR_U32(ctx, 4), pad = GPR_U32(ctx, 5);
            if (g_on.load(std::memory_order_relaxed) && inRam(ped) && inRam(pad))
            {
                const int8_t cur = static_cast<int8_t>(rdram[(ped + kPedSlot) & kRamMask]);
                const uint16_t mode = rd16(rdram, pad + 0x7E);
                const bool controls = rd16(rdram, pad + 0x82) == 0 && mode <= 4u;
                const bool prev = rd16(rdram, pad + 0x0C) != 0 && rd16(rdram, pad + 0x3C) == 0;
                const bool next = rd16(rdram, pad + 0x10) != 0 && rd16(rdram, pad + 0x40) == 0;
                if (cur >= 0 && cur < kSlots && controls && (prev != next) &&
                    rd32(rdram, ped + kPedWeapons + static_cast<uint32_t>(cur) * kSlotSize) == kDetonator)
                {
                    const int step = next ? 1 : kSlots - 1;
                    for (int s = (cur + step) % kSlots; s != cur; s = (s + step) % kSlots)
                    {
                        if (!usable(rdram, ped, s))
                            continue;
                        std::fprintf(stderr, "[weapons] the detonator put away: slot %d -> %d\n", cur, s);
                        SET_GPR_U32(ctx, 4, ped);
                        SET_GPR_U32(ctx, 5, static_cast<uint32_t>(s));
                        ctx->pc = kSetCurrentWeaponSlot; // returns to the switch's caller
                        return;
                    }
                }
            }
            g_orig(rdram, ctx, runtime);
        }
    }

    void registerSettings()
    {
        settings::Spec s;
        s.key = "enhance.detonator_switch";
        s.page = "Enhancements";
        s.section = "Combat";
        s.label = "Switch weapons with the detonator";
        s.help = "After throwing remote grenades Toni holds the detonator. Off, as on the PS2: no other weapon until the "
                 "grenades go off. On: the weapon buttons (or the mouse wheel) change weapon as usual; the detonator "
                 "stays in its place, so switching back to it and firing sets them off.";
        s.type = settings::Type::Toggle;
        s.defaultValue = 0;
        s.env = "LCS_DETONATOR_SWITCH";
        s.onChange = [](int v) { g_on.store(v != 0, std::memory_order_relaxed); };
        settings::add(s);
    }

    void install(PS2Runtime &runtime)
    {
        g_orig = runtime.lookupFunction(kProcessWeaponSwitch);
        if (g_orig)
            runtime.replaceFunction(kProcessWeaponSwitch, &processWeaponSwitch);
    }
}
