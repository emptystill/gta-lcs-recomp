// Game-specific runtime bindings for Liberty City Stories (SLUS_214.23), applied when the ELF loads - the LCS
// counterpart of vcs_overrides.cpp (which only applies to VCS's executable). Each hook here is an LCS address with
// the VCS one it mirrors and how it was found (docs/LCS.md).
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace
{
    PS2_GUEST_CC void returnZero(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)rdram;
        (void)runtime;
        SET_GPR_U64(ctx, 2, 0);
        ctx->pc = GPR_U32(ctx, 31);
    }

    bool envOff(const char *name)
    {
        const char *v = std::getenv(name);
        return v && v[0] == '0';
    }

    bool envOn(const char *name)
    {
        const char *v = std::getenv(name);
        return v && v[0] && v[0] != '0';
    }

    // The movie player's Play(path, 0, n) (VCS's CMoviePlayer::Play 0x40D8B8). Found from the movie paths: the boot
    // function FUN_001F3728 (VCS FUN_0021CA70) loads cdrom0:\MOVIES\RSNY.PSS, RSLEEDS, RSNORTH and DM.PSS (0x3B5A58..)
    // and calls 0x359F40 (init, VCS 0x40D7D8), 0x35A020 for each (Play) and 0x35A000 (shut down, VCS 0x40D898).
    // Emulated, the first movie never finishes (the stream stops after a few reads, the game thread stays inside Play):
    // skipped until the movies play natively, as VCS did before game/vcs_movie.h. LCS_MOVIES=1 runs the game's own.
    constexpr uint32_t kMoviePlay = 0x0035A020u;

    void applyLcs(PS2Runtime &runtime)
    {
        std::fprintf(stderr, "[lcs] Liberty City Stories overrides\n");
        if (!envOn("LCS_MOVIES"))
        {
            if (runtime.replaceFunction(kMoviePlay, &returnZero))
                std::fprintf(stderr, "[lcs] movies skipped (LCS_MOVIES=1 plays them, emulated)\n");
            else
                std::fprintf(stderr, "[lcs] could not skip the movie player at 0x%x\n", kMoviePlay);
        }
        (void)envOff;
    }
}

PS2_REGISTER_GAME_OVERRIDE("GTA Liberty City Stories (SLUS-21423)", "SLUS_214.23", 0x001C6A60u, 0u, &applyLcs)
