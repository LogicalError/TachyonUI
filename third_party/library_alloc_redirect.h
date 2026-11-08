/* Included ahead of the C sources of the text libraries that call malloc and free directly
   (skribidi's skb_common.c, SheenBidi), so their memory goes where TachyonUI's library
   allocator sends it. The functions are defined in gui_core (gui_library_allocator.cpp). */
#ifndef TACHYON_LIBRARY_ALLOC_REDIRECT_H
#define TACHYON_LIBRARY_ALLOC_REDIRECT_H

#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif
void* tachyon_library_malloc(size_t size);
void* tachyon_library_calloc(size_t count, size_t size);
void* tachyon_library_realloc(void* ptr, size_t size);
void  tachyon_library_free(void* ptr);
#ifdef __cplusplus
}
#endif

#define malloc(size)        tachyon_library_malloc(size)
#define calloc(count, size) tachyon_library_calloc(count, size)
#define realloc(ptr, size)  tachyon_library_realloc(ptr, size)
#define free(ptr)           tachyon_library_free(ptr)

#endif
