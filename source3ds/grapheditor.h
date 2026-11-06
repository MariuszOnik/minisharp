/* MiniSharp - graph mode of the code editor: the method under the cursor as a graph on the bottom screen,
   its code on the top screen. Every action changes the code (one undo step each). */
#ifndef MS_GRAPHEDITOR_H
#define MS_GRAPHEDITOR_H

#include <stdint.h>

typedef struct {
    const char* (*text)(void);                  /* the file's current text */
    int (*cursor)(void);
    void (*set_cursor)(int pos);
    void (*replace)(const char* text);          /* new text of the file: one undo step, marked as changed */
    void (*undo)(void);
    int (*save)(void);                          /* 1 = saved */
    void (*draw_top)(void);                     /* the code on the top screen (with the cursor's line) */
    void (*status)(uint32_t rgb, const char* msg);
} GraphHost;

/* runs until the user goes back to the code; the cursor is left on the selected node's code */
void graph_mode(const GraphHost* h);

#endif
