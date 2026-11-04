/* MiniSharp - projects. The .cs files of a folder are joined into one source (classes may be in any
 * file, 'using' and 'namespace' are allowed in each) and line numbers in errors are mapped back. */
#include "project.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_FILES 64
#define MAX_TOTAL (4 * 1024 * 1024)

static int is_cs(const char* name) {
    size_t n = strlen(name);
    return n > 3 && n < sizeof(((ProjFile*)0)->name) && !strcmp(name + n - 3, ".cs");
}

static int cmpname(const void* a, const void* b) { return strcmp(((const ProjFile*)a)->name, ((const ProjFile*)b)->name); }

int project_count_cs(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return 0;
    int n = 0;
    struct dirent* e;
    while ((e = readdir(d))) if (is_cs(e->d_name)) n++;
    closedir(d);
    return n;
}

int project_load(const char* dir, Project* pr, char* err, int errlen) {
    memset(pr, 0, sizeof *pr);
    DIR* d = opendir(dir);
    if (!d) { snprintf(err, errlen, "cannot open folder %s", dir); return 0; }
    pr->files = (ProjFile*)calloc(MAX_FILES, sizeof(ProjFile));
    struct dirent* e;
    while (pr->files && (e = readdir(d))) {
        if (!is_cs(e->d_name)) continue;
        if (pr->nfiles == MAX_FILES) { closedir(d); snprintf(err, errlen, "more than %d .cs files in %s", MAX_FILES, dir); project_free(pr); return 0; }
        strcpy(pr->files[pr->nfiles++].name, e->d_name);
    }
    closedir(d);
    if (!pr->files) { snprintf(err, errlen, "out of memory"); return 0; }
    if (pr->nfiles == 0) { snprintf(err, errlen, "no .cs files in %s", dir); project_free(pr); return 0; }
    qsort(pr->files, pr->nfiles, sizeof(ProjFile), cmpname);

    size_t len = 0, cap = 0;
    int line = 1;
    for (int i = 0; i < pr->nfiles; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s", dir, pr->files[i].name);
        FILE* f = fopen(path, "rb");
        if (!f) { snprintf(err, errlen, "cannot read %s", pr->files[i].name); project_free(pr); return 0; }
        fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
        if (n < 0 || len + n + 2 > MAX_TOTAL) { fclose(f); snprintf(err, errlen, "project too big"); project_free(pr); return 0; }
        if (len + n + 2 > cap) {
            cap = (len + n + 2) * 2;
            char* s = (char*)realloc(pr->src, cap);
            if (!s) { fclose(f); snprintf(err, errlen, "out of memory"); project_free(pr); return 0; }
            pr->src = s;
        }
        size_t got = fread(pr->src + len, 1, n, f);
        fclose(f);
        pr->files[i].firstLine = line;
        for (size_t k = 0; k < got; k++) if (pr->src[len + k] == '\n') line++;
        len += got;
        if (got == 0 || pr->src[len - 1] != '\n') { pr->src[len++] = '\n'; line++; }   /* next file on a new line */
        pr->src[len] = 0;
    }
    return 1;
}

void project_free(Project* pr) {
    free(pr->src);
    free(pr->files);
    memset(pr, 0, sizeof *pr);
}

void project_map_error(const Project* pr, char* err, int errlen) {
    if (strncmp(err, "line ", 5) || !isdigit((unsigned char)err[5]) || pr->nfiles == 0) return;
    char* rest;
    long line = strtol(err + 5, &rest, 10);
    int fi = 0;
    while (fi + 1 < pr->nfiles && pr->files[fi + 1].firstLine <= line) fi++;
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s line %ld%s", pr->files[fi].name, line - pr->files[fi].firstLine + 1, rest);
    snprintf(err, errlen, "%s", tmp);
}
