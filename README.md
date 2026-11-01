# Pokémon Emerald: Real Genetics

So, this is the playable version of the mod from my video **[I Gave Pokémon Real Genetics](https://www.youtube.com/watch?v=EN6cjSL-OmY)**. A lot of you guys asked if you could actually play it, so here it is.

It's Pokémon Emerald, but breeding follows real genetics. Two normal brown Eevees can have an albino baby, shiny is a gene you can breed for, IVs come from genes, some eggs never hatch, and Ditto makes clones. The rest of the game is just normal Emerald.

It's built on [pret/pokeemerald](https://github.com/pret/pokeemerald), the decompilation of Emerald. You get it as a `.bps` patch that you put on your own copy of the game. I don't give out ROMs, and there are no ROMs in this repo.

**[Download the latest patch](../../releases/latest)**

> **Heads up, this is a beta.** I'm still testing everything in game, so please back up your save before you play, and if anything breaks, [open an issue](../../issues/new/choose) and tell me what happened.

## Contents

- [Why I made this](#why-i-made-this)
- [How to play](#how-to-play)
- [The rules](#the-rules)
- [What's different and known limitations](#whats-different-and-known-limitations)
- [FAQ](#faq)
- [Building from source](#building-from-source)
- [Credits](#credits)
- [License](#license)
- [Disclaimer](#disclaimer)

## Why I made this

So, while I was researching the video I kept noticing how little real genetics Pokémon actually uses. In the normal game a baby always looks like its parents, the only exception is a random 1 in 8,192 shiny, and the code never even checks whether the parents are shiny. Any two Pokémon in the same egg group can have a baby, and IVs are basically random numbers with a few copied over.

I wanted to see what happens if breeding worked the way it does in real animals. It made breeding a lot harder, but honestly I think it made it way more fun, because you never really know what's going to come out of an egg. After the video came out a bunch of you asked to play it, so I cleaned it all up into a version you can actually play.

## How to play

You need three things: your own Pokémon Emerald ROM, the patch from the [releases page](../../releases/latest), and a GBA emulator.

### 1. Get your own Emerald ROM

Dump it from your own cartridge (a GBxCart RW or a GB Operator both work for this). It has to be the English version, **Pokémon Emerald (USA, Europe)**. The patch doesn't work on Ruby, Sapphire, FireRed, LeafGreen, or the Japanese, French, German, Italian or Spanish versions of Emerald.

### 2. Check its SHA1

Your ROM's SHA1 has to be exactly:

```
f3ae088181bf583e55daf962a92bb46f4f1d07b7
```

Rom Patcher JS shows the SHA1 when you load your ROM, so you can check it there. Or from a terminal:

- **Mac / Linux:** `shasum -a 1 "Pokemon Emerald.gba"`
- **Windows (PowerShell):** `Get-FileHash "Pokemon Emerald.gba" -Algorithm SHA1`

If it doesn't match, it's usually because the ROM is still inside a `.zip`, it's a different language, it's already been patched with something else, or it's a bad dump. The patch checks this itself too, so it'll refuse to apply to the wrong file instead of giving you a broken game.

### 3. Patch it

The easiest way is **[Rom Patcher JS](https://www.marcrobledo.com/RomPatcher.js/)**. It runs in your browser, so it works on phones too.

1. Open the site.
2. Pick your Emerald ROM under "ROM file".
3. Pick `pokeemerald-real-genetics-<version>.bps` under "Patch file".
4. Click **Apply patch**. It downloads the patched game.

If you'd rather use a desktop app, **[Floating IPS (Flips)](https://github.com/Alcaro/Flips)** works too: click "Apply Patch", pick the `.bps`, then pick your ROM.

### 4. Play

Open the patched `.gba` in any GBA emulator. [mGBA](https://mgba.io/) is a good one, but pretty much anything that runs Emerald runs this.

**Using your old save:** the save format is the same as normal Emerald, so you can keep playing an existing save. Just name the `.sav` the same as the patched ROM (or import it in your emulator). Pokémon you already have get genes made up from their current IVs. Back up your save first, just in case.

## The rules

These are the same rules as in the video. Every Pokémon has two copies of every gene, one from each parent, and each copy is picked 50/50, exactly like in real animals.

### Colours

There are three hidden colour genes, all recessive, so a Pokémon only shows the colour when it has two copies:

- **Albino** (based on the real TYR gene). White, and it wins over everything else.
- **Red** (based on MC1R, the gene behind red hair in people). Wins over black.
- **Black** (based on ASIP, the gene that makes black cats black).

Each colour shows up in about 1 in 20,000 wild Pokémon, which is about how rare albinism is in people. But the gene itself is way more common than that: about 1 in 71 wild Pokémon is secretly carrying it. If you breed two carriers together, about 1 in 4 of the babies gets two copies and comes out that colour, even though both parents look completely normal.

### Shiny

Shiny is a recessive gene now too. Wild shinies are still exactly 1 in 8,192, so nothing changes when you're out catching. The difference is that about 1 in 46 wild Pokémon is secretly carrying the shiny gene, and it gets passed down. So two shiny parents always have shiny babies, and breeding the right carriers is actually a way to hunt shinies now.

### IVs

In the normal game every IV from 0 to 31 is equally likely. Here each stat is controlled by 5 genes (so 10 copies), and every copy is either a 0 or a 1. The stat's IV is the number of 1s scaled up to 31:

```
IV = round(31 × (number of 1 copies) / 10)
```

So IVs only come in 11 values: 0, 3, 6, 9, 12, 16, 19, 22, 25, 28 and 31. Most wild Pokémon land somewhere in the middle, like height in people, and a 31 in any one stat is about 1 in 1,000. The upside is that two parents with perfect stats always have a perfect baby, because they have nothing else to pass on.

### Lethal genes

Every real animal, including you, carries a few hidden broken genes that would be deadly with two copies. In people it's about 0.58 on average, so wild Pokémon carry the same amount. If an egg ends up with two copies of the same broken gene, it never hatches. When it's time to hatch you'll get "The EGG didn't hatch…" and the egg is gone.

Breeding two random wild Pokémon pretty much never runs into this (about 1 egg in 2,700). Breeding Pokémon that are related to each other (siblings, or a parent and its baby) runs into it a lot more, about 1 egg in 23 for siblings, because they're likely to carry the same broken genes. That's a bit lower than the 1 in 14 from the video, because the save data only has room to track one broken gene per Pokémon.

### Same species only

Only Pokémon from the same evolution family can breed now. So Pikachu can breed with Raichu, but Pikachu and Eevee can't, even though they're in the same egg group in the normal game. In real life, different species almost never have fertile babies (that's why mules can't have babies).

Two small exceptions: Nidoran♀ and Nidoran♂ count as one family, and so do Volbeat and Illumise, because they're basically the female and male of the same Pokémon. And a heads up, species that only come in one gender and don't have a partner like that (Tauros, Miltank, the Hitmon line) can only breed with Ditto now, which means clones.

### Ditto makes clones

Ditto still works, but it works like the Amazon molly, a real fish where the egg is a full genetic copy of the mom. Any egg you get from Ditto is a genetic copy of the other parent: same colour genes, same shiny gene, same IV genes, same broken genes. Two Dittos together don't make anything.

### What stays the same

Egg timing, the Day Care itself, natures (including the Everstone), gender, abilities and egg moves all work like normal Emerald.

## What's different and known limitations

- **It doesn't use any new save space.** The genes are packed into bits of the save data the game never uses. That includes 11 ribbon bits for ribbons that you can never earn in Emerald (Marine, Land, Sky, Country, National, Earth, World, plus four unused ones). If you bring in a Pokémon from another game (like Colosseum or XD) that actually has one of those ribbons, the ribbon gets wiped the first time the game writes its genes.
- **Trading with an unmodded game sends weird IVs.** The bits where normal Emerald keeps IVs are used for genes here, so a Pokémon you trade to a normal game (or to Ruby, Sapphire, FireRed, LeafGreen, Colosseum or XD) shows up over there with IVs that look random. Going the other way is fine: a Pokémon from a normal game gets genes made up from its IVs, with normal colouring and no broken genes.
- **Party and PC icons stay the normal colour.** The coloured version shows in battle, on the summary screen, when an egg hatches, when evolving or trading, in contests and in the PC's big preview, but the little menu icons and the Pokédex entries use the normal palette. Also, albino eyes go pale instead of red, because the game doesn't know which pixels are eyes.
- **Fixed IVs get rounded.** Anything in the game that sets IVs directly (like trainer Pokémon) gets rounded to the nearest of the 11 possible values.
- **I haven't tested it on a real GBA or a flash cart.** It should work like any other patch, but I've only played it in emulators.

## FAQ

**Can you just send me the patched ROM?**
No, sorry. I can't share Nintendo's game, so the patch is the only thing I can give out. You have to patch your own copy.

**Rom Patcher JS says the checksum doesn't match. What do I do?**
Your ROM isn't the exact version the patch is made for. Check the SHA1 again (see [step 2](#2-check-its-sha1)). It has to be the English USA/Europe Emerald, unzipped and not already patched.

**Does it work with my existing save?**
Yes. Your Pokémon get genes based on their current IVs, keep their normal colours, and don't carry any broken genes. Back up your save before you try it though.

**Can I play it on my phone?**
Yes. Patch it with Rom Patcher JS in your phone's browser, then open it in any GBA emulator on your phone.

**Can I trade or battle with my friends?**
If you're both playing this mod, it should be fine, since both games read the genes the same way. With a normal copy of a Gen 3 game, see the trading note [above](#whats-different-and-known-limitations).

**Why are my Pokémon's IVs weird numbers like 16 or 19?**
That's on purpose. IVs come from 10 gene copies now, so there are only 11 possible values (see [IVs](#ivs)).

**My egg disappeared. Is that a bug?**
If you saw "The EGG didn't hatch…", that's the lethal genes doing their thing (see [Lethal genes](#lethal-genes)). If it vanished without that message, please open an issue.

**Will you make this for other games?**
Honestly I don't know yet. Emerald was the easiest one because the decompilation is so complete. If you want it for another game, open an idea issue and let me know.

**I found a bug.**
Please [open a bug report](../../issues/new/choose). Tell me what happened, what you expected, which emulator you used and which patch version. A save file helps a lot.

## Building from source

You only need this if you want to change the code. To just play, use the patch from the releases page.

1. Set up the pret toolchain and agbcc by following [INSTALL.md](INSTALL.md). It's the same setup as the normal decompilation.
2. Build the game:

   ```sh
   make
   ```

   That gives you `pokeemerald.gba` with Real Genetics in it. It won't match the retail ROM anymore (that's the whole point), so `make compare` will fail, and that's expected.

3. Build the patch:

   ```sh
   tools/realgen/make_patch.sh            # version defaults to the git tag, or dev-<commit>
   tools/realgen/make_patch.sh v1.0.0     # or pick a version yourself
   ```

   The script builds the unmodified upstream pokeemerald in a temporary worktree, checks that it matches the retail ROM's SHA1, builds the modded game, writes `dist/pokeemerald-real-genetics-<version>.bps`, and then applies the patch to the vanilla build to make sure it gives back the modded ROM byte for byte. Set `REALGEN_MAKE` if you want a different build command (for example `REALGEN_MAKE="make -j8"`).

4. The BPS encoder and decoder are in `tools/realgen/bps.py`, and you can run their tests with:

   ```sh
   python3 tools/realgen/test_bps.py
   ```

Where things are:

| What | Where |
|---|---|
| How the genome is stored and every rule, in detail | [`REALGEN_SPEC.md`](REALGEN_SPEC.md) |
| The genome API | `include/genetics.h` |
| Gene logic (inheritance, wild genes, shiny, IVs) | `src/genetics.c`, `src/pokemon.c` |
| Day Care and hatching | `src/daycare.c`, `src/egg_hatch.c` |
| Albino, red and black colours | `src/genetics_palette.c` |
| Patch tools | `tools/realgen/` |

Every GitHub release is built by [`.github/workflows/release.yml`](.github/workflows/release.yml), and the only file it attaches is the `.bps` patch. If you want to help out, read [CONTRIBUTING.md](CONTRIBUTING.md).

## Credits

- **[pret](https://pret.github.io/)** and everyone who has contributed to **[pokeemerald](https://github.com/pret/pokeemerald)** ([contributors](https://github.com/pret/pokeemerald/graphs/contributors)). None of this would exist without their decompilation, which took years of work. Their original README is in [docs/PRET_README.md](docs/PRET_README.md).
- **[Floating IPS](https://github.com/Alcaro/Flips)** by Alcaro and **[Rom Patcher JS](https://github.com/marcrobledo/RomPatcher.js)** by Marc Robledo, which is how you guys actually put the patch on your game.

The science behind the rules, same sources as in the video:

- Albinism, about 1 in 20,000 people: [MedlinePlus Genetics, oculocutaneous albinism](https://medlineplus.gov/genetics/condition/oculocutaneous-albinism/).
- Hidden lethal genes, about 0.58 per person: Gao, Waggoner, Stephens, Ober and Przeworski (2015), "An Estimate of the Average Number of Recessive Lethal Mutations Carried by Humans", *Genetics* 199(4).
- The Amazon molly, the all-female fish whose eggs don't take the male's DNA: the *Poecilia formosa* genome paper, *BMC Genomics* 2017 ([PMC5753479](https://pmc.ncbi.nlm.nih.gov/articles/PMC5753479/)).
- Why mules can't have babies (horse and donkey chromosome counts don't match, 64 vs 62): Benirschke et al. (1964), *Journal of Heredity*.

## License

So, I can't put a license on the whole repo, because most of it isn't mine. The decompilation belongs to pret and its contributors, and Pokémon Emerald itself belongs to Nintendo, Creatures and GAME FREAK.

The new files I wrote for this mod (the genetics code, the patch tools and the docs) are under the MIT license, see [LICENSE-REALGEN](LICENSE-REALGEN). That doesn't cover pret's code or my changes to their files.

## Disclaimer

This is a fan project. It isn't affiliated with or endorsed by Nintendo, Creatures, GAME FREAK or The Pokémon Company. Pokémon and Pokémon Emerald are trademarks of their respective owners. This repo doesn't include any ROMs or copyrighted game files, and the patch only works on a copy of the game you already own. Please don't ask for ROMs in issues, those get closed.
