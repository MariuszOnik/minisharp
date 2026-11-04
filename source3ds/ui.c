/* MiniSharp - small UI pieces shared by the editor, its Help and dialogs. */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <string.h>
#include "../src/platform.h"
#include "platform_3ds.h"
#include "keyboard.h"
#include "ui.h"

void ui_frame_begin(uint32_t topBg, uint32_t bottomBg) {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    plat3ds_begin_frame(plat3ds_color(topBg), plat3ds_color(bottomBg));
}
void ui_frame_end(void) { C3D_FrameEnd(0); }

void ui_button(float x, float y, float w, float h, const char* label, uint32_t bg) {
    plat_rect(x + 1, y + 1, w - 2, h - 2, bg);
    float tw = plat3ds_text_width(label, 0.5f);
    plat_text(x + (w - tw) / 2, y + (h - 15) / 2, label, 0.5f, 0xFFFFFF);
}

int ui_tapped(float x, float y, float w, float h) {
    if (!(plat3ds_down() & KEY_TOUCH)) return 0;
    int px, py;
    plat3ds_touch(&px, &py);
    return px >= x && px < x + w && py >= y && py < y + h;
}

int ui_wrap(float x, float y, float w, const char* s, float scale, uint32_t rgb, int skip, int maxLines) {
    char line[256];
    int n = 0;
    float lh = 30 * scale;
    while (*s) {
        int len = 0, lastSpace = -1;
        while (s[len] && s[len] != '\n' && len < (int)sizeof line - 8) {
            int clen = 1;                                   /* whole UTF-8 characters only */
            while ((s[len + clen] & 0xC0) == 0x80) clen++;
            memcpy(line, s, len + clen); line[len + clen] = 0;
            if (plat3ds_text_width(line, scale) > w) break;
            if (s[len] == ' ') lastSpace = len;
            len += clen;
        }
        if (s[len] && s[len] != '\n' && lastSpace > 0) len = lastSpace;     /* break after a word */
        if (len == 0 && s[0] && s[0] != '\n') { len = 1; while ((s[len] & 0xC0) == 0x80) len++; }
        memcpy(line, s, len); line[len] = 0;
        if (n >= skip && n - skip < maxLines) plat_text(x, y + (n - skip) * lh, line, scale, rgb);
        n++;
        s += len;
        if (*s == ' ' || *s == '\n') s++;
    }
    return n;
}

int ui_ask(const char* title, const char* text, const char* yes, const char* no, int allowCancel) {
    while (aptMainLoop()) {
        plat3ds_input();
        u32 k = plat3ds_down();
        if ((k & KEY_A) || ui_tapped(20, 150, 130, 40)) return 1;
        if ((k & KEY_B) || ui_tapped(170, 150, 130, 40)) return 0;
        if (k & KEY_START) return allowCancel ? -1 : 0;
        ui_frame_begin(0x101418, 0x0C0C0C);
        plat_text(10, 10, title, 0.7f, 0x7FB2FF);
        ui_wrap(10, 44, 380, text, 0.5f, 0xFFFFFF, 0, 10);
        plat_screen(1);
        ui_button(20, 150, 130, 40, yes, 0x2F6B3A);
        ui_button(170, 150, 130, 40, no, 0x6B2F2F);
        if (allowCancel) plat_text(10, 210, "START: wróć", 0.5f, 0x8090A0);
        ui_frame_end();
    }
    return 0;
}

int ui_prompt(const char* title, const char* hint, char* buf, int cap) {
    int len = (int)strlen(buf);
    kbd_reset();
    kbd_set_enter_label("OK");
    while (aptMainLoop()) {
        plat3ds_input();
        u32 rep = plat3ds_repeat(), k = plat3ds_down();
        if (k & KEY_START) return 0;
        const char* t = kbd_update();
        if ((k & KEY_A) || (t && !strcmp(t, KBD_ENTER))) { if (len) return 1; }
        else if ((rep & KEY_B) || (t && !strcmp(t, KBD_BACKSPACE))) { if (len) buf[--len] = 0; }
        else if (t && len + (int)strlen(t) < cap) { strcpy(buf + len, t); len += (int)strlen(t); }
        ui_frame_begin(0x101418, 0x0C0C0C);
        plat_text(10, 10, title, 0.7f, 0x7FB2FF);
        ui_wrap(10, 44, 380, hint, 0.5f, 0xC0C8D0, 0, 8);
        plat_text(10, 210, "A / OK: gotowe   B: usuń   START: anuluj", 0.5f, 0x8090A0);
        plat_screen(1);
        plat_rect(0, 30, 320, 26, 0x1C2230);
        plat_text(8, 34, buf, 0.55f, 0xFFFFFF);
        if ((osGetTime() / 500) % 2 == 0) plat_rect(8 + plat3ds_text_width(buf, 0.55f), 34, 1.5f, 18, 0xFFFFFF);
        kbd_draw();
        ui_frame_end();
    }
    return 0;
}
