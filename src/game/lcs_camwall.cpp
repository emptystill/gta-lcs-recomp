// See lcs_camwall.h.
//
// CWorld::ProcessLineOfSight (FUN_00174538; the weapons' line test 0x2B0BD0 is a jump to it): a0 the start, a1 the
// end (CVectors), a2 the CColPoint written on a hit (its point first; 0x40 bytes), a3 the entity hit (out), t0..t3
// buildings / vehicles / peds / objects, then on the stack (8-byte slots from sp) dummies, see-through ignored, some
// objects ignored, shoot-through ignored; v0 true on a hit. Read from its calls: the instant-hit shot (0x2A59B4) and
// CCamera's post-process 0x287020 (t0 0, t1 1, the rest 0: vehicles only there).
//
// The chained call needs its stack arguments below the caller's frame: sp is lowered by kStackSpace for it and put
// back when it returns (the frame boundary's hook, which sees the return by $ra). Its vectors and outputs live in the
// EE kernel RAM no one uses (0x7FE00.., beside the mouse aim's 0x7FF00).
#include "game/lcs_camwall.h"

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lcs::camwall
{
    namespace
    {
        constexpr uint32_t kRamMask = 0x01FFFFFFu;
        constexpr uint32_t kLineOfSight = 0x00174538u;
        constexpr uint32_t kFrom = 0x0007FE00u, kTo = 0x0007FE10u, kColPoint = 0x0007FE20u, kEntity = 0x0007FE70u;
        constexpr float kMargin = 0.3f;  // the camera stays this far in front of a wall it backs onto (1 m put it in his neck)
        constexpr float kNearest = 0.9f; // and never nearer the pivot than this (a wall right behind him: past it, facing away)

        const bool g_on = [] {
            const char *e = std::getenv("LCS_FOOT_WALL");
            return !(e && e[0] == '0');
        }();
        const bool g_log = std::getenv("LCS_FOOT_WALL_LOG") != nullptr;
        // LCS_CAMWALL_PROBE=1 (debugging): instead of the pivot's line, lines 2 m out from the camera in ten directions
        // in turn, a frame each, logged - what is around the camera
        const bool g_probe = std::getenv("LCS_CAMWALL_PROBE") != nullptr;
        int g_probeK = 0, g_probeAt = 0;
        constexpr float kProbe[10][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
                                         {0.7f, 0.7f, 0}, {-0.7f, 0.7f, 0}, {0.7f, -0.7f, 0}, {-0.7f, -0.7f, 0}};

        // game thread only
        bool g_asked = false;            // a line was asked for since the last test
        float g_askPivot[3] = {}, g_askPos[3] = {};
        bool g_tested = false;           // the last test's line and answer
        float g_testPivot[3] = {}, g_testPos[3] = {};
        float g_testClear = 1.0f;        // the clear fraction of that line
        float g_shown = 1.0f;            // the fraction given last (eased outwards)
        uint32_t g_lastAsk = 0;

        void wv(uint8_t *ram, uint32_t a, const float v[3])
        {
            const float w[4] = {v[0], v[1], v[2], 0.0f};
            std::memcpy(ram + (a & kRamMask), w, 16);
        }
    }

    float clear(const float pivot[3], const float pos[3])
    {
        if (!g_on)
            return 1.0f;
        std::memcpy(g_askPivot, pivot, sizeof(g_askPivot));
        std::memcpy(g_askPos, pos, sizeof(g_askPos));
        g_asked = true;
        float want = 1.0f;
        if (g_tested)
        {
            // the last tested line counts for this one if it points the same way (within ~12 degrees) from about
            // the same pivot: the answer is a frame old, the camera turns by less than that in a frame
            float a[3], b[3], la = 0.0f, lb = 0.0f, dot = 0.0f, dp = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                a[i] = pos[i] - pivot[i];
                b[i] = g_testPos[i] - g_testPivot[i];
                la += a[i] * a[i];
                lb += b[i] * b[i];
                dot += a[i] * b[i];
                dp += (pivot[i] - g_testPivot[i]) * (pivot[i] - g_testPivot[i]);
            }
            if (la > 1e-4f && lb > 1e-4f && dot / std::sqrt(la * lb) > 0.978f && dp < 1.0f)
                want = std::clamp(g_testClear * std::sqrt(lb / la), 0.0f, 1.0f); // the same clear length on this line
        }
        // in at once, out over ~0.3 s (a frame is ~1/60 s; the clock is not needed for this)
        g_shown = want < g_shown ? want : g_shown + (want - g_shown) * 0.06f;
        float len = 0.0f;
        for (int i = 0; i < 3; ++i)
            len += (pos[i] - pivot[i]) * (pos[i] - pivot[i]);
        len = std::sqrt(len);
        return len > kNearest ? std::max(g_shown, kNearest / len) : 1.0f;
    }

    bool chain(uint8_t *rdram, R5900Context *ctx)
    {
        if (!g_on || !g_asked)
        {
            g_tested = g_tested && g_asked; // no camera asked last frame (a car, a cutscene): forget the old line
            return false;
        }
        g_asked = false;
        // the line from the pivot to past the camera by the margin: what is clear up to kMargin before a hit
        float d[3], len = 0.0f;
        for (int i = 0; i < 3; ++i)
        {
            d[i] = g_askPos[i] - g_askPivot[i];
            len += d[i] * d[i];
        }
        len = std::sqrt(len);
        if (len < 0.2f)
            return false;
        float to[3];
        for (int i = 0; i < 3; ++i)
            to[i] = g_askPivot[i] + d[i] / len * (len + kMargin);
        wv(rdram, kFrom, g_askPivot);
        wv(rdram, kTo, to);
        if (g_probe)
        {
            g_probeAt = g_probeK;
            g_probeK = (g_probeK + 1) % 10;
            for (int i = 0; i < 3; ++i)
                to[i] = g_askPos[i] + kProbe[g_probeAt][i] * 2.0f;
            wv(rdram, kFrom, g_askPos);
            wv(rdram, kTo, to);
        }
        std::memset(rdram + kColPoint, 0, 0x50);
        std::memcpy(g_testPivot, g_askPivot, sizeof(g_testPivot));
        std::memcpy(g_testPos, g_askPos, sizeof(g_testPos));
        const uint32_t sp = GPR_U32(ctx, 29) - kStackSpace;
        SET_GPR_U32(ctx, 29, sp);
        const uint32_t stackArgs[4] = {0u, 0u, 0u, 0u}; // dummies no; see-through, some objects, shoot-through not ignored
        for (int i = 0; i < 4; ++i)
        {
            const uint64_t v = stackArgs[i];
            std::memcpy(rdram + ((sp + 8u * i) & kRamMask), &v, 8);
        }
        SET_GPR_U32(ctx, 4, kFrom);
        SET_GPR_U32(ctx, 5, kTo);
        SET_GPR_U32(ctx, 6, kColPoint);
        SET_GPR_U32(ctx, 7, kEntity);
        SET_GPR_U32(ctx, 8, 1u);  // buildings
        SET_GPR_U32(ctx, 9, 0u);  // vehicles
        SET_GPR_U32(ctx, 10, 0u); // peds
        SET_GPR_U32(ctx, 11, 1u); // objects (doors, fences, panels: one stood between it and Toni in a courtyard)
        ctx->pc = kLineOfSight;
        ++g_lastAsk;
        return true;
    }

    void onReturn(uint8_t *rdram, R5900Context *ctx)
    {
        const bool hit = (GPR_U32(ctx, 2) & 0xFFu) != 0u;
        if (g_probe)
        {
            float p[4];
            std::memcpy(p, rdram + kColPoint, 16);
            const float h = std::sqrt((p[0] - g_testPos[0]) * (p[0] - g_testPos[0]) + (p[1] - g_testPos[1]) * (p[1] - g_testPos[1]) +
                                      (p[2] - g_testPos[2]) * (p[2] - g_testPos[2]));
            uint32_t ent;
            std::memcpy(&ent, rdram + kEntity, 4);
            std::fprintf(stderr, "[probe] cam %.2f %.2f %.2f dir %d: %s %.2f m entity %08x\n", g_testPos[0], g_testPos[1], g_testPos[2],
                         g_probeAt, hit ? "hit" : "clear", hit ? h : 2.0f, hit ? ent : 0u);
            g_testClear = 1.0f;
            g_tested = true;
            return;
        }
        float len = 0.0f;
        for (int i = 0; i < 3; ++i)
            len += (g_testPos[i] - g_testPivot[i]) * (g_testPos[i] - g_testPivot[i]);
        len = std::sqrt(len);
        float clearFrac = 1.0f;
        if (hit && len > 0.2f)
        {
            float p[4];
            std::memcpy(p, rdram + kColPoint, 16);
            float h = 0.0f;
            for (int i = 0; i < 3; ++i)
                h += (p[i] - g_testPivot[i]) * (p[i] - g_testPivot[i]);
            h = std::sqrt(h);
            clearFrac = std::clamp((h - kMargin) / len, 0.0f, 1.0f);
            if (g_log)
                std::fprintf(stderr, "[camwall] hit at %.2f of %.2f m (%.2f %.2f %.2f) -> clear %.2f\n", h, len, p[0], p[1], p[2], clearFrac);
        }
        g_testClear = clearFrac;
        g_tested = true;
    }
}
