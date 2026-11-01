// Seedable host PRNG behind Random()/Random2() for the genetics tests (splitmix64 seeding, xoshiro256** output).
#include "global.h"
#include "random.h"

u32 gRngValue;
u32 gRng2Value;
static u64 sState[4] = {1, 2, 3, 4};
static u64 sCalls;

static u64 SplitMix(u64 *x)
{
    u64 z = (*x += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static u64 Rotl(u64 x, int k) { return (x << k) | (x >> (64 - k)); }

static u64 Next(void)
{
    u64 r = Rotl(sState[1] * 5, 7) * 9;
    u64 t = sState[1] << 17;
    sState[2] ^= sState[0];
    sState[3] ^= sState[1];
    sState[1] ^= sState[2];
    sState[0] ^= sState[3];
    sState[2] ^= t;
    sState[3] = Rotl(sState[3], 45);
    return r;
}

void HostSeedRng(u64 seed)
{
    int i;
    for (i = 0; i < 4; i++)
        sState[i] = SplitMix(&seed);
    sCalls = 0;
}

u64 HostRandomCalls(void) { return sCalls; }

u16 Random(void)
{
    sCalls++;
    return (u16)(Next() >> 48);
}

u16 Random2(void) { return Random(); }
void SeedRng(u16 seed) { HostSeedRng(seed); }
void SeedRng2(u16 seed) { HostSeedRng(seed); }
