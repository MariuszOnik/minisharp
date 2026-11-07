/* MiniSharp - Help in the editor: language topics (sdk/Help.txt) and the API (sdk/MiniSharp.Api.cs),
 * both built into help_data.h by tools/gen_help.py. List on the bottom screen, details on the top. */
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "../src/platform.h"
#include "platform_3ds.h"
#include "ui.h"
#include "help.h"
#include "help_data.h"

#define NGROUPS ((int)(sizeof ms_help_groups / sizeof ms_help_groups[0]))
#define NHELP ((int)(sizeof ms_help / sizeof ms_help[0]))
#define ROW_H 24
#define LIST_Y 32
#define ROWS 8

static int nameq(const char* a, const char* b, int blen) { return (int)strlen(a) == blen && !memcmp(a, b, blen); }

const void* help_api_entries(int* count) { *count = (int)(sizeof ms_help / sizeof ms_help[0]); return ms_help; }

const char* help_signature(const char* cls, int clen, const char* name, int nlen, int* overloads) {
    const char* sig = NULL;
    *overloads = 0;
    for (int i = 0; i < NHELP; i++) {
        const HelpEntry* e = &ms_help[i];
        if (!nameq(e->name, name, nlen)) continue;
        /* StartCoroutine etc. are written without a class */
        if (clen ? !nameq(e->group, cls, clen) : strcmp(e->group, "Coroutines")) continue;
        if (!sig) sig = e->sig;
        (*overloads)++;
    }
    return sig;
}

/* entries of group g: their indexes in ms_help */
static int members(int g, int* idx) {
    int n = 0;
    for (int i = 0; i < NHELP; i++) if (!strcmp(ms_help[i].group, ms_help_groups[g].name)) idx[n++] = i;
    return n;
}

const char* help_browse(const char* word) {
    static int idx[NHELP];
    int level = 0, group = 0, sel = 0, scroll = 0, nidx = 0, textScroll = 0;
    if (word && *word) {                                /* "Gfx.Circle" or "Gfx" */
        const char* dot = strchr(word, '.');
        int clen = dot ? (int)(dot - word) : (int)strlen(word);
        for (int g = 0; g < NGROUPS; g++)
            if (nameq(ms_help_groups[g].name, word, clen)) {
                group = g; level = 1; nidx = members(g, idx);
                for (int i = 0; dot && i < nidx; i++) if (!strcmp(ms_help[idx[i]].name, dot + 1)) { sel = i; break; }
            }
        if (!level) for (int g = 0; g < NGROUPS; g++) if (nameq(ms_help_groups[g].name, word, clen)) group = g;
        if (!level) sel = group;
    }

    while (aptMainLoop()) {
        plat3ds_input();
        u32 k = plat3ds_down(), rep = plat3ds_repeat();
        int count = level == 0 ? NGROUPS : nidx;
        int topicOnly = level == 1 && nidx == 0;        /* a language topic: just its text */
        int open = 0;

        if ((k & KEY_START) || ((k & KEY_B) && level == 0) || ui_tapped(250, 0, 70, 28)) {
            if (level == 0 || (k & KEY_START)) return NULL;
            level = 0; sel = group; textScroll = 0; continue;
        }
        if (k & KEY_B) { level = 0; sel = group; textScroll = 0; continue; }
        if (topicOnly) {
            if (rep & KEY_DOWN) textScroll++;
            if ((rep & KEY_UP) && textScroll > 0) textScroll--;
        } else {
            if ((rep & KEY_DOWN) && sel < count - 1) sel++;
            if ((rep & KEY_UP) && sel > 0) sel--;
            if (rep & KEY_R) { sel += ROWS; if (sel >= count) sel = count - 1; }
            if (rep & KEY_L) { sel -= ROWS; if (sel < 0) sel = 0; }
            if (k & KEY_A) open = 1;
            for (int r = 0; r < ROWS && scroll + r < count; r++)
                if (ui_tapped(0, LIST_Y + r * ROW_H, 320, ROW_H)) { if (sel == scroll + r) open = 1; sel = scroll + r; }
            if (ui_tapped(0, 226, 160, 14) && scroll > 0) { scroll -= ROWS; sel = scroll; }
            if (ui_tapped(160, 226, 160, 14) && scroll + ROWS < count) { scroll += ROWS; sel = scroll; }
        }
        if (open) {
            if (level == 0) { group = sel; level = 1; nidx = members(group, idx); sel = 0; scroll = 0; textScroll = 0; continue; }
            return ms_help[idx[sel]].insert;
        }
        if (sel < scroll) scroll = sel;
        if (sel >= scroll + ROWS) scroll = sel - ROWS + 1;

        ui_frame_begin(0x101418, 0x0C0C0C);
        /* top: details */
        const HelpGroup* G = &ms_help_groups[level == 0 ? sel : group];
        plat_rect(0, 0, 400, 22, 0x1C2230);
        plat_text(8, 2, G->name, 0.6f, G->api ? 0x4EC9B0 : 0xF0D080);
        if (topicOnly) {
            int lines = ui_wrap(8, 28, 384, G->doc, 0.5f, 0xE0E0E0, textScroll, 13);
            if (textScroll >= lines) textScroll = lines ? lines - 1 : 0;
        } else if (level == 0 || nidx == 0) {
            ui_wrap(8, 28, 384, G->doc, 0.5f, 0xE0E0E0, 0, 13);
        } else {
            const HelpEntry* e = &ms_help[idx[sel]];
            int n = ui_wrap(8, 30, 384, e->sig, 0.55f, 0xFFFFFF, 0, 4);
            float y = 30 + n * 16.5f + 8;
            n = ui_wrap(8, y, 384, *e->doc ? e->doc : "(brak opisu)", 0.5f, 0xD0D0D0, 0, 6);
            y += n * 15 + 10;
            ui_wrap(8, y, 384, G->doc, 0.45f, 0x8090A0, 0, 4);
            plat_text(8, 222, "A: wstaw do kodu", 0.45f, 0x60E060);
        }
        /* bottom: the list */
        plat_screen(1);
        plat_rect(0, 0, 320, 28, 0x1C2230);
        plat_text(8, 6, level == 0 ? "Help" : G->name, 0.55f, 0x7FB2FF);
        ui_button(250, 0, 70, 28, level == 0 ? "Zamknij" : "Wróć", 0x34405A);
        if (topicOnly) {
            plat_text(8, 40, "Góra/Dół: przewijanie tekstu", 0.5f, 0xC0C8D0);
            plat_text(8, 58, "B: wróć do listy", 0.5f, 0xC0C8D0);
        } else {
            for (int r = 0; r < ROWS && scroll + r < count; r++) {
                int i = scroll + r;
                float y = LIST_Y + r * ROW_H;
                if (i == sel) plat_rect(0, y, 320, ROW_H, 0x2D4A80);
                const char* label = level == 0 ? ms_help_groups[i].name : ms_help[idx[i]].sig;
                uint32_t c = level == 0 ? (ms_help_groups[i].api ? 0x4EC9B0 : 0xF0D080) : 0xE0E0E0;
                plat_text(8, y + 4, label, 0.5f, c);
            }
            char pg[48];
            snprintf(pg, sizeof pg, "%d / %d", sel + 1, count);
            plat_text(140, 226, pg, 0.4f, 0x8090A0);
            if (scroll > 0) plat_text(8, 226, "< poprzednie", 0.4f, 0x8090A0);
            if (scroll + ROWS < count) plat_text(250, 226, "następne >", 0.4f, 0x8090A0);
        }
        ui_frame_end();
    }
    return NULL;
}
