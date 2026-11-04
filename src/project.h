/* MiniSharp - a project is a folder: every .cs file in it is compiled together. */
#ifndef MS_PROJECT_H
#define MS_PROJECT_H

typedef struct { char name[64]; int firstLine; } ProjFile;

typedef struct {
    char* src;                          /* the files one after another */
    ProjFile* files; int nfiles;        /* sorted by name; firstLine = their line 1 in src */
} Project;

/* 1 = ok; 0 = err says why (no .cs files, cannot read, too big) */
int project_load(const char* dir, Project* pr, char* err, int errlen);
void project_free(Project* pr);
int project_count_cs(const char* dir);  /* how many .cs files the folder has */

/* "line 57: msg" / "line 57 (A.B): msg" -> "Player.cs line 12: msg" (in place) */
void project_map_error(const Project* pr, char* err, int errlen);

#endif
