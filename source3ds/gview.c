/* MiniSharp - drawing a method graph (see gview.h). Measured on the console (docs/graph-model.md, section 7):
   the cost is the text, so it is parsed once per graph and not drawn at all below zoom 0.5. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/platform.h"
#include "platform_3ds.h"
#include "gview.h"

#define TEXT_SCALE 0.36f       /* text height about ROW at zoom 1 */
#define HEAD 13.0f
#define ROW 10.0f

void gv_init(GView* v, float sw, float sh, float top) {
    memset(v, 0, sizeof *v);
    v->sw = sw; v->sh = sh; v->top = top;
    v->zoom = 1.0f; v->text = 1; v->sel = -1;
}

void gv_detach(GView* v) {
    free(v->title); free(v->row); free(v->port); free(v->port0);
    v->title = v->row = v->port = NULL; v->port0 = NULL;
    if (v->buf) { C2D_TextBufDelete(v->buf); v->buf = NULL; }
    v->g = NULL;
}

static void parse_text(GView* v, C2D_Text* t, const char* s) {
    C2D_TextParse(t, v->buf, s);
    C2D_TextOptimize(t);
}

void gv_attach(GView* v, Graph* g) {
    gv_detach(v);
    v->g = g;
    if (!g) return;
    int glyphs = 64, nports = 0;
    char s[96], t[64];
    for (int n = 0; n < g->nn; n++) {
        glyphs += (int)strlen(g->n[n].title);
        if (g->n[n].kind == GN_FLOW) nports += g->n[n].nports - 1;
    }
    glyphs += g->nin * 50 + nports * 16;
    v->buf = C2D_TextBufNew(glyphs);
    v->title = (C2D_Text*)calloc((size_t)g->nn, sizeof(C2D_Text));
    v->row = (C2D_Text*)calloc((size_t)(g->nin ? g->nin : 1), sizeof(C2D_Text));
    v->port = (C2D_Text*)calloc((size_t)(nports ? nports : 1), sizeof(C2D_Text));
    v->port0 = (int*)calloc((size_t)g->nn, sizeof(int));
    int pi = 0;
    for (int n = 0; n < g->nn; n++) {
        GNode* x = &g->n[n];
        parse_text(v, &v->title[n], x->title);
        for (int k = 0; k < x->nin; k++) {
            GInput* in = &g->in[x->in0 + k];
            if (in->node >= 0) snprintf(s, sizeof s, "%s", in->label);
            else { gr_input_text(g, in, t, 28); snprintf(s, sizeof s, "%s: %s", in->label, t); }
            parse_text(v, &v->row[x->in0 + k], s);
        }
        v->port0[n] = pi;
        if (x->kind == GN_FLOW)
            for (int p = 1; p < x->nports; p++) { gr_port_label(g, n, p, t, sizeof t); parse_text(v, &v->port[pi++], t); }
    }
    v->minx = v->miny = 1e9f; v->maxx = v->maxy = -1e9f;
    for (int n = 0; n < g->nn; n++) {
        GNode* x = &g->n[n];
        if (x->x < v->minx) v->minx = x->x;
        if (x->y < v->miny) v->miny = x->y;
        if (x->x + x->w > v->maxx) v->maxx = x->x + x->w;
        if (x->y + x->h > v->maxy) v->maxy = x->y + x->h;
    }
    if (v->sel >= g->nn) v->sel = -1;
}

/* ---------- coordinates ---------- */
static float cy0(GView* v) { return v->top + (v->sh - v->top) / 2; }
static float sx(GView* v, float wx) { return (wx - v->camx) * v->zoom + v->sw / 2; }
static float sy(GView* v, float wy) { return (wy - v->camy) * v->zoom + cy0(v); }
void gv_to_world(GView* v, float x, float y, float* wx, float* wy) {
    *wx = (x - v->sw / 2) / v->zoom + v->camx;
    *wy = (y - cy0(v)) / v->zoom + v->camy;
}

void gv_home(GView* v) {
    if (!v->g) return;
    v->camx = v->minx + (v->sw / 2) / v->zoom - 8;
    v->camy = v->miny + ((v->sh - v->top) / 2) / v->zoom - 6;
}

void gv_show(GView* v, int node) {
    if (!v->g || node < 0 || node >= v->g->nn) return;
    GNode* n = &v->g->n[node];
    float x0 = sx(v, n->x), y0 = sy(v, n->y), x1 = sx(v, n->x + n->w), y1 = sy(v, n->y + n->h);
    if (x0 < 4 || x1 > v->sw - 4) v->camx = n->x + n->w / 2;
    if (y0 < v->top + 4 || y1 > v->sh - 4) v->camy = n->y + n->h / 2;
}

/* ---------- drawing ---------- */
static u32 C(uint32_t rgb) { return plat3ds_color(rgb); }
static void rect(GView* v, float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.5f, w, h, c); v->quads++; }
static void line(GView* v, float x1, float y1, float x2, float y2, float w, u32 c) { C2D_DrawLine(x1, y1, c, x2, y2, c, w, 0.5f); v->quads++; }
static void text(GView* v, C2D_Text* t, float x, float y, float sc, u32 c) {     /* every glyph is one quad */
    C2D_DrawText(t, C2D_WithColor, x, y, 0.5f, sc, sc, c);
    v->quads += (int)(t->end - t->begin);
}

static int off_screen(GView* v, float x0, float y0, float x1, float y1) {
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { float t = y0; y0 = y1; y1 = t; }
    return x1 < 0 || y1 < v->top || x0 > v->sw || y0 > v->sh;
}

static void port_pos(GNode* n, int port, float* x, float* y) {     /* world position of a flow output */
    if (port == 0) { *x = n->x + 6; *y = n->y + n->h; return; }
    *x = n->x + n->w * (float)port / (float)n->nports; *y = n->y + n->h;
}

static void draw_edges(GView* v, float oy) {
    Graph* g = v->g;
    float z = v->zoom;
    int segs = z >= 0.6f ? 8 : 1;
    for (int i = 1; i < g->nn; i++) {
        GNode* n = &g->n[i];
        GNode* f = &g->n[n->from];
        if (n->kind == GN_FLOW) {
            float px, py; port_pos(f, n->fromPort, &px, &py);
            float x0 = sx(v, px), y0 = sy(v, py + oy), x1 = sx(v, n->x + 6), y1 = sy(v, n->y + oy);
            if (off_screen(v, x0, y0, x1, y1)) continue;
            u32 c = C(n->fromPort == 0 ? 0xB0B8C8 : f->st && f->st->k == S_IF ? (n->fromPort == 1 ? 0x60D070 : 0xE06060) : 0xE0C060);
            float ym = y0 + 4 * z;
            line(v, x0, y0, x0, ym, 1.5f, c);
            line(v, x0, ym, x1, ym, 1.5f, c);
            line(v, x1, ym, x1, y1, 1.5f, c);
        } else if (n->kind == GN_VALUE) {
            float x0 = sx(v, n->x + n->w), y0 = sy(v, n->y + HEAD / 2 + oy);
            float x1 = sx(v, f->x), y1 = sy(v, f->y + HEAD + ROW * n->fromPort + ROW / 2 + oy);
            if (off_screen(v, x0, y0, x1, y1)) continue;
            u32 c = C(0x70A8E0);
            if (segs == 1) { line(v, x0, y0, x1, y1, 1.2f, c); continue; }
            float dx = (x1 - x0) * 0.5f, lx = x0, ly = y0;
            for (int s = 1; s <= segs; s++) {        /* cubic bezier: (x0,y0) (x0+dx,y0) (x1-dx,y1) (x1,y1) */
                float t = (float)s / segs, u = 1 - t;
                float bx = u * u * u * x0 + 3 * u * u * t * (x0 + dx) + 3 * u * t * t * (x1 - dx) + t * t * t * x1;
                float by = u * u * u * y0 + 3 * u * u * t * y0 + 3 * u * t * t * y1 + t * t * t * y1;
                line(v, lx, ly, bx, by, 1.2f, c);
                lx = bx; ly = by;
            }
        }
    }
}

static void draw_nodes(GView* v, float oy) {
    Graph* g = v->g;
    float z = v->zoom, ts = TEXT_SCALE * z;
    int withText = v->text && z >= 0.5f;
    for (int i = 0; i < g->nn; i++) {
        GNode* n = &g->n[i];
        float x = sx(v, n->x), y = sy(v, n->y + oy), w = n->w * z, h = n->h * z;
        if (off_screen(v, x, y, x + w, y + h)) continue;
        v->vis++;
        uint32_t head = n->kind == GN_ENTRY ? 0x7A4FC0 : n->kind == GN_VALUE ? 0x2F7A4A : 0x3A5A9A;
        if (i == v->sel) rect(v, x - 2, y - 2, w + 4, h + 4, C(0xF0D060));
        rect(v, x, y, w, h, C(0x22262E));
        rect(v, x, y, w, HEAD * z, C(head));
        if (i == v->sel && n->kind != GN_VALUE) {          /* where '+' inserts: after the node or on the chosen branch */
            float px, py; port_pos(n, v->selPort, &px, &py);
            rect(v, sx(v, px) - 3 * z, sy(v, py + oy) - 1, 6 * z, 4 * z + 2, C(0xF0D060));
        }
        if (!withText) continue;
        text(v, &v->title[i], x + 3 * z, y + 1 * z, ts, C(0xFFFFFF));
        for (int k = 0; k < n->nin; k++) {
            float ry = y + (HEAD + ROW * k) * z;
            int linked = g->in[n->in0 + k].node >= 0;
            if (linked) rect(v, x - 2 * z, ry + 3 * z, 4 * z, 4 * z, C(0x70A8E0));
            text(v, &v->row[n->in0 + k], x + 4 * z, ry, ts, C(linked ? 0x9CC8F0 : 0xC8CCD4));
        }
        if (n->kind == GN_FLOW)
            for (int p = 1; p < n->nports; p++) {
                float px, py; port_pos(n, p, &px, &py);
                if (i == v->sel && p == v->selPort) rect(v, sx(v, px) - 9 * z, sy(v, py + oy) - ROW * z, 24 * z, ROW * z, C(0x5A4A20));
                text(v, &v->port[v->port0[i] + p - 1], sx(v, px) - 8 * z, sy(v, py + oy) - ROW * z, ts * 0.9f, C(0xE0C060));
            }
    }
}

void gv_draw(GView* v, float oy) {
    v->vis = v->quads = 0;
    if (!v->g) return;
    draw_edges(v, oy);
    draw_nodes(v, oy);
}

int gv_hit(GView* v, float x, float y, int* input, int* port) {
    *input = *port = -1;
    if (!v->g || y < v->top) return -1;
    float wx, wy;
    gv_to_world(v, x, y, &wx, &wy);
    Graph* g = v->g;
    float slack = 4 / v->zoom;                           /* a finger is not a mouse pointer */
    for (int i = g->nn - 1; i >= 0; i--) {
        GNode* n = &g->n[i];
        if (wx < n->x - slack || wx > n->x + n->w + slack || wy < n->y - slack || wy > n->y + n->h + slack) continue;
        if (n->kind == GN_FLOW && n->nports > 1 && wy >= n->y + n->h - ROW - 2) {
            int best = -1; float bd = 1e9f;
            for (int p = 1; p < n->nports; p++) {
                float px, py; port_pos(n, p, &px, &py);
                float d = wx - px; if (d < 0) d = -d;
                if (d < bd) { bd = d; best = p; }
            }
            *port = best;
            return i;
        }
        float ry = wy - (n->y + HEAD);
        if (ry >= 0 && n->nin > 0) { int k = (int)(ry / ROW); if (k < n->nin) *input = k; }
        return i;
    }
    return -1;
}
