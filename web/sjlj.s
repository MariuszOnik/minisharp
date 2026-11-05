/* wasm exception tag used by setjmp/longjmp (wasi-libc expects the program to define it) */
.globl __c_longjmp
.tagtype __c_longjmp i32
__c_longjmp:
