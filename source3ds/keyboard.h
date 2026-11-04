/* MiniSharp - touch keyboard for the bottom screen (terminal, later the editor). */
#ifndef MS_KEYBOARD_H
#define MS_KEYBOARD_H

#define KBD_Y 80                        /* top edge; the keyboard fills the bottom screen below it */
#define KBD_BACKSPACE "\b"
#define KBD_ENTER "\n"

int kbd_init(void);                     /* 0 = no VRAM for the keyboard image */
void kbd_exit(void);                    /* after the GPU is done with the last frame */
void kbd_reset(void);
void kbd_set_enter_label(const char* s);   /* "Run" in the terminal, "Enter" in the editor */
/* call once per frame after plat3ds_input(): text to insert (or KBD_BACKSPACE / KBD_ENTER), or NULL.
 * Holding a key repeats it. */
const char* kbd_update(void);
void kbd_draw(void);                    /* on the bottom screen */

#endif
