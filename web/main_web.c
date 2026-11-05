/* MiniSharp - browser host. Compiled to WebAssembly (wasm32-wasi) by web/build.sh.
 * Drawing, input, sound and timing go to JavaScript (web/app.js) through the "env" imports below;
 * files (the project's .cs files, images, sounds) live in a small in-memory file system in app.js. */
#include "../src/ms.h"
#include "../src/platform.h"
#include "../src/media.h"
#include "../src/project.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define IMPORT(name) __attribute__((import_module("env"), import_name(name)))
#define EXPORT(name) __attribute__((export_name(name)))

IMPORT("js_print") void js_print(const char* s, int len);
IMPORT("js_screen") void js_screen(int s);
IMPORT("js_clear") void js_clear(uint32_t rgb);
IMPORT("js_rect") void js_rect(float x, float y, float w, float h, uint32_t rgb);
IMPORT("js_circle") void js_circle(float x, float y, float r, uint32_t rgb);
IMPORT("js_line") void js_line(float x1, float y1, float x2, float y2, float w, uint32_t rgb);
IMPORT("js_text") void js_text(float x, float y, const char* s, int len, float scale, uint32_t rgb);
IMPORT("js_millis") int js_millis(void);
IMPORT("js_fps") int js_fps(void);
IMPORT("js_image_create") void js_image_create(int id, int w, int h, const unsigned char* rgba);
IMPORT("js_image_draw") void js_image_draw(int id, float x, float y, int sx, int sy, int sw, int sh, float scx, float scy, float angle);
IMPORT("js_sound_create") void js_sound_create(int id, const int16_t* pcm, int frames, int channels, int rate);
IMPORT("js_sound_play") void js_sound_play(int id, float volume, int loop);
IMPORT("js_sound_stop") void js_sound_stop(int id);
IMPORT("js_media_reset") void js_media_reset(void);

/* ---------- input, set by JavaScript once per frame ---------- */
static uint32_t g_held, g_pressed;
static int g_touching, g_tx, g_ty;
static float g_sx, g_sy;

/* ---------- platform ---------- */
void plat_print(const char* s) { js_print(s, (int)strlen(s)); }
void plat_screen(int s) { js_screen(s ? 1 : 0); }
void plat_clear(uint32_t rgb) { js_clear(rgb); }
void plat_rect(float x, float y, float w, float h, uint32_t rgb) { js_rect(x, y, w, h, rgb); }
void plat_circle(float x, float y, float r, uint32_t rgb) { js_circle(x, y, r, rgb); }
void plat_line(float x1, float y1, float x2, float y2, float w, uint32_t rgb) { js_line(x1, y1, x2, y2, w, rgb); }
void plat_text(float x, float y, const char* s, float scale, uint32_t rgb) { js_text(x, y, s, (int)strlen(s), scale, rgb); }
uint32_t plat_buttons_held(void) { return g_held; }
uint32_t plat_buttons_pressed(void) { return g_pressed; }
int plat_touch(int* x, int* y) {
    if (!g_touching) { *x = 0; *y = 0; return 0; }
    *x = g_tx; *y = g_ty;
    return 1;
}
float plat_stick_x(void) { return g_sx; }
float plat_stick_y(void) { return g_sy; }
int plat_millis(void) { return js_millis(); }
int plat_fps(void) { return js_fps(); }

/* images and sounds: decoded here (same code as the console), kept as canvases / audio buffers in JS */
#define MAXMEDIA 256
typedef struct { char path[256]; int w, h; } WebImage;
typedef struct { char path[256]; } WebSound;
static WebImage images[MAXMEDIA]; static int nimages;
static WebSound sounds[MAXMEDIA]; static int nsounds;

int plat_image_load(const char* path, char* err, int errlen) {
    for (int i = 0; i < nimages; i++) if (!strcmp(images[i].path, path)) return i;
    if (nimages == MAXMEDIA) { snprintf(err, errlen, "too many images"); return -1; }
    int w, h;
    unsigned char* px = ms_image_decode(path, &w, &h, err, errlen);
    if (!px) return -1;
    if (w > 1024 || h > 1024) { ms_image_free(px); snprintf(err, errlen, "image is %dx%d, the 3DS allows at most 1024x1024", w, h); return -1; }
    js_image_create(nimages, w, h, px);
    ms_image_free(px);
    snprintf(images[nimages].path, sizeof images[nimages].path, "%s", path);
    images[nimages].w = w; images[nimages].h = h;
    return nimages++;
}
int plat_image_size(int id, int* w, int* h) {
    if (id < 0 || id >= nimages) return 0;
    *w = images[id].w; *h = images[id].h;
    return 1;
}
void plat_image_draw(int id, float x, float y, int sx, int sy, int sw, int sh, float scaleX, float scaleY, float angle) {
    if (id < 0 || id >= nimages) return;
    js_image_draw(id, x, y, sx, sy, sw, sh, scaleX, scaleY, angle);
}
int plat_sound_load(const char* path, char* err, int errlen) {
    for (int i = 0; i < nsounds; i++) if (!strcmp(sounds[i].path, path)) return i;
    if (nsounds == MAXMEDIA) { snprintf(err, errlen, "too many sounds"); return -1; }
    int rate, ch, frames;
    int16_t* data = ms_wav_load(path, &rate, &ch, &frames, err, errlen);
    if (!data) return -1;
    js_sound_create(nsounds, data, frames, ch, rate);
    free(data);
    snprintf(sounds[nsounds].path, sizeof sounds[nsounds].path, "%s", path);
    return nsounds++;
}
int plat_sound_valid(int id) { return id >= 0 && id < nsounds; }
void plat_sound_play(int id, float volume, int loop) { if (plat_sound_valid(id)) js_sound_play(id, volume, loop); }
void plat_sound_stop(int id) { js_sound_stop(id); }
void plat_media_reset(void) { nimages = 0; nsounds = 0; js_media_reset(); }

/* ---------- running a program ---------- */
static Program* g_prog;
static VM* g_vm;
static Project g_proj;
static char g_err[400];

static void stop(void) {
    if (g_vm) vm_free(g_vm);
    if (g_prog) ms_free_program(g_prog);
    if (g_vm || g_prog) plat_media_reset();
    project_free(&g_proj);
    g_vm = NULL; g_prog = NULL;
}

static int failed(void) {
    if (g_vm && g_vm->quit) { stop(); return 0; }       /* App.Quit() */
    snprintf(g_err, sizeof g_err, "%s", g_vm ? g_vm->err : "out of memory");
    project_map_error(&g_proj, g_err, sizeof g_err);
    stop();
    return -1;
}

EXPORT("web_error") const char* web_error(void) { return g_err; }

/* Compiles every .cs file in the folder 'dir' and runs Main().
 * 1 = a game: call web_frame() every frame; 0 = finished; -1 = compile error (web_error); -2 = runtime error */
EXPORT("web_run") int web_run(const char* dir) {
    stop();
    g_err[0] = 0;
    g_held = g_pressed = 0; g_touching = 0;
    ms_set_base_dir(dir);
    if (!project_load(dir, &g_proj, g_err, sizeof g_err)) return -1;
    g_prog = ms_compile(g_proj.src, g_err, sizeof g_err);
    if (!g_prog) { project_map_error(&g_proj, g_err, sizeof g_err); project_free(&g_proj); return -1; }
    g_vm = vm_new(g_prog);
    if (!g_vm) return failed() ? -2 : 0;
    js_screen(0);
    int ok = vm_call(g_vm, g_prog->staticInit);
    if (ok && g_prog->entryMain >= 0) ok = vm_call(g_vm, g_prog->entryMain);
    if (!ok) return failed() ? -2 : 0;
    if (g_prog->entryUpdate < 0) { stop(); return 0; }
    return 1;
}

EXPORT("web_input") void web_input(uint32_t held, uint32_t pressed, int touching, int tx, int ty, float sx, float sy) {
    g_held = held; g_pressed = pressed; g_touching = touching; g_tx = tx; g_ty = ty; g_sx = sx; g_sy = sy;
}

/* one frame: Update, coroutines, Draw. 1 = go on, 0 = finished (App.Quit), -2 = runtime error */
EXPORT("web_frame") int web_frame(void) {
    if (!g_vm) return 0;
    js_screen(0);
    int ok = vm_call(g_vm, g_prog->entryUpdate);
    if (ok) ok = vm_tick_coroutines(g_vm);
    if (ok && g_prog->entryDraw >= 0) ok = vm_call(g_vm, g_prog->entryDraw);
    if (!ok) return failed() ? -2 : 0;
    return 1;
}

EXPORT("web_stop") void web_stop(void) { stop(); }

/* buffers for passing strings from JavaScript */
EXPORT("web_alloc") void* web_alloc(int n) { return malloc((size_t)n); }
EXPORT("web_free") void web_free(void* p) { free(p); }
