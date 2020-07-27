/**
 * \usergroup{SceLibc}
 * \usage{psp2/libc.h,SceLibc_stub}
 */


#ifndef _DOLCESDK_PSP2_LIBC_H_
#define _DOLCESDK_PSP2_LIBC_H_

#include <psp2/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// string

char* sceLibcStrcpy(char* destination, const char* source);
size_t sceLibcStrlen(const char* str);
char* sceLibcStrcat(char* destination, const char* source);
int sceLibcStrcasecmp(const char* s1, const char* s2);

// stdlib

void sceLibcQsort(void* base, size_t num, size_t size, int(*compar)(const void*, const void*));
void sceLibcSrand(unsigned int seed);
int sceLibcRand(void);
int sceLibcAtoi(const char* str);

// malloc

void* sceLibcMalloc(size_t size);
void sceLibcFree(void* ptr);
void* sceLibcCalloc(size_t num, size_t size);

// stdio

int sceLibcSscanf(const char* s, const char* format, ...);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _DOLCESDK_PSP2_LIBC_H_ */
