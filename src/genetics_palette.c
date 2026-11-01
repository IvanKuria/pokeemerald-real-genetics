// Coat colour palette transforms (REALGEN_SPEC.md, colours lane). Every step is integer maths and is mirrored
// exactly by tools/realgen/coat_preview.py, which renders a contact sheet over real sprites; tune both together.
//
// Each colour is sorted into one of three kinds:
//   outline - very dark (line art). Albino and red keep it, black darkens it further.
//   white   - bright and nearly grey (white fur, teeth, eye whites). No coat gene touches it.
//   pigment - everything else. Its lightness is measured relative to the palette's other pigment colours, so the
//             shading ramp survives on dark and light species alike.
// Albino: pigment becomes cream inside a light range, with a trace of the old hue so regions stay apart.
// Red:    pigment keeps its luma, hue folds into a warm band (dark shades deep red, light shades apricot).
// Black:  pigment drops into a dark range with a faint trace of its hue, like the pattern on a melanistic leopard.
#include "global.h"
#include "constants/rgb.h"
#include "decompress.h"
#include "palette.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "sprite.h"
#include "genetics.h"
#include "genetics_palette.h"

#define OUTLINE_Y            40
#define WHITE_Y              200
#define WHITE_CHROMA         48
#define ALBINO_LO            150
#define ALBINO_HI            248
#define ALBINO_HUE           160    // hue scale 0..1535, 256 = 60 degrees
#define ALBINO_CHROMA        26
#define ALBINO_RESIDUE_SHIFT 3
#define RED_HUE_BASE         0
#define RED_HUE_SPAN         80
#define RED_HUE_LUMA         120
#define RED_CHROMA_MIN       120
#define BLACK_LO             18
#define BLACK_HI             96
#define BLACK_CHROMA_NUM     1
#define BLACK_CHROMA_DEN     6
#define BLACK_CHROMA_MAX     16
#define BLACK_OUTLINE_NUM    1
#define BLACK_OUTLINE_DEN    3

enum {
    KIND_OUTLINE,
    KIND_WHITE,
    KIND_PIGMENT,
};

u8 GetBoxMonCoat(const struct BoxPokemon *boxMon)
{
    struct Genome genome;

    if (!boxMon->hasSpecies || boxMon->isEgg)
        return COAT_NORMAL; // eggs keep the egg palette
    GetBoxMonGenome(boxMon, &genome);
    return GenomeCoat(&genome);
}

u8 GetMonCoat(struct Pokemon *mon)
{
    return GetBoxMonCoat(&mon->box);
}

static bool8 IsSameMon(const struct BoxPokemon *boxMon, u32 personality, u32 otId)
{
    return boxMon->hasSpecies && boxMon->personality == personality && boxMon->otId == otId;
}

u8 FindCoatByPersonality(u32 personality, u32 otId)
{
    u32 i, j;

    for (i = 0; i < PARTY_SIZE; i++)
    {
        if (IsSameMon(&gPlayerParty[i].box, personality, otId))
            return GetBoxMonCoat(&gPlayerParty[i].box);
        if (IsSameMon(&gEnemyParty[i].box, personality, otId))
            return GetBoxMonCoat(&gEnemyParty[i].box);
    }
    for (i = 0; i < TOTAL_BOXES_COUNT; i++)
    {
        for (j = 0; j < IN_BOX_COUNT; j++)
        {
            struct BoxPokemon *boxMon = GetBoxedMonPtr(i, j);
            if (IsSameMon(boxMon, personality, otId))
                return GetBoxMonCoat(boxMon);
        }
    }
    return COAT_NORMAL;
}

static inline s32 Expand5(s32 v)
{
    return (v << 3) | (v >> 2);
}

static s32 To5(s32 v)
{
    if (v < 0)
        v = 0;
    if (v > 255)
        v = 255;
    return (v * 31 + 127) / 255;
}

static inline s32 Luma(s32 r, s32 g, s32 b)
{
    return (r * 77 + g * 150 + b * 29) >> 8;
}

static s32 Max3(s32 a, s32 b, s32 c)
{
    s32 m = a > b ? a : b;
    return m > c ? m : c;
}

static s32 Min3(s32 a, s32 b, s32 c)
{
    s32 m = a < b ? a : b;
    return m < c ? m : c;
}

static s32 Hue(s32 r, s32 g, s32 b)
{
    s32 mx = Max3(r, g, b);
    s32 c = mx - Min3(r, g, b);
    s32 h;

    if (c == 0)
        return 0;
    if (mx == r)
        h = (g >= b) ? (256 * (g - b)) / c : 1536 - (256 * (b - g)) / c;
    else if (mx == g)
        h = (b >= r) ? 512 + (256 * (b - r)) / c : 512 - (256 * (r - b)) / c;
    else
        h = (r >= g) ? 1024 + (256 * (r - g)) / c : 1024 - (256 * (g - r)) / c;
    if (h >= 1536)
        h -= 1536;
    return h;
}

static void PureHue(s32 h, s32 *p)
{
    s32 f = h & 255;

    switch (h >> 8)
    {
    case 0:  p[0] = 255;     p[1] = f;       p[2] = 0;       break;
    case 1:  p[0] = 255 - f; p[1] = 255;     p[2] = 0;       break;
    case 2:  p[0] = 0;       p[1] = 255;     p[2] = f;       break;
    case 3:  p[0] = 0;       p[1] = 255 - f; p[2] = 255;     break;
    case 4:  p[0] = f;       p[1] = 0;       p[2] = 255;     break;
    default: p[0] = 255;     p[1] = 0;       p[2] = 255 - f; break;
    }
}

// a colour with luma `y`, hue `h` and as much of `chroma` (0..255) as fits without clipping
static void FromLumaHueChroma(s32 y, s32 h, s32 chroma, s32 *out)
{
    s32 p[3];
    s32 yp, d, lim, v, i;

    PureHue(h, p);
    yp = Luma(p[0], p[1], p[2]);
    for (i = 0; i < 3; i++)
    {
        d = p[i] - yp;
        if (d > 0)
            lim = ((255 - y) * 255) / d;
        else if (d < 0)
            lim = (y * 255) / (-d);
        else
            continue;
        if (chroma > lim)
            chroma = lim;
    }
    for (i = 0; i < 3; i++)
    {
        d = p[i] - yp;
        if (d >= 0)
            v = y + (d * chroma) / 255;
        else
            v = y - ((-d) * chroma) / 255;
        if (v < 0)
            v = 0;
        if (v > 255)
            v = 255;
        out[i] = v;
    }
}

static u8 Classify(s32 r, s32 g, s32 b, s32 *y, s32 *c)
{
    *y = Luma(r, g, b);
    *c = Max3(r, g, b) - Min3(r, g, b);
    if (*y < OUTLINE_Y)
        return KIND_OUTLINE;
    if (*y >= WHITE_Y && *c < WHITE_CHROMA)
        return KIND_WHITE;
    return KIND_PIGMENT;
}

static void ApplyCoatTo16(u16 *pal, u8 coat)
{
    s32 lo = 255, hi = 0, span, t, y, c, r, g, b, d, i, k;
    s32 out[3], rgb[3];
    u8 kind;

    for (i = 1; i < 16; i++)
    {
        r = Expand5(GET_R(pal[i]));
        g = Expand5(GET_G(pal[i]));
        b = Expand5(GET_B(pal[i]));
        if (Classify(r, g, b, &y, &c) == KIND_PIGMENT)
        {
            if (y < lo)
                lo = y;
            if (y > hi)
                hi = y;
        }
    }
    span = (hi > lo) ? hi - lo : 0;

    for (i = 1; i < 16; i++)
    {
        rgb[0] = r = Expand5(GET_R(pal[i]));
        rgb[1] = g = Expand5(GET_G(pal[i]));
        rgb[2] = b = Expand5(GET_B(pal[i]));
        kind = Classify(r, g, b, &y, &c);
        if (kind == KIND_WHITE)
            continue;
        if (span)
            t = (kind == KIND_PIGMENT) ? ((y - lo) * 255) / span : 0;
        else
            t = y;

        switch (coat)
        {
        case COAT_ALBINO:
            if (kind == KIND_OUTLINE)
                continue;
            FromLumaHueChroma(ALBINO_LO + (t * (ALBINO_HI - ALBINO_LO)) / 255, ALBINO_HUE, ALBINO_CHROMA, out);
            for (k = 0; k < 3; k++)
            {
                d = rgb[k] - y;
                out[k] += (d >= 0) ? (d >> ALBINO_RESIDUE_SHIFT) : -((-d) >> ALBINO_RESIDUE_SHIFT);
            }
            break;
        case COAT_RED:
            if (kind == KIND_OUTLINE)
                continue;
            FromLumaHueChroma(y,
                              RED_HUE_BASE + (Hue(r, g, b) * RED_HUE_SPAN) / 1536 + (y * RED_HUE_LUMA) / 255,
                              c > RED_CHROMA_MIN ? c : RED_CHROMA_MIN,
                              out);
            break;
        case COAT_BLACK:
            if (kind == KIND_OUTLINE)
            {
                out[0] = out[1] = out[2] = (y * BLACK_OUTLINE_NUM) / BLACK_OUTLINE_DEN;
            }
            else
            {
                c = (c * BLACK_CHROMA_NUM) / BLACK_CHROMA_DEN;
                if (c > BLACK_CHROMA_MAX)
                    c = BLACK_CHROMA_MAX;
                FromLumaHueChroma(BLACK_LO + (t * (BLACK_HI - BLACK_LO)) / 255, Hue(r, g, b), c, out);
            }
            break;
        default:
            return;
        }
        pal[i] = RGB(To5(out[0]), To5(out[1]), To5(out[2]));
    }
}

void ApplyCoatToPalette(u16 *palette, u32 count, u8 coat)
{
    u32 i;

    if (coat == COAT_NORMAL)
        return;
    for (i = 0; i < count; i++)
        ApplyCoatTo16(&palette[i * 16], coat);
}

void ApplyCoatToLoadedPalette(u16 offset, u8 coat)
{
    if (coat == COAT_NORMAL)
        return;
    ApplyCoatTo16(&gPlttBufferUnfaded[offset], coat);
    CpuCopy16(&gPlttBufferUnfaded[offset], &gPlttBufferFaded[offset], PLTT_SIZEOF(16));
}

void LoadCompressedPaletteWithCoat(const u32 *src, u16 offset, u16 size, u8 coat)
{
    LoadCompressedPalette(src, offset, size);
    if (coat != COAT_NORMAL)
    {
        ApplyCoatToPalette(&gPlttBufferUnfaded[offset], size / PLTT_SIZE_4BPP, coat);
        CpuCopy16(&gPlttBufferUnfaded[offset], &gPlttBufferFaded[offset], size);
    }
}

void LoadCompressedSpritePaletteWithCoat(const struct CompressedSpritePalette *src, u8 coat)
{
    struct SpritePalette dest;

    LZ77UnCompWram(src->data, gDecompressionBuffer);
    ApplyCoatToPalette((u16 *)gDecompressionBuffer, 1, coat);
    dest.data = (void *) gDecompressionBuffer;
    dest.tag = src->tag;
    LoadSpritePalette(&dest);
}
