// Host statistical tests for src/genetics.c against REALGEN_SPEC.md and the video's reference numbers
// (research/pokemon-genetics/realgen/results.json, produced by realgen_sim.py). Fixed seeds, so every run is
// reproducible. Run with: make -C test/realgen
//
// Lines marked "ok"/"FAIL" are assertions. Lines marked "info" are measurements reported without a pass/fail,
// used where the 15-locus game model is expected to differ from the 2,000-locus simulation.
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "global.h"
#include "random.h"
#include "genetics.h"

static int sChecks, sFails;

#define CHECK(cond, ...) do {                                   \
    sChecks++;                                                  \
    if (cond) printf("  ok    "); else { sFails++; printf("  FAIL  "); } \
    printf(__VA_ARGS__); printf("\n");                          \
} while (0)
#define INFO(...) do { printf("  info  "); printf(__VA_ARGS__); printf("\n"); } while (0)
#define SECTION(name) printf("\n[%s]\n", name)

// spec rule 4: IV = round(31 * ones / 10)
static const u8 sIvTable[11] = {0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31};

static int Copies(u8 locus) { return (locus & 1) + ((locus >> 1) & 1); }
static bool8 InRange(double x, double lo, double hi) { return x >= lo && x <= hi; }

static void Wild(struct Genome *g)
{
    memset(g, 0, sizeof(*g));
    GenerateWildGenome(g);
}

static void Inherit(struct Genome *child, const struct Genome *m, const struct Genome *f)
{
    memset(child, 0, sizeof(*child));
    InheritGenome(child, m, f);
}

static bool8 AllPerfect(const struct Genome *g)
{
    int s;
    for (s = 0; s < NUM_STATS; s++)
        if (GenomeIV(g, s) != 31)
            return FALSE;
    return TRUE;
}

static bool8 VanillaShiny(u32 personality, u32 otId)
{
    u32 v = (otId >> 16) ^ (otId & 0xFFFF) ^ (personality >> 16) ^ (personality & 0xFFFF);
    return v < 8;
}

static u32 Rand32Host(void) { return ((u32)Random() << 16) | Random(); }

// ---------------------------------------------------------------------------------------------- phenotype tables

static void TestPhenotypeTables(void)
{
    struct Genome g;
    int s, i, code, bad, c, e, a;

    SECTION("phenotype: GenomeIV table (all 1,024 locus arrangements per stat)");
    bad = 0;
    for (s = 0; s < NUM_STATS; s++)
    {
        for (code = 0; code < 1024; code++)
        {
            int ones = 0, other;
            memset(&g, 0, sizeof(g));
            for (i = 0; i < GENOME_IV_LOCI; i++)
            {
                g.iv[s][i] = (code >> (2 * i)) & 3;
                ones += Copies(g.iv[s][i]);
            }
            if (GenomeIV(&g, s) != sIvTable[ones])
                bad++;
            for (other = 0; other < NUM_STATS; other++)
                if (other != s && GenomeIV(&g, other) != 0)
                    bad++;
        }
    }
    CHECK(bad == 0, "GenomeIV = round(31 * ones / 10) for every arrangement, other stats untouched (%d mismatches)", bad);

    SECTION("phenotype: coat epistasis (all 64 genotypes)");
    bad = 0;
    for (c = 0; c < 4; c++)
        for (e = 0; e < 4; e++)
            for (a = 0; a < 4; a++)
            {
                u8 want = (c == 3) ? COAT_ALBINO : (e == 3) ? COAT_RED : (a == 3) ? COAT_BLACK : COAT_NORMAL;
                memset(&g, 0, sizeof(g));
                g.coat[COAT_LOCUS_C] = c;
                g.coat[COAT_LOCUS_E] = e;
                g.coat[COAT_LOCUS_A] = a;
                if (GenomeCoat(&g) != want)
                    bad++;
            }
    CHECK(bad == 0, "c/c albino masks all, e/e red masks black, a/a black (%d mismatches)", bad);

    SECTION("phenotype: shiny and lethal");
    bad = 0;
    for (c = 0; c < 4; c++)
    {
        memset(&g, 0, sizeof(g));
        g.shiny = c;
        if (GenomeIsShiny(&g) != (c == 3))
            bad++;
    }
    CHECK(bad == 0, "shiny only for s/s (%d mismatches)", bad);
    {
        static const u8 slots[][3] = { {0, 0, 0}, {5, 0, 0}, {0, 5, 0}, {3, 9, 0}, {7, 7, 1}, {127, 127, 1}, {1, 127, 0} };
        bad = 0;
        for (i = 0; i < (int)ARRAY_COUNT(slots); i++)
        {
            memset(&g, 0, sizeof(g));
            g.lethal[0] = slots[i][0];
            g.lethal[1] = slots[i][1];
            if (!GenomeIsLethal(&g) != !slots[i][2])
                bad++;
        }
        CHECK(bad == 0, "lethal only when lethal[1] == lethal[0] != 0 (%d mismatches)", bad);
    }
}

// ---------------------------------------------------------------------------------------------- wild population

static void TestWild(void)
{
    const long N = 8000000;
    long n, homo[NUM_COAT_LOCI] = {0}, het[NUM_COAT_LOCI] = {0}, alleles[NUM_COAT_LOCI] = {0}, phen[4] = {0};
    long shinyHomo = 0, shinyHet = 0, shinyAlleles = 0, shinyPheno = 0;
    long ivHist[32] = {0}, ivBadValue = 0, ivPlus = 0, ivCopies = 0, ivRaw = 0;
    long lethalCarriers = 0, lethalBadId = 0, lethalSecond = 0, lethalDead = 0, lethalById[GENOME_LETHAL_LOCI + 1] = {0};
    double q = 1.0 / sqrt(20000.0), qs = 1.0 / sqrt(8192.0);
    int l, s, i;

    SECTION("wild population (8,000,000 GenerateWildGenome calls, seed 1)");
    HostSeedRng(1);
    for (n = 0; n < N; n++)
    {
        struct Genome g;
        Wild(&g);
        for (l = 0; l < NUM_COAT_LOCI; l++)
        {
            int k = Copies(g.coat[l]);
            alleles[l] += k;
            het[l] += (k == 1);
            homo[l] += (k == 2);
        }
        phen[GenomeCoat(&g) & 3]++;
        shinyAlleles += Copies(g.shiny);
        shinyHet += Copies(g.shiny) == 1;
        shinyHomo += Copies(g.shiny) == 2;
        shinyPheno += GenomeIsShiny(&g) ? 1 : 0;
        for (s = 0; s < NUM_STATS; s++)
        {
            u8 iv = GenomeIV(&g, s);
            if (iv > 31) iv = 31, ivBadValue++;
            ivHist[iv]++;
            for (i = 0; i < GENOME_IV_LOCI; i++)
            {
                if (g.iv[s][i] > 3) ivRaw++;
                ivPlus += Copies(g.iv[s][i]);
                ivCopies += 2;
            }
        }
        if (g.lethal[0] > GENOME_LETHAL_LOCI) lethalBadId++;
        else if (g.lethal[0] != 0) { lethalCarriers++; lethalById[g.lethal[0]]++; }
        if (g.lethal[1] != 0) lethalSecond++;
        if (GenomeIsLethal(&g)) lethalDead++;
    }
    for (i = 0; i < 32; i++)
    {
        int ok = 0, t;
        for (t = 0; t < 11; t++) ok |= (sIvTable[t] == i);
        if (!ok) ivBadValue += ivHist[i];
    }

    {
        static const char *names[] = {"TYR c (albino)", "MC1R e (red)", "ASIP a (black)"};
        for (l = 0; l < NUM_COAT_LOCI; l++)
        {
            double freq = (double)alleles[l] / (2.0 * N);
            CHECK(InRange(freq / q, 0.97, 1.03), "%s allele frequency %.6f (spec q = 1/sqrt(20000) = %.6f)", names[l], freq, q);
            CHECK(het[l] && InRange((double)N / het[l], 68.0, 74.5), "%s carriers 1 in %.1f (results.json: 1 in 71.2)",
                  names[l], het[l] ? (double)N / het[l] : 0.0);
            CHECK(homo[l] && InRange((double)N / homo[l], 16000, 25000), "%s homozygous 1 in %.0f (spec: 1 in 20,000)",
                  names[l], homo[l] ? (double)N / homo[l] : 0.0);
        }
    }
    CHECK(phen[COAT_ALBINO] && InRange((double)N / phen[COAT_ALBINO], 16000, 25000), "albino coat 1 in %.0f (spec: 1 in 20,000)",
          phen[COAT_ALBINO] ? (double)N / phen[COAT_ALBINO] : 0.0);
    CHECK(phen[COAT_RED] && InRange((double)N / phen[COAT_RED], 16000, 25000), "red coat 1 in %.0f (spec: 1 in 20,000)",
          phen[COAT_RED] ? (double)N / phen[COAT_RED] : 0.0);
    CHECK(phen[COAT_BLACK] && InRange((double)N / phen[COAT_BLACK], 16000, 25000), "black coat 1 in %.0f (spec: 1 in 20,000)",
          phen[COAT_BLACK] ? (double)N / phen[COAT_BLACK] : 0.0);

    {
        double freq = (double)shinyAlleles / (2.0 * N);
        CHECK(InRange(freq / qs, 0.97, 1.03), "shiny allele frequency %.6f (spec q = 1/sqrt(8192) = %.6f)", freq, qs);
        CHECK(shinyHet && InRange((double)N / shinyHet, 44.5, 47.2), "shiny carriers 1 in %.1f (results.json: 1 in 45.8)",
              shinyHet ? (double)N / shinyHet : 0.0);
        CHECK(shinyPheno && InRange((double)N / shinyPheno, 7314, 9309), "wild shiny 1 in %.0f (spec: exactly 1 in 8,192 expected)",
              shinyPheno ? (double)N / shinyPheno : 0.0);
        CHECK(shinyPheno == shinyHomo, "GenomeIsShiny agrees with s/s count (%ld vs %ld)", shinyPheno, shinyHomo);
    }

    {
        long total = N * NUM_STATS, mid = 0;
        for (i = 12; i <= 19; i++) mid += ivHist[i];
        CHECK(ivBadValue == 0 && ivRaw == 0, "every IV is one of the 11 spec values and every IV locus is 2 bits (%ld bad IVs, %ld bad loci)",
              ivBadValue, ivRaw);
        CHECK(InRange((double)ivPlus / ivCopies, 0.499, 0.501), "IV \"+\" copy frequency %.4f (spec 0.5)", (double)ivPlus / ivCopies);
        CHECK(InRange(100.0 * mid / total, 64.6, 66.6), "IVs in 12..19: %.2f%% (results.json: 65.6%%)", 100.0 * mid / total);
        CHECK(InRange(100.0 * ivHist[31] / total, 0.090, 0.105), "IV 31: %.4f%% = 1 in %.0f stats (results.json: 0.097%%, about 1 in 1,000)",
              100.0 * ivHist[31] / total, ivHist[31] ? (double)total / ivHist[31] : 0.0);
        INFO("IV histogram %%: 0:%.2f 3:%.2f 6:%.2f 9:%.2f 12:%.2f 16:%.2f 19:%.2f 22:%.2f 25:%.2f 28:%.2f 31:%.3f",
             100.0 * ivHist[0] / total, 100.0 * ivHist[3] / total, 100.0 * ivHist[6] / total, 100.0 * ivHist[9] / total,
             100.0 * ivHist[12] / total, 100.0 * ivHist[16] / total, 100.0 * ivHist[19] / total, 100.0 * ivHist[22] / total,
             100.0 * ivHist[25] / total, 100.0 * ivHist[28] / total, 100.0 * ivHist[31] / total);
    }

    {
        // spec: Poisson(0.58) copies, at most ONE kept, so P(carrier) = 1 - e^-0.58 = 0.4401, at a uniform locus 1..127
        double frac = (double)lethalCarriers / N, expect = 1.0 - exp(-0.58), perLocus = (double)lethalCarriers / GENOME_LETHAL_LOCI;
        long lo = LONG_MAX, hi = 0;
        for (i = 1; i <= GENOME_LETHAL_LOCI; i++)
        {
            if (lethalById[i] < lo) lo = lethalById[i];
            if (lethalById[i] > hi) hi = lethalById[i];
        }
        CHECK(lethalBadId == 0 && lethalSecond == 0 && lethalDead == 0,
              "wild lethal: id in 1..%d, lethal[1] always 0, no wild Pokemon is lethal (%ld bad ids, %ld with lethal[1], %ld lethal)",
              GENOME_LETHAL_LOCI, lethalBadId, lethalSecond, lethalDead);
        CHECK(InRange(frac, expect - 0.002, expect + 0.002), "wild lethal carriers %.4f (1 - e^-0.58 = %.4f)", frac, expect);
        CHECK(lo > 0.95 * perLocus && hi < 1.05 * perLocus, "lethal locus uniform over 1..%d: per-locus count %ld..%ld (mean %.0f)",
              GENOME_LETHAL_LOCI, lo, hi, perLocus);
    }
}

// ---------------------------------------------------------------------------------------------- inheritance mechanics

static void TestMendel(void)
{
    const long N = 200000;
    struct Genome m, f, k;
    long n, mBit[NUM_STATS][GENOME_IV_LOCI] = {{0}}, fBitLeak = 0, both = 0, bothSame = 0, both2 = 0;
    int s, i, worst = 0;
    double worstDev = 0;

    SECTION("Mendel: segregation and independent assortment (200,000 eggs, seed 2)");
    HostSeedRng(2);
    memset(&m, 0, sizeof(m));
    memset(&f, 0, sizeof(f));
    for (s = 0; s < NUM_STATS; s++)
        for (i = 0; i < GENOME_IV_LOCI; i++)
            m.iv[s][i] = 1;            // mother heterozygous (one "+" copy) everywhere, father has none
    for (n = 0; n < N; n++)
    {
        Inherit(&k, &m, &f);
        for (s = 0; s < NUM_STATS; s++)
            for (i = 0; i < GENOME_IV_LOCI; i++)
            {
                mBit[s][i] += k.iv[s][i] & 1;
                fBitLeak += (k.iv[s][i] >> 1) & 1;
            }
        both += (k.iv[0][0] & 1) && (k.iv[3][4] & 1);
        bothSame += (k.iv[2][1] & 1) && (k.iv[2][2] & 1);
        both2 += (k.iv[0][0] & 1) && (k.iv[0][1] & 1);
    }
    for (s = 0; s < NUM_STATS; s++)
        for (i = 0; i < GENOME_IV_LOCI; i++)
        {
            double d = fabs((double)mBit[s][i] / N - 0.5);
            if (d > worstDev) worstDev = d, worst = s * GENOME_IV_LOCI + i;
        }
    CHECK(worstDev < 0.006, "het mother passes each copy 50/50 at every IV locus (worst locus %d off by %.4f)", worst, worstDev);
    CHECK(fBitLeak == 0, "mother's copies land in bit 0 only, father's bit 1 stays the father's (%ld leaks)", fBitLeak);
    CHECK(InRange((double)both / N, 0.24, 0.26), "loci on different stats assort independently: P(both) = %.4f (0.25)", (double)both / N);
    CHECK(InRange((double)bothSame / N, 0.24, 0.26) && InRange((double)both2 / N, 0.24, 0.26),
          "loci on the same stat assort independently: P(both) = %.4f, %.4f (0.25)", (double)bothSame / N, (double)both2 / N);

    // father side
    {
        long fBit = 0, mLeak = 0;
        memset(&m, 0, sizeof(m));
        memset(&f, 0, sizeof(f));
        f.coat[COAT_LOCUS_E] = 2;
        f.shiny = 1;
        for (n = 0; n < N; n++)
        {
            Inherit(&k, &m, &f);
            fBit += ((k.coat[COAT_LOCUS_E] >> 1) & 1) + ((k.shiny >> 1) & 1);
            mLeak += (k.coat[COAT_LOCUS_E] & 1) + (k.shiny & 1);
        }
        CHECK(InRange((double)fBit / (2 * N), 0.494, 0.506) && mLeak == 0,
              "het father passes coat/shiny 50/50 into bit 1: %.4f (%ld copies in bit 0)", (double)fBit / (2 * N), mLeak);
    }
}

static void TestLethalMechanics(void)
{
    const long N = 400000;
    struct Genome m, f, k;
    long n, dead = 0, carrier = 0, bad = 0, sum3 = 0, sum9 = 0, none = 0, bad2 = 0;

    SECTION("lethal inheritance mechanics (seed 3)");
    HostSeedRng(3);
    memset(&m, 0, sizeof(m));
    memset(&f, 0, sizeof(f));
    m.lethal[0] = 5;
    f.lethal[0] = 5;
    for (n = 0; n < N; n++)
    {
        Inherit(&k, &m, &f);
        if (GenomeIsLethal(&k)) { dead++; if (k.lethal[0] != 5 || k.lethal[1] != 5) bad++; }
        else if (k.lethal[0] == 5 && k.lethal[1] == 0) carrier++;
        else if (k.lethal[0] || k.lethal[1]) bad++;
    }
    CHECK(InRange((double)dead / N, 0.245, 0.255) && InRange((double)carrier / N, 0.495, 0.505) && bad == 0,
          "carrier x carrier at the same locus: %.2f%% dead (25), %.2f%% carriers (50), %ld malformed", 100.0 * dead / N,
          100.0 * carrier / N, bad);

    // different loci: each passes with p = .5; if both arrive, one is kept at random. P(carry 3) = P(carry 9) = 3/8
    memset(&m, 0, sizeof(m));
    memset(&f, 0, sizeof(f));
    m.lethal[0] = 3;
    f.lethal[0] = 9;
    dead = 0;
    for (n = 0; n < N; n++)
    {
        Inherit(&k, &m, &f);
        dead += GenomeIsLethal(&k) ? 1 : 0;
        sum3 += (k.lethal[0] == 3);
        sum9 += (k.lethal[0] == 9);
        none += (k.lethal[0] == 0);
        if (k.lethal[1] != 0 || (k.lethal[0] && k.lethal[0] != 3 && k.lethal[0] != 9)) bad2++;
    }
    CHECK(dead == 0 && bad2 == 0, "different loci: never dead, child keeps one id, lethal[1] stays 0 (%ld dead, %ld malformed)", dead, bad2);
    CHECK(InRange((double)sum3 / N, 0.37, 0.38) && InRange((double)sum9 / N, 0.37, 0.38) && InRange((double)none / N, 0.245, 0.255),
          "different loci: carries 3 %.4f, carries 9 %.4f, none %.4f (3/8, 3/8, 1/4)", (double)sum3 / N, (double)sum9 / N, (double)none / N);
}

// ---------------------------------------------------------------------------------------------- the video's numbers

static void TestCoatCross(void)
{
    const long N = 400000, C = 50000;
    struct Genome m, f, k;
    long n, cnt[4] = {0}, four = 0;
    int l;

    SECTION("triple carrier cross (400,000 eggs, seed 4)");
    HostSeedRng(4);
    Wild(&m);
    Wild(&f);
    for (l = 0; l < NUM_COAT_LOCI; l++)
        m.coat[l] = 1, f.coat[l] = 1;
    m.lethal[0] = m.lethal[1] = f.lethal[0] = f.lethal[1] = 0;
    for (n = 0; n < N; n++)
    {
        Inherit(&k, &m, &f);
        cnt[GenomeCoat(&k) & 3]++;
    }
    CHECK(InRange(100.0 * cnt[COAT_ALBINO] / N, 24.54, 25.54), "albino %.2f%% (results.json 25.04)", 100.0 * cnt[COAT_ALBINO] / N);
    CHECK(InRange(100.0 * cnt[COAT_RED] / N, 18.20, 19.20), "red %.2f%% (results.json 18.70)", 100.0 * cnt[COAT_RED] / N);
    CHECK(InRange(100.0 * cnt[COAT_BLACK] / N, 13.49, 14.49), "black %.2f%% (results.json 13.99)", 100.0 * cnt[COAT_BLACK] / N);
    CHECK(InRange(100.0 * cnt[COAT_NORMAL] / N, 41.77, 42.77), "normal %.2f%% (results.json 42.27)", 100.0 * cnt[COAT_NORMAL] / N);
    for (n = 0; n < C; n++)
    {
        int seen = 0, e;
        for (e = 0; e < 4; e++) { Inherit(&k, &m, &f); seen |= 1 << (GenomeCoat(&k) & 3); }
        four += (seen == 15);
    }
    CHECK(four && InRange((double)C / four, 14.0, 17.2), "clutch of 4 all different colours 1 in %.1f (results.json 1 in 15.5)",
          four ? (double)C / four : 0.0);
}

static int CmpInt(const void *a, const void *b) { return *(const int *)a - *(const int *)b; }

static void TestShinyBreeding(void)
{
    enum { T = 20000 };
    static int eggs[T];
    const long B = 100000;
    struct Genome sh, mate, kid0, kid1, egg, kid;
    long n, sum = 0, shinyBack = 0;
    int t, capped = 0;

    SECTION("shiny breeding from one wild shiny (20,000 runs, seed 5)");
    HostSeedRng(5);
    for (t = 0; t < T; t++)
    {
        int count = 2;
        Wild(&sh);
        sh.shiny = 3;
        Wild(&mate);
        Inherit(&kid0, &sh, &mate);    // one son, one daughter, as in realgen_sim.py
        Inherit(&kid1, &sh, &mate);
        for (;;)
        {
            count++;
            Inherit(&egg, &kid0, &kid1);
            if (GenomeIsShiny(&egg)) break;
            if (count > 10000) { capped++; break; }
        }
        eggs[t] = count;
        sum += count;
    }
    qsort(eggs, T, sizeof(eggs[0]), CmpInt);
    CHECK(capped == 0 && InRange((double)sum / T, 5.80, 6.12), "mean eggs to the next shiny %.2f (results.json 5.96)", (double)sum / T);
    CHECK(eggs[T / 2] == 5, "median eggs %d (results.json 5)", eggs[T / 2]);

    SECTION("backcross: shiny parent x its own carrier child (100,000 eggs, seed 6)");
    HostSeedRng(6);
    Wild(&sh);
    sh.shiny = 3;
    for (n = 0; n < B; n++)
    {
        Wild(&mate);
        Inherit(&kid, &sh, &mate);
        Inherit(&egg, &sh, &kid);
        shinyBack += GenomeIsShiny(&egg) ? 1 : 0;
    }
    CHECK(InRange(100.0 * shinyBack / B, 49.8, 51.8), "shiny %.2f%% (results.json 50.81)", 100.0 * shinyBack / B);
}

static void TestPerfectParents(void)
{
    const long N = 10000;
    struct Genome m, f, k;
    long n, perfect = 0;
    int s, i;

    SECTION("two perfect parents (10,000 eggs, seed 7)");
    HostSeedRng(7);
    Wild(&m);
    Wild(&f);
    for (s = 0; s < NUM_STATS; s++)
        for (i = 0; i < GENOME_IV_LOCI; i++)
            m.iv[s][i] = f.iv[s][i] = 3;
    m.lethal[0] = m.lethal[1] = f.lethal[0] = f.lethal[1] = 0;
    CHECK(AllPerfect(&m) && AllPerfect(&f), "parents read 31/31/31/31/31/31");
    for (n = 0; n < N; n++)
    {
        Inherit(&k, &m, &f);
        perfect += AllPerfect(&k) ? 1 : 0;
    }
    CHECK(perfect == N, "perfect babies %.2f%% (results.json 100)", 100.0 * perfect / N);
}

static void TestLethalRates(void)
{
    const long N = 1000000;
    struct Genome p, q, a, b, egg;
    long n, deadWild = 0, deadSib = 0;

    SECTION("egg death: random wild pairs vs full siblings (1,000,000 eggs each, seed 8)");
    HostSeedRng(8);
    for (n = 0; n < N; n++)
    {
        Wild(&p);
        Wild(&q);
        Inherit(&egg, &p, &q);
        deadWild += GenomeIsLethal(&egg) ? 1 : 0;
    }
    for (n = 0; n < N; n++)
    {
        Wild(&p);
        Wild(&q);
        Inherit(&a, &p, &q);
        Inherit(&b, &p, &q);
        Inherit(&egg, &a, &b);
        deadSib += GenomeIsLethal(&egg) ? 1 : 0;
    }
    INFO("random wild pair: %.3f%% of eggs die = 1 in %.0f (2,000-locus sim: 0.006%%, old 15-locus model: 1 in 194)", 100.0 * deadWild / N,
         deadWild ? (double)N / deadWild : 0.0);
    INFO("full siblings:    %.3f%% of eggs die = 1 in %.1f (2,000-locus sim: 7.14%% = 1 in 14)", 100.0 * deadSib / N,
         deadSib ? (double)N / deadSib : 0.0);
    {
        // one-allele model, a = P(carrier) = 1 - e^-0.58, L = 127 loci:
        //   random pair: a^2 / L same locus, then both pass (1/4)
        //   siblings: a kid carries a parent's id with 1/2 (other parent not a carrier) or 3/8 (both carriers);
        //   dead = 1/4 * P(both kids carry the same id) = 1/4 * (a^2 * 2 * (3/8)^2 + 2a(1-a) * (1/2)^2)
        double a = 1.0 - exp(-0.58), L = GENOME_LETHAL_LOCI;
        double wantWild = a * a / L / 4.0;
        double wantSib = (a * a * 2.0 * 0.140625 + 2.0 * a * (1.0 - a) * 0.25) / 4.0;
        INFO("expected: random pair 1 in %.0f, siblings 1 in %.1f", 1.0 / wantWild, 1.0 / wantSib);
        CHECK(InRange((double)deadWild / N, 0.85 * wantWild, 1.15 * wantWild), "random wild pair death %.4f%% within 15%% of %.4f%%",
              100.0 * deadWild / N, 100.0 * wantWild);
        CHECK(InRange((double)deadSib / N, 0.97 * wantSib, 1.03 * wantSib), "sibling death %.3f%% within 3%% of %.3f%%",
              100.0 * deadSib / N, 100.0 * wantSib);
        CHECK(deadSib > 50 * deadWild, "siblings die far more often than random pairs (%.0fx)", deadWild ? (double)deadSib / deadWild : 0.0);
    }
}

// ---------------------------------------------------------------------------------------------- Ditto, setters, personality

static void TestClone(void)
{
    struct Genome p, k;
    int n, bad = 0;

    SECTION("CloneGenome (Ditto gynogenesis, 10,000 parents, seed 9)");
    HostSeedRng(9);
    for (n = 0; n < 10000; n++)
    {
        Wild(&p);
        memset(&k, 0xAA, sizeof(k));
        CloneGenome(&k, &p);
        if (memcmp(&k, &p, sizeof(k)) != 0)
            bad++;
    }
    CHECK(bad == 0, "egg is an exact genetic copy of the non-Ditto parent (%d differ)", bad);
}

static void TestGenomeFromIVs(void)
{
    struct Genome g, before;
    int s, iv, seed, bad = 0, badOther = 0, badRaw = 0, exact = 0, exactBad = 0, other, i;

    SECTION("GenomeFromIVs round trips (6 stats x 32 IVs x 64 seeds, seed 10)");
    HostSeedRng(10);
    for (s = 0; s < NUM_STATS; s++)
        for (iv = 0; iv <= 31; iv++)
            for (seed = 0; seed < 64; seed++)
            {
                int k = (int)floor(iv * 10.0 / 31.0 + 0.5);   // spec: k = round(IV * 10 / 31), no ties for 0..31
                Wild(&g);
                before = g;
                GenomeFromIVs(&g, s, iv, Rand32Host());
                if (GenomeIV(&g, s) != sIvTable[k]) bad++;
                if (sIvTable[k] == iv) { exact++; if (GenomeIV(&g, s) != iv) exactBad++; }
                for (i = 0; i < GENOME_IV_LOCI; i++)
                    if (g.iv[s][i] > 3) badRaw++;
                for (other = 0; other < NUM_STATS; other++)
                    if (other != s && memcmp(g.iv[other], before.iv[other], sizeof(g.iv[other])) != 0) badOther++;
                if (memcmp(g.coat, before.coat, sizeof(g.coat)) || g.shiny != before.shiny || memcmp(g.lethal, before.lethal, sizeof(g.lethal)))
                    badOther++;
            }
    CHECK(exactBad == 0, "the 11 representable IVs read back exactly (%d of %d wrong)", exactBad, exact);
    CHECK(bad == 0, "any IV 0..31 reads back as the nearest representable value (%d wrong)", bad);
    CHECK(badOther == 0 && badRaw == 0, "only that stat's loci change, all loci stay 2-bit (%d other changes, %d bad loci)", badOther, badRaw);

    // the seed should pick the arrangement, so legacy Pokemon with the same IV do not all share one genotype
    {
        u32 patterns[256];
        int distinct = 0, j;
        for (seed = 0; seed < 256; seed++)
        {
            u32 code = 0;
            memset(&g, 0, sizeof(g));
            GenomeFromIVs(&g, 2, 16, (u32)seed * 2654435761u);
            for (i = 0; i < GENOME_IV_LOCI; i++) code = (code << 2) | (g.iv[2][i] & 3);
            for (j = 0; j < distinct && patterns[j] != code; j++);
            if (j == distinct) patterns[distinct++] = code;
        }
        CHECK(distinct >= 20, "IV 16 from 256 seeds gives %d distinct locus arrangements (spec: random arrangement seeded)", distinct);
    }
}

static void TestSolvePersonality(void)
{
    const long N = 400000;
    long n, badGender = 0, badAbility = 0, badNature = 0, badShiny[2] = {0}, tried[2] = {0}, kept = 0, keptOf = 0;

    SECTION("SolvePersonalityForShiny (400,000 cases, seed 11)");
    HostSeedRng(11);
    for (n = 0; n < N; n++)
    {
        u32 p = Rand32Host(), otId = Rand32Host(), r;
        bool8 want = (n & 1);
        if (n % 1000 == 0)    // occasionally start from an already-shiny personality
            p = (p & 0xFFFF) | ((((otId >> 16) ^ (otId & 0xFFFF) ^ (p & 0xFFFF)) & 0xFFF8) << 16);
        r = SolvePersonalityForShiny(p, otId, want);
        tried[want]++;
        if ((r & 0xFF) != (p & 0xFF)) badGender++;
        if ((r & 1) != (p & 1)) badAbility++;
        if (r % 25 != p % 25) badNature++;
        if (VanillaShiny(r, otId) != want) badShiny[want]++;
        if (VanillaShiny(p, otId) == want) { keptOf++; kept += (r == p); }
    }
    CHECK(badGender == 0, "gender byte (personality & 0xFF) kept (%ld changed)", badGender);
    CHECK(badAbility == 0, "ability bit (personality & 1) kept (%ld changed)", badAbility);
    CHECK(badNature == 0, "nature (personality %% 25) kept (%ld changed)", badNature);
    CHECK(badShiny[1] == 0, "shiny = TRUE gives a vanilla-shiny personality (%ld of %ld wrong)", badShiny[1], tried[1]);
    CHECK(badShiny[0] == 0, "shiny = FALSE gives a vanilla-non-shiny personality (%ld of %ld wrong)", badShiny[0], tried[0]);
    INFO("personality already matching the request left unchanged in %ld of %ld cases", kept, keptOf);
}

int main(void)
{
    printf("Real Genetics host tests (REALGEN_SPEC.md, reference: research/pokemon-genetics/realgen/results.json)\n");
    TestPhenotypeTables();
    TestWild();
    TestMendel();
    TestLethalMechanics();
    TestCoatCross();
    TestShinyBreeding();
    TestPerfectParents();
    TestLethalRates();
    TestClone();
    TestGenomeFromIVs();
    TestSolvePersonality();
    printf("\n%d checks, %d failed\n", sChecks, sFails);
    return sFails ? 1 : 0;
}
