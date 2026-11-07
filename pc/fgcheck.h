/* MiniSharp - checks of the flow graph generator (PC test tool). */
#ifndef MS_FGCHECK_H
#define MS_FGCHECK_H
int ms_fgraph_check(const char* dir);       /* --fgraph-check dir: fixtures from the browser tests */
int ms_fgraph_print(const char* path);      /* --fgraph graph.json (or a generated .cs): the code */
#endif
