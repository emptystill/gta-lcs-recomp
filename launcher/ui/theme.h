// The look: Liberty City Stories' - the pause menu's orange for the thing to do, on a night-blue dark or on newsprint
// (light); teal for done, yellow and red for attention, and nothing else. Which of the two follows Windows' app mode unless the player picks one
// (Settings). Every colour the UI draws comes from here; sizes are logical units.
#pragma once

#include "core/math.h"

namespace theme
{
    using core::Color;
    using core::rgb;

    struct Palette
    {
        // surfaces, back to front
        Color night;      // behind everything
        Color base;       // sheets, drawers
        Color surface;    // cards
        Color surfaceHi;  // hovered cards, controls
        Color surfaceTop; // what sits on controls
        // text
        Color text, dim, faint;
        // signal colours
        Color pink;   // the primary: the thing to do, the selection
        Color pinkHi; // its gradients' lighter end
        Color onPink; // text and marks on pink
        Color teal, amber, red;
        // what is laid over surfaces at an alpha: hover tints, hairlines, dividers (its own alpha scales them)
        Color ink;
        Color shadow; // its alpha scales every shadow
        Color scrim;  // behind dialogs and sheets
        // particular fills
        Color errorFill, doneFill;                     // a failure's card, a finished task's card
        Color dangerFill, dangerFillHi, dangerText;    // the danger button
        Color logFill;                                 // the log's well
        Color well, wellBottom, wellGlow, wellGlowBottom; // the disc drop zone, idle and with a file over it
        Color sky, sea;                                // the backdrop's top and its sea
        Color horizon;                                 // the sky's glow at the horizon
        Color skyline, cloud, lights;                  // silhouettes (towers), cloud streaks, lit windows
        Color contrail;                                // jet trails, lit by the low sun
    };

    // Liberty City Stories' pause menu: its tab bar's orange for the thing to do (dark text on it), its red titles, its
    // options' pale blue on the night's dark navy (DECISIONS.md 2026-11-17: the launcher's look is LCS's; the field is
    // still called pink)
    inline constexpr Palette kDarkPalette{
        .night = rgb(0x05070F),
        .base = rgb(0x0A0E1C),
        .surface = rgb(0x111729),
        .surfaceHi = rgb(0x182038),
        .surfaceTop = rgb(0x232D4C),
        .text = rgb(0xF1F4FA),
        .dim = rgb(0xA3ADC4),
        .faint = rgb(0x667089),
        .pink = rgb(0xF6A623),
        .pinkHi = rgb(0xFFC65E),
        .onPink = rgb(0x1A1206),
        .teal = rgb(0x3DDBC6),
        .amber = rgb(0xF2D24B),
        .red = rgb(0xE5484D),
        .ink = rgb(0xFFFFFF),
        .shadow = rgb(0x000000),
        .scrim = rgb(0x03050C, 180),
        .errorFill = rgb(0x2A1216),
        .doneFill = rgb(0x0F2220),
        .dangerFill = rgb(0x3A1418),
        .dangerFillHi = rgb(0x4A1A1F),
        .dangerText = rgb(0xFFC9C9),
        .logFill = rgb(0x070A14),
        .well = rgb(0x0E1324),
        .wellBottom = rgb(0x0A0E1C),
        .wellGlow = rgb(0x3A2A0E),
        .wellGlowBottom = rgb(0x2A1E0A),
        .sky = rgb(0x0B1024),
        .sea = rgb(0x070A16),
        .horizon = rgb(0x1E2F63),
        .skyline = rgb(0x05070F),
        .cloud = rgb(0x16203F),
        .lights = rgb(0xFFD27A),
        .contrail = rgb(0xB2C8EE),
    };

    // light: newsprint - warm paper, navy ink, a deeper orange that white text sits on
    inline constexpr Palette kLightPalette{
        .night = rgb(0xF4EFE6),
        .base = rgb(0xFFFDF8),
        .surface = rgb(0xFFFFFF),
        .surfaceHi = rgb(0xFFF4E0),
        .surfaceTop = rgb(0xFDE7C2),
        .text = rgb(0x14182A),
        .dim = rgb(0x5A6178),
        .faint = rgb(0x9298A8),
        .pink = rgb(0xD96A00),
        .pinkHi = rgb(0xF0891C),
        .onPink = rgb(0xFFFFFF),
        .teal = rgb(0x0B9C84),
        .amber = rgb(0xA87A00),
        .red = rgb(0xC8202A),
        .ink = rgb(0x14182A, 200),
        .shadow = rgb(0x3A2A10, 70),
        .scrim = rgb(0xF4EFE6, 160), // light: the content behind washes out
        .errorFill = rgb(0xFFEFEF),
        .doneFill = rgb(0xEEF9F6),
        .dangerFill = rgb(0xFFE3E3),
        .dangerFillHi = rgb(0xFFD3D3),
        .dangerText = rgb(0xA81E28),
        .logFill = rgb(0xFFFBF2),
        .well = rgb(0xFFFFFF),
        .wellBottom = rgb(0xFFF8EC),
        .wellGlow = rgb(0xFFE9C4),
        .wellGlowBottom = rgb(0xFFDDA6),
        .sky = rgb(0xEDE6D8),
        .sea = rgb(0xF4EFE6),
        .horizon = rgb(0xE8C9A0),
        .skyline = rgb(0x2A2F45),
        .cloud = rgb(0xFFFFFF),
        .lights = rgb(0xFFF2CC),
        .contrail = rgb(0xFFFFFF),
    };

    // the palette in use: between the two while it changes (apply)
    inline Palette current = kDarkPalette;
    inline float darkness = 1; // 1 dark, 0 light

    // every colour, blended: 0 is light, 1 is dark
    inline void apply(float dark)
    {
        static_assert(sizeof(Palette) % sizeof(Color) == 0);
        constexpr int n = int(sizeof(Palette) / sizeof(Color));
        const Color *a = reinterpret_cast<const Color *>(&kLightPalette);
        const Color *b = reinterpret_cast<const Color *>(&kDarkPalette);
        Color *out = reinterpret_cast<Color *>(&current);
        for (int i = 0; i < n; ++i)
            out[i] = core::mix(a[i], b[i], dark);
        darkness = dark;
    }

    // the names the UI draws with
    inline Color &kNight = current.night;
    inline Color &kBase = current.base;
    inline Color &kSurface = current.surface;
    inline Color &kSurfaceHi = current.surfaceHi;
    inline Color &kSurfaceTop = current.surfaceTop;
    inline Color &kText = current.text;
    inline Color &kDim = current.dim;
    inline Color &kFaint = current.faint;
    inline Color &kPink = current.pink;
    inline Color &kPinkHi = current.pinkHi;
    inline Color &kOnPink = current.onPink;
    inline Color &kTeal = current.teal;
    inline Color &kAmber = current.amber;
    inline Color &kRed = current.red;
    inline Color &kScrim = current.scrim;
    inline Color &kErrorFill = current.errorFill;
    inline Color &kDoneFill = current.doneFill;
    inline Color &kDangerFill = current.dangerFill;
    inline Color &kDangerFillHi = current.dangerFillHi;
    inline Color &kDangerText = current.dangerText;
    inline Color &kLogFill = current.logFill;
    inline Color &kWell = current.well;
    inline Color &kWellBottom = current.wellBottom;
    inline Color &kWellGlow = current.wellGlow;
    inline Color &kWellGlowBottom = current.wellGlowBottom;
    inline Color &kSky = current.sky;
    inline Color &kSea = current.sea;
    inline Color &kHorizon = current.horizon;
    inline Color &kSkyline = current.skyline;
    inline Color &kCloud = current.cloud;
    inline Color &kLights = current.lights;
    inline Color &kContrail = current.contrail;

    // a tint over a surface (white on dark, a deep pink on light)
    inline Color ink(float a) { return core::withAlpha(current.ink, a * core::alphaOf(current.ink)); }
    // a shadow's colour at strength a (light mode's are softer and pinker)
    inline Color shade(float a) { return core::withAlpha(current.shadow, a * core::alphaOf(current.shadow)); }

    // type sizes
    constexpr float kWordmark = 46;
    constexpr float kTitle = 30;
    constexpr float kHeading = 19;
    constexpr float kBody = 14.5f;
    constexpr float kSmall = 13;
    constexpr float kEyebrow = 11.5f;

    // shape
    constexpr float kRadius = 12;      // cards
    constexpr float kRadiusSmall = 8;  // buttons, controls
    constexpr float kTitleBar = 44;

    // the icons render/icons.cpp draws (their Segoe Fluent Icons code points, kept as identifiers)
    namespace icon
    {
        constexpr char32_t kPlay = 0xE768;
        constexpr char32_t kSettings = 0xE713;
        constexpr char32_t kHelp = 0xE897;
        constexpr char32_t kSave = 0xE74E;
        constexpr char32_t kFolder = 0xED25;   // an open folder
        constexpr char32_t kOpenFile = 0xE8E5; // a page with an arrow: choosing a file
        constexpr char32_t kDisc = 0xE958;
        constexpr char32_t kDownload = 0xE896;
        constexpr char32_t kRefresh = 0xE72C;
        constexpr char32_t kMinimize = 0xE921;
        constexpr char32_t kMaximize = 0xE922;
        constexpr char32_t kRestore = 0xE923;
        constexpr char32_t kClose = 0xE8BB;
        constexpr char32_t kCheck = 0xE73E;
        constexpr char32_t kCancel = 0xE711;
        constexpr char32_t kWarning = 0xE7BA;
        constexpr char32_t kInfo = 0xE946;
        constexpr char32_t kBack = 0xE72B;
        constexpr char32_t kChevronRight = 0xE76C;
        constexpr char32_t kChevronDown = 0xE70D;
        constexpr char32_t kDocument = 0xE8A5;
        constexpr char32_t kRepair = 0xE90F;
        constexpr char32_t kCode = 0xE943;
        constexpr char32_t kChip = 0xE950;
        constexpr char32_t kClock = 0xE823;
        constexpr char32_t kSync = 0xE895;
        constexpr char32_t kLink = 0xE71B;
        constexpr char32_t kUndo = 0xE7A7;
        constexpr char32_t kBug = 0xEBE8;
        constexpr char32_t kDrive = 0xEDA2;
        constexpr char32_t kOpen = 0xE8A7; // open in new window
        constexpr char32_t kList = 0xEA37;
        constexpr char32_t kGame = 0xE7FC;
        constexpr char32_t kStop = 0xE71A;
    }
}
