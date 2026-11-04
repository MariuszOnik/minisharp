/* MiniSharp - small UI pieces shared by the editor, its Help and dialogs. */
#ifndef MS_UI_H
#define MS_UI_H

#include <stdint.h>

void ui_frame_begin(uint32_t topBg, uint32_t bottomBg);    /* C3D_FrameBegin + clear; top screen selected */
void ui_frame_end(void);

void ui_button(float x, float y, float w, float h, const char* label, uint32_t bg);
int ui_tapped(float x, float y, float w, float h);         /* touch started inside this frame */

/* draws text wrapped to width w (skipping the first 'skip' lines); returns how many lines it has */
int ui_wrap(float x, float y, float w, const char* s, float scale, uint32_t rgb, int skip, int maxLines);

/* A: 1, B: 0, START: -1 (if allowCancel, else START = B) */
int ui_ask(const char* title, const char* text, const char* yes, const char* no, int allowCancel);

/* one line typed on the touch keyboard into buf; 1 = OK, 0 = cancelled */
int ui_prompt(const char* title, const char* hint, char* buf, int cap);

#endif
