// VCS disc streaming without the IOP (docs/NATIVE_PORT_ROADMAP.md milestone 6 step 1).
#pragma once

class PS2Runtime;

namespace vcs
{
    // The EE side of CDSTREAM.IRX's stream server (SIF RPC 0x65686577): read / poll / fn 5 replaced by reads from the
    // disc image on a host thread straight into EE RAM, the server's init call answered on the host. False with
    // VCS_NATIVE_STREAM=0, which keeps the IOP's (the reference).
    bool installStreamHooks(PS2Runtime &runtime);

    // The same at another game's addresses of the three EE functions (Liberty City Stories: game/lcs_overrides.cpp).
    bool installStreamHooksAt(PS2Runtime &runtime, unsigned read, unsigned poll, unsigned fn5);

    // Another game's stream client in full: LCS's CDSTREAM answers an accepted read with 0 (VCS's with 1) and adds fn 3
    // (wait for a channel) and fn 4 (the head's LSN). 0 for a function the game does not have.
    struct StreamLayout
    {
        unsigned read = 0, poll = 0, fn5 = 0;
        unsigned wait = 0, head = 0;
        unsigned readAccepted = 1;
    };
    bool installStreamHooksAt(PS2Runtime &runtime, const StreamLayout &layout);

    // Milestone 6 step 3: with streaming and sound both native the emulated IOP is switched off - no IRX module is
    // loaded or run - unless PS2X_IOP=1; PS2X_IOP=0 switches it off regardless.
    void retireIop(bool nativeComplete);
}
