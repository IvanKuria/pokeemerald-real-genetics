#ifndef GUARD_REALGEN_TEST_H
#define GUARD_REALGEN_TEST_H

// Test-only hooks, compiled in only with `make REALGEN_TEST=1` (see src/realgen_test.c).
#if REALGEN_TEST
void RealgenTest_Frame(void);
void RealgenTest_NewGame(void);
u8 RealgenTest_EggHatchState(void);   // CB2_EggHatch state, 0xFF when the hatch scene is not running
#endif

#endif // GUARD_REALGEN_TEST_H
