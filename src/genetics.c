// Real Mendelian genetics (REALGEN_SPEC.md). STUB: compiles and links so every lane can build against
// include/genetics.h; the core lane replaces these bodies.
#include "global.h"
#include "pokemon.h"
#include "genetics.h"

void GetBoxMonGenome(const struct BoxPokemon *boxMon, struct Genome *genome) { memset(genome, 0, sizeof(*genome)); }
void SetBoxMonGenome(struct BoxPokemon *boxMon, const struct Genome *genome) { }
void GenerateWildGenome(struct Genome *genome) { memset(genome, 0, sizeof(*genome)); }
void InheritGenome(struct Genome *child, const struct Genome *mother, const struct Genome *father) { *child = *mother; }
void CloneGenome(struct Genome *child, const struct Genome *parent) { *child = *parent; }
void GenomeFromIVs(struct Genome *genome, u8 stat, u8 iv, u32 seed) { }
bool8 GenomeIsShiny(const struct Genome *genome) { return FALSE; }
bool8 GenomeIsLethal(const struct Genome *genome) { return FALSE; }
u8 GenomeCoat(const struct Genome *genome) { return COAT_NORMAL; }
u8 GenomeIV(const struct Genome *genome, u8 stat) { return 0; }
u32 SolvePersonalityForShiny(u32 personality, u32 otId, bool8 shiny) { return personality; }
