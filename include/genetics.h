#ifndef GUARD_GENETICS_H
#define GUARD_GENETICS_H

// Real Mendelian genetics for Pokémon (see REALGEN_SPEC.md). Every Pokémon is diploid: each locus holds two allele
// bits, bit 0 = copy from the mother, bit 1 = copy from the father. 1 = the recessive variant (coat, shiny, lethal)
// or the "+" copy (IV loci).

#define GENOME_IV_LOCI      5   // loci per stat
#define GENOME_LETHAL_SLOTS 2   // carried lethal alleles stored per Pokémon
#define GENOME_LETHAL_LOCI  15  // locus ids 1..15 (0 = empty slot)

enum {
    COAT_LOCUS_C,   // TYR: c/c albino, masks everything
    COAT_LOCUS_E,   // MC1R: e/e red, masks black
    COAT_LOCUS_A,   // ASIP: a/a black
    NUM_COAT_LOCI
};

enum {
    COAT_NORMAL,
    COAT_ALBINO,
    COAT_RED,
    COAT_BLACK,
};

struct Genome
{
    u8 iv[NUM_STATS][GENOME_IV_LOCI];   // two allele bits per locus, stat order = MON_DATA_HP_IV .. MON_DATA_SPDEF_IV order
    u8 coat[NUM_COAT_LOCI];             // two allele bits per locus
    u8 shiny;                           // two allele bits
    u8 lethal[GENOME_LETHAL_SLOTS];     // carried lethal locus ids (1..15), 0 = empty; each slot is ONE copy.
                                        // Both slots holding the SAME id = two copies at that locus = dead egg
                                        // (GenomeIsLethal). Living Pokemon always have distinct ids or empty slots.
};

struct BoxPokemon;

// storage
void GetBoxMonGenome(const struct BoxPokemon *boxMon, struct Genome *genome);   // synthesizes for hasGenome == 0
void SetBoxMonGenome(struct BoxPokemon *boxMon, const struct Genome *genome);   // packs, sets hasGenome, fixes checksum

// creation and inheritance
void GenerateWildGenome(struct Genome *genome);
void InheritGenome(struct Genome *child, const struct Genome *mother, const struct Genome *father);
void CloneGenome(struct Genome *child, const struct Genome *parent);
void GenomeFromIVs(struct Genome *genome, u8 stat, u8 iv, u32 seed);   // that stat's loci rebuilt to give `iv`

// phenotype
bool8 GenomeIsShiny(const struct Genome *genome);
bool8 GenomeIsLethal(const struct Genome *genome);
u8 GenomeCoat(const struct Genome *genome);
u8 GenomeIV(const struct Genome *genome, u8 stat);

// a personality that keeps gender byte, ability bit and nature, and is vanilla-shiny iff `shiny`
u32 SolvePersonalityForShiny(u32 personality, u32 otId, bool8 shiny);

#endif // GUARD_GENETICS_H
