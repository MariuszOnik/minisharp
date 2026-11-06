#ifndef MS_HELP_H
#define MS_HELP_H

/* Browses the Help (language topics and the API). 'word' such as "Gfx.Circle" or "Gfx" opens
 * that place (may be NULL). Returns the text to insert into the code (A on an API entry), or NULL. */
const char* help_browse(const char* word);

/* signature of Cls.Name for a hint, e.g. "void Circle(float x, float y, float r, int color)"; NULL if unknown.
 * *overloads gets how many there are. */
const char* help_signature(const char* cls, int clen, const char* name, int nlen, int* overloads);

/* the API entries of the Help (group, name, sig, doc, insert: the same layout as FHelpEntry in src/fgraph.h) */
const void* help_api_entries(int* count);

#endif
