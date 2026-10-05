/* Minimal libc/kernel shim for the freestanding -m32 T20 firmware harness. */
#ifndef T20FW_KSHIM_H
#define T20FW_KSHIM_H
#include <stdint.h>
#include <stddef.h>
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;
typedef signed char s8; typedef short s16; typedef int s32; typedef long long s64;
void *memset(void *, int, size_t);
void *memcpy(void *, const void *, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
char *strrchr(const char *, int);
int strncmp(const char *, const char *, size_t);
int snprintf(char *, size_t, const char *, ...);
int printk(const char *, ...);
#define assert(x) ((void)0)
#ifndef KERN_INFO
#define KERN_INFO "<6>"
#endif
#endif
