/* MiniSharp - touch keyboard: three layers (letters, capitals, symbols for code), 10 keys a row. */
#include <3ds.h>
#include <citro2d.h>
#include <string.h>
#include "../src/platform.h"
#include "platform_3ds.h"
#include "keyboard.h"

#define ROWS 5
#define KEY_W 32
#define KEY_H 32

/* keys separated by spaces; the special ones are written in capitals and SPACE is 3 keys wide */
static const char* LAYERS[3][ROWS] = {
    { "1 2 3 4 5 6 7 8 9 0", "q w e r t y u i o p", "a s d f g h j k l ;",
      "SHIFT z x c v b n m . BKSP", "SYM ( ) \" SPACE = , ENTER" },
    { "! ? # $ % ^ & * - +", "Q W E R T Y U I O P", "A S D F G H J K L :",
      "SHIFT Z X C V B N M _ BKSP", "SYM < > ' SPACE / | ENTER" },
    { "+ - * / % = < > ! ?", "( ) [ ] { } & | ^ ~", "\" ' ; : , . _ # @ \\",
      "+= == != <= >= && || ++ -- BKSP", "ABC ( ) \" SPACE = , ENTER" },
};

enum { SHIFT_OFF, SHIFT_ONCE, SHIFT_LOCK };
static int g_layer, g_shift;            /* g_layer: 0 letters, 2 symbols; capitals come from g_shift */
static int g_pressed = -1;              /* key index (row * 16 + n) under the finger */
static u64 g_pressTime, g_lastRepeat;

typedef struct { char word[8]; int x, w; } Key;

static int parse_row(const char* row, Key* keys) {
    int n = 0, x = 0;
    while (*row) {
        while (*row == ' ') row++;
        if (!*row) break;
        int len = 0;
        while (row[len] && row[len] != ' ') len++;
        Key* k = &keys[n++];
        if (len > 7) len = 7;
        memcpy(k->word, row, len); k->word[len] = 0;
        k->w = !strcmp(k->word, "SPACE") ? 3 * KEY_W : KEY_W;
        k->x = x; x += k->w;
        row += len;
    }
    return n;
}

static int cur_layer(void) { return g_layer == 2 ? 2 : g_shift ? 1 : 0; }

static int is_special(const char* w) { return strlen(w) > 2 && w[0] >= 'A' && w[0] <= 'Z' && w[1] >= 'A' && w[1] <= 'Z'; }

static const char* label(const char* w) {
    if (!strcmp(w, "SHIFT")) return "Shift";
    if (!strcmp(w, "BKSP")) return "Del";
    if (!strcmp(w, "SYM")) return "#+=";
    if (!strcmp(w, "ABC")) return "abc";
    if (!strcmp(w, "SPACE")) return "space";
    if (!strcmp(w, "ENTER")) return "Run";
    return w;
}

void kbd_reset(void) { g_layer = 0; g_shift = SHIFT_OFF; g_pressed = -1; }

/* what pressing the key does; NULL for keys that only switch layers */
static const char* press(const Key* k) {
    static char out[8];
    const char* w = k->word;
    if (!strcmp(w, "SHIFT")) { g_shift = (g_shift + 1) % 3; return NULL; }
    if (!strcmp(w, "SYM")) { g_layer = 2; return NULL; }
    if (!strcmp(w, "ABC")) { g_layer = 0; return NULL; }
    if (!strcmp(w, "BKSP")) return KBD_BACKSPACE;
    if (!strcmp(w, "ENTER")) return KBD_ENTER;
    if (!strcmp(w, "SPACE")) return " ";
    strcpy(out, w);
    if (g_shift == SHIFT_ONCE && g_layer != 2) g_shift = SHIFT_OFF;
    return out;
}

static int key_at(int px, int py, Key* key) {
    if (py < KBD_Y) return -1;
    int row = (py - KBD_Y) / KEY_H;
    if (row >= ROWS) return -1;
    Key keys[16];
    int n = parse_row(LAYERS[cur_layer()][row], keys);
    for (int i = 0; i < n; i++)
        if (px >= keys[i].x && px < keys[i].x + keys[i].w) { *key = keys[i]; return row * 16 + i; }
    return -1;
}

const char* kbd_update(void) {
    int px, py;
    plat3ds_touch(&px, &py);
    u64 now = osGetTime();
    if (!(plat3ds_held() & KEY_TOUCH)) { g_pressed = -1; return NULL; }
    Key k;
    int idx = key_at(px, py, &k);
    if (plat3ds_down() & KEY_TOUCH) {
        g_pressed = idx;
        g_pressTime = g_lastRepeat = now;
        return idx >= 0 ? press(&k) : NULL;
    }
    /* held: repeat letters and Del (not the layer keys or Run) after a short wait */
    if (idx != g_pressed) { g_pressed = -1; return NULL; }
    if (idx < 0 || (is_special(k.word) && strcmp(k.word, "BKSP") && strcmp(k.word, "SPACE"))) return NULL;
    if (now - g_pressTime < 450 || now - g_lastRepeat < 60) return NULL;
    g_lastRepeat = now;
    return press(&k);
}

static void draw_key(const Key* k, float y, int pressed) {
    int special = is_special(k->word);
    uint32_t bg = special ? 0x34405A : 0x2A2E36;
    if (!strcmp(k->word, "ENTER")) bg = 0x2F6B3A;
    if (!strcmp(k->word, "SHIFT") && g_shift) bg = g_shift == SHIFT_LOCK ? 0xC08030 : 0x5078C0;
    if (pressed) bg = 0x6A90D8;
    plat_rect(k->x + 1, y + 1, k->w - 2, KEY_H - 2, bg);
    const char* s = label(k->word);
    float scale = special ? 0.45f : 0.6f;
    float tw = plat3ds_text_width(s, scale);
    plat_text(k->x + (k->w - tw) / 2, y + (special ? 9 : 6), s, scale, 0xFFFFFF);
}

/* The keys are drawn into a texture only when the layer or Shift changes; each frame then draws
 * that one image and the pressed key on top. */
static C3D_Tex g_tex;
static C3D_RenderTarget* g_target;
static int g_drawnState = -1;           /* cur_layer() * 3 + g_shift when the texture was drawn */

int kbd_init(void) {
    if (!C3D_TexInitVRAM(&g_tex, 512, 256, GPU_RGBA8)) return 0;
    C3D_TexSetFilter(&g_tex, GPU_NEAREST, GPU_NEAREST);
    g_target = C3D_RenderTargetCreateFromTex(&g_tex, GPU_TEXFACE_2D, 0, GPU_RB_DEPTH16);
    if (!g_target) { C3D_TexDelete(&g_tex); return 0; }
    g_drawnState = -1;
    return 1;
}

void kbd_exit(void) {
    if (!g_target) return;
    C3D_RenderTargetDelete(g_target);
    C3D_TexDelete(&g_tex);
    g_target = NULL;
}

void kbd_draw(void) {
    int layer = cur_layer(), state = layer * 3 + g_shift;
    Key keys[16];
    if (state != g_drawnState) {
        plat3ds_set_canvas(g_target);
        C2D_TargetClear(g_target, plat3ds_color(0x101216));
        C2D_SceneBegin(g_target);
        for (int row = 0; row < ROWS; row++) {
            int n = parse_row(LAYERS[layer][row], keys);
            for (int i = 0; i < n; i++) draw_key(&keys[i], row * KEY_H, 0);
        }
        plat3ds_set_canvas(NULL);
        plat_screen(1);
        g_drawnState = state;
    }
    static const Tex3DS_SubTexture sub = { 320, ROWS * KEY_H, 0.0f, 1.0f, 320 / 512.0f, 1.0f - ROWS * KEY_H / 256.0f };
    C2D_Image img = { &g_tex, &sub };
    C2D_DrawImageAt(img, 0, KBD_Y, 0.5f, NULL, 1.0f, 1.0f);
    if (g_pressed >= 0) {
        int row = g_pressed / 16, i = g_pressed % 16;
        if (row < ROWS && i < parse_row(LAYERS[layer][row], keys)) draw_key(&keys[i], KBD_Y + row * KEY_H, 1);
    }
}
