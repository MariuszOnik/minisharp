/* MiniSharp - PC platform: stdout console and a software framebuffer for both 3DS screens. */
#include "../src/platform.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#define TW 400
#define BW 320
#define H 240
static uint32_t top[TW * H], bot[BW * H];
static int cur = 0;
static int frame = 0;

void pc_begin_frame(void) { frame++; cur = 0; }

static void px(int x, int y, uint32_t c) {
    int w = cur ? BW : TW;
    if (x < 0 || y < 0 || x >= w || y >= H) return;
    (cur ? bot : top)[y * w + x] = c;
}

void plat_print(const char* s) { fputs(s, stdout); fflush(stdout); }
void plat_screen(int s) { cur = s; }
void plat_clear(uint32_t c) {
    int n = (cur ? BW : TW) * H;
    uint32_t* b = cur ? bot : top;
    for (int i = 0; i < n; i++) b[i] = c;
}
void plat_rect(float x, float y, float w, float h, uint32_t c) {
    int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)floorf(x + w), y1 = (int)floorf(y + h);
    for (int j = y0; j < y1; j++) for (int i = x0; i < x1; i++) px(i, j, c);
}
void plat_circle(float cx, float cy, float r, uint32_t c) {
    for (int j = (int)(cy - r); j <= (int)(cy + r); j++)
        for (int i = (int)(cx - r); i <= (int)(cx + r); i++)
            if ((i + 0.5f - cx) * (i + 0.5f - cx) + (j + 0.5f - cy) * (j + 0.5f - cy) <= r * r) px(i, j, c);
}
void plat_line(float x1, float y1, float x2, float y2, float w, uint32_t c) {
    (void)w;
    float dx = x2 - x1, dy = y2 - y1;
    int steps = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
    for (int i = 0; i <= steps; i++) px((int)(x1 + dx * i / steps), (int)(y1 + dy * i / steps), c);
}
void plat_text(float x, float y, const char* s, float scale, uint32_t c) {
    /* no font on PC: draw a small box per character so layout is visible */
    float cw = 16 * scale, ch = 24 * scale;
    for (int i = 0; s[i]; i++)
        if (s[i] != ' ') plat_rect(x + i * cw + 1, y + 2, cw - 2, ch - 4, c);
}
uint32_t plat_buttons_held(void) { return 0; }
uint32_t plat_buttons_pressed(void) { return 0; }
int plat_touch(int* x, int* y) { *x = 0; *y = 0; return 0; }
float plat_stick_x(void) { return 0; }
float plat_stick_y(void) { return 0; }
int plat_millis(void) { return frame * 1000 / 60; }   /* 60 Hz like the 3DS, so timings match the console */
int plat_fps(void) { return 60; }

void pc_save_ppm(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", TW, H * 2);
    for (int y = 0; y < H * 2; y++)
        for (int x = 0; x < TW; x++) {
            uint32_t c = 0x202020;
            if (y < H) c = top[y * TW + x];
            else if (x >= 40 && x < 40 + BW) c = bot[(y - H) * BW + (x - 40)];
            unsigned char rgb[3] = { (unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

/* ---- images and sounds (sounds are only loaded and checked: the PC build has no audio) ---- */
#include "../src/media.h"
#include <stdlib.h>

#define MAXMEDIA 256
typedef struct { char path[256]; int w, h; unsigned char* px; } PcImage;
typedef struct { char path[256]; int frames; } PcSound;
static PcImage images[MAXMEDIA]; static int nimages;
static PcSound sounds[MAXMEDIA]; static int nsounds;

int plat_image_load(const char* path, char* err, int errlen) {
    for (int i = 0; i < nimages; i++) if (!strcmp(images[i].path, path)) return i;
    if (nimages == MAXMEDIA) { snprintf(err, errlen, "too many images"); return -1; }
    PcImage* im = &images[nimages];
    im->px = ms_image_decode(path, &im->w, &im->h, err, errlen);
    if (!im->px) return -1;
    if (im->w > 1024 || im->h > 1024) { ms_image_free(im->px); snprintf(err, errlen, "image is %dx%d, the 3DS allows at most 1024x1024", im->w, im->h); return -1; }
    snprintf(im->path, sizeof im->path, "%s", path);
    return nimages++;
}

int plat_image_size(int id, int* w, int* h) {
    if (id < 0 || id >= nimages) return 0;
    *w = images[id].w; *h = images[id].h;
    return 1;
}

void plat_image_draw(int id, float x, float y, int sx, int sy, int sw, int sh, float scaleX, float scaleY, float angle) {
    PcImage* im = &images[id];
    float w = sw * fabsf(scaleX), hh = sh * fabsf(scaleY);
    if (w <= 0 || hh <= 0) return;
    float cx = x + w / 2, cy = y + hh / 2;
    float a = angle * 3.14159265f / 180.0f, ca = cosf(a), sa = sinf(a);
    /* bounding box of the turned rectangle, then map every screen pixel back into the image */
    float ex = fabsf(ca) * w / 2 + fabsf(sa) * hh / 2, ey = fabsf(sa) * w / 2 + fabsf(ca) * hh / 2;
    for (int j = (int)floorf(cy - ey); j <= (int)ceilf(cy + ey); j++)
        for (int i = (int)floorf(cx - ex); i <= (int)ceilf(cx + ex); i++) {
            float dx = i + 0.5f - cx, dy = j + 0.5f - cy;
            float lx = dx * ca + dy * sa + w / 2, ly = -dx * sa + dy * ca + hh / 2;
            if (lx < 0 || ly < 0 || lx >= w || ly >= hh) continue;
            int u = (int)(lx / fabsf(scaleX)), v = (int)(ly / fabsf(scaleY));
            if (scaleX < 0) u = sw - 1 - u;
            if (scaleY < 0) v = sh - 1 - v;
            const unsigned char* p = im->px + ((size_t)(sy + v) * im->w + (sx + u)) * 4;
            if (p[3] == 0) continue;
            int bw = cur ? BW : TW;
            if (i < 0 || j < 0 || i >= bw || j >= H) continue;
            uint32_t* d = &(cur ? bot : top)[j * bw + i];
            int al = p[3], r = p[0], g = p[1], b = p[2];
            if (al < 255) {
                r = (r * al + (int)((*d >> 16) & 255) * (255 - al)) / 255;
                g = (g * al + (int)((*d >> 8) & 255) * (255 - al)) / 255;
                b = (b * al + (int)(*d & 255) * (255 - al)) / 255;
            }
            *d = (uint32_t)(r << 16 | g << 8 | b);
        }
}

int plat_sound_load(const char* path, char* err, int errlen) {
    for (int i = 0; i < nsounds; i++) if (!strcmp(sounds[i].path, path)) return i;
    if (nsounds == MAXMEDIA) { snprintf(err, errlen, "too many sounds"); return -1; }
    int rate, ch, frames;
    int16_t* data = ms_wav_load(path, &rate, &ch, &frames, err, errlen);
    if (!data) return -1;
    free(data);
    snprintf(sounds[nsounds].path, sizeof sounds[nsounds].path, "%s", path);
    sounds[nsounds].frames = frames;
    return nsounds++;
}
int plat_sound_valid(int id) { return id >= 0 && id < nsounds; }
void plat_sound_play(int id, float volume, int loop) { (void)id; (void)volume; (void)loop; }
void plat_sound_stop(int id) { (void)id; }

void plat_media_reset(void) {
    for (int i = 0; i < nimages; i++) ms_image_free(images[i].px);
    nimages = 0; nsounds = 0;
}
