/* MiniSharp - bytecode listing, handy when debugging the compiler. */
#include "ms.h"
#include <stdio.h>
#include <string.h>

static const struct { const char* name; int nargs; } ops[] = {
    [OP_ICONST] = {"ICONST", 1}, [OP_FCONST] = {"FCONST", 1}, [OP_SCONST] = {"SCONST", 1},
    [OP_NULL] = {"NULL", 0}, [OP_TRUE] = {"TRUE", 0}, [OP_FALSE] = {"FALSE", 0},
    [OP_POP] = {"POP", 0}, [OP_DUP] = {"DUP", 0}, [OP_DUP2] = {"DUP2", 0},
    [OP_LOAD] = {"LOAD", 1}, [OP_STORE] = {"STORE", 1}, [OP_STOREP] = {"STOREP", 1},
    [OP_GETSTATIC] = {"GETSTATIC", 1}, [OP_SETSTATIC] = {"SETSTATIC", 1},
    [OP_GETFIELD] = {"GETFIELD", 1}, [OP_SETFIELD] = {"SETFIELD", 1},
    [OP_GETELEM] = {"GETELEM", 0}, [OP_SETELEM] = {"SETELEM", 0},
    [OP_IADD] = {"IADD", 0}, [OP_ISUB] = {"ISUB", 0}, [OP_IMUL] = {"IMUL", 0}, [OP_IDIV] = {"IDIV", 0},
    [OP_IMOD] = {"IMOD", 0}, [OP_INEG] = {"INEG", 0},
    [OP_FADD] = {"FADD", 0}, [OP_FSUB] = {"FSUB", 0}, [OP_FMUL] = {"FMUL", 0}, [OP_FDIV] = {"FDIV", 0},
    [OP_FMOD] = {"FMOD", 0}, [OP_FNEG] = {"FNEG", 0},
    [OP_I2F] = {"I2F", 1}, [OP_F2I] = {"F2I", 0}, [OP_TOSTR] = {"TOSTR", 2}, [OP_CONCAT] = {"CONCAT", 0},
    [OP_ILT] = {"ILT", 0}, [OP_ILE] = {"ILE", 0}, [OP_IGT] = {"IGT", 0}, [OP_IGE] = {"IGE", 0},
    [OP_IEQ] = {"IEQ", 0}, [OP_INE] = {"INE", 0},
    [OP_FLT] = {"FLT", 0}, [OP_FLE] = {"FLE", 0}, [OP_FGT] = {"FGT", 0}, [OP_FGE] = {"FGE", 0},
    [OP_FEQ] = {"FEQ", 0}, [OP_FNE] = {"FNE", 0},
    [OP_SEQ] = {"SEQ", 0}, [OP_SNE] = {"SNE", 0}, [OP_REQ] = {"REQ", 0}, [OP_RNE] = {"RNE", 0}, [OP_NOT] = {"NOT", 0},
    [OP_JMP] = {"JMP", 1}, [OP_JZ] = {"JZ", 1}, [OP_JFALSE_KEEP] = {"JFALSE_KEEP", 1}, [OP_JTRUE_KEEP] = {"JTRUE_KEEP", 1},
    [OP_CALL] = {"CALL", 2}, [OP_CALLN] = {"CALLN", 2}, [OP_NULLCHK] = {"NULLCHK", 1},
    [OP_RET] = {"RET", 0}, [OP_RETV] = {"RETV", 0},
    [OP_NEW] = {"NEW", 1}, [OP_NEWARR] = {"NEWARR", 1}, [OP_ARRLEN] = {"ARRLEN", 0}, [OP_STRLEN] = {"STRLEN", 0},
    [OP_NORET] = {"NORET", 0},
    [OP_IAND] = {"IAND", 0}, [OP_IOR] = {"IOR", 0}, [OP_IXOR] = {"IXOR", 0}, [OP_ISHL] = {"ISHL", 0},
    [OP_ISHR] = {"ISHR", 0}, [OP_INOT] = {"INOT", 0}, [OP_I2C] = {"I2C", 0}, [OP_C2I] = {"C2I", 1},
    [OP_LIST] = {"LIST", 1}, [OP_CORO_NEW] = {"CORO_NEW", 2}, [OP_YIELD] = {"YIELD", 1}, [OP_CORO] = {"CORO", 1}, [OP_DICT] = {"DICT", 1},
    [OP_CALLV] = {"CALLV", 2}, [OP_ISINST] = {"ISINST", 2},
    [OP_BOX] = {"BOX", 0}, [OP_GETCELL] = {"GETCELL", 0}, [OP_SETCELL] = {"SETCELL", 0}, [OP_UPVAL] = {"UPVAL", 1},
    [OP_CLOSURE] = {"CLOSURE", 3}, [OP_CALLF] = {"CALLF", 1},
};

void ms_disassemble(Program* p) {
    for (int fi = 0; fi < p->nfuncs; fi++) {
        Func* f = &p->funcs[fi];
        printf("== #%d %s (params %d, locals %d)\n", fi, f->name, f->nparams, f->nlocals);
        for (int ip = 0; ip < f->len;) {
            int o = f->code[ip];
            printf("  %4d  L%-3d %-12s", ip, f->lines[ip], ops[o].name ? ops[o].name : "?");
            int n = ops[o].nargs;
            for (int k = 0; k < n; k++) printf(" %d", (int)f->code[ip + 1 + k]);
            if (o == OP_JMP || o == OP_JZ || o == OP_JFALSE_KEEP || o == OP_JTRUE_KEEP) printf("   -> %d", ip + 2 + (int)f->code[ip + 1]);
            if (o == OP_CALL) printf("   %s", p->funcs[f->code[ip + 1]].name);
            if (o == OP_CALLN) printf("   %s.%s", ms_natives[f->code[ip + 1]].cls, ms_natives[f->code[ip + 1]].name);
            if (o == OP_ISINST) printf("   %s", p->classes[f->code[ip + 1]].name);
            if (o == OP_SCONST) printf("   \"%s\"", p->strings[f->code[ip + 1]]->chars);
            if (o == OP_FCONST) { float v; memcpy(&v, &f->code[ip + 1], 4); printf("   %g", v); }
            printf("\n");
            ip += 1 + n;
        }
    }
}
