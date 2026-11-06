/* MiniSharp - graph view of one method (see docs/graph-model.md).
   The graph is only a view of the AST: statements are flow nodes, expressions are value nodes,
   small expressions stay as text inside an input. It points into the AST and the source text,
   so it lives exactly as long as the parse that made it; after every code change it is built again. */
#ifndef MS_GRAPH_H
#define MS_GRAPH_H

#include "ast.h"
#include "printer.h"

enum { GN_ENTRY, GN_FLOW, GN_VALUE };
enum { GF_PLACED = 1 };              /* position comes from the @layout comment */

typedef struct {
    char label[16];
    Expr* ex;                        /* NULL: text only (the init statement of a for loop) */
    const char *s0, *s1;             /* source text shown while folded (NULL: printed from ex) */
    int node;                        /* expanded into this value node, or -1 (folded: shown as text) */
} GInput;

typedef struct {
    int kind;                        /* GN_* */
    Stmt* st; Expr* ex;              /* the statement (flow node) or expression (value node) it shows */
    char title[40];
    char tag[8];                     /* short kind name, stored in @layout to notice code that changed */
    char key[48];                    /* path in the method: "3", "3.t0" (true branch of 3), "3.t0.a1" (input 1) */
    int from, fromPort;              /* flow: hangs on output port fromPort of node from; value: feeds input fromPort of node from */
    int in0, nin;                    /* inputs g->in[in0 .. in0 + nin) */
    int nports;                      /* flow outputs: port 0 = next, then branches (see gr_port_label) */
    float x, y, w, h;
    unsigned flags;                  /* GF_* */
} GNode;

typedef struct { char key[48]; char tag[8]; float x, y; int fold; } GLayoutItem;   /* fold: 0 position, 1 open, 2 closed */

typedef struct {
    const char* src;
    ClassDecl* cls; MethodDecl* m;
    char name[80];                   /* "Ship.Update" */
    GNode* n; int nn, ncap;
    GInput* in; int nin, incap;
    GLayoutItem* lay; int nlay, laycap;
    int oom;
} Graph;

/* Graph of method cls.method in a parsed program (src = its text, also searched for the @layout comment).
   method may be the class name for the constructor. NULL if there is no such method (or out of memory). */
Graph* gr_build(ProgramAST* prog, const char* src, const char* cls, int clen, const char* method, int mlen);
void gr_free(Graph* g);

/* label of a flow output port: "next", "true", "false", "body", "case 3", "default" */
void gr_port_label(Graph* g, int node, int port, char* out, int outlen);
/* text of a folded input (one line, at most max chars, "..." when cut) */
void gr_input_text(Graph* g, GInput* in, char* out, int max);

/* this method's line for the @layout comment ("Ship.Update 0:if:0,30 ...") */
void gr_layout_line(Graph* g, MsBuf* out);
/* readable dump of the graph (used by `minisharp --graph` and tests) */
void gr_dump(Graph* g, MsBuf* out);

#endif
