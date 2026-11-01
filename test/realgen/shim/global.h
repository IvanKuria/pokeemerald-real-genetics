// Host stand-in for pokeemerald's global.h, placed first on the include path by test/realgen/Makefile.
// Pulls in only host-safe headers from the real include/ tree (types and constant #defines).
#ifndef GUARD_GLOBAL_H
#define GUARD_GLOBAL_H

#include <string.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include "gba/types.h"
#include "constants/global.h"
#include "constants/species.h"
#include "constants/pokemon.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NULL
#define NULL ((void *)0)
#endif

#define ARRAY_COUNT(array) (size_t)(sizeof(array) / sizeof((array)[0]))
#define NELEMS(arr) (sizeof(arr)/sizeof(*(arr)))
#define SWAP(a, b, temp) { temp = a; a = b; b = temp; }
#define min(a, b) ((a) < (b) ? (a) : (b))
#define max(a, b) ((a) >= (b) ? (a) : (b))

#define AGB_ASSERT(exp)
#define AGB_WARNING(exp)
#define DebugPrintf(...)

#endif // GUARD_GLOBAL_H
