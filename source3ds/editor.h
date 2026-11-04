#ifndef MS_EDITOR_H
#define MS_EDITOR_H

/* edits "file.cs" or a project "Name/" in APP_DIR until START */
void editor_open(const char* name);

/* asks for a name, makes APP_DIR/Name/Game.cs from a template and opens it.
 * 1 = made (made gets "Name/"), 0 = cancelled */
int editor_new_project(char* made, int cap);

#endif
