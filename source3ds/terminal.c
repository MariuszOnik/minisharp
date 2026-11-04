/* MiniSharp - terminal (REPL) on the console. A line typed on the touch keyboard runs with A or Run;
 * variables stay for the next lines. The top screen shows the output, or the canvas that Gfx draws
 * into (it is not cleared between lines; X switches between the two). */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>
#include "../src/ms.h"
#include "../src/platform.h"
#include "../src/repl.h"
#include "platform_3ds.h"
#include "keyboard.h"
#include "terminal.h"

#define LINE_CAP 256
#define HISTORY 32
#define NSUGGEST 4
#define INPUT_X 18                      /* text starts after the "> " prompt */
#define INPUT_W 296

static char g_line[LINE_CAP];
static int g_len, g_cur, g_scroll;      /* g_scroll: first character shown in the input box */
static char g_hist[HISTORY][LINE_CAP], g_draft[LINE_CAP];
static int g_nhist, g_histPos;
static char g_sug[NSUGGEST][REPL_WORD];
static int g_nsug, g_sugStart;

/* ---------- editing ---------- */
static void insert(const char* s) {
    int n = (int)strlen(s);
    if (g_len + n >= LINE_CAP) return;
    memmove(g_line + g_cur + n, g_line + g_cur, g_len - g_cur + 1);
    memcpy(g_line + g_cur, s, n);
    g_len += n; g_cur += n;
}
static void backspace(void) {
    if (g_cur == 0) return;
    memmove(g_line + g_cur - 1, g_line + g_cur, g_len - g_cur + 1);
    g_len--; g_cur--;
}
static void set_line(const char* s) {
    snprintf(g_line, LINE_CAP, "%s", s);
    g_len = g_cur = (int)strlen(g_line);
    g_scroll = 0;
}
/* replaces the word being typed with a completion */
static void complete(const char* word) {
    memmove(g_line + g_sugStart, g_line + g_cur, g_len - g_cur + 1);
    g_len -= g_cur - g_sugStart;
    g_cur = g_sugStart;
    insert(word);
}

static void history_add(const char* s) {
    if (g_nhist && !strcmp(g_hist[(g_nhist - 1) % HISTORY], s)) return;
    snprintf(g_hist[g_nhist % HISTORY], LINE_CAP, "%s", s);
    g_nhist++;
}
static void history_move(int dir) {
    int oldest = g_nhist > HISTORY ? g_nhist - HISTORY : 0;
    int pos = g_histPos + dir;
    if (pos < oldest || pos > g_nhist) return;
    if (g_histPos == g_nhist) snprintf(g_draft, LINE_CAP, "%s", g_line);   /* leaving the new line */
    g_histPos = pos;
    set_line(pos == g_nhist ? g_draft : g_hist[pos % HISTORY]);
}

/* width of g_line[from..to) on screen */
static float width_of(int from, int to) {
    char buf[LINE_CAP];
    memcpy(buf, g_line + from, to - from); buf[to - from] = 0;
    return plat3ds_text_width(buf, 0.5f);
}

/* ---------- running ---------- */
static void end_output_line(void) {
    int n = log_count();
    if (n && log_line(n - 1)[0]) plat_print("\n");      /* Console.Write left a line open */
}

/* runs the line inside the current frame; returns 1 if it drew something */
static int run_line(Repl* r, C3D_RenderTarget* canvas) {
    char buf[LINE_CAP + 8], err[300];
    end_output_line();
    snprintf(buf, sizeof buf, LOG_INPUT "> %s\n", g_line);
    plat_print(buf);
    history_add(g_line);
    g_histPos = g_nhist;

    plat3ds_set_canvas(canvas);
    plat3ds_draws = 0;
    int ok = repl_eval(r, g_line, err, sizeof err);
    int drew = plat3ds_draws;
    plat3ds_set_canvas(NULL);
    if (!ok) {
        end_output_line();
        snprintf(buf, sizeof buf, LOG_ERROR "%.250s\n", err);
        plat_print(buf);
        flog("terminal error: %s", err);
    }
    set_line("");
    return ok && drew;
}

/* ---------- drawing ---------- */
static void draw_input(void) {
    plat_rect(0, 18, 320, 24, 0x1C2230);
    plat_text(5, 21, ">", 0.5f, 0x7FB2FF);
    if (g_cur < g_scroll) g_scroll = g_cur;
    while (g_scroll < g_cur && width_of(g_scroll, g_cur) > INPUT_W - 4) g_scroll++;
    int end = g_scroll;                                 /* as much as fits after the scroll position */
    while (end < g_len && width_of(g_scroll, end + 1) <= INPUT_W) end++;
    char buf[LINE_CAP];
    memcpy(buf, g_line + g_scroll, end - g_scroll); buf[end - g_scroll] = 0;
    plat_text(INPUT_X, 21, buf, 0.5f, 0xFFFFFF);
    if ((osGetTime() / 500) % 2 == 0) plat_rect(INPUT_X + width_of(g_scroll, g_cur), 21, 1.5f, 16, 0xFFFFFF);
}

static void draw_suggestions(void) {
    for (int i = 0; i < NSUGGEST; i++) {
        float x = i * 80;
        plat_rect(x + 1, 45, 78, 31, i < g_nsug ? 0x24324A : 0x15181E);
        if (i >= g_nsug) continue;
        float scale = 0.5f, w = plat3ds_text_width(g_sug[i], scale);
        if (w > 74) { scale = 0.5f * 74 / w; w = 74; }
        plat_text(x + (80 - w) / 2, 52, g_sug[i], scale, i == 0 ? 0xFFFFFF : 0xC0D0E8);
    }
}

static void draw_canvas(C3D_Tex* tex) {
    /* the texture is 512x256; the canvas is its top-left 400x240 (texture v grows upwards) */
    static const Tex3DS_SubTexture sub = { 400, 240, 0.0f, 1.0f, 400 / 512.0f, 1.0f - 240 / 256.0f };
    C2D_Image img = { tex, &sub };
    C2D_DrawImageAt(img, 0, 0, 0.5f, NULL, 1.0f, 1.0f);
}

/* ---------- main loop ---------- */
void terminal_run(void) {
    Repl* r = repl_new();
    if (!r) { flog("terminal: out of memory"); return; }
    C3D_Tex tex;
    if (!C3D_TexInitVRAM(&tex, 512, 256, GPU_RGBA8)) { repl_free(r); flog("terminal: no VRAM for the canvas"); return; }
    C3D_TexSetFilter(&tex, GPU_NEAREST, GPU_NEAREST);
    /* citro2d tests depth, so the canvas needs its own depth buffer (the screen's has another size) */
    C3D_RenderTarget* canvas = C3D_RenderTargetCreateFromTex(&tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
    if (!canvas) { C3D_TexDelete(&tex); repl_free(r); flog("terminal: cannot make the canvas"); return; }    flog("terminal opened");

    hidSetRepeatParameters(25, 4);      /* B / Left / Right repeat: after 25 frames, every 4 */
    kbd_reset();
    kbd_set_enter_label("Run");
    set_line("");
    g_histPos = g_nhist;
    log_clear();
    plat_print("MiniSharp terminal. Try:\n  1 + 2\n  var x = 5\n  Gfx.Circle(200, 120, 40, Color.Red)\n");
    int clearCanvas = 1, showCanvas = 0, frames = 0, minFps = 0;
    u64 start = osGetTime();

    while (aptMainLoop()) {
        frames++;
        if (frames > 120 && (minFps == 0 || plat_fps() < minFps)) minFps = plat_fps();
        plat3ds_input();
        u32 down = plat3ds_down(), rep = plat3ds_repeat();
        if (down & KEY_START) break;
        int run = 0;

        const char* k = kbd_update();
        if (k) {
            if (!strcmp(k, KBD_BACKSPACE)) backspace();
            else if (!strcmp(k, KBD_ENTER)) run = 1;
            else insert(k);
        }
        if (down & KEY_TOUCH) {
            int px, py;
            plat3ds_touch(&px, &py);
            if (py >= 44 && py < 77 && px / 80 < g_nsug) complete(g_sug[px / 80]);
            else if (py >= 18 && py < 42) {                 /* tap in the line moves the cursor */
                g_cur = g_scroll;
                while (g_cur < g_len && INPUT_X + width_of(g_scroll, g_cur + 1) < px + 3) g_cur++;
            }
        }
        if (down & KEY_A) run = 1;
        if (rep & KEY_B) backspace();
        if ((rep & KEY_LEFT) && g_cur > 0) g_cur--;
        if ((rep & KEY_RIGHT) && g_cur < g_len) g_cur++;
        if (down & KEY_UP) history_move(-1);
        if (down & KEY_DOWN) history_move(1);
        if ((down & KEY_R) && g_nsug) complete(g_sug[0]);
        if (down & KEY_X) showCanvas = !showCanvas;
        if (down & KEY_Y) { log_clear(); clearCanvas = 1; }

        C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
        plat3ds_begin_frame(plat3ds_color(0x101418), plat3ds_color(0x0C0C0C));
        if (clearCanvas) { C2D_TargetClear(canvas, plat3ds_color(0x000000)); clearCanvas = 0; }
        if (run && g_len) showCanvas = run_line(r, canvas);
        g_nsug = repl_complete(r, g_line, g_cur, &g_sugStart, g_sug, NSUGGEST);

        plat_screen(0);
        if (showCanvas) draw_canvas(&tex);
        else {
            plat_rect(0, 0, 400, 16, 0x1C2230);
            plat_text(6, 1, "Terminal", 0.45f, 0x7FB2FF);
            plat_text(90, 1, "X: canvas   Y: clear   START: menu", 0.45f, 0x8090A0);
            log_draw(6, 20, 14);
        }
        plat_screen(1);
        plat_text(4, 2, "A: run  B: del  Up/Down: history  R: complete", 0.42f, 0x8090A0);
        draw_input();
        draw_suggestions();
        kbd_draw();
        C3D_FrameEnd(0);
    }

    double secs = (osGetTime() - start) / 1000.0;
    flog("terminal closed: %d frames in %.1f s = %.1f FPS (min %d)", frames, secs, secs > 0 ? frames / secs : 0, minFps);
    repl_free(r);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);     /* let the GPU finish with the canvas and images first */
    C3D_FrameEnd(0);
    C3D_RenderTargetDelete(canvas);
    C3D_TexDelete(&tex);
    plat_media_reset();
    log_clear();
}
