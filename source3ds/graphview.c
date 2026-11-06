/* MiniSharp - graph view PROTOTYPE: shows the graph of each method of a file on the bottom screen
   and measures what drawing costs (docs/graph-model.md, section 7). Read-only for now.
   Menu: B on a file. Circle pad / D-pad / touch drag: move, L/R: zoom, X/Y: next/previous method,
   A: 1 or 4 copies of the graph (stress), B: text on/off, SELECT: automatic benchmark into the log, START: back. */
#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/ast.h"
#include "../src/graph.h"
#include "../src/platform.h"
#include "platform_3ds.h"
#include "ui.h"
#include "app.h"
#include "graphview.h"

#define SW 320.0f
#define SH 240.0f
#define TEXT_SCALE 0.36f       /* text height about ROW (10) at zoom 1 */
#define HEAD 13.0f
#define ROW 10.0f

typedef struct { int cls, m; } MethodRef;

static char* g_src;
static Arena g_arena;
static ProgramAST* g_prog;
static MethodRef g_methods[256];
static int g_nmethods, g_cur;
static Graph* g_graph;
static C2D_TextBuf g_tbuf;
static C2D_Text* g_title;      /* per node */
static C2D_Text* g_row;        /* per input */
static C2D_Text* g_port;       /* per node: nports texts (port 1..) at g_port[g_port0[n] + p - 1] */
static int* g_port0;
static float g_minx, g_miny, g_maxx, g_maxy;
static float g_camx, g_camy, g_zoom = 1.0f;
static int g_copies = 1, g_text = 1;
static double g_buildMs;
static char g_err[256];

/* stats of the last 60 frames */
static double g_cpuAvg, g_cpuWorst, g_gpuAvg;
static int g_visAvg, g_quadsAvg;
static int g_vis, g_quads;

static void free_graph(void) {
    gr_free(g_graph); g_graph = NULL;
    free(g_title); free(g_row); free(g_port); free(g_port0);
    g_title = g_row = g_port = NULL; g_port0 = NULL;
    if (g_tbuf) { C2D_TextBufDelete(g_tbuf); g_tbuf = NULL; }
}

static void parse_text(C2D_Text* t, const char* s) {
    C2D_TextParse(t, g_tbuf, s);
    C2D_TextOptimize(t);
}

/* builds the graph of method i and its text (all text is parsed once here, never per frame) */
static void open_method(int i) {
    free_graph();
    g_cur = i;
    ClassDecl* cd = &g_prog->classes[g_methods[i].cls];
    MethodDecl* m = &cd->methods[g_methods[i].m];
    u64 t0 = svcGetSystemTick();
    g_graph = gr_build(g_prog, g_src, cd->name, cd->nlen, m->name, m->nlen);
    if (!g_graph) return;
    Graph* g = g_graph;
    /* glyph count for the buffer */
    int glyphs = 64, nports = 0;
    char s[96], t[64];
    for (int n = 0; n < g->nn; n++) {
        glyphs += (int)strlen(g->n[n].title);
        if (g->n[n].kind == GN_FLOW) nports += g->n[n].nports - 1;
    }
    glyphs += g->nin * 50 + nports * 16;
    g_tbuf = C2D_TextBufNew(glyphs);
    g_title = (C2D_Text*)calloc((size_t)g->nn, sizeof(C2D_Text));
    g_row = (C2D_Text*)calloc((size_t)(g->nin ? g->nin : 1), sizeof(C2D_Text));
    g_port = (C2D_Text*)calloc((size_t)(nports ? nports : 1), sizeof(C2D_Text));
    g_port0 = (int*)calloc((size_t)g->nn, sizeof(int));
    int pi = 0;
    for (int n = 0; n < g->nn; n++) {
        GNode* x = &g->n[n];
        parse_text(&g_title[n], x->title);
        for (int k = 0; k < x->nin; k++) {
            GInput* in = &g->in[x->in0 + k];
            if (in->node >= 0) snprintf(s, sizeof s, "%s", in->label);
            else { gr_input_text(g, in, t, 28); snprintf(s, sizeof s, "%s: %s", in->label, t); }
            parse_text(&g_row[x->in0 + k], s);
        }
        g_port0[n] = pi;
        if (x->kind == GN_FLOW)
            for (int p = 1; p < x->nports; p++) { gr_port_label(g, n, p, t, sizeof t); parse_text(&g_port[pi++], t); }
    }
    g_buildMs = (svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11;
    g_minx = g_miny = 1e9f; g_maxx = g_maxy = -1e9f;
    for (int n = 0; n < g->nn; n++) {
        GNode* x = &g->n[n];
        if (x->x < g_minx) g_minx = x->x;
        if (x->y < g_miny) g_miny = x->y;
        if (x->x + x->w > g_maxx) g_maxx = x->x + x->w;
        if (x->y + x->h > g_maxy) g_maxy = x->y + x->h;
    }
    g_camx = g_minx + SW / 2 / g_zoom - 8; g_camy = g_miny + SH / 2 / g_zoom - 8;
    flog("graph %s: %d nodes, %d inputs, built in %.2f ms (with text)", g->name, g->nn, g->nin, g_buildMs);
}

/* ---------- drawing (bottom screen) ---------- */
static u32 C(uint32_t rgb) { return plat3ds_color(rgb); }

static void text(C2D_Text* t, float x, float y, float sc, u32 c) {   /* every glyph is one quad for citro2d */
    C2D_DrawText(t, C2D_WithColor, x, y, 0.5f, sc, sc, c);
    g_quads += (int)(t->end - t->begin);
}
static void rect(float x, float y, float w, float h, u32 c) { C2D_DrawRectSolid(x, y, 0.5f, w, h, c); g_quads++; }
static void line(float x1, float y1, float x2, float y2, float w, u32 c) { C2D_DrawLine(x1, y1, c, x2, y2, c, w, 0.5f); g_quads++; }

static int off_screen(float x0, float y0, float x1, float y1) {   /* box (any corner order) outside the screen */
    if (x0 > x1) { float t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { float t = y0; y0 = y1; y1 = t; }
    return x1 < 0 || y1 < 0 || x0 > SW || y0 > SH;
}

static float sx(float wx, float ox) { return (wx + ox - g_camx) * g_zoom + SW / 2; }
static float sy(float wy, float oy) { return (wy + oy - g_camy) * g_zoom + SH / 2; }

static void port_pos(GNode* n, int port, float* x, float* y) {     /* world position of a flow output */
    if (port == 0) { *x = n->x + 6; *y = n->y + n->h; return; }
    *x = n->x + n->w * (float)port / (float)n->nports; *y = n->y + n->h;
}

static void draw_edges(Graph* g, float oy) {
    float z = g_zoom;
    int segs = z >= 0.6f ? 8 : 1;
    for (int i = 1; i < g->nn; i++) {
        GNode* n = &g->n[i];
        GNode* f = &g->n[n->from];
        if (n->kind == GN_FLOW) {
            float px, py; port_pos(f, n->fromPort, &px, &py);
            float x0 = sx(px, 0), y0 = sy(py, oy), x1 = sx(n->x + 6, 0), y1 = sy(n->y, oy);
            if (off_screen(x0, y0, x1, y1)) continue;
            u32 c = C(n->fromPort == 0 ? 0xB0B8C8 : f->st && f->st->k == S_IF ? (n->fromPort == 1 ? 0x60D070 : 0xE06060) : 0xE0C060);
            float ym = y0 + 4 * z;
            line(x0, y0, x0, ym, 1.5f, c);
            line(x0, ym, x1, ym, 1.5f, c);
            line(x1, ym, x1, y1, 1.5f, c);
        } else if (n->kind == GN_VALUE) {
            float x0 = sx(n->x + n->w, 0), y0 = sy(n->y + HEAD / 2, oy);
            float x1 = sx(f->x, 0), y1 = sy(f->y + HEAD + ROW * n->fromPort + ROW / 2, oy);
            if (off_screen(x0, y0, x1, y1)) continue;
            u32 c = C(0x70A8E0);
            if (segs == 1) { line(x0, y0, x1, y1, 1.2f, c); continue; }
            float dx = (x1 - x0) * 0.5f, lx = x0, ly = y0;
            for (int s = 1; s <= segs; s++) {        /* cubic bezier: (x0,y0) (x0+dx,y0) (x1-dx,y1) (x1,y1) */
                float t = (float)s / segs, u = 1 - t;
                float bx = u * u * u * x0 + 3 * u * u * t * (x0 + dx) + 3 * u * t * t * (x1 - dx) + t * t * t * x1;
                float by = u * u * u * y0 + 3 * u * u * t * y0 + 3 * u * t * t * y1 + t * t * t * y1;
                line(lx, ly, bx, by, 1.2f, c);
                lx = bx; ly = by;
            }
        }
    }
}

static void draw_nodes(Graph* g, float oy) {
    float z = g_zoom, ts = TEXT_SCALE * z;
    int withText = g_text && z >= 0.5f;           /* LOD: no text when it would be too small to read */
    for (int i = 0; i < g->nn; i++) {
        GNode* n = &g->n[i];
        float x = sx(n->x, 0), y = sy(n->y, oy), w = n->w * z, h = n->h * z;
        if (off_screen(x, y, x + w, y + h)) continue;
        g_vis++;
        uint32_t head = n->kind == GN_ENTRY ? 0x7A4FC0 : n->kind == GN_VALUE ? 0x2F7A4A : 0x3A5A9A;
        rect(x, y, w, h, C(0x22262E));
        rect(x, y, w, HEAD * z, C(head));
        if (!withText) continue;
        text(&g_title[i], x + 3 * z, y + 1 * z, ts, C(0xFFFFFF));
        for (int k = 0; k < n->nin; k++) {
            float ry = y + (HEAD + ROW * k) * z;
            int linked = g->in[n->in0 + k].node >= 0;
            if (linked) rect(x - 2 * z, ry + 3 * z, 4 * z, 4 * z, C(0x70A8E0));
            text(&g_row[n->in0 + k], x + 4 * z, ry, ts, C(linked ? 0x9CC8F0 : 0xC8CCD4));
        }
        if (n->kind == GN_FLOW)
            for (int p = 1; p < n->nports; p++) {
                float px, py; port_pos(n, p, &px, &py);
                text(&g_port[g_port0[i] + p - 1], sx(px, 0) - 8 * z, sy(py, oy) - ROW * z, ts * 0.9f, C(0xE0C060));
            }
    }
}

static void draw_graph(void) {
    if (!g_graph) return;
    float gh = g_maxy - g_miny + 40;
    for (int c = 0; c < g_copies; c++) {
        draw_edges(g_graph, c * gh);
        draw_nodes(g_graph, c * gh);
    }
}

/* ---------- top screen: what is measured ---------- */
static void draw_info(const char* status) {
    char s[160];
    plat_text(8, 4, "Graph prototype (read-only)", 0.6f, 0x7FB2FF);
    if (g_graph) {
        snprintf(s, sizeof s, "%s   %d/%d   nodes %d x%d", g_graph->name, g_cur + 1, g_nmethods, g_graph->nn, g_copies);
        plat_text(8, 26, s, 0.5f, 0xFFFFFF);
    } else plat_text(8, 26, g_err[0] ? g_err : "no methods", 0.5f, 0xFF8080);
    snprintf(s, sizeof s, "FPS %d   zoom %.2f   text %s", plat_fps(), g_zoom, g_text ? (g_zoom >= 0.5f ? "on" : "off (LOD)") : "off");
    plat_text(8, 46, s, 0.5f, 0xC0C8D0);
    snprintf(s, sizeof s, "CPU draw avg %.2f ms, worst %.2f ms", g_cpuAvg, g_cpuWorst);
    plat_text(8, 64, s, 0.5f, 0xC0E0C0);
    snprintf(s, sizeof s, "GPU %.2f ms   visible nodes %d   quads %d", g_gpuAvg, g_visAvg, g_quadsAvg);
    plat_text(8, 82, s, 0.5f, 0xC0E0C0);
    snprintf(s, sizeof s, "build (parse done) + text: %.2f ms", g_buildMs);
    plat_text(8, 100, s, 0.5f, 0xA0A8B0);
    plat_text(8, 130, "Pad/touch: move   L/R: zoom   X/Y: method", 0.5f, 0xA0A0A0);
    plat_text(8, 148, "A: 1 or 4 copies   B: text on/off", 0.5f, 0xA0A0A0);
    plat_text(8, 166, "SELECT: benchmark to log   START: back", 0.5f, 0xA0A0A0);
    if (status) plat_text(8, 200, status, 0.5f, 0xF0D080);
}

/* one frame; returns CPU ms of the drawing calls */
static double frame(const char* status) {
    ui_frame_begin(0x10141C, 0x181C24);
    draw_info(status);
    plat_screen(1);
    g_vis = g_quads = 0;
    u64 t0 = svcGetSystemTick();
    draw_graph();
    double ms = (svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11;
    ui_frame_end();
    return ms;
}

static void bench(void) {
    flog("graph benchmark: method, copies, zoom -> CPU avg / worst ms, GPU avg ms, visible nodes, quads (60 frames sweeping down)");
    int keepText = g_text; float keepZoom = g_zoom; int keepCopies = g_copies, keepCur = g_cur;
    g_text = 1;
    static const float zooms[] = { 1.0f, 0.5f, 0.35f };
    for (int i = 0; i < g_nmethods && aptMainLoop(); i++) {
        open_method(i);
        if (!g_graph || g_graph->nn < 8) continue;
        for (int c = 1; c <= 4; c += 3)
            for (int zi = 0; zi < 3; zi++) {
                g_copies = c; g_zoom = zooms[zi];
                double sum = 0, worst = 0, gpu = 0; long vis = 0, quads = 0;
                float total = (g_maxy - g_miny + 40) * c;
                char st[96]; snprintf(st, sizeof st, "benchmark %d/%d ...", i + 1, g_nmethods);
                for (int f = 0; f < 60; f++) {
                    g_camx = (g_minx + g_maxx) / 2;
                    g_camy = g_miny + total * f / 60.0f;
                    double ms = frame(st);
                    if (f >= 2) { sum += ms; if (ms > worst) worst = ms; gpu += C3D_GetDrawingTime(); vis += g_vis; quads += g_quads; }
                }
                flog("  %s x%d zoom %.2f: CPU %.2f / %.2f ms, GPU %.2f ms, %ld nodes, %ld quads",
                     g_graph->name, c, g_zoom, sum / 58, worst, gpu / 58, vis / 58, quads / 58);
            }
    }
    g_text = keepText; g_zoom = keepZoom; g_copies = keepCopies;
    open_method(keepCur);
    flog("graph benchmark done");
}

void graphview_open(const char* name) {
    char path[256];
    size_t nl = strlen(name);
    if (nl && name[nl - 1] == '/') snprintf(path, sizeof path, "%s/%.200sGame.cs", APP_DIR, name);
    else snprintf(path, sizeof path, "%s/%.200s", APP_DIR, name);
    FILE* f = fopen(path, "rb");
    if (!f) { flog("graph: cannot open %s", path); return; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    g_src = (char*)malloc((size_t)len + 1);
    if (!g_src) { fclose(f); return; }
    len = (long)fread(g_src, 1, (size_t)len, f); g_src[len] = 0;
    fclose(f);

    memset(&g_arena, 0, sizeof g_arena);
    g_err[0] = 0; g_nmethods = 0; g_prog = NULL;
    jmp_buf jb;
    ParseCtx pc = { &g_arena, &jb, g_err, sizeof g_err };
    u64 t0 = svcGetSystemTick();
    if (!setjmp(jb)) {
        int n; Token* t = ms_lex(&pc, g_src, &n);
        g_prog = ms_parse(&pc, t, n);
    }
    double parseMs = (svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11;
    if (g_prog) {
        flog("graph: %s parsed in %.2f ms (%ld bytes)", path, parseMs, len);
        for (int c = 0; c < g_prog->nclasses; c++)
            for (int m = 0; m < g_prog->classes[c].nmethods && g_nmethods < 256; m++)
                g_methods[g_nmethods++] = (MethodRef){ c, m };
    } else flog("graph: %s: %s", path, g_err);
    g_zoom = 1.0f; g_copies = 1; g_text = 1;
    if (g_nmethods) open_method(0);

    double sum = 0, worst = 0, gpu = 0; long vis = 0, quads = 0; int frames = 0;
    int touching = 0, tx0 = 0, ty0 = 0;
    while (aptMainLoop()) {
        plat3ds_input();
        u32 k = plat3ds_down(), held = plat3ds_held();
        if (k & KEY_START) break;
        if (g_nmethods) {
            if (k & KEY_X) open_method((g_cur + 1) % g_nmethods);
            if (k & KEY_Y) open_method((g_cur + g_nmethods - 1) % g_nmethods);
        }
        if (k & KEY_A) g_copies = g_copies == 1 ? 4 : 1;
        if (k & KEY_B) g_text = !g_text;
        if (k & KEY_SELECT) { bench(); sum = worst = gpu = 0; vis = quads = frames = 0; continue; }
        if (held & KEY_R) g_zoom *= 1.03f;
        if (held & KEY_L) g_zoom /= 1.03f;
        if (g_zoom > 2.5f) g_zoom = 2.5f;
        if (g_zoom < 0.2f) g_zoom = 0.2f;
        float sp = 4 / g_zoom;
        circlePosition cp; hidCircleRead(&cp);
        /* dead zone: a worn circle pad reports a few units even when left alone (the view drifted) */
        if (abs(cp.dx) > 24) g_camx += (cp.dx - (cp.dx > 0 ? 24 : -24)) / 132.0f * sp * 1.5f;
        if (abs(cp.dy) > 24) g_camy -= (cp.dy - (cp.dy > 0 ? 24 : -24)) / 132.0f * sp * 1.5f;
        if (held & KEY_DLEFT) g_camx -= sp;
        if (held & KEY_DRIGHT) g_camx += sp;
        if (held & KEY_DUP) g_camy -= sp;
        if (held & KEY_DDOWN) g_camy += sp;
        if (held & KEY_TOUCH) {
            int px, py; plat3ds_touch(&px, &py);
            if (touching) { g_camx -= (px - tx0) / g_zoom; g_camy -= (py - ty0) / g_zoom; }
            touching = 1; tx0 = px; ty0 = py;
        } else touching = 0;

        double ms = frame(NULL);
        sum += ms; if (ms > worst) worst = ms; gpu += C3D_GetDrawingTime(); vis += g_vis; quads += g_quads;
        if (++frames == 60) {
            g_cpuAvg = sum / 60; g_cpuWorst = worst; g_gpuAvg = gpu / 60; g_visAvg = (int)(vis / 60); g_quadsAvg = (int)(quads / 60);
            sum = worst = gpu = 0; vis = quads = frames = 0;
        }
    }
    free_graph();
    arena_free(&g_arena);
    free(g_src); g_src = NULL;
}
