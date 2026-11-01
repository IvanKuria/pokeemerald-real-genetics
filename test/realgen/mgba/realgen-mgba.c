// Headless mGBA runner for Real Genetics smoke tests. Links against a static libmgba (0.10.x) built with Lua.
// No window, no audio device, no GL: the core runs frames in this process and renders into a memory buffer.
//
//   realgen-mgba ROM [--frames N] [--screenshot out.png] [--script file.lua] [--press FRAME:KEYS:LEN]... [--save file.sav] [--min-colors K]
//
// --press   hold KEYS (any of A B L R U D Lt Rt S s = A, B, Left, Right, Up, Down, L, R, Start, select) from FRAME
//           for LEN frames, e.g. --press 300:S:5 --press 600:A:5
// --script  Lua with the mGBA scripting API (emu:read8, emu:addKey, emu:screenshot, callbacks:add("frame", ...)).
//           The runner fires the "start" callback once and "frame" after every frame. os.exit(code) ends early.
// --min-colors K  fail (exit 3) if the last frame has fewer than K distinct colours (blank/crashed screen check)
// --save    load this save file read-only (changes are not written back). Without it the game starts with no save.
// Exit code 0 when the run completes and the screenshot (if asked for) was written.
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/core/scripting.h>
#include <mgba/script/context.h>
#include <mgba/internal/script/lua.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/png-io.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PRESSES 256

struct Press { unsigned frame, len; uint32_t keys; };

static void QuietLog(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{
    (void)logger;
    if (level & (mLOG_FATAL | mLOG_ERROR))
    {
        fprintf(stderr, "[mgba %s] ", mLogCategoryName(category));
        vfprintf(stderr, format, args);
        fputc('\n', stderr);
    }
}

static void ScriptLog(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{
    (void)logger; (void)category; (void)level;
    vfprintf(stdout, format, args);
    fputc('\n', stdout);
    fflush(stdout);
}

static struct mLogger sCoreLogger = { .log = QuietLog };
static struct mLogger sScriptLogger = { .log = ScriptLog };

static uint32_t ParseKeys(const char *s)
{
    uint32_t keys = 0;
    for (; *s; s++)
    {
        switch (*s)
        {
        case 'A': keys |= 1 << GBA_KEY_A; break;
        case 'B': keys |= 1 << GBA_KEY_B; break;
        case 's': keys |= 1 << GBA_KEY_SELECT; break;
        case 'S': keys |= 1 << GBA_KEY_START; break;
        case 'R': keys |= 1 << GBA_KEY_RIGHT; break;
        case 'L': keys |= 1 << GBA_KEY_LEFT; break;
        case 'U': keys |= 1 << GBA_KEY_UP; break;
        case 'D': keys |= 1 << GBA_KEY_DOWN; break;
        case 'r': keys |= 1 << GBA_KEY_R; break;
        case 'l': keys |= 1 << GBA_KEY_L; break;
        default: fprintf(stderr, "unknown key '%c' (use A B s S R L U D r l)\n", *s); exit(2);
        }
    }
    return keys;
}

static unsigned CountColours(const color_t *pixels, size_t n)
{
    static uint32_t seen[4096];
    unsigned count = 0;
    size_t i, j;
    for (i = 0; i < n && count < 4096; i++)
    {
        for (j = 0; j < count && seen[j] != (uint32_t)pixels[i]; j++);
        if (j == count)
            seen[count++] = (uint32_t)pixels[i];
    }
    return count;
}

static int WritePng(const char *path, const color_t *pixels, unsigned w, unsigned h, size_t stride)
{
    struct VFile *vf = VFileOpen(path, O_WRONLY | O_CREAT | O_TRUNC);
    png_structp png;
    png_infop info;
    if (!vf)
        return 0;
    png = PNGWriteOpen(vf);
    info = PNGWriteHeader(png, w, h);
    if (!PNGWritePixels(png, w, h, stride, pixels))
    {
        PNGWriteClose(png, info);
        vf->close(vf);
        return 0;
    }
    PNGWriteClose(png, info);
    vf->close(vf);
    return 1;
}

int main(int argc, char **argv)
{
    const char *rom = NULL, *shot = NULL, *script = NULL, *save = NULL;
    unsigned frames = 600, nPress = 0, w, h, f, i, minColours = 0, colours;
    struct Press presses[MAX_PRESSES];
    struct mCore *core;
    color_t *buffer;
    struct mScriptContext scriptContext;
    int useScript;

    for (i = 1; i < (unsigned)argc; i++)
    {
        if (!strcmp(argv[i], "--frames") && i + 1 < (unsigned)argc) frames = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--screenshot") && i + 1 < (unsigned)argc) shot = argv[++i];
        else if (!strcmp(argv[i], "--script") && i + 1 < (unsigned)argc) script = argv[++i];
        else if (!strcmp(argv[i], "--min-colors") && i + 1 < (unsigned)argc) minColours = (unsigned)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "--save") && i + 1 < (unsigned)argc) save = argv[++i];
        else if (!strcmp(argv[i], "--press") && i + 1 < (unsigned)argc && nPress < MAX_PRESSES)
        {
            char keys[32] = {0};
            unsigned at, len;
            if (sscanf(argv[++i], "%u:%31[^:]:%u", &at, keys, &len) != 3) { fprintf(stderr, "bad --press %s\n", argv[i]); return 2; }
            presses[nPress].frame = at;
            presses[nPress].len = len;
            presses[nPress].keys = ParseKeys(keys);
            nPress++;
        }
        else if (argv[i][0] != '-' && !rom) rom = argv[i];
        else { fprintf(stderr, "usage: %s ROM [--frames N] [--screenshot out.png] [--script f.lua] [--press F:KEYS:LEN] [--save f.sav] [--min-colors K]\n", argv[0]); return 2; }
    }
    if (!rom) { fprintf(stderr, "no ROM given\n"); return 2; }

    mLogSetDefaultLogger(&sCoreLogger);
    core = mCoreFind(rom);
    if (!core || !core->init(core)) { fprintf(stderr, "could not create a core for %s\n", rom); return 1; }
    mCoreInitConfig(core, NULL);
    core->desiredVideoDimensions(core, &w, &h);
    buffer = calloc((size_t)w * h, sizeof(*buffer));
    core->setVideoBuffer(core, buffer, w);
    if (!mCoreLoadFile(core, rom)) { fprintf(stderr, "could not load %s\n", rom); return 1; }
    if (save && !core->loadTemporarySave(core, VFileOpen(save, O_RDONLY)))
        fprintf(stderr, "warning: could not load save %s\n", save);
    core->reset(core);

    useScript = script != NULL;
    if (useScript)
    {
        mScriptContextInit(&scriptContext);
        mScriptContextAttachStdlib(&scriptContext);
        mScriptContextAttachCore(&scriptContext, core);
        mScriptContextAttachLogger(&scriptContext, &sScriptLogger);
        {
            struct mScriptEngineContext *lua = mScriptContextRegisterEngine(&scriptContext, mSCRIPT_ENGINE_LUA);
            struct VFile *vf = VFileOpen(script, O_RDONLY);
            bool ok = vf && lua->load(lua, script, vf) && lua->run(lua);
            if (vf)
                vf->close(vf);
            if (!ok)
            {
                const char *err = lua->getError ? lua->getError(lua) : NULL;
                fprintf(stderr, "script %s failed: %s\n", script, err ? err : "could not open");
                return 1;
            }
        }
        mScriptContextTriggerCallback(&scriptContext, "start");
    }

    for (f = 0; f < frames; f++)
    {
        uint32_t keys = 0;
        for (i = 0; i < nPress; i++)
            if (f >= presses[i].frame && f < presses[i].frame + presses[i].len)
                keys |= presses[i].keys;
        if (nPress)
            core->setKeys(core, keys);
        core->runFrame(core);
        if (useScript)
            mScriptContextTriggerCallback(&scriptContext, "frame");
    }

    if (shot && !WritePng(shot, buffer, w, h, w)) { fprintf(stderr, "could not write %s\n", shot); return 1; }
    colours = CountColours(buffer, (size_t)w * h);
    printf("ran %u frames of %s (%ux%u, %u colours on the last frame)%s%s\n", frames, rom, w, h, colours,
           shot ? ", screenshot " : "", shot ? shot : "");
    if (colours < minColours)
    {
        fprintf(stderr, "last frame has %u colours, fewer than --min-colors %u\n", colours, minColours);
        return 3;
    }

    if (useScript)
    {
        mScriptContextDetachCore(&scriptContext);
        mScriptContextDeinit(&scriptContext);
    }
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    free(buffer);
    return 0;
}
