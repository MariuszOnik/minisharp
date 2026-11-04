/* MiniSharp for Nintendo 3DS - file menu, compiler and game loop.
 * Programs live in sdmc:/minisharp/ (files ending in .cs) */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include "../src/ms.h"
#include "../src/platform.h"
#include "../src/media.h"
#include "platform_3ds.h"
#include "samples.h"

u32 __stacksize__ = 256 * 1024;

#define APP_DIR "sdmc:/minisharp"
#define LOG_PATH APP_DIR "/log.txt"
#define MAX_FILES 128

static char files[MAX_FILES][64];
static int nfiles, sel, scroll;
static char status[200];
static int showFps;                     /* FPS overlay in games, toggled with X in the menu */

/* ---------- files ---------- */
static int cmpname(const void* a, const void* b) { return strcmp((const char*)a, (const char*)b); }

static void scan_files(void) {
    nfiles = 0;
    DIR* d = opendir(APP_DIR);
    if (!d) return;
    struct dirent* e;
    while ((e = readdir(d)) && nfiles < MAX_FILES) {
        size_t n = strlen(e->d_name);
        if (n > 3 && n < sizeof files[0] && !strcmp(e->d_name + n - 3, ".cs")) strcpy(files[nfiles++], e->d_name);
    }
    closedir(d);
    qsort(files, nfiles, sizeof files[0], cmpname);
    if (sel >= nfiles) sel = nfiles ? nfiles - 1 : 0;
}

static void install_samples(void) {
    mkdir(APP_DIR, 0777);
    for (size_t i = 0; i < sizeof ms_samples / sizeof ms_samples[0]; i++) {
        char path[160];
        snprintf(path, sizeof path, "%s/%s", APP_DIR, ms_samples[i].name);
        FILE* f = fopen(path, "rb");
        if (f) { fclose(f); continue; }      /* never overwrite the user's files */
        f = fopen(path, "wb");
        if (!f) continue;
        fwrite(ms_samples[i].data, 1, ms_samples[i].len, f);     /* .cs text or a .png/.wav file */
        fclose(f);
    }
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n < 0 || n > 4 * 1024 * 1024) { fclose(f); return NULL; }
    char* s = (char*)malloc(n + 1);
    if (!s) { fclose(f); return NULL; }
    size_t got = fread(s, 1, n, f);
    s[got] = 0;
    fclose(f);
    return s;
}

/* ---------- drawing helpers ---------- */
static void text(float x, float y, const char* s, uint32_t rgb) { plat_text(x, y, s, 0.5f, rgb); }

/* draws text with simple wrapping at 'cols' characters; returns next y */
static float text_wrapped(float x, float y, const char* s, int cols, uint32_t rgb) {
    char line[128];
    while (*s) {
        int n = 0;
        while (s[n] && s[n] != '\n' && n < cols) n++;
        if (s[n] && s[n] != '\n' && n == cols) {      /* break at last space if possible */
            int k = n; while (k > 0 && s[k] != ' ') k--;
            if (k > 0) n = k;
        }
        memcpy(line, s, n); line[n] = 0;
        text(x, y, line, rgb);
        y += 15;
        s += n;
        while (*s == ' ' || *s == '\n') { if (*s == '\n') { s++; break; } s++; }
    }
    return y;
}

static void draw_log(float y0, int maxLines) {
    int n = log_count();
    int first = n > maxLines ? n - maxLines : 0;
    for (int i = first; i < n; i++) text(6, y0 + (i - first) * 15, log_line(i), 0xD0D0D0);
}

/* ---------- message screen ---------- */
static void message(const char* title, const char* body, uint32_t titleColor, int showLog) {
    while (aptMainLoop()) {
        plat3ds_input();
        if (plat3ds_down() & (KEY_A | KEY_B | KEY_START)) break;
        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        plat3ds_begin_frame(plat3ds_color(0x101418), plat3ds_color(0x000000));
        plat_text(10, 8, title, 0.7f, titleColor);
        text_wrapped(10, 40, body, 52, 0xFFFFFF);
        text(10, 220, "A / B: back to menu", 0x808080);
        plat_screen(1);
        if (showLog) draw_log(4, 15);
        C3D_FrameEnd(0);
    }
}

/* ---------- running a program ---------- */
static void run_file(const char* name) {
    char path[160];
    snprintf(path, sizeof path, "%s/%s", APP_DIR, name);
    char* src = read_file(path);
    flog("run %s", name);
    if (!src) { snprintf(status, sizeof status, "Cannot read %s", name); flog("cannot read %s", path); return; }

    static char err[320];
    u64 t0 = osGetTime();
    Program* p = ms_compile(src, err, sizeof err);
    free(src);
    if (!p) { flog("compile error: %s", err); message("Compile error", err, 0xFF5050, 0); snprintf(status, sizeof status, "%s: compile error", name); return; }
    u64 compileMs = osGetTime() - t0;
    flog("compiled in %d ms, output:", (int)compileMs);

    log_clear();
    VM* vm = vm_new(p);
    if (!vm) { ms_free_program(p); snprintf(status, sizeof status, "Out of memory"); flog("out of memory (vm_new)"); return; }

    /* static initializers and Main() run inside one frame so they may draw */
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    plat3ds_begin_frame(plat3ds_color(0), plat3ds_color(0));
    int ok = vm_call(vm, p->staticInit);
    if (ok && p->entryMain >= 0) ok = vm_call(vm, p->entryMain);
    C3D_FrameEnd(0);

    int frames = 0, minFps = 0;
    u64 vmTicks = 0, worstTicks = 0, runStart = osGetTime();
    if (ok && p->entryUpdate >= 0) {
        while (aptMainLoop()) {
            plat3ds_input();
            if (plat3ds_down() & KEY_START) break;
            C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
            plat3ds_begin_frame(plat3ds_color(0), plat3ds_color(0));
            u64 t0 = svcGetSystemTick();
            ok = vm_call(vm, p->entryUpdate);
            if (ok) ok = vm_tick_coroutines(vm);    /* after Update, before Draw, like Unity */
            if (ok && p->entryDraw >= 0) ok = vm_call(vm, p->entryDraw);
            u64 dt = svcGetSystemTick() - t0;
            vmTicks += dt; if (dt > worstTicks) worstTicks = dt; frames++;
            if (frames > 120 && (minFps == 0 || plat_fps() < minFps)) minFps = plat_fps();   /* skip the warm-up second */
            if (showFps) {
                char buf[48];
                snprintf(buf, sizeof buf, "%d FPS  %.2f ms", plat_fps(), dt * 1000.0 / SYSCLOCK_ARM11);
                plat_screen(0);
                plat_rect(0, 0, 112, 14, 0x000000);
                plat_text(3, 1, buf, 0.4f, 0x60FF60);
            }
            C3D_FrameEnd(0);
            if (!ok) break;
        }
    }
    if (frames) {
        double secs = (osGetTime() - runStart) / 1000.0;
        flog("%d frames in %.1f s = %.1f FPS (min %d), Update+Draw avg %.3f ms, worst %.3f ms",
             frames, secs, secs > 0 ? frames / secs : 0, minFps,
             vmTicks * 1000.0 / SYSCLOCK_ARM11 / frames, worstTicks * 1000.0 / SYSCLOCK_ARM11);
    }

    if (!ok && !vm->quit) {
        flog("runtime error: %s", vm->err);
        message("Runtime error", vm->err, 0xFF5050, 1);
        snprintf(status, sizeof status, "%s: runtime error", name);
    } else {
        flog(vm->quit ? "finished (App.Quit)" : "finished ok");
        if (p->entryUpdate < 0) message("Program finished", "Output is on the bottom screen.", 0x60E060, 1);
        snprintf(status, sizeof status, "%s: ok (compiled in %d ms)", name, (int)compileMs);
    }
    vm_free(vm);
    ms_free_program(p);
    /* textures may still be in use by the last frame: let the GPU finish before freeing them */
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C3D_FrameEnd(0);
    plat_media_reset();
}

/* ---------- menu ---------- */
static void draw_menu(void) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    plat3ds_begin_frame(plat3ds_color(0x141A26), plat3ds_color(0x0C0C0C));
    plat_text(10, 6, "MiniSharp", 0.8f, 0x7FB2FF);
    text(150, 12, "C#-like language for 3DS", 0x8090A0);
    if (nfiles == 0) {
        text(10, 50, "No .cs files in sdmc:/minisharp/", 0xFFFFFF);
        text(10, 68, "Copy your programs there and press Y.", 0xA0A0A0);
    }
    const int visible = 11;
    if (sel < scroll) scroll = sel;
    if (sel >= scroll + visible) scroll = sel - visible + 1;
    for (int i = scroll; i < nfiles && i < scroll + visible; i++) {
        float y = 40 + (i - scroll) * 16;
        if (i == sel) plat_rect(4, y - 1, 392, 16, 0x2D4A80);
        text(12, y, files[i], i == sel ? 0xFFFFFF : 0xC0C8D0);
    }
    text(10, 222, status, 0x80C080);

    plat_screen(1);
    text(8, 6, "A: run  Y: refresh  X: FPS  START: exit", 0xFFFFFF);
    text(8, 22, "In a game: START returns here", 0xA0A0A0);
    text(8, 44, "Last program output:", 0x7FB2FF);
    draw_log(62, 11);
    C3D_FrameEnd(0);
}

int main(void) {
    gfxInitDefault();
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
    C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
    C2D_Prepare();
    C3D_RenderTarget* top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    C3D_RenderTarget* bot = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    plat3ds_init(top, bot);

    install_samples();
    ms_set_base_dir(APP_DIR);
    flog_open(LOG_PATH);
    flog("MiniSharp started");
    if (!plat3ds_audio_init()) flog("no sound: DSP firmware missing (run DSP1 once to dump it)");
    scan_files();
    flog("%d .cs file(s) in %s", nfiles, APP_DIR);
    snprintf(status, sizeof status, "%d program(s) in %s", nfiles, APP_DIR);

    while (aptMainLoop()) {
        plat3ds_input();
        u32 k = plat3ds_down();
        if (k & KEY_START) break;
        if (nfiles) {
            if (k & KEY_DOWN) sel = (sel + 1) % nfiles;
            if (k & KEY_UP) sel = (sel + nfiles - 1) % nfiles;
            if (k & KEY_RIGHT) { sel += 10; if (sel >= nfiles) sel = nfiles - 1; }
            if (k & KEY_LEFT) { sel -= 10; if (sel < 0) sel = 0; }
            if (k & KEY_A) { run_file(files[sel]); continue; }
        }
        if (k & KEY_X) { showFps = !showFps; snprintf(status, sizeof status, "FPS overlay %s", showFps ? "on" : "off"); }
        if (k & KEY_Y) { scan_files(); snprintf(status, sizeof status, "%d program(s) found", nfiles); }
        draw_menu();
    }

    flog("exit");
    flog_close();
    plat_media_reset();
    plat3ds_audio_exit();
    plat3ds_exit();
    C2D_Fini();
    C3D_Fini();
    gfxExit();
    return 0;
}
