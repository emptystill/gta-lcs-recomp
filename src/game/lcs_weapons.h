// Liberty City Stories' weapon switching (docs/LCS.md "The detonator"). Enhancements > Combat: **Switch weapons with
// the detonator** (off: as the original, which refuses to cycle while the remote grenades' detonator is in hand until
// they are set off). On, the cycle buttons move to the next / previous slot that holds a weapon; the detonator stays in
// its slot, so cycling back to it and firing sets the grenades off.
#pragma once

class PS2Runtime;

namespace lcs::weapons
{
    void registerSettings();       // main.cpp, with the other settings
    void install(PS2Runtime &runtime); // applyLcs: the hook on CPlayerPed::ProcessWeaponSwitch (0x34BE08)
}
