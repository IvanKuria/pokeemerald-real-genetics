// Real Mendelian genetics (REALGEN_SPEC.md). Pure logic: no save structs, no hardware, only Random(), so this file
// can also be compiled on a host for tests. Storage of the packed genome inside BoxPokemon lives in pokemon.c.
#include "global.h"
#include "genetics.h"
#include "random.h"

// Allele frequencies as u32 fixed point (q * 2^32). A copy is the variant iff Random32() < threshold.
#define COAT_ALLELE_Q32     30370005u   // 1 / sqrt(20000): each coat colour is 1 in 20,000 in the wild
#define SHINY_ALLELE_Q32    47453133u   // 1 / sqrt(8192): wild shinies stay 1 in 8,192

// Poisson(0.58) P(0) as u32 fixed point. One or more copies: the Pokemon carries ONE lethal allele (only one fits).
#define LETHAL_LOAD_P0      2404745173u

#define IV_COPIES_PER_STAT  (GENOME_IV_LOCI * 2)

#define UNOWN_LETTERS 28
#define UNOWN_BITS(p) ((((p) & 0x03000000) >> 18) | (((p) & 0x00030000) >> 12) | (((p) & 0x00000300) >> 6) | ((p) & 3))

// Canonical bit order of the packed genome. pokemon.c stores these bits, lowest first, across the unused fields.
#define GENOME_BIT_IV       0   // 60 bits: stat * 10 + locus * 2 + copy
#define GENOME_BIT_SHINY    60  // 2 bits
#define GENOME_BIT_COAT     62  // 6 bits: C, E, A
#define GENOME_BIT_LETHAL   68  // 7-bit carried locus id
#define GENOME_BIT_DEAD     75  // 1 bit: two copies at that id
#define GENOME_BIT_HAS      76  // 1 bit

struct BitPool
{
    u32 bits;
    u8 count;
};

static u32 TakeRandomBit(struct BitPool *pool)
{
    u32 bit;

    if (pool->count == 0)
    {
        pool->bits = Random();
        pool->count = 16;
    }
    bit = pool->bits & 1;
    pool->bits >>= 1;
    pool->count--;
    return bit;
}

// Deterministic generator for reads that must not touch the game RNG (legacy synthesis, IV setters).
static u32 NextSeeded(u32 *seed)
{
    *seed = ISO_RANDOMIZE1(*seed);
    return *seed >> 16;
}

static u8 RandomCopy(u32 q32)
{
    return ((u32)Random32() < q32) ? 1 : 0;
}

static u8 RandomDiploid(u32 q32)
{
    u8 alleles = RandomCopy(q32);
    alleles |= RandomCopy(q32) << 1;
    return alleles;
}

static u32 CountStatCopies(const struct Genome *genome, u8 stat)
{
    u32 i, count = 0;

    for (i = 0; i < GENOME_IV_LOCI; i++)
        count += (genome->iv[stat][i] & 1) + ((genome->iv[stat][i] >> 1) & 1);
    return count;
}

static bool8 IsVanillaShiny(u32 personality, u32 otId)
{
    u32 value = (otId >> 16) ^ (otId & 0xFFFF) ^ (personality >> 16) ^ (personality & 0xFFFF);
    return value < SHINY_ODDS;
}

void GenerateWildGenome(struct Genome *genome)
{
    struct BitPool pool = {0};
    u32 i, j, r;

    memset(genome, 0, sizeof(*genome));

    for (i = 0; i < NUM_STATS; i++)
    {
        for (j = 0; j < GENOME_IV_LOCI; j++)
        {
            genome->iv[i][j] = TakeRandomBit(&pool);
            genome->iv[i][j] |= TakeRandomBit(&pool) << 1;
        }
    }

    for (i = 0; i < NUM_COAT_LOCI; i++)
        genome->coat[i] = RandomDiploid(COAT_ALLELE_Q32);

    genome->shiny = RandomDiploid(SHINY_ALLELE_Q32);

    r = Random32();
    if (r >= LETHAL_LOAD_P0)  // 2 or more copies: keep 1 at random, which is just a random locus
        genome->lethal[0] = (Random() % GENOME_LETHAL_LOCI) + 1;
}

static u8 PassAllele(u8 alleles, struct BitPool *pool)
{
    return (alleles >> TakeRandomBit(pool)) & 1;
}

// The lethal copy one parent passes on (0 = none). A dead (two-copy) parent always passes its id; otherwise its one
// carried copy is passed with p = 0.5.
static u8 PassLethal(const struct Genome *parent, struct BitPool *pool)
{
    if (parent->lethal[0] == 0)
        return 0;
    if (GenomeIsLethal(parent) || TakeRandomBit(pool))
        return parent->lethal[0];
    return 0;
}

void InheritGenome(struct Genome *child, const struct Genome *mother, const struct Genome *father)
{
    struct BitPool pool = {0};
    u8 fromMother, fromFather;
    u32 i, j;

    memset(child, 0, sizeof(*child));

    for (i = 0; i < NUM_STATS; i++)
    {
        for (j = 0; j < GENOME_IV_LOCI; j++)
        {
            child->iv[i][j] = PassAllele(mother->iv[i][j], &pool);
            child->iv[i][j] |= PassAllele(father->iv[i][j], &pool) << 1;
        }
    }

    for (i = 0; i < NUM_COAT_LOCI; i++)
    {
        child->coat[i] = PassAllele(mother->coat[i], &pool);
        child->coat[i] |= PassAllele(father->coat[i], &pool) << 1;
    }

    child->shiny = PassAllele(mother->shiny, &pool);
    child->shiny |= PassAllele(father->shiny, &pool) << 1;

    fromMother = PassLethal(mother, &pool);
    fromFather = PassLethal(father, &pool);

    if (fromMother != 0 && fromMother == fromFather)
    {
        // Two copies at the same locus: the egg is dead. This always wins.
        child->lethal[0] = fromMother;
        child->lethal[1] = fromMother;
    }
    else if (fromMother != 0 && fromFather != 0)
    {
        // Two different ids: only one fits, keep one at random.
        child->lethal[0] = (Random() & 1) ? fromFather : fromMother;
    }
    else
    {
        child->lethal[0] = fromMother | fromFather;
    }
}

void CloneGenome(struct Genome *child, const struct Genome *parent)
{
    *child = *parent;
}

// Rebuilds the stat's loci with the fewest changes: copies are added or removed at seeded random positions until the
// count gives `iv`. A stat already at that value is left untouched, so heredity survives Get/Set round trips.
void GenomeFromIVs(struct Genome *genome, u8 stat, u8 iv, u32 seed)
{
    u32 target, count, pick, i;

    if (stat >= NUM_STATS)
        return;
    if (iv > MAX_PER_STAT_IVS)
        iv = MAX_PER_STAT_IVS;

    target = (20 * iv + 31) / 62;   // round(iv * 10 / 31)
    count = CountStatCopies(genome, stat);
    seed ^= (stat + 1) * 0x9E3779B9;

    while (count != target)
    {
        bool32 adding = (count < target);

        pick = NextSeeded(&seed) % (adding ? (IV_COPIES_PER_STAT - count) : count);
        for (i = 0; i < IV_COPIES_PER_STAT; i++)
        {
            u8 mask = 1 << (i & 1);
            bool32 isSet = (genome->iv[stat][i >> 1] & mask) != 0;

            if (isSet != adding)
            {
                if (pick == 0)
                {
                    genome->iv[stat][i >> 1] ^= mask;
                    break;
                }
                pick--;
            }
        }
        if (adding)
            count++;
        else
            count--;
    }
}

void SynthesizeLegacyGenome(struct Genome *genome, const u8 *ivs, u32 personality, u32 otId)
{
    u32 i;

    memset(genome, 0, sizeof(*genome));
    for (i = 0; i < NUM_STATS; i++)
        GenomeFromIVs(genome, i, ivs[i], personality);
    genome->shiny = IsVanillaShiny(personality, otId) ? 3 : 0;
}

void MatchGenomeShininess(struct Genome *genome, bool8 shiny)
{
    if (shiny)
        genome->shiny = 3;
    else if (genome->shiny == 3)
        genome->shiny = 1 << (Random() & 1);  // keep it a carrier
}

bool8 GenomeIsShiny(const struct Genome *genome)
{
    return genome->shiny == 3;
}

bool8 GenomeIsLethal(const struct Genome *genome)
{
    return genome->lethal[0] != 0 && genome->lethal[1] == genome->lethal[0];
}

// Epistasis: albino masks everything, red masks black.
u8 GenomeCoat(const struct Genome *genome)
{
    if (genome->coat[COAT_LOCUS_C] == 3)
        return COAT_ALBINO;
    if (genome->coat[COAT_LOCUS_E] == 3)
        return COAT_RED;
    if (genome->coat[COAT_LOCUS_A] == 3)
        return COAT_BLACK;
    return COAT_NORMAL;
}

u8 GenomeIV(const struct Genome *genome, u8 stat)
{
    if (stat >= NUM_STATS)
        return 0;
    return (31 * CountStatCopies(genome, stat) + 5) / 10;  // round(31 * count / 10)
}

static void PutBits(u32 *packed, u32 pos, u32 count, u32 value)
{
    u32 i;

    for (i = 0; i < count; i++, pos++)
    {
        if ((value >> i) & 1)
            packed[pos / 32] |= 1u << (pos % 32);
    }
}

static u32 GetBits(const u32 *packed, u32 pos, u32 count)
{
    u32 i, value = 0;

    for (i = 0; i < count; i++, pos++)
        value |= ((packed[pos / 32] >> (pos % 32)) & 1) << i;
    return value;
}

void PackGenome(const struct Genome *genome, u32 *packed)
{
    u32 i, j;

    for (i = 0; i < GENOME_PACKED_WORDS; i++)
        packed[i] = 0;

    for (i = 0; i < NUM_STATS; i++)
        for (j = 0; j < GENOME_IV_LOCI; j++)
            PutBits(packed, GENOME_BIT_IV + (i * GENOME_IV_LOCI + j) * 2, 2, genome->iv[i][j]);
    PutBits(packed, GENOME_BIT_SHINY, 2, genome->shiny);
    for (i = 0; i < NUM_COAT_LOCI; i++)
        PutBits(packed, GENOME_BIT_COAT + i * 2, 2, genome->coat[i]);
    PutBits(packed, GENOME_BIT_LETHAL, 7, genome->lethal[0]);
    PutBits(packed, GENOME_BIT_DEAD, 1, GenomeIsLethal(genome));
    PutBits(packed, GENOME_BIT_HAS, 1, 1);
}

bool8 UnpackGenome(const u32 *packed, struct Genome *genome)
{
    u32 i, j;

    for (i = 0; i < NUM_STATS; i++)
        for (j = 0; j < GENOME_IV_LOCI; j++)
            genome->iv[i][j] = GetBits(packed, GENOME_BIT_IV + (i * GENOME_IV_LOCI + j) * 2, 2);
    genome->shiny = GetBits(packed, GENOME_BIT_SHINY, 2);
    for (i = 0; i < NUM_COAT_LOCI; i++)
        genome->coat[i] = GetBits(packed, GENOME_BIT_COAT + i * 2, 2);
    genome->lethal[0] = GetBits(packed, GENOME_BIT_LETHAL, 7);
    genome->lethal[1] = GetBits(packed, GENOME_BIT_DEAD, 1) ? genome->lethal[0] : 0;
    return GetBits(packed, GENOME_BIT_HAS, 1);
}

// 2 = Unown bits unchanged, 1 = same Unown letter, 0 = letter changed.
static s32 UnownScore(u32 candidate, u32 original)
{
    if (UNOWN_BITS(candidate) == UNOWN_BITS(original))
        return 2;
    if (UNOWN_BITS(candidate) % UNOWN_LETTERS == UNOWN_BITS(original) % UNOWN_LETTERS)
        return 1;
    return 0;
}

// Vanilla's test is (tid ^ sid ^ hi ^ lo) < 8. The low byte (gender, ability bit) and the nature (p % 25) are always
// kept; the Unown letter is kept when any solution allows it (always for non-shiny, often for shiny).
u32 SolvePersonalityForShiny(u32 personality, u32 otId, bool8 shiny)
{
    u32 nature = personality % NUM_NATURES;
    u32 best = personality;
    s32 bestScore = -1;
    u32 i;

    if (IsVanillaShiny(personality, otId) == (shiny ? TRUE : FALSE))
        return personality;

    if (shiny)
    {
        // Every shiny personality with this low byte: lo = lowByte | x << 8, hi = tsid ^ lo ^ v, x < 256, v < 8.
        // Some candidate always has the right nature.
        u32 tsid = (otId >> 16) ^ (otId & 0xFFFF);
        u32 lowByte = personality & 0xFF;
        u32 start = (personality >> 8) & 0x7FF;

        for (i = 0; i < 0x800; i++)
        {
            u32 n = (start + i) & 0x7FF;
            u32 lo = lowByte | ((n >> 3) << 8);
            u32 hi = (tsid ^ lo ^ (n & 7)) & 0xFFFF;
            u32 candidate = (hi << 16) | lo;
            s32 score;

            if (candidate % NUM_NATURES != nature)
                continue;
            score = UnownScore(candidate, personality);
            if (score > bestScore)
            {
                best = candidate;
                bestScore = score;
                if (score == 2)
                    break;
            }
        }
    }
    else
    {
        // Adding a multiple of 25 to the high half keeps p % 25 (2^16 * 25k) and the low half. Any change of 25 or more
        // flips a high-half bit above bit 2, which takes the shiny value to 8 or more.
        u32 hi = personality >> 16;
        u32 lo = personality & 0xFFFF;
        u32 step;

        for (step = 25; step <= 0xFFFF; step += 25)
        {
            for (i = 0; i < 2; i++)
            {
                u32 newHi;
                u32 candidate;
                s32 score;

                if (i == 0)
                {
                    if (hi + step > 0xFFFF)
                        continue;
                    newHi = hi + step;
                }
                else
                {
                    if (hi < step)
                        continue;
                    newHi = hi - step;
                }
                candidate = (newHi << 16) | lo;
                if (IsVanillaShiny(candidate, otId))
                    continue;
                score = UnownScore(candidate, personality);
                if (score > bestScore)
                {
                    best = candidate;
                    bestScore = score;
                }
            }
            if (bestScore == 2)
                break;
        }
    }

    return best;
}
