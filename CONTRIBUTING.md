# Contributing

So, thanks for wanting to help out. This is a pretty small project, so there isn't a big process, but here's what makes it easy for me to look at your stuff.

## Bugs

If something's broken, [open a bug report](../../issues/new/choose). The template asks for the useful stuff: which patch version you're on, which emulator, what happened and what you expected. A save file (zip it so GitHub accepts it) or a save state right before the bug helps a ton.

Before you report a bug, please check that your ROM's SHA1 is `f3ae088181bf583e55daf962a92bb46f4f1d07b7`. A lot of "the patch doesn't work" problems are actually just the wrong ROM.

Please don't post ROMs or links to ROMs anywhere in this repo. Issues like that get closed.

## Ideas

If you've got an idea for another real genetics rule (or you think one of mine is wrong), open an idea issue. If it's based on real science, link the paper or article, because that's how I check it and it's what I'd use if it ever ends up in a video.

## Pull requests

1. Set up the build by following [INSTALL.md](INSTALL.md), then check that `make` builds.
2. Keep the change focused on one thing. Smaller PRs are way easier for me to actually review.
3. Read [REALGEN_SPEC.md](REALGEN_SPEC.md) first if you're touching the genetics. The genome has to fit in the bits the save already has, so please don't add new save data or remove features from the normal game.
4. If you change how the rules work, update the spec and the [rules section of the README](README.md#the-rules) so they still match the game.
5. Before you open the PR, run:

   ```sh
   make
   tools/realgen/make_patch.sh
   python3 tools/realgen/test_bps.py
   ```

   `make_patch.sh` builds the vanilla game and your version and checks that the patch rebuilds your ROM exactly. CI runs the same thing on every PR.

Code style just follows the rest of pokeemerald: 4 spaces, braces on their own line, and the same naming as the code around it.

Never commit a `.gba`, `.sav` or any other game file. The `.gitignore` already covers them, but double check with `git status` before you commit.

## The code that isn't mine

Most of this repo is pret's decompilation. If you find a bug that's also in normal pokeemerald (so it has nothing to do with genetics), it's probably better to report it to [pret/pokeemerald](https://github.com/pret/pokeemerald) instead, so everyone gets the fix.

## License

By contributing new files for the mod, you're fine with them being under the MIT license in [LICENSE-REALGEN](LICENSE-REALGEN). Changes to pret's files stay under whatever terms pret's project has.
