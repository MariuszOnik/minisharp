/* MiniSharp - flow graphs ("mini Bolt"): the node catalog and the graph -> code generator.
   A C port of web/graph.js that gives byte for byte the same code, warnings and errors, so a graph made in the
   browser and one made on the 3DS are the same file. The graph is the JSON kept in the file's "// @graph {...}" line;
   the code above it is generated (see docs/graphs-notes.md). */
#ifndef MS_FGRAPH_H
#define MS_FGRAPH_H

#include "json.h"
#include "ast.h"

typedef struct { char *name, *type, *def; int flow; } FPort;      /* def NULL: no default */
typedef struct { char *type, *name, *port; } FParam;              /* port NULL: same as name */

typedef struct {
    char *id, *title, *category, *kind, *doc, *sig;
    char *callee, *ret, *op, *symbol, *method, *file;   /* NULL when the kind does not use it */
    int custom, user;
    FPort* in; int nin;
    FPort* out; int nout;
    FParam* params; int nparams; int hasParams;
} FDef;

typedef struct { FDef* d; int n, cap; } FCatalog;

/* one API entry of the Help (same fields as source3ds/help_data.h's HelpEntry) */
typedef struct { const char *group, *name, *sig, *doc, *insert; } FHelpEntry;

/* events, control, variables, operators, then every API entry, then the user's nodes (if their id is new) */
void fg_catalog_build(FCatalog* c, const FHelpEntry* help, int nhelp, const FCatalog* user);
void fg_catalog_free(FCatalog* c);
const FDef* fg_find(const FCatalog* c, const char* id);
/* the user's nodes from the project's code: every static method (not Main/Update/Draw) of a class in a file
   that is not a graph file; "// @node Title" and "// @node hide" in the comment lines right above the method */
void fg_user_nodes(FCatalog* out, const char* fileName, const char* src);
/* node definitions given as JSON (the browser's objects), for tests */
void fg_defs_from_json(FCatalog* out, const JVal* list);
/* the palette: best matches first (limit 0: 50); returns how many indexes were written */
int fg_search(const FCatalog* c, const char* query, int* out, int limit);

typedef struct { char* node; char* message; } FMsg;               /* node NULL: the whole graph */
typedef struct {
    char* code;                                                   /* malloc'd */
    FMsg* warnings; int nwarnings;
    FMsg* errors; int nerrors;
} FGenResult;

/* embed: add the "// @graph" line with the graph itself */
void fg_generate(const JVal* graph, const FCatalog* c, int embed, FGenResult* out);
void fg_result_free(FGenResult* r);

char* fg_serialize(const JVal* graph);                            /* the "// @graph {...}" line (malloc'd) */
JVal* fg_extract(const char* code);                               /* the graph from a generated file, or NULL */

#endif
