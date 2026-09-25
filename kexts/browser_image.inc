#pragma once
static u32 image_used,image_limit;
static void *image_alloc(unsigned long size)
{
    if(size>image_limit-image_used||size>0x7ffffff0u)return 0;u32 *p=api->kmalloc((u32)size+16);if(!p)return 0;*p=(u32)size;image_used+=(u32)size;return p+4;
}
static void image_free(void *ptr){if(ptr){u32 *p=(u32 *)ptr-4;image_used-=*p;api->kfree(p);}}
static void *image_realloc(void *ptr,unsigned long size)
{
    if(!ptr)return image_alloc(size);u32 old=((u32 *)ptr)[-4];if(size>old&&size-old>image_limit-image_used)return 0;void *p=image_alloc(size);if(!p)return 0;api->memcpy(p,ptr,old<size?old:(u32)size);image_free(ptr);return p;
}
#define STBI_FLOPNIX
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_GIF
#define STBI_ONLY_BMP
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 2048
#define STBI_ASSERT(x) ((void)0)
#define STBI_MALLOC(n) image_alloc(n)
#define STBI_REALLOC(p,n) image_realloc(p,n)
#define STBI_FREE(p) image_free(p)
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define memcpy(d,s,n) api->memcpy(d,s,n)
#define memset(d,c,n) api->memset(d,c,n)
#define memmove(d,s,n) api->memmove(d,s,n)
#define abs(n) ((n)<0?-(n):(n))
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "vendor/stb_image.h"
#pragma clang diagnostic pop
#undef memcpy
#undef memset
#undef memmove
#undef abs
