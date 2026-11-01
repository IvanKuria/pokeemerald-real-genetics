// Real genetics in-ROM test driver. TEST-ONLY: the whole file is empty unless the ROM is built with
// `make REALGEN_TEST=1` (which also writes a separate ROM, pokeemerald_realgen_test.gba). The shipped build never
// contains any of this.
//
// RealgenTest_Frame() runs once per frame from the main loop, right after the keys are read. It drives the game
// through scripted scenarios by injecting key presses and calling the game's own breeding/battle/summary code, and
// talks to the headless mGBA runner (test/realgen/rom_test.lua) through gRealgenTest: a text log and screenshot
// requests. See test/realgen/rom_test.sh.
#include "global.h"

#if REALGEN_TEST

#include "main.h"
#include "pokemon.h"
#include "genetics.h"
#include "genetics_palette.h"
#include "daycare.h"
#include "egg_hatch.h"
#include "overworld.h"
#include "battle_main.h"
#include "battle_setup.h"
#include "pokemon_summary_screen.h"
#include "palette.h"
#include "script.h"
#include "event_data.h"
#include "string_util.h"
#include "random.h"
#include "map_name_popup.h"
#include "constants/characters.h"
#include "realgen_test.h"
#include "constants/species.h"
#include "constants/maps.h"
#include "constants/flags.h"
#include "constants/vars.h"
#include "constants/opponents.h"

#define LOG_SIZE 2048   // ring buffer; the runner drains it every frame

struct RealgenTestMailbox
{
    u32 magic;          // 'RGT1' once initialised
    u16 shotSeq;        // incremented for every screenshot request; the runner saves shotName
    u8 finished;        // 0 running, 1 all checks passed, 2 some check failed
    u8 pad;
    u32 logLen;         // total bytes ever written; byte i is at log[i % LOG_SIZE]
    u32 fails;
    u32 checks;
    u8 shotName[32];
    u8 log[LOG_SIZE];
};

EWRAM_DATA struct RealgenTestMailbox gRealgenTest = {0};

static EWRAM_DATA u16 sStage = 0;
static EWRAM_DATA u16 sSub = 0;
static EWRAM_DATA u32 sTimer = 0;
static EWRAM_DATA u32 sCount = 0;
static EWRAM_DATA u16 sHoldKeys = 0;
static EWRAM_DATA u16 sHoldFrames = 0;
static EWRAM_DATA bool8 sHoldFirst = FALSE;
static EWRAM_DATA u16 sShotIndex = 0;
static EWRAM_DATA u8 sLastHatchState = 0;
static EWRAM_DATA u16 sStateTicks = 0;
static EWRAM_DATA u32 sTally[8] = {0};
static EWRAM_DATA struct Genome sEggGenome[PARTY_SIZE] = {0};
static EWRAM_DATA u8 sWalkDir = 0;

// ------------------------------------------------------------------------------------------------- mailbox

static void LogStr(const char *s)
{
    while (*s)
        gRealgenTest.log[gRealgenTest.logLen++ % LOG_SIZE] = *s++;
}

static void LogNum(s32 n)
{
    char buf[12];
    int i = 0;
    u32 u;

    if (n < 0)
    {
        LogStr("-");
        u = -n;
    }
    else
    {
        u = n;
    }
    do
    {
        buf[i++] = '0' + (u % 10);
        u /= 10;
    } while (u != 0);
    while (i > 0)
    {
        char c[2];
        c[0] = buf[--i];
        c[1] = 0;
        LogStr(c);
    }
}

static void Check(bool32 ok, const char *what)
{
    gRealgenTest.checks++;
    if (!ok)
        gRealgenTest.fails++;
    LogStr(ok ? "  ok    " : "  FAIL  ");
    LogStr(what);
    LogStr("\n");
}

// screenshot named "<NN>_<name>"
static void Shot(const char *name)
{
    int i = 0;
    u16 n = ++sShotIndex;

    gRealgenTest.shotName[i++] = '0' + (n / 10) % 10;
    gRealgenTest.shotName[i++] = '0' + n % 10;
    gRealgenTest.shotName[i++] = '_';
    while (*name && i < 31)
        gRealgenTest.shotName[i++] = *name++;
    gRealgenTest.shotName[i] = 0;
    gRealgenTest.shotSeq++;
    LogStr("  shot  ");
    LogStr((const char *)gRealgenTest.shotName);
    LogStr(".png\n");
}

// ------------------------------------------------------------------------------------------------- input

static void Press(u16 keys)
{
    gMain.heldKeysRaw |= keys;
    gMain.newKeysRaw |= keys;
    gMain.heldKeys |= keys;
    gMain.newKeys |= keys;
    gMain.newAndRepeatedKeys |= keys;
}

static void Hold(u16 keys, u16 frames)
{
    sHoldKeys = keys;
    sHoldFrames = frames;
    sHoldFirst = TRUE;
}

static void ApplyHold(void)
{
    if (sHoldFrames == 0)
        return;
    if (sHoldFirst)
        Press(sHoldKeys);
    sHoldFirst = FALSE;
    gMain.heldKeysRaw |= sHoldKeys;
    gMain.heldKeys |= sHoldKeys;
    sHoldFrames--;
}

static bool8 FieldIdle(void)
{
    return gMain.callback2 == CB2_Overworld
        && gMain.callback1 == CB1_Overworld
        && !ArePlayerFieldControlsLocked()
        && !gPaletteFade.active
        && sHoldFrames == 0;
}

// press `button` every 60 frames until the field is idle again (B closes messages and answers NO, A answers YES);
// with `shots`, takes a screenshot named `name` 55 frames after every press while on the field
static bool8 MashUntilIdle(u16 button, const char *name, bool8 shots)
{
    sTimer++;
    if (sTimer > 30 && FieldIdle())
        return TRUE;
    if (shots && (sTimer % 60) == 55 && gMain.callback2 == CB2_Overworld)
        Shot(name);
    if ((sTimer % 60) == 0)
        Press(button);
    if (sTimer > 60 * 120)
    {
        Check(FALSE, "timed out waiting for the field");
        return TRUE;
    }
    return FALSE;
}

static bool8 MashBUntilIdle(const char *name, bool8 shots)
{
    return MashUntilIdle(B_BUTTON, name, shots);
}

// one step down or up, alternating, so the player stays in the fence gap below the Day Care man
static void TakeStep(void)
{
    sWalkDir ^= 1;
    Hold(sWalkDir ? DPAD_DOWN : DPAD_UP, 24);
}

// ------------------------------------------------------------------------------------------------- mons

// personality low byte 0 = female, 0xFF = male for every species with both genders
static void MakeMon(struct Pokemon *mon, u16 species, u8 level, bool8 female, u8 coat, u8 shiny, u8 lethal)
{
    struct Genome g;
    u32 personality = (Random() << 16) | (Random() & 0xFF00) | (female ? 0x00 : 0xFF);
    int i;

    CreateMon(mon, species, level, USE_RANDOM_IVS, TRUE, personality, OT_ID_PLAYER_ID, 0);
    GetBoxMonGenome(&mon->box, &g);
    for (i = 0; i < NUM_COAT_LOCI; i++)
        g.coat[i] = 0;
    if (coat == COAT_ALBINO) g.coat[COAT_LOCUS_C] = 3;
    if (coat == COAT_RED)    g.coat[COAT_LOCUS_E] = 3;
    if (coat == COAT_BLACK)  g.coat[COAT_LOCUS_A] = 3;
    if (coat == 0xFF)        // triple carrier
        for (i = 0; i < NUM_COAT_LOCI; i++)
            g.coat[i] = female ? 1 : 2;
    g.shiny = shiny;
    g.lethal[0] = lethal;
    g.lethal[1] = 0;
    SetBoxMonGenome(&mon->box, &g);
    CalculateMonStats(mon);
}

static void SetParty(u8 count)
{
    int i;
    for (i = count; i < PARTY_SIZE; i++)
        ZeroMonData(&gPlayerParty[i]);
    CalculatePlayerPartyCount();
}

static void ClearDaycare(void)
{
    struct DayCare *dc = &gSaveBlock1Ptr->daycare;
    u8 *p = (u8 *)dc;
    u32 i;
    for (i = 0; i < sizeof(*dc); i++)
        p[i] = 0;
    FlagClear(FLAG_PENDING_DAYCARE_EGG);
}

static void SetDaycare(struct Pokemon *a, struct Pokemon *b)
{
    ClearDaycare();
    gSaveBlock1Ptr->daycare.mons[0].mon = a->box;
    gSaveBlock1Ptr->daycare.mons[1].mon = b->box;
}

// the game's own path: the Day Care man finds an egg (pending) and the player takes it
static struct Pokemon *MakeDaycareEgg(void)
{
    TriggerPendingDaycareEgg();
    GiveEggFromDaycare();
    return &gPlayerParty[gPlayerPartyCount - 1];
}

static void DropLastPartyMon(void)
{
    ZeroMonData(&gPlayerParty[gPlayerPartyCount - 1]);
    CalculatePlayerPartyCount();
}

static u8 Coat(struct Pokemon *mon)
{
    struct Genome g;
    GetBoxMonGenome(&mon->box, &g);
    return GenomeCoat(&g);
}

static bool8 SameGenome(const struct Genome *a, const struct Genome *b)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    u32 i;
    for (i = 0; i < sizeof(*a); i++)
        if (x[i] != y[i])
            return FALSE;
    return TRUE;
}

static void StartWildBattle(u16 species, u8 coat)
{
    ZeroEnemyPartyMons();
    MakeMon(&gEnemyParty[0], species, 5, TRUE, coat, 0, 0);
    BattleSetup_StartWildBattle();
}

// prepare the lead eggs to hatch on the next step through the real step counter path
static void ArmHatch(void)
{
    u32 zero = 0;
    int i;
    for (i = 0; i < gPlayerPartyCount; i++)
        if (GetMonData(&gPlayerParty[i], MON_DATA_IS_EGG))
            SetMonData(&gPlayerParty[i], MON_DATA_FRIENDSHIP, &zero);
    gSaveBlock1Ptr->daycare.stepCounter = 254;
}

// ------------------------------------------------------------------------------------------------- scenarios

enum {
    ST_BOOT,
    ST_SETUP,
    ST_A_SUMMARY,
    ST_A_BATTLE1,
    ST_A_BATTLE2,
    ST_B_STATS,
    ST_B_HATCH,
    ST_B_SUMMARY,
    ST_C_LETHAL,
    ST_D_FAMILY,
    ST_D_DITTO,
    ST_E_SHINY,
    ST_DONE,
};

static void NextStage(void)
{
    sStage++;
    sSub = 0;
    sTimer = 0;
    sCount = 0;
}

static void Boot(void)
{
    // title screen and main menu: A/Start until the overworld runs (NEW GAME goes straight to CB2_NewGame here)
    sTimer++;
    if (gMain.callback2 == CB2_Overworld && FieldIdle() && sTimer > 60)
    {
        LogStr("[boot] new game on Route 117 by the Day Care\n");
        NextStage();
        return;
    }
    if ((sTimer % 60) == 0)
        Press((sTimer / 60) & 1 ? START_BUTTON : A_BUTTON);
}

static void Setup(void)
{
    int i;
    for (i = 0; i < TRAINERS_COUNT; i++)
        FlagSet(TRAINER_FLAGS_START + i);
    gSaveBlock2Ptr->optionsTextSpeed = OPTIONS_TEXT_SPEED_FAST;
    VarSet(VAR_REPEL_STEP_COUNT, 250);
    Shot("boot_field");
    NextStage();
}

// (a) albino, red, black and normal Pikachu on the summary screen
static void A_Summary(void)
{
    switch (sSub)
    {
    case 0:
        LogStr("[a] coat colours on the summary screen and in battle\n");
        MakeMon(&gPlayerParty[0], SPECIES_PIKACHU, 100, TRUE, COAT_ALBINO, 0, 0);
        MakeMon(&gPlayerParty[1], SPECIES_PIKACHU, 100, FALSE, COAT_RED, 0, 0);
        MakeMon(&gPlayerParty[2], SPECIES_PIKACHU, 100, TRUE, COAT_BLACK, 0, 0);
        MakeMon(&gPlayerParty[3], SPECIES_PIKACHU, 100, FALSE, COAT_NORMAL, 0, 0);
        SetParty(4);
        Check(Coat(&gPlayerParty[0]) == COAT_ALBINO && GetMonCoat(&gPlayerParty[0]) == COAT_ALBINO, "party 1 is albino");
        Check(Coat(&gPlayerParty[1]) == COAT_RED && GetMonCoat(&gPlayerParty[1]) == COAT_RED, "party 2 is red");
        Check(Coat(&gPlayerParty[2]) == COAT_BLACK && GetMonCoat(&gPlayerParty[2]) == COAT_BLACK, "party 3 is black");
        HideMapNamePopUpWindow();   // like the start menu does before opening a menu
        ShowPokemonSummaryScreen(SUMMARY_MODE_NORMAL, gPlayerParty, 0, gPlayerPartyCount - 1, CB2_ReturnToField);
        sSub++;
        break;
    case 1:
    case 3:
    case 5:
    case 7:
        if (++sTimer == 150)
        {
            static const char *const names[] = {"a_summary_albino", "a_summary_red", "a_summary_black", "a_summary_normal"};
            Shot(names[(sSub - 1) / 2]);
            sTimer = 0;
            sSub++;
        }
        break;
    case 2:
    case 4:
    case 6:
        Press(DPAD_DOWN);
        sSub++;
        break;
    case 8:
        Press(B_BUTTON);
        sSub++;
        break;
    case 9:
        if (++sTimer > 30 && FieldIdle())
            NextStage();
        break;
    }
}

// (a) battle: wild Pikachu of one coat against the player's lead of another. Waits for the action menu, then runs.
static void A_Battle(u8 lead, u8 enemyCoat, const char *name)
{
    struct Pokemon tmp;

    switch (sSub)
    {
    case 0:
        if (lead != 0)
        {
            tmp = gPlayerParty[0];
            gPlayerParty[0] = gPlayerParty[lead];
            gPlayerParty[lead] = tmp;
        }
        StartWildBattle(SPECIES_PIKACHU, enemyCoat);
        sSub++;
        break;
    case 1:
        // "Wild PIKACHU appeared!" waits for a button, then the lead is sent out and the action menu opens
        if (gMain.callback2 == BattleMainCB2 && ++sTimer == 60 * 6)
            Press(B_BUTTON);
        if (sTimer == 60 * 10)
        {
            Shot(name);
            sTimer = 0;
            sSub++;
        }
        break;
    case 2:
        // RUN is bottom right of the action menu
        sTimer++;
        if (sTimer == 1) Press(DPAD_RIGHT);
        if (sTimer == 10) Press(DPAD_DOWN);
        if (sTimer == 20) Press(A_BUTTON);
        if (sTimer > 30)
        {
            sTimer = 0;
            sSub++;
        }
        break;
    case 3:
        if (MashBUntilIdle(NULL, FALSE))
        {
            Check(FieldIdle(), "battle ended by running, back on the field");
            NextStage();
        }
        break;
    }
}

// (b) triple carrier Pikachu in the Day Care: 1,000 eggs through the game's egg code
static void B_Stats(void)
{
    struct Pokemon m, f;
    int i;

    switch (sSub)
    {
    case 0:
        LogStr("[b] triple carrier parents in the Day Care\n");
        MakeMon(&gPlayerParty[0], SPECIES_PIKACHU, 100, TRUE, COAT_NORMAL, 0, 0);
        SetParty(1);
        MakeMon(&f, SPECIES_PIKACHU, 30, TRUE, 0xFF, 0, 0);
        MakeMon(&m, SPECIES_PIKACHU, 30, FALSE, 0xFF, 0, 0);
        SetDaycare(&f, &m);
        for (i = 0; i < 8; i++)
            sTally[i] = 0;
        sSub++;
        break;
    case 1:
        for (i = 0; i < 20; i++)
        {
            struct Pokemon *egg = MakeDaycareEgg();
            if (GetMonData(egg, MON_DATA_IS_EGG) && GetMonData(egg, MON_DATA_SPECIES) == SPECIES_PICHU)
                sTally[Coat(egg) & 3]++;
            else
                sTally[4]++;
            DropLastPartyMon();
            sCount++;
        }
        if (sCount >= 2000)
        {
            LogStr("  info  2000 eggs: albino ");
            LogNum(sTally[COAT_ALBINO]);
            LogStr(", red ");
            LogNum(sTally[COAT_RED]);
            LogStr(", black ");
            LogNum(sTally[COAT_BLACK]);
            LogStr(", normal ");
            LogNum(sTally[COAT_NORMAL]);
            LogStr(" (expected 500 / 375 / 281 / 844)\n");
            Check(sTally[4] == 0, "every egg is a Pichu egg");
            // 4 sd bands around 1/4, 3/16, 9/64, 27/64 of 2000
            Check(sTally[COAT_ALBINO] >= 422 && sTally[COAT_ALBINO] <= 578, "albino about 25%");
            Check(sTally[COAT_RED] >= 305 && sTally[COAT_RED] <= 445, "red about 18.8%");
            Check(sTally[COAT_BLACK] >= 219 && sTally[COAT_BLACK] <= 343, "black about 14.1%");
            Check(sTally[COAT_NORMAL] >= 756 && sTally[COAT_NORMAL] <= 932, "normal about 42.2%");
            NextStage();
        }
        break;
    }
}

// (b) keep one egg of each coat, hatch them through the real step counter, check the hatchling keeps its genes
static void B_Hatch(void)
{
    u8 want, state;
    struct Genome g;
    int i;

    switch (sSub)
    {
    case 0:
        // party: lead + 4 eggs in the order albino, red, black, normal
        for (want = COAT_ALBINO; ; want = (want == COAT_BLACK) ? COAT_NORMAL : want + 1)
        {
            for (i = 0; i < 500; i++)
            {
                struct Pokemon *egg = MakeDaycareEgg();
                if (Coat(egg) == want)
                    break;
                DropLastPartyMon();
            }
            if (want == COAT_NORMAL)
                break;
        }
        for (i = 1; i < gPlayerPartyCount; i++)
            GetBoxMonGenome(&gPlayerParty[i].box, &sEggGenome[i]);
        Check(gPlayerPartyCount == 5, "kept 4 eggs: albino, red, black, normal");
        ClearDaycare();
        sCount = 1;
        sSub++;
        break;
    case 1:
        // hatch party slot sCount
        ArmHatch();
        TakeStep();
        sTimer = 0;
        sLastHatchState = 0xFF;
        sSub++;
        break;
    case 2:
        state = RealgenTest_EggHatchState();
        if (state == 6 && sLastHatchState != 6)
            sStateTicks = 0;
        sLastHatchState = state;
        if (state == 6 && ++sStateTicks == 50)
        {
            static const char *const names[] = {"", "b_hatch_albino", "b_hatch_red", "b_hatch_black", "b_hatch_normal"};
            Shot(names[sCount]);
        }
        if (state == 0xFF || state >= 9)
        {
            if (MashBUntilIdle(NULL, FALSE))
            {
                GetBoxMonGenome(&gPlayerParty[sCount].box, &g);
                Check(!GetMonData(&gPlayerParty[sCount], MON_DATA_IS_EGG) && GetMonData(&gPlayerParty[sCount], MON_DATA_SPECIES) == SPECIES_PICHU,
                      "egg hatched into a Pichu");
                Check(SameGenome(&g, &sEggGenome[sCount]), "hatchling has exactly the egg's genome");
                Check(GetMonCoat(&gPlayerParty[sCount]) == GenomeCoat(&sEggGenome[sCount]), "hatchling coat follows the egg's genes");
                sTimer = 0;
                if (++sCount >= gPlayerPartyCount)
                    NextStage();
                else
                    sSub = 1;
            }
        }
        else if (++sTimer > 60 * 60)
        {
            Check(FALSE, "hatch did not finish");
            NextStage();
        }
        break;
    }
}

static void B_Summary(void)
{
    switch (sSub)
    {
    case 0:
        ShowPokemonSummaryScreen(SUMMARY_MODE_NORMAL, gPlayerParty, 1, gPlayerPartyCount - 1, CB2_ReturnToField);
        sSub++;
        break;
    case 1:
    case 3:
    case 5:
    case 7:
        if (++sTimer == 150)
        {
            static const char *const names[] = {"b_summary_albino", "b_summary_red", "b_summary_black", "b_summary_normal"};
            Shot(names[(sSub - 1) / 2]);
            sTimer = 0;
            sSub++;
        }
        break;
    case 2:
    case 4:
    case 6:
        Press(DPAD_DOWN);
        sSub++;
        break;
    case 8:
        Press(B_BUTTON);
        sSub++;
        break;
    case 9:
        if (++sTimer > 30 && FieldIdle())
            NextStage();
        break;
    }
}

// (c) two carriers of the same lethal allele: a quarter of the eggs never hatch
static void C_Lethal(void)
{
    struct Pokemon m, f;
    struct Genome g;
    int i;
    u32 dead = 0;

    switch (sSub)
    {
    case 0:
        LogStr("[c] lethal egg\n");
        SetParty(1);
        MakeMon(&f, SPECIES_ZIGZAGOON, 30, TRUE, COAT_NORMAL, 0, 77);
        MakeMon(&m, SPECIES_ZIGZAGOON, 30, FALSE, COAT_NORMAL, 0, 77);
        SetDaycare(&f, &m);
        for (i = 0; i < 400; i++)
        {
            GetBoxMonGenome(&MakeDaycareEgg()->box, &g);
            dead += GenomeIsLethal(&g) ? 1 : 0;
            DropLastPartyMon();
        }
        LogStr("  info  carrier x carrier, same locus: ");
        LogNum(dead);
        LogStr(" of 400 eggs lethal (expected 100)\n");
        Check(dead >= 70 && dead <= 130, "about a quarter of the eggs are lethal");
        for (i = 0; i < 200; i++)
        {
            GetBoxMonGenome(&MakeDaycareEgg()->box, &g);
            if (GenomeIsLethal(&g))
                break;
            DropLastPartyMon();
        }
        Check(gPlayerPartyCount == 2 && GenomeIsLethal(&g), "kept a lethal egg in party slot 2");
        ClearDaycare();
        ArmHatch();
        TakeStep();
        sSub++;
        break;
    case 1:
        if (MashBUntilIdle("c_lethal_msg", TRUE))
        {
            Check(gPlayerPartyCount == 1, "the lethal egg was removed from the party");
            Check(!GetMonData(&gPlayerParty[0], MON_DATA_IS_EGG), "only the lead is left");
            sTimer = 0;
            sSub++;
        }
        break;
    case 2:
        // play continues: walk a few steps
        sTimer++;
        if (sTimer == 1 || sTimer == 30 || sTimer == 60)
            TakeStep();
        if (sTimer == 100)
        {
            Check(FieldIdle(), "the field still runs after the lethal egg");
            Shot("c_after_walking");
            NextStage();
        }
        break;
    }
}

// simulate `steps` steps of Day Care time through the game's step handler; TRUE if an egg became pending
static bool8 WalkDaycareSteps(u32 steps)
{
    u32 i;
    for (i = 0; i < steps; i++)
    {
        ShouldEggHatch();
        if (FlagGet(FLAG_PENDING_DAYCARE_EGG))
            return TRUE;
    }
    return FALSE;
}

// (d) species barrier: Pikachu + Raichu breed, Pikachu + Clefairy do not; the Day Care man says so
static void D_Family(void)
{
    struct Pokemon a, b;

    switch (sSub)
    {
    case 0:
        LogStr("[d] species barrier and Ditto\n");
        SetParty(1);
        MakeMon(&a, SPECIES_PIKACHU, 30, TRUE, COAT_NORMAL, 0, 0);
        MakeMon(&b, SPECIES_CLEFAIRY, 30, FALSE, COAT_NORMAL, 0, 0);
        SetDaycare(&a, &b);
        Hold(DPAD_UP, 4);   // face the Day Care man
        sSub++;
        break;
    case 1:
    case 3:
    case 5:
        // talk to the Day Care man
        Press(A_BUTTON);
        sTimer = 0;
        sSub++;
        break;
    case 2:
        if (MashBUntilIdle("d_pikachu_clefairy", TRUE))
        {
            Check(!WalkDaycareSteps(256 * 100), "Pikachu + Clefairy: no egg in 25,600 steps");
            MakeMon(&a, SPECIES_PIKACHU, 30, TRUE, COAT_NORMAL, 0, 0);
            MakeMon(&b, SPECIES_RAICHU, 30, FALSE, COAT_NORMAL, 0, 0);
            SetDaycare(&a, &b);
            sSub++;
        }
        break;
    case 4:
        if (MashBUntilIdle("d_pikachu_raichu", TRUE))
        {
            Check(WalkDaycareSteps(256 * 200), "Pikachu + Raichu: an egg is waiting");
            sSub++;
        }
        break;
    case 6:
        // the man offers the egg; A through the talk answers YES
        if (MashUntilIdle(A_BUTTON, "d_egg_offer", TRUE))
        {
            Check(gPlayerPartyCount == 2 && GetMonData(&gPlayerParty[1], MON_DATA_IS_EGG)
               && GetMonData(&gPlayerParty[1], MON_DATA_SPECIES) == SPECIES_PICHU, "received a Pichu egg from Pikachu + Raichu");
            NextStage();
        }
        break;
    }
}

static void D_Ditto(void)
{
    struct Pokemon ditto, x, *egg;
    struct Genome gx, ge;
    int i, sex;
    u32 badGenome, badGender, badSpecies;

    SetParty(1);
    for (sex = 0; sex < 2; sex++)
    {
        badGenome = badGender = badSpecies = 0;
        MakeMon(&ditto, SPECIES_DITTO, 30, TRUE, COAT_NORMAL, 0, 0);
        MakeMon(&x, SPECIES_ZIGZAGOON, 30, sex == 0, COAT_RED, 1, 12);
        GetBoxMonGenome(&x.box, &gx);
        SetDaycare(&ditto, &x);
        for (i = 0; i < 100; i++)
        {
            egg = MakeDaycareEgg();
            GetBoxMonGenome(&egg->box, &ge);
            if (!SameGenome(&ge, &gx)) badGenome++;
            if (GetMonGender(egg) != GetMonGender(&x)) badGender++;
            if (GetMonData(egg, MON_DATA_SPECIES) != SPECIES_ZIGZAGOON) badSpecies++;
            DropLastPartyMon();
        }
        Check(badGenome == 0, sex == 0 ? "Ditto + female Zigzagoon: 100 eggs are genetic copies" : "Ditto + male Zigzagoon: 100 eggs are genetic copies");
        Check(badGender == 0, sex == 0 ? "Ditto + female Zigzagoon: every egg is female" : "Ditto + male Zigzagoon: every egg is male");
        Check(badSpecies == 0, "Ditto eggs are the other parent's species");
    }
    MakeMon(&ditto, SPECIES_DITTO, 30, TRUE, COAT_NORMAL, 0, 0);
    MakeMon(&x, SPECIES_DITTO, 30, FALSE, COAT_NORMAL, 0, 0);
    SetDaycare(&ditto, &x);
    Check(!WalkDaycareSteps(256 * 100), "Ditto + Ditto: no egg in 25,600 steps");
    ClearDaycare();
    NextStage();
}

// (e) s/s x s/s: every egg is shiny, and the hatchling looks shiny. Electrike, whose shiny form is yellow instead of
// green, so the difference is plain; a normal Electrike egg hatches next to it for comparison.
static void E_Shiny(void)
{
    struct Pokemon m, f, *egg;
    struct Genome g;
    int i;
    u32 bad = 0;
    u8 state;

    switch (sSub)
    {
    case 0:
        LogStr("[e] shiny parents\n");
        SetParty(1);
        MakeMon(&f, SPECIES_ELECTRIKE, 30, TRUE, COAT_NORMAL, 3, 0);
        MakeMon(&m, SPECIES_MANECTRIC, 30, FALSE, COAT_NORMAL, 3, 0);
        Check(IsMonShiny(&f) && IsMonShiny(&m), "both parents are s/s and vanilla-shiny");
        SetDaycare(&f, &m);
        for (i = 0; i < 100; i++)
        {
            egg = MakeDaycareEgg();
            GetBoxMonGenome(&egg->box, &g);
            if (!GenomeIsShiny(&g) || !IsMonShiny(egg))
                bad++;
            DropLastPartyMon();
        }
        Check(bad == 0, "100 eggs from s/s x s/s are all shiny (genome and vanilla check)");
        MakeDaycareEgg();
        // a normal S/S pair for the comparison egg
        MakeMon(&f, SPECIES_ELECTRIKE, 30, TRUE, COAT_NORMAL, 0, 0);
        MakeMon(&m, SPECIES_MANECTRIC, 30, FALSE, COAT_NORMAL, 0, 0);
        SetDaycare(&f, &m);
        egg = MakeDaycareEgg();
        Check(!IsMonShiny(egg), "S/S x S/S egg is not shiny");
        ClearDaycare();
        sCount = 1;
        sSub++;
        break;
    case 1:
        ArmHatch();
        TakeStep();
        sTimer = 0;
        sLastHatchState = 0xFF;
        sSub++;
        break;
    case 2:
        state = RealgenTest_EggHatchState();
        if (state == 6 && sLastHatchState != 6)
            sStateTicks = 0;
        sLastHatchState = state;
        if (state == 6 && ++sStateTicks == 50)
            Shot(sCount == 1 ? "e_hatch_shiny" : "e_hatch_normal");
        if ((state == 0xFF || state >= 9) && MashBUntilIdle(NULL, FALSE))
        {
            if (sCount == 1)
                Check(!GetMonData(&gPlayerParty[1], MON_DATA_IS_EGG) && IsMonShiny(&gPlayerParty[1]), "the shiny-gene hatchling is shiny");
            else
                Check(!GetMonData(&gPlayerParty[2], MON_DATA_IS_EGG) && !IsMonShiny(&gPlayerParty[2]), "the normal hatchling is not shiny");
            if (++sCount <= 2)
            {
                sSub = 1;
            }
            else
            {
                ShowPokemonSummaryScreen(SUMMARY_MODE_NORMAL, gPlayerParty, 1, gPlayerPartyCount - 1, CB2_ReturnToField);
                sTimer = 0;
                sSub++;
            }
        }
        break;
    case 3:
        if (++sTimer == 150)
        {
            Shot("e_summary_shiny");
            Press(DPAD_DOWN);
        }
        if (sTimer == 300)
        {
            Shot("e_summary_normal");
            Press(B_BUTTON);
            sTimer = 0;
            sSub++;
        }
        break;
    case 4:
        if (++sTimer > 30 && FieldIdle())
            NextStage();
        break;
    }
}

void RealgenTest_Frame(void)
{
    if (gRealgenTest.magic != 0x31544752)
    {
        gRealgenTest.magic = 0x31544752;
        LogStr("Real genetics ROM test\n");
    }
    if (gRealgenTest.finished)
        return;

    ApplyHold();
    if (sHoldFrames != 0)
        return;

    switch (sStage)
    {
    case ST_BOOT:      Boot(); break;
    case ST_SETUP:     if (FieldIdle()) Setup(); break;
    case ST_A_SUMMARY: if (sSub != 0 || FieldIdle()) A_Summary(); break;
    case ST_A_BATTLE1: if (sSub != 0 || FieldIdle()) A_Battle(0, COAT_RED, "a_battle_albino_vs_red"); break;
    case ST_A_BATTLE2: if (sSub != 0 || FieldIdle()) A_Battle(2, COAT_ALBINO, "a_battle_black_vs_albino"); break;
    case ST_B_STATS:   if (sSub != 0 || FieldIdle()) B_Stats(); break;
    case ST_B_HATCH:   if (sSub != 0 || FieldIdle()) B_Hatch(); break;
    case ST_B_SUMMARY: if (sSub != 0 || FieldIdle()) B_Summary(); break;
    case ST_C_LETHAL:  if (sSub != 0 || FieldIdle()) C_Lethal(); break;
    case ST_D_FAMILY:  if (sSub != 0 || FieldIdle()) D_Family(); break;
    case ST_D_DITTO:   if (FieldIdle()) D_Ditto(); break;
    case ST_E_SHINY:   if (sSub != 0 || FieldIdle()) E_Shiny(); break;
    case ST_DONE:
        LogStr("\n");
        LogNum(gRealgenTest.checks);
        LogStr(" checks, ");
        LogNum(gRealgenTest.fails);
        LogStr(" failed\n");
        gRealgenTest.finished = gRealgenTest.fails ? 2 : 1;
        break;
    }
}

// called from CB2_NewGame after NewGameInitData: skip the truck and start next to the Route 117 Day Care man
void RealgenTest_NewGame(void)
{
    static const u8 name[] = _("TEST");
    int i;

    for (i = 0; i < PLAYER_NAME_LENGTH + 1; i++)
        gSaveBlock2Ptr->playerName[i] = (i < 4) ? name[i] : EOS;
    gSaveBlock2Ptr->playerGender = MALE;
    SetWarpDestination(MAP_GROUP(MAP_ROUTE117), MAP_NUM(MAP_ROUTE117), WARP_ID_NONE, 47, 5);
    WarpIntoMap();
}

#endif // REALGEN_TEST
