#ifndef GUARD_GENETICS_PALETTE_H
#define GUARD_GENETICS_PALETTE_H

#include "genetics.h"

// Coat colour palette transforms for Real Genetics (REALGEN_SPEC.md). Generic rules, no per-species data, so they
// work on any 16-colour Pokemon palette, normal or shiny. COAT_NORMAL never changes anything.

// sprite palette tags for scenes that show two mons at once and give a recoloured one its own palette
#define PALTAG_COAT_MON(n) (0x7C00 + (n))

struct BoxPokemon;
struct Pokemon;
struct CompressedSpritePalette;

u8 GetBoxMonCoat(const struct BoxPokemon *boxMon);
u8 GetMonCoat(struct Pokemon *mon);

// For views that only know species/otId/personality: finds the mon in the player's party, the enemy party or the
// PC and returns its coat. Not found (other players' mons in link contests, released mons) = COAT_NORMAL.
u8 FindCoatByPersonality(u32 personality, u32 otId);

// recolour decompressed BGR555 palettes in place, `count` palettes of 16 colours; index 0 of each is left alone
void ApplyCoatToPalette(u16 *palette, u32 count, u8 coat);

// recolour 16 colours already loaded at `offset` in both palette buffers
void ApplyCoatToLoadedPalette(u16 offset, u8 coat);

// LoadCompressedPalette, recoloured for `coat`
void LoadCompressedPaletteWithCoat(const u32 *src, u16 offset, u16 size, u8 coat);

// LoadCompressedSpritePalette, recoloured for `coat`. Like vanilla, a tag that is already loaded is reused as is.
void LoadCompressedSpritePaletteWithCoat(const struct CompressedSpritePalette *src, u8 coat);

#endif // GUARD_GENETICS_PALETTE_H
