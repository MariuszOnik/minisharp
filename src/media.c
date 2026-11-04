/* MiniSharp - image decoding (stb_image) and WAV loading, shared by the PC and 3DS hosts. */
#include "media.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "external/stb_image.h"

static char g_base[160] = ".";

void ms_set_base_dir(const char* dir) { snprintf(g_base, sizeof g_base, "%s", dir && *dir ? dir : "."); }

void ms_resolve_path(const char* path, char* out, int outlen) {
    /* absolute: "sdmc:/...", "C:\...", "/..." */
    if (strchr(path, ':') || path[0] == '/' || path[0] == '\\') snprintf(out, outlen, "%s", path);
    else snprintf(out, outlen, "%s/%s", g_base, path);
}

unsigned char* ms_image_decode(const char* path, int* w, int* h, char* err, int errlen) {
    int ch;
    FILE* f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "no such file: %s", path); return NULL; }
    fclose(f);
    unsigned char* px = stbi_load(path, w, h, &ch, 4);
    if (!px) snprintf(err, errlen, "%s", stbi_failure_reason() ? stbi_failure_reason() : "cannot read the file");
    return px;
}

void ms_image_free(unsigned char* pixels) { stbi_image_free(pixels); }

static uint32_t rd32(const unsigned char* p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }

int16_t* ms_wav_load(const char* path, int* rate, int* channels, int* frames, char* err, int errlen) {
    FILE* f = fopen(path, "rb");
    if (!f) { snprintf(err, errlen, "no such file: %s", path); return NULL; }
    unsigned char hdr[12], ck[8];
    int fmtOk = 0, bits = 0;
    int16_t* out = NULL;
    if (fread(hdr, 1, 12, f) != 12 || memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4)) {
        snprintf(err, errlen, "not a WAV file");
        goto done;
    }
    while (fread(ck, 1, 8, f) == 8) {
        uint32_t size = rd32(ck + 4);
        if (!memcmp(ck, "fmt ", 4)) {
            unsigned char fmt[16];
            if (size < 16 || fread(fmt, 1, 16, f) != 16) { snprintf(err, errlen, "broken WAV header"); goto done; }
            if (rd16(fmt) != 1) { snprintf(err, errlen, "only PCM WAV is supported (save it as 16-bit PCM)"); goto done; }
            *channels = rd16(fmt + 2); *rate = (int)rd32(fmt + 4); bits = rd16(fmt + 14);
            if (*channels < 1 || *channels > 2) { snprintf(err, errlen, "only mono or stereo WAV is supported"); goto done; }
            if (bits != 8 && bits != 16) { snprintf(err, errlen, "only 8 or 16-bit WAV is supported"); goto done; }
            fmtOk = 1;
            fseek(f, (long)(size - 16 + (size & 1)), SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) {
            if (!fmtOk) { snprintf(err, errlen, "broken WAV: data before format"); goto done; }
            int bytesPer = bits / 8 * *channels;
            *frames = (int)(size / (uint32_t)bytesPer);
            size_t n = (size_t)*frames * (size_t)*channels;
            out = (int16_t*)malloc(n * 2 + 2);
            unsigned char* raw = (unsigned char*)malloc((size_t)size + 1);
            if (!out || !raw) { free(out); free(raw); out = NULL; snprintf(err, errlen, "out of memory"); goto done; }
            size_t got = fread(raw, 1, size, f);
            *frames = (int)(got / (size_t)bytesPer);
            n = (size_t)*frames * (size_t)*channels;
            for (size_t i = 0; i < n; i++)
                out[i] = bits == 16 ? (int16_t)rd16(raw + i * 2) : (int16_t)((raw[i] - 128) << 8);   /* 8-bit WAV is unsigned */
            free(raw);
            goto done;
        } else fseek(f, (long)(size + (size & 1)), SEEK_CUR);
    }
    snprintf(err, errlen, "WAV has no sound data");
done:
    fclose(f);
    return out;
}
