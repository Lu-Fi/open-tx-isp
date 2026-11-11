/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tuning glue of the vendor tisp core: the riscv fast-parameter window
 * (tiziano_fastpara_init / _exit), the tuning attribute block tisp_tattr
 * (tisp_enable_tuning / tisp_disable_tuning), the "tisp" proc entry
 * (tisp_core_tuning_init / _deinit) and tisp_switch_bin.
 *
 * Kernel and driver calls go through struct t32_tuning_ops (the kernel passes
 * __ioremap, printk, private_vmalloc, system_irq_func_set, private_proc_create,
 * tisp_code_create_tuning_node, ...), so that the host oracle can record them.
 *
 * Vendor behaviour kept (not fixed, a change needs the user's OK):
 *  - tiziano_fastpara_init maps idx << 14 bytes (size 0 for sensor 0) and
 *    prints two words of the mapping before its NULL check (a failed map
 *    oopses in that printk, the "ioremap failed" message is unreachable);
 *  - the init flag is set before the map, so a failed init is not retried.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_tuning_oracle).
 */
#ifndef TX_ISP_T32_TUNING_H
#define TX_ISP_T32_TUNING_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <stddef.h>
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

#define T32_FASTPARA_IOREMAP_FLAGS	1024
#define T32_FASTPARA_HEAD_OFF		12520	/* words printed from the mapped parameters */
#define T32_FASTPARA_SENSOR_OFF		12604
#define T32_FASTPARA_FMT	"AE Para ADDR = 0x%x HEAD:%x SENSOR:%x\n"
#define T32_FASTPARA_FAIL_FMT	"riscv param ioremap failed\n"

#define T32_TISP_PROC_NAME	"tisp"
#define T32_TISP_PROC_FAIL_FMT	"[ISP][%s, %d]: proc create error !!! \n"

#define T32_TATTR_BYTES		4532
#define T32_TATTR_FAIL_FMT	"[ %s:%d ] Failed to allocate TISP TUNING DATA!!!\n"
#define T32_TOP_FRAME_IRQ	57

struct t32_tuning_ops {
	void *(*ioremap)(u32 phys, u32 size, u32 flags);
	void (*iounmap)(void *addr);
	int (*printk)(const char *fmt, ...);
	int (*isp_printf)(int level, const char *fmt, ...);
	void *(*vmalloc)(u32 size);
	void (*vfree)(void *addr);
	int (*irq_func_set)(int chn, int irq, void *handler);
	void *(*proc_create)(const char *name, u32 mode, void *parent, void *fops);
	void (*remove_proc_entry)(const char *name, void *parent);
	int (*create_tuning_node)(void);
	int (*destroy_tuning_node)(void);
	int (*core_switch_bin)(void);
	void *top_frame_count;		/* tisp_top_frame_count, the handler bound to irq 57 */
};

/* the vendor objects behind these functions */
struct t32_tuning_state {
	u32 ae_para_addr;	/* g_ae_para_addr, physical base of the riscv parameters */
	u32 fastpara_inited;
	u32 *init_para;		/* g_pstTizianoInitPara, the mapping */
	u8 *tattr;		/* tisp_tattr, T32_TATTR_BYTES */
	void *proc;		/* the "tisp" proc entry */
	void **fops_owner;	/* owner field of the proc file operations */
	void *this_module;
	u8 is_switch_bin;	/* isSwitchbin */
};

/* tiziano_fastpara_init (sensor index in the low byte) */
static inline void t32_fastpara_init(const struct t32_tuning_ops *k, struct t32_tuning_state *st, u32 idx)
{
	u32 *p;

	if (st->fastpara_inited == 1)
		return;
	idx &= 0xff;
	st->fastpara_inited = 1;
	p = k->ioremap(st->ae_para_addr, idx << 14, T32_FASTPARA_IOREMAP_FLAGS);
	st->init_para = p;
	k->printk(T32_FASTPARA_FMT, st->ae_para_addr, p[T32_FASTPARA_HEAD_OFF / 4], p[T32_FASTPARA_SENSOR_OFF / 4]);
	if (!st->init_para)
		k->printk(T32_FASTPARA_FAIL_FMT);
}

/* tiziano_fastpara_exit */
static inline void t32_fastpara_exit(const struct t32_tuning_ops *k, struct t32_tuning_state *st)
{
	if (!st->init_para)
		return;
	k->iounmap(st->init_para);
	st->init_para = NULL;
}

/*
 * tisp_enable_tuning: a zeroed tisp_tattr with its defaults, then the frame
 * counter on the top-frame interrupt.  Layout (offsets in tisp_tattr):
 *   0..19     20 bytes 0x80
 *   1064 + 128 * r + 8 * k   (r < 4, k < 16) byte 0x80
 *   1572 + 72 * i            (i < 4) per-channel block, below
 */
static inline int t32_enable_tuning(const struct t32_tuning_ops *k, struct t32_tuning_state *st)
{
	u8 *t;
	int r, i;

	st->tattr = k->vmalloc(T32_TATTR_BYTES);
	if (!st->tattr) {
		k->isp_printf(2, T32_TATTR_FAIL_FMT, "tisp_enable_tuning", 94);
		return -1;
	}
	t = st->tattr;
	memset(t, 0, T32_TATTR_BYTES);
	memset(t, 0x80, 20);
	for (r = 0; r < 4; r++)
		for (i = 0; i < 16; i++)
			t[1064 + 128 * r + 8 * i] = 0x80;
	for (i = 0; i < 4; i++) {
		u8 *c = t + 1572 + 72 * i;

		*(u32 *)(c + 4) = 1;
		*(u32 *)(c + 8) = 1;
		c[12] = 15;
		c[13] = 15;
		c[20] = 13;
		c[21] = 64;
		c[22] = 0x90;
		c[23] = 0xc0;
		*(u32 *)(c + 28) = 1;
		*(u32 *)(c + 32) = 1;
		c[36] = 15;
		c[37] = 15;
		*(u32 *)(c + 48) = 1;
		*(u32 *)(c + 52) = 3;
		c[56] = 15;
		c[57] = 15;
		*(u16 *)(c + 62) = 42;
		*(u16 *)(c + 64) = 22;
		*(u16 *)(c + 66) = 200;
		*(u16 *)(c + 68) = 19;
		*(u16 *)(c + 70) = 58;
	}
	k->irq_func_set(0, T32_TOP_FRAME_IRQ, k->top_frame_count);
	return 0;
}

/* tisp_disable_tuning */
static inline void t32_disable_tuning(const struct t32_tuning_ops *k, struct t32_tuning_state *st)
{
	if (!st->tattr)
		return;
	k->vfree(st->tattr);
	st->tattr = NULL;
}

/* tisp_core_tuning_init: the "tisp" proc entry, then the tuning device node */
static inline int t32_core_tuning_init(const struct t32_tuning_ops *k, struct t32_tuning_state *st, void *fops)
{
	*st->fops_owner = st->this_module;
	st->proc = k->proc_create(T32_TISP_PROC_NAME, 0, NULL, fops);
	if (!st->proc)
		return k->printk(T32_TISP_PROC_FAIL_FMT, "tisp_core_tuning_init", 853);
	return k->create_tuning_node();
}

/* tisp_core_tuning_deinit */
static inline int t32_core_tuning_deinit(const struct t32_tuning_ops *k, struct t32_tuning_state *st)
{
	if (st->proc) {
		k->remove_proc_entry(T32_TISP_PROC_NAME, NULL);
		st->proc = NULL;
	}
	return k->destroy_tuning_node();
}

/* tisp_switch_bin */
static inline int t32_switch_bin(const struct t32_tuning_ops *k, struct t32_tuning_state *st)
{
	int ret = k->core_switch_bin();

	st->is_switch_bin = 1;
	return ret;
}

#endif /* TX_ISP_T32_TUNING_H */
