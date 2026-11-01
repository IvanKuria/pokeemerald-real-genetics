# Pokémon Emerald: Real Genetics (pokeemerald fork) spec

Breeding in Emerald, rewritten to follow real Mendelian genetics, as shown in the video "I Gave Pokémon Real Genetics". Every number matches `research/pokemon-genetics/realgen/realgen_sim.py`, the simulation behind the video's numbers. Ships as a `.bps` patch for a legally owned Pokémon Emerald (USA) ROM. **The ROM is never distributed.**

## Rules (player-facing)

1. **Diploid genes.** Every Pokémon has two copies of each gene, one from each parent, each copy picked 50/50.
2. **Coat colour:** three recessive genes, with real epistasis (which gene masks which):
   - **TYR** c/c → **albino** (white, masks everything).
   - **MC1R** e/e → **red** (masks black).
   - **ASIP** a/a → **black**.
   - Each variant allele has frequency q = 1/√20000, so each colour is 1 in 20,000 in the wild, and about 1 in 71 wild Pokémon carry it.
3. **Shiny is a recessive gene.** q = 1/√8192, so wild shinies stay exactly 1 in 8,192, and about 1 in 46 wild Pokémon carry it. Shiny = s/s.
4. **IVs are polygenic.** Each stat is set by 5 genes × 2 copies, each copy 0 or 1, with wild frequency 0.5. The stat's IV = round(31 × (number of 1 copies) / 10), so IVs take 11 values: 0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31.
5. **Lethal recessives.** A wild Pokémon carries Poisson(0.58) hidden lethal alleles at random among loci 1 to 127, and keeps at most ONE (if it would carry 2 or more, one is kept at random). An egg with two copies at the same locus **never hatches**: when it would hatch, it shows "The EGG didn't hatch…" and is removed. Random wild pairs almost never make a dead egg; related pairs (siblings) do much more often.
   - In the simulation there were 2,000 loci and Pokémon carried several. Here there are 127 loci and each Pokémon stores at most 1 carried allele, plus a "dead" bit for an egg with two copies.
6. **Species barrier.** Only Pokémon from the same evolution family can breed (same egg species).
7. **Ditto** works like the Amazon molly (gynogenesis): the egg is a full genetic copy of the non-Ditto parent. Ditto × Ditto makes nothing.

Vanilla Day Care behaviour that stays: egg timing, nature/gender/ability rolls (unless stated), move inheritance.

## Storage: no new save space, no feature removed

The genome is 77 bits, packed into bits vanilla never uses:

| bits | where | holds |
|---|---|---|
| 30 | `PokemonSubstruct3` IV fields (repurposed; IVs are now *computed* from genes) | IV loci, part 1 |
| 16 | `PokemonSubstruct0.filler` | IV loci, part 2 |
| 16 | `BoxPokemon.unknown` | 14 bits IV loci (part 3) + 2 bits shiny |
| 4 | `BoxPokemon.unused` flag bits | coat c, e (2 + 2) |
| 11 | `PokemonSubstruct3` ribbons that can never be earned in Emerald (marine, land, sky, country, national, earth, world, unusedRibbons ×4) | coat a (2) + lethal locus id (7 bits, 1..127, 0 = none) + dead bit (1, two copies at that id) + `hasGenome` flag (1) |

60 + 2 + 6 + 7 + 1 + 1 = 77. The exact bit order is defined in `src/genetics.c` (`PackGenome` / `UnpackGenome`) and nowhere else.

**Old saves and traded-in Pokémon** (`hasGenome == 0`): the genome is synthesized deterministically.
- IV loci: random arrangement with k = round(IV × 10 / 31) "1" copies, seeded from the personality value.
- Shiny: s/s if vanilla-shiny, otherwise S/S.
- Coat: wild type.
- Lethal: none.

Reads synthesize; the first write stores the genome and sets the flag.

**Shininess vs. sprites:** vanilla decides shininess from personality ^ otId in many places. Instead of rewriting every call site, any Pokémon whose genome is created or changed gets its personality re-solved (`SolvePersonalityForShiny`), so vanilla's check returns the genome's answer. Gender byte, ability bit and nature are kept.

## API (`include/genetics.h`): every lane codes against this, only the core lane changes it

See the header. In short:
- `GetBoxMonGenome` / `SetBoxMonGenome`
- `GenerateWildGenome`, `InheritGenome`, `CloneGenome`
- `GenomeIsShiny`, `GenomeIsLethal`, `GenomeCoat`, `GenomeIV`
- `GenomeFromIVs`
- `SolvePersonalityForShiny`

`GetMonData(MON_DATA_*_IV)` returns `GenomeIV`. `SetMonData(MON_DATA_*_IV)` rewrites that stat's loci to match (via `GenomeFromIVs`), so trainer/fixed-IV code keeps working.

## Lanes

| lane | owns | does |
|---|---|---|
| core | `src/genetics.c`, `src/pokemon.c` | pack/unpack, legacy synthesis, IV getters/setters, wild genome at `CreateBoxMon`, shiny personality solver, `MON_DATA_*_IV` behaviour |
| breeding | `src/daycare.c`, `src/egg_hatch.c` | species barrier, Ditto cloning, Mendelian inheritance, lethal eggs that don't hatch, egg personality re-solve |
| colours | new `src/genetics_palette.c` + palette call sites (battle, summary, hatch, trade, evolution, contest, Pokédex-not) | generic, per-species-safe palette transforms for albino, red and black |
| tests | `test/realgen/` (host C tests) + emulator smoke scripts | statistical tests vs. `realgen_sim.py` numbers; boot/egg/hatch checks in mGBA |
| release | `tools/realgen/`, `README_REALGEN.md`, CI | `.bps` patch from clean vs. built ROM (SHA1-checked base), player README, GitHub Actions that build and publish ONLY the patch |

**Build rule (8 GB machine):** always build with `~/Tools/gba/bin/locked-make`, never plain `make`. Only one build runs at a time.

**Lethal encoding (amended again):** 8 bits hold the lethal genes: a 7-bit carried locus id (1..127, 0 = none) and 1 dead bit. In `struct Genome`, `lethal[0]` = the carried id and `lethal[1]` = the same id if the egg is dead, else 0; `GenomeIsLethal` = `lethal[0] && lethal[1] == lethal[0]`. Wild: Poisson(0.58) copies at random loci, keep 1 at random if 2 or more. Inheritance: each parent passes its carried copy with p = .5. Both passing the same id = dead egg (both slots), which always wins. Two different ids arrive = keep one at random. Legacy Pokémon carry none. (With 15 loci and 2 slots, random wild crosses died 1 in 194, far too often; siblings stay around 1 in 15.)

**Core lane layering:**
- `src/genetics.c` = pure logic (inheritance, wild generation, phenotype, personality solver). It only needs basic types and `Random()`, so it can be compiled on the host for tests.
- Pack/unpack against the save struct lives in `src/pokemon.c`, which has the encrypt/decrypt helpers.
