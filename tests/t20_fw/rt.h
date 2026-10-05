#ifndef T20FW_RT_H
#define T20FW_RT_H
#include <stdarg.h>
#include <stddef.h>
long rt_write(int fd, const void *b, unsigned long n);
long rt_read(int fd, void *b, unsigned long n);
int rt_open(const char *p, int flags);
int rt_close(int fd);
void rt_exit(int code) __attribute__((noreturn));
void rt_flush(void);
void rt_puts(const char *s);
void rt_printf(const char *f, ...);
int vsnprintf(char *b, size_t sz, const char *f, va_list ap);
int snprintf(char *b, size_t sz, const char *f, ...);
void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
#endif
