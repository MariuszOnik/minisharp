/* MiniSharp - what a host (PC or 3DS) must provide to the built-in API. */
#ifndef MS_PLATFORM_H
#define MS_PLATFORM_H

#include <stdint.h>

/* Button bits used by Input.Held / Input.Pressed (Btn.A etc.) */
enum {
    MSB_A = 1 << 0, MSB_B = 1 << 1, MSB_X = 1 << 2, MSB_Y = 1 << 3,
    MSB_L = 1 << 4, MSB_R = 1 << 5, MSB_ZL = 1 << 6, MSB_ZR = 1 << 7,
    MSB_UP = 1 << 8, MSB_DOWN = 1 << 9, MSB_LEFT = 1 << 10, MSB_RIGHT = 1 << 11,
    MSB_START = 1 << 12, MSB_SELECT = 1 << 13
};

/* Screen sizes: top 400x240, bottom 320x240 */
void plat_print(const char* s);                 /* Console output, no newline added */
void plat_screen(int screen);                   /* 0 = top, 1 = bottom */
void plat_clear(uint32_t rgb);
void plat_rect(float x, float y, float w, float h, uint32_t rgb);
void plat_circle(float x, float y, float r, uint32_t rgb);
void plat_line(float x1, float y1, float x2, float y2, float width, uint32_t rgb);
void plat_text(float x, float y, const char* s, float scale, uint32_t rgb);
uint32_t plat_buttons_held(void);
uint32_t plat_buttons_pressed(void);
int plat_touch(int* x, int* y);                 /* returns 1 while touching */
float plat_stick_x(void);                       /* circle pad, -1..1 */
float plat_stick_y(void);
int plat_millis(void);
int plat_fps(void);                             /* measured frames per second */

/* Gfx.Background: the colour both screens are cleared with before every frame (like a camera in Unity).
 * Black when a program starts (vm_new). The 3DS host clears with it itself; PC and web call ms_clear_frame(). */
extern uint32_t ms_background;
void ms_clear_frame(void);

/* Images and sounds. Ids start at 0; loading the same path again gives the same id.
 * Everything is freed by plat_media_reset() when a program ends. */
int plat_image_load(const char* path, char* err, int errlen);          /* id, or -1 and err */
int plat_image_size(int id, int* w, int* h);                           /* 0 = no such image */
/* draws the part (sx, sy, sw, sh) of the image with its top-left corner at (x, y);
 * negative scale flips, the angle (degrees) turns it around its centre */
void plat_image_draw(int id, float x, float y, int sx, int sy, int sw, int sh, float scaleX, float scaleY, float angle);
int plat_sound_load(const char* path, char* err, int errlen);          /* id, or -1 and err */
int plat_sound_valid(int id);
void plat_sound_play(int id, float volume, int loop);
void plat_sound_stop(int id);                                          /* id < 0: every sound */
void plat_media_reset(void);

#endif
