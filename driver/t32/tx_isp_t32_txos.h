/* SPDX-License-Identifier: GPL-2.0 */
/*
 * OS hooks of the T32 tx_* glue ports (tx_isp_t32_tx*.h): the kernel calls
 * the vendor module makes through its private_* wrappers, kept as named
 * functions so that the host oracles (tests/t32_oracle/t32_op3_glue.h)
 * record them in order against the vendor flow.
 *
 * Kernel build: thin mappings to the kernel API.  Host build: the oracle
 * defines every t32_tx_os_* function as a recorder.
 *
 * Pointers the vendor keeps in its objects are 32-bit (MIPS o32); the glue
 * objects therefore hold them as u32 and convert with T32_TX_P().
 */
#ifndef TX_ISP_T32_TXOS_H
#define TX_ISP_T32_TXOS_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/delay.h>
#include <linux/vmalloc.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_TX_P(a)	((void *)(uintptr_t)(a))
#define T32_TX_A(p)	((u32)(uintptr_t)(p))

/* word access to a vendor object at a 32-bit address */
static inline u32 t32_tx_ld(u32 a, u32 off)
{
	return *(volatile u32 *)T32_TX_P(a + off);
}

static inline void t32_tx_st(u32 a, u32 off, u32 v)
{
	*(volatile u32 *)T32_TX_P(a + off) = v;
}

/* hardware register in a KSEG1 window (fixed address, as the vendor accesses it) */
static inline u32 t32_tx_rd(u32 a)
{
	return *(volatile u32 *)T32_TX_P(a);
}

static inline void t32_tx_wr(u32 a, u32 v)
{
	*(volatile u32 *)T32_TX_P(a) = v;
}

#define T32_EINVAL	22U

#ifdef __KERNEL__
#define t32_tx_os_printk(...)		printk(__VA_ARGS__)
#define t32_tx_os_isp_printf(l, ...)	printk(__VA_ARGS__)
#define t32_tx_os_msleep(ms)		msleep(ms)
#define t32_tx_os_vmalloc(n)		T32_TX_A(vmalloc(n))
#define t32_tx_os_vfree(a)		vfree(T32_TX_P(a))
#define t32_tx_os_memset(a, c, n)	memset(T32_TX_P(a), c, n)
#define t32_tx_os_memcpy(d, s, n)	memcpy(T32_TX_P(d), T32_TX_P(s), n)
#else
void t32_tx_os_printk(const char *fmt, ...);
void t32_tx_os_isp_printf(u32 level, const char *fmt, ...);
void t32_tx_os_msleep(u32 ms);
u32 t32_tx_os_vmalloc(u32 n);
void t32_tx_os_vfree(u32 a);
void t32_tx_os_memset(u32 a, u32 c, u32 n);
void t32_tx_os_memcpy(u32 d, u32 s, u32 n);
#endif

#endif
