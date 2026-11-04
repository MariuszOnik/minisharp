/* MiniSharp - PC host for testing the compiler and VM.
 *   minisharp file.cs                 run Main(), or 60 frames of Update()/Draw()
 *   minisharp file.cs --frames N      run N frames
 *   minisharp file.cs --ppm out.ppm   save both screens of the last frame as an image
 *   minisharp file.cs --check         only compile, report errors
 *   minisharp file.cs --dis           print the bytecode */
#include "../src/ms.h"
#include "../src/platform.h"
#include "../src/media.h"
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

int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY); /* "\n" not "\r\n", so output matches tests/expected */
    _setmode(_fileno(stderr), _O_BINARY);
#endif
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
    char* slash = strrchr(dir, '/'); char* bslash = strrchr(dir, '\\');
    if (bslash > slash) slash = bslash;
    if (slash) *slash = 0; else snprintf(dir, sizeof dir, ".");
    ms_set_base_dir(dir);
    char* src = read_file(argv[1]);
    if (!src) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }
    char err[300];
    Program* p = ms_compile(src, err, sizeof err);
    free(src);
    if (!p) { fprintf(stderr, "compile error: %s\n", err); return 1; }
    if (dis) ms_disassemble(p);
    if (check) { ms_free_program(p); return 0; }

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
    if (!ok && !vm->quit) { fprintf(stderr, "runtime error: %s\n", vm->err); rc = 3; }
    if (ppm) pc_save_ppm(ppm);
    vm_free(vm);
    plat_media_reset();
    ms_free_program(p);
    return rc;
}
