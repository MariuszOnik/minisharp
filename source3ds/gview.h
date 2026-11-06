/* MiniSharp - drawing a method graph on a 3DS screen (used by the graph editor and the prototype). */
#ifndef MS_GVIEW_H
#define MS_GVIEW_H

#include <3ds.h>
#include <citro2d.h>
#include "../src/graph.h"

typedef struct {
    Graph* g;
    C2D_TextBuf buf;                /* all text of the graph, parsed once in gv_attach */
    C2D_Text *title, *row, *port;
    int* port0;
    float camx, camy, zoom;         /* world point in the middle of the screen */
    float sw, sh, top;              /* screen size; top: y where the canvas starts (a toolbar above) */
    int text;                       /* 0: boxes only */
    int sel, selPort;               /* selected node (-1: none) and its output port (0: after the node) */
    float minx, miny, maxx, maxy;   /* bounds of the graph */
    int vis, quads;                 /* drawn in the last gv_draw */
} GView;

void gv_init(GView* v, float sw, float sh, float top);
void gv_attach(GView* v, Graph* g);  /* parses the text; g stays owned by the caller */
void gv_detach(GView* v);
void gv_draw(GView* v, float oy);   /* on the current screen; oy: extra world offset (stress copies) */
void gv_home(GView* v);             /* top-left of the graph into view */
void gv_show(GView* v, int node);   /* scrolls so that the node is visible */
/* what is under screen point (x, y): node or -1; *input = input row (-1), *port = flow port label (-1) */
int gv_hit(GView* v, float x, float y, int* input, int* port);
void gv_to_world(GView* v, float x, float y, float* wx, float* wy);

#endif
