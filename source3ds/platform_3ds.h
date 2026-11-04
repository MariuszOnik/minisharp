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
void plat3ds_begin_frame(u32 clearTop, u32 clearBottom);
u32 plat3ds_color(uint32_t rgb);

void log_clear(void);
int log_count(void);
const char* log_line(int i);

/* log file on the SD card (sdmc:/minisharp/log.txt), also gets Console output */
void flog_open(const char* path);
void flog_close(void);
void flog(const char* fmt, ...);

/* sounds (media_3ds.c): 0 = no DSP firmware, the game runs silent */
int plat3ds_audio_init(void);
void plat3ds_audio_exit(void);

#endif
