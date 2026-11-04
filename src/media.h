/* MiniSharp - loading images and sounds from files (shared by the PC and 3DS hosts). */
#ifndef MS_MEDIA_H
#define MS_MEDIA_H

#include <stdint.h>

/* Folder that relative paths in Gfx.LoadImage / Audio.Load start from (the program's folder). */
void ms_set_base_dir(const char* dir);
void ms_resolve_path(const char* path, char* out, int outlen);

/* PNG, JPG or BMP as RGBA bytes (4 per pixel, rows from the top); free with ms_image_free. NULL + err on failure. */
unsigned char* ms_image_decode(const char* path, int* w, int* h, char* err, int errlen);
void ms_image_free(unsigned char* pixels);

/* PCM WAV (8 or 16 bit, mono or stereo) as interleaved 16-bit samples; free with free(). NULL + err on failure. */
int16_t* ms_wav_load(const char* path, int* rate, int* channels, int* frames, char* err, int errlen);

#endif
