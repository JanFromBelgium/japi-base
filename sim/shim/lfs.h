/* Host shim for littlefs lfs.h — opaque stubs so japi_base.h compiles on Linux.
   The simulator never uses littlefs internals; only the type sizes matter. */
#ifndef LFS_SHIM_H
#define LFS_SHIM_H
typedef struct { _Alignas(8) unsigned char _opaque[64]; } lfs_file_t;
typedef struct { _Alignas(8) unsigned char _opaque[128]; } lfs_dir_t;
#endif
