/* MiniSharp - the "mini Bolt" graph editor on the 3DS (docs/mini-bolt.md). It edits the graph kept in the file's
   "// @graph {...}" line; every change generates the file's code again (src/fgraph.c, the same as the browser). */
#ifndef MS_BOLT_H
#define MS_BOLT_H

#include <stdint.h>

typedef struct {
    const char* (*text)(void);                  /* the graph file's text */
    void (*replace)(const char* text);          /* new text of the graph file: one undo step */
    void (*undo)(void);
    int (*save)(void);
    int (*ndocs)(void);                         /* the project's files, for the user's own nodes */
    const char* (*doc_name)(int i);
    const char* (*doc_text)(int i);
    void (*status)(uint32_t rgb, const char* msg);
    const char* fileName;
} BoltHost;

void bolt_mode(const BoltHost* h);

/* the text of a new graph file: class `name` with Update and Draw event nodes (malloc'd) */
char* bolt_new_graph_file(const char* name);

#endif
