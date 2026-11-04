/* MiniSharp - PC host for testing the compiler and VM.
 *   minisharp file.cs                 run Main(), or 60 frames of Update()/Draw()
 *   minisharp folder                  the same for a project (all .cs files in the folder)
 *   minisharp file.cs --frames N      run N frames
 *   minisharp file.cs --ppm out.ppm   save both screens of the last frame as an image
 *   minisharp file.cs --check         only compile, report errors
 *   minisharp file.cs --dis           print the bytecode
 *   minisharp --repl                  terminal: run lines from stdin */
#include "../src/ms.h"
#include "../src/platform.h"
#include "../src/media.h"
#include "../src/repl.h"
#include "../src/project.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

void pc_begin_frame(void);
void pc_save_ppm(const char* path);
void ms_disassemble(Program* p);

static char* read_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char* s = (char*)malloc(n + 1);
    if (fread(s, 1, n, f) != (size_t)n) { fclose(f); free(s); return NULL; }
    s[n] = 0; fclose(f);
    return s;
}

/* terminal mode: one line of code per input line; "#complete text" lists completions instead */
static int repl_main(void) {
    Repl* r = repl_new();
    if (!r) { fprintf(stderr, "out of memory\n"); return 2; }
    char line[1024], err[300];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        printf("> %s\n", line);
        if (!strncmp(line, "#complete ", 10)) {
            char out[8][REPL_WORD]; int start;
            const char* t = line + 10;
            int n = repl_complete(r, t, (int)strlen(t), &start, out, 8);
            for (int i = 0; i < n; i++) printf("%s%s", i ? " " : "", out[i]);
            printf("\n");
            continue;
        }
        if (!repl_eval(r, line, err, sizeof err)) printf("error: %s\n", err);
        fflush(stdout);
    }
    repl_free(r);
    return 0;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); /* "\n" not "\r\n", so output matches tests/expected */
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    if (argc == 2 && !strcmp(argv[1], "--repl")) return repl_main();
    if (argc < 2) { fprintf(stderr, "usage: %s file.cs [--frames N] [--ppm out.ppm] [--check] [--dis]\n", argv[0]); return 2; }
    int frames = 60, check = 0, dis = 0; const char* ppm = NULL;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc) ppm = argv[++i];
        else if (!strcmp(argv[i], "--check")) check = 1;
        else if (!strcmp(argv[i], "--dis")) dis = 1;
    }
    /* Gfx.LoadImage("x.png") and Audio.Load start from the program's folder */
    char dir[256]; snprintf(dir, sizeof dir, "%s", argv[1]);
    size_t dl = strlen(dir);
    int isProject = dl < 3 || strcmp(dir + dl - 3, ".cs");     /* a folder: all its .cs files */
    while (isProject && dl > 1 && (dir[dl - 1] == '/' || dir[dl - 1] == '\\')) dir[--dl] = 0;
    if (!isProject) {
        char* slash = strrchr(dir, '/'); char* bslash = strrchr(dir, '\\');
        if (bslash > slash) slash = bslash;
        if (slash) *slash = 0; else snprintf(dir, sizeof dir, ".");
    }
    ms_set_base_dir(dir);
    char err[300];
    Project proj;
    char* src;
    if (isProject) {
        if (!project_load(dir, &proj, err, sizeof err)) { fprintf(stderr, "%s\n", err); return 2; }
        src = proj.src; proj.src = NULL;
    } else {
        memset(&proj, 0, sizeof proj);
        src = read_file(argv[1]);
        if (!src) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    }
    Program* p = ms_compile(src, err, sizeof err);
    free(src);
    project_map_error(&proj, err, sizeof err);
    if (!p) { fprintf(stderr, "compile error: %s\n", err); project_free(&proj); return 1; }
    if (dis) ms_disassemble(p);
    if (check) { ms_free_program(p); project_free(&proj); return 0; }

    VM* vm = vm_new(p);
    int ok = vm_call(vm, p->staticInit);
    if (ok && p->entryMain >= 0) ok = vm_call(vm, p->entryMain);
    if (ok && p->entryUpdate >= 0) {
        for (int f = 0; f < frames && ok; f++) {
            pc_begin_frame();
            ok = vm_call(vm, p->entryUpdate);
            if (ok) ok = vm_tick_coroutines(vm);    /* after Update, before Draw, like Unity */
            if (ok && p->entryDraw >= 0) ok = vm_call(vm, p->entryDraw);
        }
    }
    int rc = 0;
    if (!ok && !vm->quit) {
        project_map_error(&proj, vm->err, sizeof vm->err);
        fprintf(stderr, "runtime error: %s\n", vm->err);
        rc = 3;
    }
    if (ppm) pc_save_ppm(ppm);
    vm_free(vm);
    plat_media_reset();
    ms_free_program(p);
    project_free(&proj);
    return rc;
}
