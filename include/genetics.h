#ifndef GUARD_GENETICS_H
#define GUARD_GENETICS_H

// Real Mendelian genetics for Pokémon (see REALGEN_SPEC.md). Every Pokémon is diploid: each locus holds two allele
// bits, bit 0 = copy from the mother, bit 1 = copy from the father. 1 = the recessive variant (coat, shiny, lethal)
// or the "+" copy (IV loci).

#define GENOME_IV_LOCI      5   // loci per stat
#define GENOME_LETHAL_SLOTS 2   // lethal[0] = carried locus id, lethal[1] = same id if the egg is dead (see struct)
#define GENOME_LETHAL_LOCI  127 // locus ids 1..127 (0 = none); a living Pokémon carries at most ONE lethal allele
#define GENOME_PACKED_BITS  77  // 60 IV + 2 shiny + 6 coat + 7 lethal id + 1 dead + 1 hasGenome
#define GENOME_PACKED_WORDS 3

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
    u8 lethal[GENOME_LETHAL_SLOTS];     // lethal[0] = the one carried lethal locus id (1..127), 0 = none.
                                        // lethal[1] = the same id when the egg got TWO copies there (dead egg,
                                        // GenomeIsLethal), else 0. Living Pokemon always have lethal[1] == 0.
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

// Added by the core lane.
// Note: SetBoxMonGenome also re-solves the personality (SolvePersonalityForShiny, data re-keyed) when vanilla
// shininess disagrees with the genome, so sprites and the shiny check follow the genome.
// The canonical 77-bit packed form (bit order defined only in genetics.c). PackGenome always sets hasGenome;
// UnpackGenome returns it. pokemon.c spreads the packed bits across the unused BoxPokemon fields.
void PackGenome(const struct Genome *genome, u32 *packed);              // packed[GENOME_PACKED_WORDS]
bool8 UnpackGenome(const u32 *packed, struct Genome *genome);
// The deterministic genome of an old-save or traded-in Pokemon (hasGenome == 0): ivs[NUM_STATS] are its vanilla IVs.
void SynthesizeLegacyGenome(struct Genome *genome, const u8 *ivs, u32 personality, u32 otId);
// Sets the shiny alleles to agree with a fixed shininess: s/s if shiny, otherwise an s/s becomes a random carrier.
void MatchGenomeShininess(struct Genome *genome, bool8 shiny);

#endif // GUARD_GENETICS_H
