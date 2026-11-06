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
/* the same with the layout given here instead of read from src (items NULL: read it; nitems 0: none) */
Graph* gr_build_ex(ProgramAST* prog, const char* src, const char* cls, int clen, const char* method, int mlen,
                   const GLayoutItem* items, int nitems);
void gr_free(Graph* g);
int gr_find(Graph* g, const char* key);       /* node with this key, or -1 */

/* label of a flow output port: "next", "true", "false", "body", "case 3", "default" */
void gr_port_label(Graph* g, int node, int port, char* out, int outlen);
/* text of a folded input (one line, at most max chars, "..." when cut) */
void gr_input_text(Graph* g, GInput* in, char* out, int max);

/* this method's line for the @layout comment ("Ship.Update 0:if:0,30 ...") */
void gr_layout_line(Graph* g, MsBuf* out);
/* readable dump of the graph (used by `minisharp --graph` and tests) */
void gr_dump(Graph* g, MsBuf* out);

/* ---------- editing (graphedit.c): every action is a change of the code text ---------- */
typedef struct { int start, end; char* text; } MsPatch;     /* replace src[start, end) with text (malloc'd) */
void ms_patch_free(MsPatch* p);
char* ms_patch_apply(const char* src, const MsPatch* p);    /* new text (malloc'd) */

/* Actions give a patch of the code; 0 and a message in err when the action is not possible here. */
int gr_set_input(Graph* g, int node, int input, const char* text, MsPatch* out, char* err, int errlen);
/* inserts statements on a flow port: port 0 = right after the node, port >= 1 = first in that branch */
int gr_insert(Graph* g, int node, int port, const char* stmts, MsPatch* out, char* err, int errlen);
int gr_delete(Graph* g, int node, MsPatch* out, char* err, int errlen);

/* Applies a code patch to the graph's source and carries this method's node positions and open/closed inputs
   over to the new code (nodes keep their place when their code only moved). Returns the new source with an
   updated @layout comment (malloc'd), or NULL and err when the new code does not parse. */
char* gr_apply(Graph* g, const MsPatch* p, char* err, int errlen);
/* layout-only changes: the new source (malloc'd) with this method's @layout line rewritten */
char* gr_move(Graph* g, int node, float x, float y);
char* gr_fold(Graph* g, int node, int input, int open);
/* g's source with its current positions written into the @layout comment (malloc'd) */
char* gr_save_layout(Graph* g);

#endif
