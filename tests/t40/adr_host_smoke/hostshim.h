#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define A32(p) host_a32((const void *)(p))
uint32_t host_a32(const void *p);
uint32_t host_ext(const char *name, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t sp, uint32_t *v1);
void host_bad_call(uint32_t fn);
void *malloc32(size_t n);
int host_reg_write(uint32_t reg, uint32_t val);
