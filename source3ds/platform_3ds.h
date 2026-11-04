#ifndef MS_PLATFORM_3DS_H
#define MS_PLATFORM_3DS_H

#include <3ds.h>
#include <citro2d.h>

#define LOG_LINES 64
#define LOG_COLS  52

void plat3ds_init(C3D_RenderTarget* top, C3D_RenderTarget* bot);
void plat3ds_exit(void);
void plat3ds_input(void);
u32 plat3ds_down(void);
u32 plat3ds_held(void);
u32 plat3ds_repeat(void);                       /* like down, repeating while held (hidSetRepeatParameters) */
void plat3ds_touch(int* x, int* y);
void plat3ds_begin_frame(u32 clearTop, u32 clearBottom);
u32 plat3ds_color(uint32_t rgb);
float plat3ds_text_width(const char* s, float scale);

/* terminal: while a canvas is set, both screens draw into it; plat3ds_draws counts drawing calls */
void plat3ds_set_canvas(C3D_RenderTarget* t);
extern int plat3ds_draws;

/* Console output kept for the screen. A line starting with LOG_INPUT or LOG_ERROR is drawn in colour. */
#define LOG_INPUT "\x01"
#define LOG_ERROR "\x02"
void log_clear(void);
int log_count(void);
const char* log_line(int i);
void log_draw(float x, float y0, int maxLines);

/* log file on the SD card (sdmc:/minisharp/log.txt), also gets Console output */
void flog_open(const char* path);
void flog_close(void);
void flog(const char* fmt, ...);

/* sounds (media_3ds.c): 0 = no DSP firmware, the game runs silent */
int plat3ds_audio_init(void);
void plat3ds_audio_exit(void);

#endif
