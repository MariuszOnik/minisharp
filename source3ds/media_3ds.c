/* MiniSharp - images (citro2d textures) and sounds (ndsp) on the 3DS.
 * The pixel conversion follows MicroLua3DS's ml_image.c, which is proven on hardware. */
#include <3ds.h>
#include <citro2d.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/platform.h"
#include "../src/media.h"
#include "platform_3ds.h"

#define MAXMEDIA 256
typedef struct { char path[256]; int w, h; C3D_Tex tex; } Img3ds;
typedef struct { char path[256]; int16_t* data; int rate, channels, frames; } Snd3ds;
static Img3ds* images[MAXMEDIA]; static int nimages;
static Snd3ds sounds[MAXMEDIA]; static int nsounds;

/* ---- images ---- */
/* RGBA rows (stb_image) -> GPU texture: 8x8 tiles, Morton order inside a tile, ABGR words */
static int upload(C3D_Tex* tex, const unsigned char* rgba, int w, int h) {
    int tw = 8, th = 8;
    while (tw < w) tw <<= 1;
    while (th < h) th <<= 1;
    if (!C3D_TexInit(tex, (u16)tw, (u16)th, GPU_RGBA8)) return 0;
    u32* dst = (u32*)tex->data;
    memset(dst, 0, (size_t)tw * th * 4);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const unsigned char* s = rgba + ((size_t)y * w + x) * 4;
            int tx = x & 7, ty = y & 7, morton = 0;
            for (int i = 0; i < 3; i++) morton |= ((tx >> i) & 1) << (2 * i) | ((ty >> i) & 1) << (2 * i + 1);
            dst[((y >> 3) * (tw >> 3) + (x >> 3)) * 64 + morton] = (u32)s[0] << 24 | (u32)s[1] << 16 | (u32)s[2] << 8 | s[3];
        }
    C3D_TexFlush(tex);
    C3D_TexSetFilter(tex, GPU_NEAREST, GPU_NEAREST);        /* crisp pixel art */
    C3D_TexSetWrap(tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    return 1;
}

int plat_image_load(const char* path, char* err, int errlen) {
    for (int i = 0; i < nimages; i++) if (!strcmp(images[i]->path, path)) return i;
    if (nimages == MAXMEDIA) { snprintf(err, errlen, "too many images"); return -1; }
    int w, h;
    unsigned char* px = ms_image_decode(path, &w, &h, err, errlen);
    if (!px) return -1;
    if (w > 1024 || h > 1024) { ms_image_free(px); snprintf(err, errlen, "image is %dx%d, the 3DS allows at most 1024x1024", w, h); return -1; }
    Img3ds* im = (Img3ds*)calloc(1, sizeof(Img3ds));
    if (!im || !upload(&im->tex, px, w, h)) {
        free(im); ms_image_free(px);
        snprintf(err, errlen, "not enough video memory");
        return -1;
    }
    ms_image_free(px);
    snprintf(im->path, sizeof im->path, "%s", path);
    im->w = w; im->h = h;
    images[nimages] = im;
    return nimages++;
}

int plat_image_size(int id, int* w, int* h) {
    if (id < 0 || id >= nimages) return 0;
    *w = images[id]->w; *h = images[id]->h;
    return 1;
}

void plat_image_draw(int id, float x, float y, int sx, int sy, int sw, int sh, float scaleX, float scaleY, float angle) {
    Img3ds* im = images[id];
    float tw = im->tex.width, th = im->tex.height;
    Tex3DS_SubTexture sub;
    sub.width = (u16)sw; sub.height = (u16)sh;
    sub.left = sx / tw; sub.right = (sx + sw) / tw;
    sub.top = 1.0f - sy / th; sub.bottom = 1.0f - (sy + sh) / th;
    if (scaleX < 0) { float t = sub.left; sub.left = sub.right; sub.right = t; }    /* flip */
    if (scaleY < 0) { float t = sub.top; sub.top = sub.bottom; sub.bottom = t; }
    C2D_Image img = { &im->tex, &sub };
    float w = sw * fabsf(scaleX), h = sh * fabsf(scaleY);
    C2D_DrawParams p;
    p.pos.x = x + w / 2; p.pos.y = y + h / 2; p.pos.w = w; p.pos.h = h;
    p.center.x = w / 2; p.center.y = h / 2;
    p.depth = 0.5f;
    p.angle = angle * 3.14159265f / 180.0f;
    C2D_DrawImage(img, &p, NULL);
}

/* ---- sounds ---- */
#define CHANNELS 24
static int g_audioOk;
static ndspWaveBuf g_wbuf[CHANNELS];
static int g_chnSound[CHANNELS];
static int g_nextChn;

int plat3ds_audio_init(void) {
    g_audioOk = R_SUCCEEDED(ndspInit());
    if (g_audioOk) ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int i = 0; i < CHANNELS; i++) g_chnSound[i] = -1;
    return g_audioOk;
}
void plat3ds_audio_exit(void) { if (g_audioOk) ndspExit(); g_audioOk = 0; }

int plat_sound_load(const char* path, char* err, int errlen) {
    for (int i = 0; i < nsounds; i++) if (!strcmp(sounds[i].path, path)) return i;
    if (nsounds == MAXMEDIA) { snprintf(err, errlen, "too many sounds"); return -1; }
    Snd3ds* s = &sounds[nsounds];
    int16_t* pcm = ms_wav_load(path, &s->rate, &s->channels, &s->frames, err, errlen);
    if (!pcm) return -1;
    size_t bytes = (size_t)s->frames * s->channels * 2;
    s->data = (int16_t*)linearAlloc(bytes ? bytes : 2);         /* the DSP reads linear memory only */
    if (!s->data) { free(pcm); snprintf(err, errlen, "not enough audio memory"); return -1; }
    memcpy(s->data, pcm, bytes);
    free(pcm);
    DSP_FlushDataCache(s->data, bytes);
    snprintf(s->path, sizeof s->path, "%s", path);
    return nsounds++;
}

int plat_sound_valid(int id) { return id >= 0 && id < nsounds; }

void plat_sound_play(int id, float volume, int loop) {
    if (!g_audioOk || sounds[id].frames == 0) return;
    /* a free channel, or else the next one in turn */
    int ch = -1;
    for (int i = 0; i < CHANNELS && ch < 0; i++) {
        int c = (g_nextChn + i) % CHANNELS;
        if (!ndspChnIsPlaying(c)) ch = c;
    }
    if (ch < 0) ch = g_nextChn;
    g_nextChn = (ch + 1) % CHANNELS;

    Snd3ds* s = &sounds[id];
    ndspChnReset(ch);
    ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
    ndspChnSetRate(ch, (float)s->rate);
    ndspChnSetFormat(ch, s->channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    float mix[12] = {0};
    mix[0] = mix[1] = volume < 0 ? 0 : volume;
    ndspChnSetMix(ch, mix);
    ndspWaveBuf* b = &g_wbuf[ch];
    memset(b, 0, sizeof *b);
    b->data_vaddr = s->data;
    b->nsamples = (u32)s->frames;
    b->looping = loop != 0;
    ndspChnWaveBufAdd(ch, b);
    g_chnSound[ch] = id;
}

void plat_sound_stop(int id) {
    if (!g_audioOk) return;
    for (int c = 0; c < CHANNELS; c++)
        if (id < 0 || g_chnSound[c] == id) { ndspChnReset(c); g_chnSound[c] = -1; }
}

void plat_media_reset(void) {
    plat_sound_stop(-1);
    for (int i = 0; i < nimages; i++) { C3D_TexDelete(&images[i]->tex); free(images[i]); }
    for (int i = 0; i < nsounds; i++) linearFree(sounds[i].data);
    nimages = 0; nsounds = 0;
}
