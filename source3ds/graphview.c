/* MiniSharp - graph BENCHMARK screen (menu: B on a file): shows each method's graph read-only and measures
   what drawing costs (docs/graph-model.md, section 7). Editing graphs happens in the code editor (Graf).
   Circle pad / D-pad / touch drag: move, L/R: zoom, X/Y: next/previous method,
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
#include "gview.h"
#include "graphview.h"

typedef struct { int cls, m; } MethodRef;

static char* g_src;
static Arena g_arena;
static ProgramAST* g_prog;
static MethodRef g_methods[256];
static int g_nmethods, g_cur, g_copies = 1;
static GView V;
static double g_buildMs;
static char g_err[256];
static double g_cpuAvg, g_cpuWorst, g_gpuAvg;
static int g_visAvg, g_quadsAvg;

static void open_method(int i) {
    gr_free(V.g);
    gv_detach(&V);
    g_cur = i;
    ClassDecl* cd = &g_prog->classes[g_methods[i].cls];
    MethodDecl* m = &cd->methods[g_methods[i].m];
    u64 t0 = svcGetSystemTick();
    Graph* g = gr_build(g_prog, g_src, cd->name, cd->nlen, m->name, m->nlen);
    gv_attach(&V, g);
    g_buildMs = (svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11;
    gv_home(&V);
    if (g) flog("graph %s: %d nodes, %d inputs, built in %.2f ms (with text)", g->name, g->nn, g->nin, g_buildMs);
}

static void draw_info(const char* status) {
    char s[160];
    plat_text(8, 4, "Graph benchmark (read-only)", 0.6f, 0x7FB2FF);
    if (V.g) {
        snprintf(s, sizeof s, "%s   %d/%d   nodes %d x%d", V.g->name, g_cur + 1, g_nmethods, V.g->nn, g_copies);
        plat_text(8, 26, s, 0.5f, 0xFFFFFF);
    } else plat_text(8, 26, g_err[0] ? g_err : "no methods", 0.5f, 0xFF8080);
    snprintf(s, sizeof s, "FPS %d   zoom %.2f   text %s", plat_fps(), V.zoom, V.text ? (V.zoom >= 0.5f ? "on" : "off (LOD)") : "off");
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
    plat_text(8, 184, "Editing: open the file (R) and press Graf.", 0.5f, 0xA0A0A0);
    if (status) plat_text(8, 206, status, 0.5f, 0xF0D080);
}

static double frame(const char* status) {
    ui_frame_begin(0x10141C, 0x181C24);
    draw_info(status);
    plat_screen(1);
    u64 t0 = svcGetSystemTick();
    float gh = V.maxy - V.miny + 40;
    int vis = 0, quads = 0;
    for (int c = 0; c < g_copies; c++) { gv_draw(&V, c * gh); vis += V.vis; quads += V.quads; }
    V.vis = vis; V.quads = quads;
    double ms = (svcGetSystemTick() - t0) * 1000.0 / SYSCLOCK_ARM11;
    ui_frame_end();
    return ms;
}

static void bench(void) {
    flog("graph benchmark: method, copies, zoom -> CPU avg / worst ms, GPU avg ms, visible nodes, quads (60 frames sweeping down)");
    int keepText = V.text, keepCopies = g_copies, keepCur = g_cur;
    float keepZoom = V.zoom;
    V.text = 1;
    static const float zooms[] = { 1.0f, 0.5f, 0.35f };
    for (int i = 0; i < g_nmethods && aptMainLoop(); i++) {
        open_method(i);
        if (!V.g || V.g->nn < 8) continue;
        for (int c = 1; c <= 4; c += 3)
            for (int zi = 0; zi < 3; zi++) {
                g_copies = c; V.zoom = zooms[zi];
                double sum = 0, worst = 0, gpu = 0; long vis = 0, quads = 0;
                float total = (V.maxy - V.miny + 40) * c;
                char st[96]; snprintf(st, sizeof st, "benchmark %d/%d ...", i + 1, g_nmethods);
                for (int f = 0; f < 60; f++) {
                    V.camx = (V.minx + V.maxx) / 2;
                    V.camy = V.miny + total * f / 60.0f;
                    double ms = frame(st);
                    if (f >= 2) { sum += ms; if (ms > worst) worst = ms; gpu += C3D_GetDrawingTime(); vis += V.vis; quads += V.quads; }
                }
                flog("  %s x%d zoom %.2f: CPU %.2f / %.2f ms, GPU %.2f ms, %ld nodes, %ld quads",
                     V.g->name, c, V.zoom, sum / 58, worst, gpu / 58, vis / 58, quads / 58);
            }
    }
    V.text = keepText; V.zoom = keepZoom; g_copies = keepCopies;
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
    gv_init(&V, 320, 240, 0);
    g_copies = 1;
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
        if (k & KEY_B) V.text = !V.text;
        if (k & KEY_SELECT) { bench(); sum = worst = gpu = 0; vis = quads = frames = 0; continue; }
        if (held & KEY_R) V.zoom *= 1.03f;
        if (held & KEY_L) V.zoom /= 1.03f;
        if (V.zoom > 2.5f) V.zoom = 2.5f;
        if (V.zoom < 0.2f) V.zoom = 0.2f;
        float sp = 4 / V.zoom;
        circlePosition cp; hidCircleRead(&cp);
        /* dead zone: a worn circle pad reports a few units even when left alone (the view drifted) */
        if (abs(cp.dx) > 24) V.camx += (cp.dx - (cp.dx > 0 ? 24 : -24)) / 132.0f * sp * 1.5f;
        if (abs(cp.dy) > 24) V.camy -= (cp.dy - (cp.dy > 0 ? 24 : -24)) / 132.0f * sp * 1.5f;
        if (held & KEY_DLEFT) V.camx -= sp;
        if (held & KEY_DRIGHT) V.camx += sp;
        if (held & KEY_DUP) V.camy -= sp;
        if (held & KEY_DDOWN) V.camy += sp;
        if (held & KEY_TOUCH) {
            int px, py; plat3ds_touch(&px, &py);
            if (touching) { V.camx -= (px - tx0) / V.zoom; V.camy -= (py - ty0) / V.zoom; }
            touching = 1; tx0 = px; ty0 = py;
        } else touching = 0;

        double ms = frame(NULL);
        sum += ms; if (ms > worst) worst = ms; gpu += C3D_GetDrawingTime(); vis += V.vis; quads += V.quads;
        if (++frames == 60) {
            g_cpuAvg = sum / 60; g_cpuWorst = worst; g_gpuAvg = gpu / 60; g_visAvg = (int)(vis / 60); g_quadsAvg = (int)(quads / 60);
            sum = worst = gpu = 0; vis = quads = frames = 0;
        }
    }
    gr_free(V.g);
    gv_detach(&V);
    arena_free(&g_arena);
    free(g_src); g_src = NULL;
}
