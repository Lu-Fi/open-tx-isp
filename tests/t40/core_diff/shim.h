#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#define REGTRACE_KERNEL_TREE_BUILD 1
typedef int64_t s64; typedef uint64_t u64; typedef uint32_t u32; typedef uint16_t u16; typedef uint8_t u8;
int32_t system_reg_write(uint32_t a0, uint32_t a1);
int32_t system_reg_read(uint32_t a0);
int32_t isp_printf(uint32_t level, const char *fmt, ...);
int32_t __private_spin_lock_irqsave();
int32_t private_spin_unlock_irqrestore();
#define REGTRACE_MSCA_STATE_BYTES 1860
