// Host stand-in for pokeemerald's random.h. Random() is backed by a seedable 64-bit PRNG (test/realgen/shim/rng.c).
#ifndef GUARD_RANDOM_H
#define GUARD_RANDOM_H

extern u32 gRngValue;
extern u32 gRng2Value;

u16 Random(void);
u16 Random2(void);
#define Random32() (Random() | (Random() << 16))
#define ISO_RANDOMIZE1(val) (1103515245 * (val) + 24691)
#define ISO_RANDOMIZE2(val) (1103515245 * (val) + 12345)
void SeedRng(u16 seed);
void SeedRng2(u16 seed);

// host-only: full 64-bit seed for the test PRNG
void HostSeedRng(u64 seed);
u64 HostRandomCalls(void);

#endif // GUARD_RANDOM_H
