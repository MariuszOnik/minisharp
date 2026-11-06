/* MiniSharp - graph mode of the code editor (see grapheditor.h and docs/graph-model.md).
   Touch: tap a node to select it, tap an input to type its value, tap a branch label (true/false/body/case)
   to choose where '+' inserts, drag a node to move it, drag the background to scroll.
   Keys: L/R zoom, circle pad scrolls, D-pad left/right: other method, up/down: previous/next statement,
   A: insert, X: delete, Y + tap on an input: open it as nodes / fold it into text, SELECT: save, B/START: back to code. */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/ast.h"
#include "../src/graph.h"
#include "../src/platform.h"
#include "platform_3ds.h"
#include "keyboard.h"
#include "ui.h"
#include "gview.h"
#include "grapheditor.h"

#define BAR_H 24
static const char* BAR[] = { "Kod", "+ Wstaw", "Usuń", "Cofnij", "Zapisz" };

static const GraphHost* H;
static Arena A;
static ProgramAST* P;
static Graph* G;
static GView V;
static char g_method[160];          /* "Class.Method" shown */
static char g_err[256];

static void status(uint32_t rgb, const char* msg) { H->status(rgb, msg); }

/* ---------- building ---------- */
static MethodDecl* method_at(int pos, ClassDecl** cls) {
    const char* src = H->text();
    MethodDecl* first = NULL; ClassDecl* firstCls = NULL;
    for (int c = 0; c < P->nclasses; c++)
        for (int m = 0; m < P->classes[c].nmethods; m++) {
            MethodDecl* md = &P->classes[c].methods[m];
            if (!md->body || !md->s0) continue;
            if (!first) { first = md; firstCls = &P->classes[c]; }
            if (pos >= md->s0 - src && pos < md->s1 - src) { *cls = &P->classes[c]; return md; }
        }
    *cls = firstCls;
    return first;
}

/* parses the file again and builds the graph of `method` ("Class.Method"; NULL: the one at the cursor) */
static int rebuild(const char* method) {
    char keepKey[48] = "";
    if (G && V.sel >= 0 && V.sel < G->nn) snprintf(keepKey, sizeof keepKey, "%s", G->n[V.sel].key);
    gv_detach(&V);
    gr_free(G); G = NULL;
    arena_free(&A);
    memset(&A, 0, sizeof A);
    g_err[0] = 0;
    jmp_buf jb;
    ParseCtx pc = { &A, &jb, g_err, sizeof g_err };
    P = NULL;
    if (!setjmp(jb)) { int n; Token* t = ms_lex(&pc, H->text(), &n); P = ms_parse(&pc, t, n); }
    if (!P) return 0;
    ClassDecl* cd = NULL; MethodDecl* md = NULL;
    if (method) {
        const char* dot = strchr(method, '.');
        for (int c = 0; c < P->nclasses && dot && !md; c++) {
            ClassDecl* k = &P->classes[c];
            if (k->nlen != dot - method || memcmp(k->name, method, (size_t)k->nlen)) continue;
            for (int m = 0; m < k->nmethods; m++)
                if ((int)strlen(dot + 1) == k->methods[m].nlen && !memcmp(k->methods[m].name, dot + 1, strlen(dot + 1))) { cd = k; md = &k->methods[m]; break; }
        }
    }
    if (!md) md = method_at(H->cursor(), &cd);
    if (!md) { snprintf(g_err, sizeof g_err, "Ten plik nie ma metod z ciałem."); return 0; }
    G = gr_build(P, H->text(), cd->name, cd->nlen, md->name, md->nlen);
    if (!G) { snprintf(g_err, sizeof g_err, "Za mało pamięci na graf."); return 0; }
    int same = !strcmp(g_method, G->name);
    snprintf(g_method, sizeof g_method, "%s", G->name);
    gv_attach(&V, G);
    V.sel = keepKey[0] && same ? gr_find(G, keepKey) : -1;
    if (V.sel < 0) V.selPort = 0;
    if (!same) gv_home(&V);
    return 1;
}

/* the code of the selected node goes under the cursor of the code view */
static void follow_selection(void) {
    if (!G) return;
    const char* s0 = NULL;
    if (V.sel >= 0) {
        GNode* n = &G->n[V.sel];
        s0 = n->kind == GN_FLOW ? n->st->s0 : n->kind == GN_VALUE ? n->ex->s0 : G->m->s0;
    } else s0 = G->m->s0;
    if (s0) H->set_cursor((int)(s0 - G->src));
}

static void select_key(const char* key) {
    int i = key && key[0] ? gr_find(G, key) : -1;
    if (i >= 0) { V.sel = i; V.selPort = 0; gv_show(&V, i); }
}

/* ---------- actions ---------- */
static void take(char* src) {                 /* new text of the file from an action */
    if (!src) return;
    H->replace(src);
    free(src);
}

static void apply(MsPatch* p) {
    char err[200] = "", key[48] = "";
    char* src = gr_apply(G, p, err, sizeof err, key, sizeof key);
    ms_patch_free(p);
    if (!src) { status(0xFF6060, err[0] ? err : "Ta zmiana psuje kod"); return; }
    take(src);
    rebuild(g_method);
    select_key(key);
}

static void edit_input(int node, int input) {
    GInput* in = &G->in[G->n[node].in0 + input];
    char buf[256] = "", title[64];
    if (in->s0 && in->s1) {                    /* the full text from the code, on one line */
        int n = 0;
        for (const char* s = in->s0; s < in->s1 && n < (int)sizeof buf - 1; s++) buf[n++] = (*s == '\n' || *s == '\r') ? ' ' : *s;
        buf[n] = 0;
    }
    snprintf(title, sizeof title, "%s: %s", G->n[node].title, in->label);
    if (!ui_prompt(title, "Nowa wartość tego wejścia (zwykły kod C#, np. x + 1 albo Mathf.Max(a, b)). "
                          "Zmienia się tylko ten kawałek kodu.", buf, sizeof buf)) return;
    MsPatch p = { 0, 0, NULL };
    char err[200];
    if (!gr_set_input(G, node, input, buf, &p, err, sizeof err)) { status(0xFF6060, err); return; }
    apply(&p);
}

static void toggle_fold(int node, int input) {
    GInput* in = &G->in[G->n[node].in0 + input];
    take(gr_fold(G, node, input, in->node < 0));
    rebuild(g_method);
}

static int flow_owner(int i) {                 /* a value node belongs to the statement it feeds */
    while (i > 0 && G->n[i].kind == GN_VALUE) i = G->n[i].from;
    return i < 0 ? 0 : i;
}

static void insert_stmt(void) {
    int node = V.sel >= 0 ? flow_owner(V.sel) : 0;
    int port = V.sel >= 0 && G->n[V.sel].kind != GN_VALUE ? V.selPort : 0;
    char where[96], buf[256] = "";
    if (port == 0) snprintf(where, sizeof where, node == 0 ? "na początku metody" : "po: %s", G->n[node].title);
    else { char pl[32]; gr_port_label(G, node, port, pl, sizeof pl); snprintf(where, sizeof where, "%s -> %s (na początku)", G->n[node].title, pl); }
    char hint[220];
    snprintf(hint, sizeof hint, "Nowa instrukcja %s. Np. x += 1, Gfx.Circle(x, y, 5, Color.Red) albo if (hp <= 0) { }. "
             "Średnik dopisze się sam.", where);
    if (!ui_prompt("Wstaw instrukcję", hint, buf, sizeof buf)) return;
    MsPatch p = { 0, 0, NULL };
    char err[200];
    if (!gr_insert(G, node, port, buf, &p, err, sizeof err)) { status(0xFF6060, err); return; }
    apply(&p);
}

static void delete_node(void) {
    if (V.sel < 0) { status(0xC0C8D0, "Najpierw dotknij węzeł do usunięcia"); return; }
    MsPatch p = { 0, 0, NULL };
    char err[200], q[160];
    if (G->n[V.sel].kind == GN_FLOW) {
        snprintf(q, sizeof q, "Usunąć \"%s\" razem z tym, co jest w środku? (Cofnij to przywróci.)", G->n[V.sel].title);
        if (ui_ask("Usunąć węzeł?", q, "Usuń", "Nie", 0) != 1) return;
    }
    if (!gr_delete(G, V.sel, &p, err, sizeof err)) { status(0xFF6060, err); return; }
    V.sel = -1;
    apply(&p);
}

/* moves a node and the value nodes feeding it (they are made after it, so one pass in order is enough) */
static void drag_node(int node, float dx, float dy) {
    static char moved[4096];
    if (G->nn > (int)sizeof moved) return;
    memset(moved, 0, (size_t)G->nn);
    moved[node] = 1;
    G->n[node].x += dx; G->n[node].y += dy;
    for (int i = node + 1; i < G->nn; i++)
        if (G->n[i].kind == GN_VALUE && moved[G->n[i].from]) { G->n[i].x += dx; G->n[i].y += dy; moved[i] = 1; }
}

static void step_selection(int dir) {          /* D-pad up/down: previous/next statement */
    if (!G) return;
    if (V.sel < 0) { V.sel = 0; return; }
    int i = V.sel;
    if (dir < 0) { if (G->n[i].from >= 0) i = flow_owner(G->n[i].from); }
    else for (int k = i + 1; k < G->nn; k++) if (G->n[k].kind == GN_FLOW) { i = k; break; }
    V.sel = i; V.selPort = 0;
    gv_show(&V, i);
}

static void other_method(int dir) {
    if (!P) return;
    static char names[256][80];                /* static: 20 kB is too much for the stack */
    int n = 0, cur = -1;
    for (int c = 0; c < P->nclasses; c++)
        for (int m = 0; m < P->classes[c].nmethods && n < 256; m++) {
            MethodDecl* md = &P->classes[c].methods[m];
            if (!md->body) continue;
            snprintf(names[n], sizeof names[n], "%.*s.%.*s", P->classes[c].nlen, P->classes[c].name, md->nlen, md->name);
            if (!strcmp(names[n], g_method)) cur = n;
            n++;
        }
    if (n < 2) return;
    cur = (cur + dir + n) % n;
    V.sel = -1;
    rebuild(names[cur]);
}

/* ---------- loop ---------- */
static void draw(void) {
    ui_frame_begin(0x0F1218, 0x181C24);
    follow_selection();
    H->draw_top();
    plat_screen(1);
    if (G) gv_draw(&V, 0);
    else ui_wrap(10, 40, 300, g_err[0] ? g_err : "Brak grafu", 0.5f, 0xFF9080, 0, 8);
    if (!G && g_err[0]) ui_wrap(10, 150, 300, "Popraw kod (B: wróć do kodu), potem znowu Graf.", 0.45f, 0xA0A8B0, 0, 3);
    for (int i = 0; i < 5; i++) ui_button(i * 64, 0, 64, BAR_H, BAR[i], i == 1 ? 0x2F6B3A : i == 2 ? 0x6B2F2F : 0x34405A);
    plat_rect(0, 228, 320, 12, 0x101418);
    char s[200];
    snprintf(s, sizeof s, "%s   A: wstaw  X: usuń  D-pad: metoda/węzeł  Y+dotyk: rozwiń", G ? g_method : "");
    plat_text(3, 228, s, 0.36f, 0x8090A0);
    ui_frame_end();
}

void graph_mode(const GraphHost* h) {
    H = h;
    gv_init(&V, 320, 240, BAR_H);
    g_method[0] = 0;
    memset(&A, 0, sizeof A);
    G = NULL; P = NULL;
    rebuild(NULL);
    if (G) {                                    /* start at the statement under the cursor */
        int pos = H->cursor(), best = -1;
        for (int i = 1; i < G->nn; i++)
            if (G->n[i].kind == GN_FLOW && G->n[i].st->s0 && G->n[i].st->s0 - G->src <= pos && G->n[i].st->s1 - G->src >= pos) best = i;
        if (best >= 0) { V.sel = best; gv_show(&V, best); }
    }
    int touching = 0, moved = 0, hit = -1, hitIn = -1, hitPort = -1, lx = 0, ly = 0, sx0 = 0, sy0 = 0;
    while (aptMainLoop()) {
        plat3ds_input();
        u32 k = plat3ds_down(), held = plat3ds_held();
        if (k & (KEY_START | KEY_B)) break;
        if (ui_tapped(0, 0, 64, BAR_H)) break;
        if (G) {
            if ((k & KEY_A) || ui_tapped(64, 0, 64, BAR_H)) insert_stmt();
            if ((k & KEY_X) || ui_tapped(128, 0, 64, BAR_H)) delete_node();
            if (k & KEY_DLEFT) other_method(-1);
            if (k & KEY_DRIGHT) other_method(1);
            if (k & KEY_DUP) step_selection(-1);
            if (k & KEY_DDOWN) step_selection(1);
        }
        if (ui_tapped(192, 0, 64, BAR_H)) { H->undo(); rebuild(g_method); }
        if ((k & KEY_SELECT) || ui_tapped(256, 0, 64, BAR_H)) { if (H->save()) status(0x60E060, "Zapisano"); }

        if (held & KEY_R) V.zoom *= 1.03f;
        if (held & KEY_L) V.zoom /= 1.03f;
        if (V.zoom > 2.5f) V.zoom = 2.5f;
        if (V.zoom < 0.25f) V.zoom = 0.25f;
        circlePosition cp; hidCircleRead(&cp);
        float sp = 4 / V.zoom;
        if (abs(cp.dx) > 24) V.camx += (cp.dx - (cp.dx > 0 ? 24 : -24)) / 132.0f * sp * 1.5f;
        if (abs(cp.dy) > 24) V.camy -= (cp.dy - (cp.dy > 0 ? 24 : -24)) / 132.0f * sp * 1.5f;

        /* touch: tap = select / edit, drag on a node = move it, drag elsewhere = scroll */
        int px, py;
        plat3ds_touch(&px, &py);
        if ((k & KEY_TOUCH) && py >= BAR_H && G) {
            touching = 1; moved = 0; sx0 = lx = px; sy0 = ly = py;
            hit = gv_hit(&V, px, py, &hitIn, &hitPort);
        } else if (touching && (held & KEY_TOUCH)) {
            if (abs(px - sx0) + abs(py - sy0) > 6) moved = 1;
            if (moved) {
                float dx = (px - lx) / V.zoom, dy = (py - ly) / V.zoom;
                if (hit >= 0) drag_node(hit, dx, dy);
                else { V.camx -= dx; V.camy -= dy; }
            }
            lx = px; ly = py;
        } else if (touching) {                  /* released */
            touching = 0;
            if (moved && hit >= 0) { take(gr_save_layout(G)); rebuild(g_method); }
            else if (!moved) {
                if (hit < 0) V.sel = -1;
                else {
                    V.sel = hit;
                    V.selPort = hitPort > 0 ? hitPort : 0;
                    if (hitIn >= 0) {
                        if (held & KEY_Y) toggle_fold(hit, hitIn);
                        else edit_input(hit, hitIn);
                        kbd_reset();
                    }
                }
            }
        }
        draw();
    }
    follow_selection();
    gv_detach(&V);
    gr_free(G); G = NULL;
    arena_free(&A);
    P = NULL;
}
