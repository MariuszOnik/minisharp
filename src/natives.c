/* MiniSharp - built-in classes: Console, Math, Random, Gfx, Input, Time, App,
 * plus methods on strings (".string") and int/float/char/string helpers. */
#include "ms.h"
#include "platform.h"
#include "media.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

static Value vint(int32_t i) { Value v; v.t = V_INT; v.as.i = i; return v; }
static Value vfloat(float f) { Value v; v.t = V_FLOAT; v.as.f = f; return v; }
static Value vbool(int b) { Value v; v.t = V_BOOL; v.as.i = b ? 1 : 0; return v; }
static Value vnone(void) { Value v; v.t = V_NULL; v.as.o = NULL; return v; }
static Value vchar(int c) { Value v; v.t = V_CHAR; v.as.i = c & 0xFFFF; return v; }

static uint32_t col(Value v) { return (uint32_t)v.as.i & 0xFFFFFFu; }
static const char* str(Value v) { return v.t == V_OBJ ? ((ObjString*)v.as.o)->chars : ""; }

/* ---- Console ---- */
static void print_value(VM* vm, Value v) {
    if (v.t == V_OBJ && v.as.o->type == O_STRING) { plat_print(((ObjString*)v.as.o)->chars); return; }
    char buf[96];
    ms_value_to_text(vm, v, buf, sizeof buf);
    plat_print(buf);
}
static Value n_writeline(VM* vm, Value* a) { print_value(vm, a[0]); plat_print("\n"); return vnone(); }
static Value n_writeline0(VM* vm, Value* a) { (void)vm; (void)a; plat_print("\n"); return vnone(); }
static Value n_write(VM* vm, Value* a) { print_value(vm, a[0]); return vnone(); }

/* ---- Math ---- */
static Value n_sin(VM* vm, Value* a) { (void)vm; return vfloat(sinf(a[0].as.f)); }
static Value n_cos(VM* vm, Value* a) { (void)vm; return vfloat(cosf(a[0].as.f)); }
static Value n_tan(VM* vm, Value* a) { (void)vm; return vfloat(tanf(a[0].as.f)); }
static Value n_atan2(VM* vm, Value* a) { (void)vm; return vfloat(atan2f(a[0].as.f, a[1].as.f)); }
static Value n_sqrt(VM* vm, Value* a) { (void)vm; return vfloat(sqrtf(a[0].as.f)); }
static Value n_pow(VM* vm, Value* a) { (void)vm; return vfloat(powf(a[0].as.f, a[1].as.f)); }
static Value n_floor(VM* vm, Value* a) { (void)vm; return vfloat(floorf(a[0].as.f)); }
static Value n_ceil(VM* vm, Value* a) { (void)vm; return vfloat(ceilf(a[0].as.f)); }
static Value n_round(VM* vm, Value* a) { (void)vm; return vfloat(rintf(a[0].as.f)); }   /* 2.5 -> 2, like .NET and Unity */
static Value n_asin(VM* vm, Value* a) { (void)vm; return vfloat(asinf(a[0].as.f)); }
static Value n_acos(VM* vm, Value* a) { (void)vm; return vfloat(acosf(a[0].as.f)); }
static Value n_atan(VM* vm, Value* a) { (void)vm; return vfloat(atanf(a[0].as.f)); }
static Value n_exp(VM* vm, Value* a) { (void)vm; return vfloat(expf(a[0].as.f)); }
static Value n_log(VM* vm, Value* a) { (void)vm; return vfloat(logf(a[0].as.f)); }
static Value n_log10(VM* vm, Value* a) { (void)vm; return vfloat(log10f(a[0].as.f)); }
static Value n_truncate(VM* vm, Value* a) { (void)vm; return vfloat(truncf(a[0].as.f)); }
static Value n_signi(VM* vm, Value* a) { (void)vm; return vint(a[0].as.i > 0 ? 1 : a[0].as.i < 0 ? -1 : 0); }
static Value n_signf(VM* vm, Value* a) { (void)vm; return vint(a[0].as.f > 0 ? 1 : a[0].as.f < 0 ? -1 : 0); }

/* ---- Mathf (Unity style) ---- */
static float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
static Value n_floori(VM* vm, Value* a) { (void)vm; return vint((int32_t)floorf(a[0].as.f)); }
static Value n_ceili(VM* vm, Value* a) { (void)vm; return vint((int32_t)ceilf(a[0].as.f)); }
static Value n_roundi(VM* vm, Value* a) { (void)vm; return vint((int32_t)rintf(a[0].as.f)); }
static Value n_mfsign(VM* vm, Value* a) { (void)vm; return vfloat(a[0].as.f >= 0 ? 1.0f : -1.0f); }   /* Unity: Sign(0) = 1 */
static Value n_clamp01(VM* vm, Value* a) { (void)vm; return vfloat(clamp01(a[0].as.f)); }
static Value n_lerp(VM* vm, Value* a) { (void)vm; return vfloat(a[0].as.f + (a[1].as.f - a[0].as.f) * clamp01(a[2].as.f)); }
static Value n_lerpu(VM* vm, Value* a) { (void)vm; return vfloat(a[0].as.f + (a[1].as.f - a[0].as.f) * a[2].as.f); }
static Value n_invlerp(VM* vm, Value* a) {
    (void)vm; float d = a[1].as.f - a[0].as.f;
    return vfloat(d != 0 ? clamp01((a[2].as.f - a[0].as.f) / d) : 0);
}
static Value n_movetowards(VM* vm, Value* a) {
    (void)vm; float cur = a[0].as.f, target = a[1].as.f, step = a[2].as.f;
    if (fabsf(target - cur) <= step) return vfloat(target);
    return vfloat(cur + (target > cur ? step : -step));
}
static Value n_smoothstep(VM* vm, Value* a) {
    (void)vm; float t = clamp01(a[2].as.f);
    t = -2.0f * t * t * t + 3.0f * t * t;
    return vfloat(a[1].as.f * t + a[0].as.f * (1.0f - t));
}
static Value n_repeat(VM* vm, Value* a) {
    (void)vm; float t = a[0].as.f, len = a[1].as.f;
    float r = t - floorf(t / len) * len;
    return vfloat(r < 0 ? 0 : r > len ? len : r);
}
static Value n_pingpong(VM* vm, Value* a) {
    (void)vm; float len = a[1].as.f, t = a[0].as.f - floorf(a[0].as.f / (len * 2)) * len * 2;
    return vfloat(len - fabsf(t - len));
}
static Value n_approx(VM* vm, Value* a) {
    (void)vm; float x = a[0].as.f, y = a[1].as.f, m = fmaxf(fabsf(x), fabsf(y));
    return vbool(fabsf(y - x) < fmaxf(1e-6f * m, 1.401298e-45f * 8)   /* same rule as Unity */);
}
static Value n_deltaangle(VM* vm, Value* a) {
    (void)vm; float d = a[1].as.f - a[0].as.f;
    d = d - floorf(d / 360.0f) * 360.0f;
    return vfloat(d > 180.0f ? d - 360.0f : d);
}
static Value n_absf(VM* vm, Value* a) { (void)vm; return vfloat(fabsf(a[0].as.f)); }
static Value n_absi(VM* vm, Value* a) { (void)vm; int32_t v = a[0].as.i; return vint(v < 0 ? (int32_t)(0u - (uint32_t)v) : v); }
static Value n_minf(VM* vm, Value* a) { (void)vm; return vfloat(a[0].as.f < a[1].as.f ? a[0].as.f : a[1].as.f); }
static Value n_maxf(VM* vm, Value* a) { (void)vm; return vfloat(a[0].as.f > a[1].as.f ? a[0].as.f : a[1].as.f); }
static Value n_mini(VM* vm, Value* a) { (void)vm; return vint(a[0].as.i < a[1].as.i ? a[0].as.i : a[1].as.i); }
static Value n_maxi(VM* vm, Value* a) { (void)vm; return vint(a[0].as.i > a[1].as.i ? a[0].as.i : a[1].as.i); }
static Value n_clampf(VM* vm, Value* a) {
    (void)vm; float v = a[0].as.f;
    if (v < a[1].as.f) v = a[1].as.f;
    if (v > a[2].as.f) v = a[2].as.f;
    return vfloat(v);
}
static Value n_clampi(VM* vm, Value* a) {
    (void)vm; int32_t v = a[0].as.i;
    if (v < a[1].as.i) v = a[1].as.i;
    if (v > a[2].as.i) v = a[2].as.i;
    return vint(v);
}

/* ---- Random (xorshift32) ---- */
static uint32_t rng_state = 0;
static uint32_t rng(void) {
    if (!rng_state) rng_state = (uint32_t)plat_millis() * 2654435761u | 1u;
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static Value n_rnext(VM* vm, Value* a) {
    if (a[0].as.i <= 0) { snprintf(vm->err, sizeof vm->err, "Random.Next: max must be greater than 0"); return vnone(); }
    return vint((int32_t)(rng() % (uint32_t)a[0].as.i));
}
static Value n_rrange(VM* vm, Value* a) {
    int32_t lo = a[0].as.i, hi = a[1].as.i;
    if (hi <= lo) { snprintf(vm->err, sizeof vm->err, "Random.Range: max must be greater than min"); return vnone(); }
    return vint(lo + (int32_t)(rng() % (uint32_t)(hi - lo)));
}
static Value n_rfloat(VM* vm, Value* a) { (void)vm; (void)a; return vfloat((rng() >> 8) / 16777216.0f); }
static Value n_rseed(VM* vm, Value* a) { (void)vm; rng_state = (uint32_t)a[0].as.i | 1u; return vnone(); }

/* ---- Gfx ---- */
static Value n_screen(VM* vm, Value* a) { (void)vm; plat_screen(a[0].as.i ? 1 : 0); return vnone(); }
static Value n_clear(VM* vm, Value* a) { (void)vm; plat_clear(col(a[0])); return vnone(); }
uint32_t ms_background;
void ms_clear_frame(void) { plat_screen(1); plat_clear(ms_background); plat_screen(0); plat_clear(ms_background); }
static Value n_background(VM* vm, Value* a) { (void)vm; ms_background = col(a[0]); return vnone(); }
static Value n_rect(VM* vm, Value* a) { (void)vm; plat_rect(a[0].as.f, a[1].as.f, a[2].as.f, a[3].as.f, col(a[4])); return vnone(); }
static Value n_circle(VM* vm, Value* a) { (void)vm; plat_circle(a[0].as.f, a[1].as.f, a[2].as.f, col(a[3])); return vnone(); }
static Value n_line(VM* vm, Value* a) { (void)vm; plat_line(a[0].as.f, a[1].as.f, a[2].as.f, a[3].as.f, 1.0f, col(a[4])); return vnone(); }
static Value n_text(VM* vm, Value* a) { (void)vm; plat_text(a[0].as.f, a[1].as.f, str(a[2]), 0.5f, col(a[3])); return vnone(); }
static Value n_texts(VM* vm, Value* a) { (void)vm; plat_text(a[0].as.f, a[1].as.f, str(a[2]), a[3].as.f, col(a[4])); return vnone(); }
static Value n_rgb(VM* vm, Value* a) {
    (void)vm;
    int r = a[0].as.i, g = a[1].as.i, b = a[2].as.i;
    r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
    return vint((r << 16) | (g << 8) | b);
}

/* ---- Input ---- */
static Value n_held(VM* vm, Value* a) { (void)vm; return vbool(plat_buttons_held() & (uint32_t)a[0].as.i); }
static Value n_pressed(VM* vm, Value* a) { (void)vm; return vbool(plat_buttons_pressed() & (uint32_t)a[0].as.i); }
static Value n_touching(VM* vm, Value* a) { (void)vm; (void)a; int x, y; return vbool(plat_touch(&x, &y)); }
static Value n_touchx(VM* vm, Value* a) { (void)vm; (void)a; int x = 0, y = 0; plat_touch(&x, &y); return vint(x); }
static Value n_touchy(VM* vm, Value* a) { (void)vm; (void)a; int x = 0, y = 0; plat_touch(&x, &y); return vint(y); }
static Value n_stickx(VM* vm, Value* a) { (void)vm; (void)a; return vfloat(plat_stick_x()); }
static Value n_sticky(VM* vm, Value* a) { (void)vm; (void)a; return vfloat(plat_stick_y()); }

/* ---- Time / Game ---- */
static Value n_millis(VM* vm, Value* a) { (void)vm; (void)a; return vint(plat_millis()); }
/* ---- strings (a[0] is the string itself; the compiler checked it for null) ---- */
static ObjString* sobj(Value v) { return (ObjString*)v.as.o; }
static Value make_or_fail(VM* vm, const char* s, int len) {
    Value v = vm_make_string(vm, s, len);
    if (v.t == V_NULL) snprintf(vm->err, sizeof vm->err, "out of memory");
    return v;
}
static int find_sub(ObjString* h, const char* n, int nl, int from) {
    for (int i = from; i + nl <= h->len; i++) if (!memcmp(h->chars + i, n, nl)) return i;
    return -1;
}
static Value n_substring2(VM* vm, Value* a) {
    ObjString* s = sobj(a[0]); int st = a[1].as.i, n = a[2].as.i;
    if (st < 0 || n < 0 || st + n > s->len) {
        snprintf(vm->err, sizeof vm->err, "Substring(%d, %d) is outside the string (length %d)", st, n, (int)s->len); return vnone();
    }
    return make_or_fail(vm, s->chars + st, n);
}
static Value n_substring1(VM* vm, Value* a) {
    ObjString* s = sobj(a[0]); int st = a[1].as.i;
    if (st < 0 || st > s->len) { snprintf(vm->err, sizeof vm->err, "Substring(%d) is outside the string (length %d)", st, (int)s->len); return vnone(); }
    return make_or_fail(vm, s->chars + st, s->len - st);
}
static Value n_indexof(VM* vm, Value* a) { (void)vm; return vint(find_sub(sobj(a[0]), str(a[1]), (int)strlen(str(a[1])), 0)); }
static Value n_indexofc(VM* vm, Value* a) { (void)vm; char ch = (char)a[1].as.i; return vint(find_sub(sobj(a[0]), &ch, 1, 0)); }
static Value n_contains(VM* vm, Value* a) { (void)vm; return vbool(find_sub(sobj(a[0]), str(a[1]), (int)strlen(str(a[1])), 0) >= 0); }
static Value n_startswith(VM* vm, Value* a) {
    (void)vm; ObjString* s = sobj(a[0]); int n = (int)strlen(str(a[1]));
    return vbool(n <= s->len && !memcmp(s->chars, str(a[1]), n));
}
static Value n_endswith(VM* vm, Value* a) {
    (void)vm; ObjString* s = sobj(a[0]); int n = (int)strlen(str(a[1]));
    return vbool(n <= s->len && !memcmp(s->chars + s->len - n, str(a[1]), n));
}
static Value change_case(VM* vm, Value* a, int upper) {
    ObjString* s = sobj(a[0]);
    Value r = make_or_fail(vm, s->chars, s->len);
    if (r.t == V_NULL) return r;
    ObjString* o = (ObjString*)r.as.o;
    for (int i = 0; i < o->len; i++) o->chars[i] = (char)(upper ? toupper((unsigned char)o->chars[i]) : tolower((unsigned char)o->chars[i]));
    return r;
}
static Value n_toupper(VM* vm, Value* a) { return change_case(vm, a, 1); }
static Value n_tolower(VM* vm, Value* a) { return change_case(vm, a, 0); }
static Value n_trim(VM* vm, Value* a) {
    ObjString* s = sobj(a[0]); int b = 0, e = s->len;
    while (b < e && isspace((unsigned char)s->chars[b])) b++;
    while (e > b && isspace((unsigned char)s->chars[e - 1])) e--;
    return make_or_fail(vm, s->chars + b, e - b);
}
static Value n_replace(VM* vm, Value* a) {
    ObjString* s = sobj(a[0]); const char* from = str(a[1]); const char* to = str(a[2]);
    int fl = (int)strlen(from), tl = (int)strlen(to);
    if (fl == 0) { snprintf(vm->err, sizeof vm->err, "Replace: the text to find cannot be empty"); return vnone(); }
    int count = 0;
    for (int i = find_sub(s, from, fl, 0); i >= 0; i = find_sub(s, from, fl, i + fl)) count++;
    int len = s->len + count * (tl - fl);
    char* buf = (char*)malloc((size_t)len + 1);
    if (!buf) { snprintf(vm->err, sizeof vm->err, "out of memory"); return vnone(); }
    int o = 0, i = 0;
    for (int at = find_sub(s, from, fl, 0); at >= 0; at = find_sub(s, from, fl, i)) {
        memcpy(buf + o, s->chars + i, at - i); o += at - i;
        memcpy(buf + o, to, tl); o += tl;
        i = at + fl;
    }
    memcpy(buf + o, s->chars + i, s->len - i);
    Value r = make_or_fail(vm, buf, len);
    free(buf);
    return r;
}
static Value n_split(VM* vm, Value* a) {
    ObjString* s = sobj(a[0]); char sep = (char)a[1].as.i;
    int n = 1;
    for (int i = 0; i < s->len; i++) if (s->chars[i] == sep) n++;
    Value arr = vm_make_array(vm, n);
    if (arr.t == V_NULL) { snprintf(vm->err, sizeof vm->err, "out of memory"); return arr; }
    vm->stack[vm->sp++] = arr;          /* keep the array alive while the parts are allocated */
    int k = 0, st = 0;
    for (int i = 0; i <= s->len; i++) {
        if (i < s->len && s->chars[i] != sep) continue;
        Value part = make_or_fail(vm, s->chars + st, i - st);
        if (part.t == V_NULL) break;
        ((ObjArray*)arr.as.o)->items[k++] = part;
        st = i + 1;
    }
    vm->sp--;
    return arr;
}
static Value n_isnullorempty(VM* vm, Value* a) { (void)vm; return vbool(a[0].t != V_OBJ || sobj(a[0])->len == 0); }

/* ---- int / float / char helpers ---- */
static Value n_intparse(VM* vm, Value* a) {
    const char* s = str(a[0]); char* end;
    long v = strtol(s, &end, 10);
    while (isspace((unsigned char)*end)) end++;
    if (!*s || *end) { snprintf(vm->err, sizeof vm->err, "int.Parse: '%.40s' is not a whole number", s); return vnone(); }
    return vint((int32_t)v);
}
static Value n_floatparse(VM* vm, Value* a) {
    const char* s = str(a[0]); char* end;
    float v = strtof(s, &end);
    while (isspace((unsigned char)*end)) end++;
    if (!*s || *end) { snprintf(vm->err, sizeof vm->err, "float.Parse: '%.40s' is not a number", s); return vnone(); }
    return vfloat(v);
}
static Value n_isdigit(VM* vm, Value* a) { (void)vm; return vbool(a[0].as.i < 128 && isdigit(a[0].as.i)); }
static Value n_isletter(VM* vm, Value* a) { (void)vm; return vbool(a[0].as.i < 128 && isalpha(a[0].as.i)); }
static Value n_iswhite(VM* vm, Value* a) { (void)vm; return vbool(a[0].as.i < 128 && isspace(a[0].as.i)); }
static Value n_ctoupper(VM* vm, Value* a) { (void)vm; return vchar(a[0].as.i < 128 ? toupper(a[0].as.i) : a[0].as.i); }
static Value n_ctolower(VM* vm, Value* a) { (void)vm; return vchar(a[0].as.i < 128 ? tolower(a[0].as.i) : a[0].as.i); }

/* ---- images and sounds ---- */
static int load_media(VM* vm, Value p, int isImage) {
    char path[256], err[160];
    ms_resolve_path(str(p), path, sizeof path);
    int id = isImage ? plat_image_load(path, err, sizeof err) : plat_sound_load(path, err, sizeof err);
    if (id < 0) snprintf(vm->err, sizeof vm->err, "%s: cannot load '%.80s': %.100s", isImage ? "Gfx.LoadImage" : "Audio.Load", str(p), err);
    return id;
}
static int image_ok(VM* vm, int id, int* w, int* h) {
    if (plat_image_size(id, w, h)) return 1;
    snprintf(vm->err, sizeof vm->err, "%d is not an image (use the number from Gfx.LoadImage)", id);
    return 0;
}
static Value n_loadimage(VM* vm, Value* a) { return vint(load_media(vm, a[0], 1)); }
static Value n_imagew(VM* vm, Value* a) { int w, h; return image_ok(vm, a[0].as.i, &w, &h) ? vint(w) : vnone(); }
static Value n_imageh(VM* vm, Value* a) { int w, h; return image_ok(vm, a[0].as.i, &w, &h) ? vint(h) : vnone(); }
static void draw_part(VM* vm, Value* a, int sx, int sy, int sw, int sh, float scx, float scy, float ang) {
    int w, h;
    if (!image_ok(vm, a[0].as.i, &w, &h)) return;
    if (sw <= 0 || sh <= 0 || sx < 0 || sy < 0 || sx + sw > w || sy + sh > h) {
        snprintf(vm->err, sizeof vm->err, "ImagePart(%d, %d, %d, %d) is outside the image (%dx%d)", sx, sy, sw, sh, w, h);
        return;
    }
    plat_image_draw(a[0].as.i, a[1].as.f, a[2].as.f, sx, sy, sw, sh, scx, scy, ang);
}
static Value n_image(VM* vm, Value* a) { int w, h; if (image_ok(vm, a[0].as.i, &w, &h)) draw_part(vm, a, 0, 0, w, h, 1, 1, 0); return vnone(); }
static Value n_images(VM* vm, Value* a) { int w, h; if (image_ok(vm, a[0].as.i, &w, &h)) draw_part(vm, a, 0, 0, w, h, a[3].as.f, a[3].as.f, 0); return vnone(); }
static Value n_imageex(VM* vm, Value* a) { int w, h; if (image_ok(vm, a[0].as.i, &w, &h)) draw_part(vm, a, 0, 0, w, h, a[3].as.f, a[4].as.f, a[5].as.f); return vnone(); }
static Value n_imagepart(VM* vm, Value* a) { draw_part(vm, a, a[3].as.i, a[4].as.i, a[5].as.i, a[6].as.i, 1, 1, 0); return vnone(); }
static Value n_imagepartex(VM* vm, Value* a) { draw_part(vm, a, a[3].as.i, a[4].as.i, a[5].as.i, a[6].as.i, a[7].as.f, a[8].as.f, a[9].as.f); return vnone(); }

static int sound_ok(VM* vm, int id) {
    if (plat_sound_valid(id)) return 1;
    snprintf(vm->err, sizeof vm->err, "%d is not a sound (use the number from Audio.Load)", id);
    return 0;
}
static Value n_soundload(VM* vm, Value* a) { return vint(load_media(vm, a[0], 0)); }
static Value n_play(VM* vm, Value* a) { if (sound_ok(vm, a[0].as.i)) plat_sound_play(a[0].as.i, 1.0f, 0); return vnone(); }
static Value n_playv(VM* vm, Value* a) { if (sound_ok(vm, a[0].as.i)) plat_sound_play(a[0].as.i, a[1].as.f, 0); return vnone(); }
static Value n_playloop(VM* vm, Value* a) { if (sound_ok(vm, a[0].as.i)) plat_sound_play(a[0].as.i, 1.0f, 1); return vnone(); }
static Value n_playloopv(VM* vm, Value* a) { if (sound_ok(vm, a[0].as.i)) plat_sound_play(a[0].as.i, a[1].as.f, 1); return vnone(); }
static Value n_soundstop(VM* vm, Value* a) { if (sound_ok(vm, a[0].as.i)) plat_sound_stop(a[0].as.i); return vnone(); }
static Value n_soundstopall(VM* vm, Value* a) { (void)vm; (void)a; plat_sound_stop(-1); return vnone(); }

static Value n_fps(VM* vm, Value* a) { (void)vm; (void)a; return vint(plat_fps()); }
static Value n_quit(VM* vm, Value* a) { (void)a; vm->quit = 1; return vnone(); }

#define F T_FLOAT
#define I T_INT
#define S T_STRING
#define B T_BOOL
#define A T_ANY
#define VD T_VOID
#define CH T_CHAR
#define SA T_STRARR

const NativeDef ms_natives[] = {
    {"Console", "WriteLine", 1, {A}, VD, n_writeline},
    {"Console", "WriteLine", 0, {0}, VD, n_writeline0},
    {"Console", "Write", 1, {A}, VD, n_write},

    {"Math", "Sin", 1, {F}, F, n_sin},
    {"Math", "Cos", 1, {F}, F, n_cos},
    {"Math", "Tan", 1, {F}, F, n_tan},
    {"Math", "Atan2", 2, {F, F}, F, n_atan2},
    {"Math", "Sqrt", 1, {F}, F, n_sqrt},
    {"Math", "Pow", 2, {F, F}, F, n_pow},
    {"Math", "Floor", 1, {F}, F, n_floor},
    {"Math", "Ceiling", 1, {F}, F, n_ceil},
    {"Math", "Round", 1, {F}, F, n_round},
    {"Math", "Abs", 1, {I}, I, n_absi},
    {"Math", "Abs", 1, {F}, F, n_absf},
    {"Math", "Min", 2, {I, I}, I, n_mini},
    {"Math", "Min", 2, {F, F}, F, n_minf},
    {"Math", "Max", 2, {I, I}, I, n_maxi},
    {"Math", "Max", 2, {F, F}, F, n_maxf},
    {"Math", "Clamp", 3, {I, I, I}, I, n_clampi},
    {"Math", "Clamp", 3, {F, F, F}, F, n_clampf},
    {"Math", "Asin", 1, {F}, F, n_asin},
    {"Math", "Acos", 1, {F}, F, n_acos},
    {"Math", "Atan", 1, {F}, F, n_atan},
    {"Math", "Exp", 1, {F}, F, n_exp},
    {"Math", "Log", 1, {F}, F, n_log},
    {"Math", "Log10", 1, {F}, F, n_log10},
    {"Math", "Truncate", 1, {F}, F, n_truncate},
    {"Math", "Sign", 1, {I}, I, n_signi},
    {"Math", "Sign", 1, {F}, I, n_signf},

    {"Mathf", "Sin", 1, {F}, F, n_sin},
    {"Mathf", "Cos", 1, {F}, F, n_cos},
    {"Mathf", "Tan", 1, {F}, F, n_tan},
    {"Mathf", "Asin", 1, {F}, F, n_asin},
    {"Mathf", "Acos", 1, {F}, F, n_acos},
    {"Mathf", "Atan", 1, {F}, F, n_atan},
    {"Mathf", "Atan2", 2, {F, F}, F, n_atan2},
    {"Mathf", "Sqrt", 1, {F}, F, n_sqrt},
    {"Mathf", "Pow", 2, {F, F}, F, n_pow},
    {"Mathf", "Exp", 1, {F}, F, n_exp},
    {"Mathf", "Log", 1, {F}, F, n_log},
    {"Mathf", "Log10", 1, {F}, F, n_log10},
    {"Mathf", "Floor", 1, {F}, F, n_floor},
    {"Mathf", "Ceil", 1, {F}, F, n_ceil},
    {"Mathf", "Round", 1, {F}, F, n_round},
    {"Mathf", "FloorToInt", 1, {F}, I, n_floori},
    {"Mathf", "CeilToInt", 1, {F}, I, n_ceili},
    {"Mathf", "RoundToInt", 1, {F}, I, n_roundi},
    {"Mathf", "Abs", 1, {I}, I, n_absi},
    {"Mathf", "Abs", 1, {F}, F, n_absf},
    {"Mathf", "Min", 2, {I, I}, I, n_mini},
    {"Mathf", "Min", 2, {F, F}, F, n_minf},
    {"Mathf", "Max", 2, {I, I}, I, n_maxi},
    {"Mathf", "Max", 2, {F, F}, F, n_maxf},
    {"Mathf", "Clamp", 3, {I, I, I}, I, n_clampi},
    {"Mathf", "Clamp", 3, {F, F, F}, F, n_clampf},
    {"Mathf", "Clamp01", 1, {F}, F, n_clamp01},
    {"Mathf", "Sign", 1, {F}, F, n_mfsign},
    {"Mathf", "Lerp", 3, {F, F, F}, F, n_lerp},
    {"Mathf", "LerpUnclamped", 3, {F, F, F}, F, n_lerpu},
    {"Mathf", "InverseLerp", 3, {F, F, F}, F, n_invlerp},
    {"Mathf", "MoveTowards", 3, {F, F, F}, F, n_movetowards},
    {"Mathf", "SmoothStep", 3, {F, F, F}, F, n_smoothstep},
    {"Mathf", "Repeat", 2, {F, F}, F, n_repeat},
    {"Mathf", "PingPong", 2, {F, F}, F, n_pingpong},
    {"Mathf", "Approximately", 2, {F, F}, B, n_approx},
    {"Mathf", "DeltaAngle", 2, {F, F}, F, n_deltaangle},

    {"Random", "Next", 1, {I}, I, n_rnext},
    {"Random", "Range", 2, {I, I}, I, n_rrange},
    {"Random", "Float", 0, {0}, F, n_rfloat},
    {"Random", "Seed", 1, {I}, VD, n_rseed},

    {"Gfx", "Screen", 1, {I}, VD, n_screen},
    {"Gfx", "Clear", 1, {I}, VD, n_clear},
    {"Gfx", "Background", 1, {I}, VD, n_background},
    {"Gfx", "Rect", 5, {F, F, F, F, I}, VD, n_rect},
    {"Gfx", "Circle", 4, {F, F, F, I}, VD, n_circle},
    {"Gfx", "Line", 5, {F, F, F, F, I}, VD, n_line},
    {"Gfx", "Text", 4, {F, F, S, I}, VD, n_text},
    {"Gfx", "Text", 5, {F, F, S, F, I}, VD, n_texts},
    {"Gfx", "Rgb", 3, {I, I, I}, I, n_rgb},
    {"Gfx", "LoadImage", 1, {S}, I, n_loadimage},
    {"Gfx", "ImageWidth", 1, {I}, I, n_imagew},
    {"Gfx", "ImageHeight", 1, {I}, I, n_imageh},
    {"Gfx", "Image", 3, {I, F, F}, VD, n_image},
    {"Gfx", "Image", 4, {I, F, F, F}, VD, n_images},
    {"Gfx", "ImageEx", 6, {I, F, F, F, F, F}, VD, n_imageex},
    {"Gfx", "ImagePart", 7, {I, F, F, I, I, I, I}, VD, n_imagepart},
    {"Gfx", "ImagePartEx", 10, {I, F, F, I, I, I, I, F, F, F}, VD, n_imagepartex},

    {"Audio", "Load", 1, {S}, I, n_soundload},
    {"Audio", "Play", 1, {I}, VD, n_play},
    {"Audio", "Play", 2, {I, F}, VD, n_playv},
    {"Audio", "PlayLoop", 1, {I}, VD, n_playloop},
    {"Audio", "PlayLoop", 2, {I, F}, VD, n_playloopv},
    {"Audio", "Stop", 1, {I}, VD, n_soundstop},
    {"Audio", "StopAll", 0, {0}, VD, n_soundstopall},

    {"Input", "Held", 1, {I}, B, n_held},
    {"Input", "Pressed", 1, {I}, B, n_pressed},
    {"Input", "Touching", 0, {0}, B, n_touching},
    {"Input", "TouchX", 0, {0}, I, n_touchx},
    {"Input", "TouchY", 0, {0}, I, n_touchy},
    {"Input", "StickX", 0, {0}, F, n_stickx},
    {"Input", "StickY", 0, {0}, F, n_sticky},

    {"Time", "Millis", 0, {0}, I, n_millis},
    {"Time", "Fps", 0, {0}, I, n_fps},
    {"App", "Quit", 0, {0}, VD, n_quit},

    {".string", "Substring", 3, {S, I, I}, S, n_substring2},
    {".string", "Substring", 2, {S, I}, S, n_substring1},
    {".string", "IndexOf", 2, {S, S}, I, n_indexof},
    {".string", "IndexOf", 2, {S, CH}, I, n_indexofc},
    {".string", "Contains", 2, {S, S}, B, n_contains},
    {".string", "StartsWith", 2, {S, S}, B, n_startswith},
    {".string", "EndsWith", 2, {S, S}, B, n_endswith},
    {".string", "ToUpper", 1, {S}, S, n_toupper},
    {".string", "ToLower", 1, {S}, S, n_tolower},
    {".string", "Trim", 1, {S}, S, n_trim},
    {".string", "Replace", 3, {S, S, S}, S, n_replace},
    {".string", "Split", 2, {S, CH}, SA, n_split},
    {"string", "IsNullOrEmpty", 1, {S}, B, n_isnullorempty},

    {"int", "Parse", 1, {S}, I, n_intparse},
    {"float", "Parse", 1, {S}, F, n_floatparse},
    {"char", "IsDigit", 1, {CH}, B, n_isdigit},
    {"char", "IsLetter", 1, {CH}, B, n_isletter},
    {"char", "IsWhiteSpace", 1, {CH}, B, n_iswhite},
    {"char", "ToUpper", 1, {CH}, CH, n_ctoupper},
    {"char", "ToLower", 1, {CH}, CH, n_ctolower},
};
const int ms_nnatives = (int)(sizeof ms_natives / sizeof ms_natives[0]);

const ConstDef ms_consts[] = {
    {"Btn", "A", 0, MSB_A, 0}, {"Btn", "B", 0, MSB_B, 0}, {"Btn", "X", 0, MSB_X, 0}, {"Btn", "Y", 0, MSB_Y, 0},
    {"Btn", "L", 0, MSB_L, 0}, {"Btn", "R", 0, MSB_R, 0}, {"Btn", "ZL", 0, MSB_ZL, 0}, {"Btn", "ZR", 0, MSB_ZR, 0},
    {"Btn", "Up", 0, MSB_UP, 0}, {"Btn", "Down", 0, MSB_DOWN, 0}, {"Btn", "Left", 0, MSB_LEFT, 0}, {"Btn", "Right", 0, MSB_RIGHT, 0},
    {"Btn", "Select", 0, MSB_SELECT, 0},   /* START is reserved: it returns to the menu */

    {"Color", "Black", 0, 0x000000, 0}, {"Color", "White", 0, 0xFFFFFF, 0}, {"Color", "Red", 0, 0xE53935, 0},
    {"Color", "Green", 0, 0x43A047, 0}, {"Color", "Blue", 0, 0x1E88E5, 0}, {"Color", "Yellow", 0, 0xFDD835, 0},
    {"Color", "Cyan", 0, 0x00ACC1, 0}, {"Color", "Magenta", 0, 0xD81B60, 0}, {"Color", "Orange", 0, 0xFB8C00, 0},
    {"Color", "Gray", 0, 0x808080, 0}, {"Color", "DarkGray", 0, 0x303030, 0},

    {"Screen", "Top", 0, 0, 0}, {"Screen", "Bottom", 0, 1, 0},
    {"Screen", "TopWidth", 0, 400, 0}, {"Screen", "BottomWidth", 0, 320, 0}, {"Screen", "Height", 0, 240, 0},

    {"Math", "PI", 1, 0, 3.14159265f}, {"Math", "E", 1, 0, 2.71828183f},
    {"Mathf", "PI", 1, 0, 3.14159265f}, {"Mathf", "Deg2Rad", 1, 0, 3.14159265f / 180.0f},
    {"Mathf", "Rad2Deg", 1, 0, 180.0f / 3.14159265f}, {"Mathf", "Epsilon", 1, 0, 1.401298e-45f},
    {"int", "MaxValue", 0, 2147483647, 0}, {"int", "MinValue", 0, (-2147483647 - 1), 0},
};
const int ms_nconsts = (int)(sizeof ms_consts / sizeof ms_consts[0]);
