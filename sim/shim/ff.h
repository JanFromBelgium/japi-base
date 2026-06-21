/* Host shim for FatFs ff.h — opaque stubs so japi_base.h compiles on Linux.
   The simulator never uses FatFs internals; only the type sizes matter.
   8-byte aligned so the sim can stash a (FILE *) in the japi_file_t union slot
   without a misaligned access (the union sits right after a uint8_t tag). */
#ifndef FF_SHIM_H
#define FF_SHIM_H
typedef struct { _Alignas(8) unsigned char _opaque[64]; } FIL;
typedef struct { _Alignas(8) unsigned char _opaque[64]; } DIR;
#endif
