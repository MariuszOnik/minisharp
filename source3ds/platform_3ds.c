/* MiniSharp - 3DS platform layer on top of libctru + citro2d. */
#include <3ds.h>
#include <citro2d.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "../src/platform.h"
#include "platform_3ds.h"

static C3D_RenderTarget* g_top;
static C3D_RenderTarget* g_bot;
static int g_cur;
static C2D_TextBuf g_textbuf;
static u32 g_held, g_down;
static touchPosition g_touch;
static circlePosition g_cpad;
static u64 g_start;

/* ---- output log (Console.WriteLine) ---- */
static char g_log[LOG_LINES][LOG_COLS + 1];
static int g_logHead, g_logCount, g_logCol;

void log_clear(void) { g_logHead = 0; g_logCount = 0; g_logCol = 0; memset(g_log, 0, sizeof g_log); }

static char* log_cur_line(void) {
    if (g_logCount == 0) { g_logCount = 1; g_log[0][0] = 0; }
    return g_log[(g_logHead + g_logCount - 1) % LOG_LINES];
}
static void log_newline(void) {
    if (g_logCount < LOG_LINES) g_logCount++;
    else g_logHead = (g_logHead + 1) % LOG_LINES;
    log_cur_line()[0] = 0;
    g_logCol = 0;
}
static FILE* g_flog;
static int g_flogMidLine;                /* Console output left the log without a final newline */
/* keeps earlier sessions; past 64 KB the old log moves to log_prev.txt */
void flog_open(const char* path) {
    FILE* f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fclose(f);
        if (n > 64 * 1024) {
            char prev[160];
            snprintf(prev, sizeof prev, "%.*s_prev.txt", (int)(strlen(path) - 4), path);
            remove(prev);
            rename(path, prev);
        }
    }
    g_flog = fopen(path, "a");
    if (g_flog) { fputs("\n======== new session ========\n", g_flog); fflush(g_flog); }
}
void flog_close(void) { if (g_flog) fclose(g_flog); g_flog = NULL; }
void flog(const char* fmt, ...) {
    if (!g_flog) return;
    if (g_flogMidLine) { fputc('\n', g_flog); g_flogMidLine = 0; }
    fprintf(g_flog, "[%7.3f] ", (osGetTime() - g_start) / 1000.0);
    va_list ap; va_start(ap, fmt); vfprintf(g_flog, fmt, ap); va_end(ap);
    fputc('\n', g_flog);
    fflush(g_flog);                      /* keep the log even if the app crashes */
}

void plat_print(const char* s) {
    if (g_flog && *s) {
        fputs(s, g_flog);
        if (strchr(s, '\n')) fflush(g_flog);
        g_flogMidLine = s[strlen(s) - 1] != '\n';
    }
    char* line = log_cur_line();
    for (; *s; s++) {
        if (*s == '\n') { log_newline(); line = log_cur_line(); continue; }
        if (g_logCol >= LOG_COLS) { log_newline(); line = log_cur_line(); }
        line[g_logCol++] = *s;
        line[g_logCol] = 0;
    }
}
int log_count(void) { return g_logCount; }
const char* log_line(int i) { return g_log[(g_logHead + i) % LOG_LINES]; }

/* ---- setup / per frame ---- */
void plat3ds_init(C3D_RenderTarget* top, C3D_RenderTarget* bot) {
    g_top = top; g_bot = bot;
    g_textbuf = C2D_TextBufNew(8192);
    g_start = osGetTime();
}
void plat3ds_exit(void) { C2D_TextBufDelete(g_textbuf); }

void plat3ds_input(void) {
    hidScanInput();
    g_held = hidKeysHeld();
    g_down = hidKeysDown();
    hidTouchRead(&g_touch);
    hidCircleRead(&g_cpad);
}
u32 plat3ds_down(void) { return g_down; }
u32 plat3ds_held(void) { return g_held; }

/* frames per second, measured over the last full second */
static int g_fps, g_fpsFrames;
static u64 g_fpsStart;
int plat_fps(void) { return g_fps; }

void plat3ds_begin_frame(u32 clearTop, u32 clearBottom) {
    u64 now = osGetTime();
    g_fpsFrames++;
    if (now - g_fpsStart >= 1000) { g_fps = (int)(g_fpsFrames * 1000 / (now - g_fpsStart)); g_fpsFrames = 0; g_fpsStart = now; }
    C2D_TextBufClear(g_textbuf);
    C2D_TargetClear(g_top, clearTop);
    C2D_TargetClear(g_bot, clearBottom);
    plat_screen(0);
}

u32 plat3ds_color(uint32_t rgb) { return C2D_Color32((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, 255); }

/* ---- drawing ---- */
void plat_screen(int s) { g_cur = s ? 1 : 0; C2D_SceneBegin(g_cur ? g_bot : g_top); }
void plat_clear(uint32_t rgb) { C2D_TargetClear(g_cur ? g_bot : g_top, plat3ds_color(rgb)); C2D_SceneBegin(g_cur ? g_bot : g_top); }
void plat_rect(float x, float y, float w, float h, uint32_t rgb) { C2D_DrawRectSolid(x, y, 0.5f, w, h, plat3ds_color(rgb)); }
void plat_circle(float x, float y, float r, uint32_t rgb) { C2D_DrawCircleSolid(x, y, 0.5f, r, plat3ds_color(rgb)); }
void plat_line(float x1, float y1, float x2, float y2, float w, uint32_t rgb) {
    u32 c = plat3ds_color(rgb);
    C2D_DrawLine(x1, y1, c, x2, y2, c, w, 0.5f);
}
void plat_text(float x, float y, const char* s, float scale, uint32_t rgb) {
    C2D_Text t;
    C2D_TextParse(&t, g_textbuf, s);
    C2D_TextOptimize(&t);
    C2D_DrawText(&t, C2D_WithColor, x, y, 0.5f, scale, scale, plat3ds_color(rgb));
}

/* ---- input ---- */
static uint32_t map_keys(u32 k) {
    uint32_t r = 0;
    if (k & KEY_A) r |= MSB_A;
    if (k & KEY_B) r |= MSB_B;
    if (k & KEY_X) r |= MSB_X;
    if (k & KEY_Y) r |= MSB_Y;
    if (k & KEY_L) r |= MSB_L;
    if (k & KEY_R) r |= MSB_R;
    if (k & KEY_ZL) r |= MSB_ZL;
    if (k & KEY_ZR) r |= MSB_ZR;
    if (k & KEY_UP) r |= MSB_UP;
    if (k & KEY_DOWN) r |= MSB_DOWN;
    if (k & KEY_LEFT) r |= MSB_LEFT;
    if (k & KEY_RIGHT) r |= MSB_RIGHT;
    if (k & KEY_START) r |= MSB_START;
    if (k & KEY_SELECT) r |= MSB_SELECT;
    return r;
}
uint32_t plat_buttons_held(void) { return map_keys(g_held); }
uint32_t plat_buttons_pressed(void) { return map_keys(g_down); }
int plat_touch(int* x, int* y) {
    if (!(g_held & KEY_TOUCH)) { *x = 0; *y = 0; return 0; }
    *x = g_touch.px; *y = g_touch.py;
    return 1;
}
static float axis(int v) { float f = v / 150.0f; if (f > 1) f = 1; if (f < -1) f = -1; if (f > -0.1f && f < 0.1f) f = 0; return f; }
float plat_stick_x(void) { return axis(g_cpad.dx); }
float plat_stick_y(void) { return -axis(g_cpad.dy); }   /* positive = down, like screen coordinates */
int plat_millis(void) { return (int)(osGetTime() - g_start); }
