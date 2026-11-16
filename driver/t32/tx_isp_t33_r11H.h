/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 ISP block teardown (vendor tisp_<blk>_deinit, tisp_process_*, tisp_rcfg_deinit, tisp_top_deinit).
 *
 * The vendor keeps per channel pointer tables (32 bit words on the MIPS32 kernel); each teardown frees the
 * buffers a block state points to, clears the table slot and releases the block's register list.  Tables are
 * passed in.  OS / module calls are the vendor functions (private_kfree, private_vfree,
 * tisp_rcfg_module_release, ...); tisp_normal_ae_deinit is ported in tx_isp_t33_deinit.h.
 * Checked against the vendor instructions by tests/t33_binoracle (block r11H).
 */
#ifndef TX_ISP_T33_R11H_H
#define TX_ISP_T33_R11H_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#include <linux/printk.h>
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
#endif
extern int printk(const char *fmt, ...);
extern void private_kfree(u32 p);
extern void private_vfree(u32 p);
extern int tisp_rcfg_module_release(u32 ch, u32 id);
extern int isp_printf(u32 level, const char *fmt, ...);
extern void tisp_normal_ae_deinit(u32 ch);
extern void system_irq_func_set(u32 ch, u32 id, u32 fn);
extern int tisp_event_exit(void);
extern void tisp_event_set_cb(u32 ch, u32 ev, u32 cb);
#endif

static inline u32 *t33_dw(u32 addr)
{
	return (u32 *)(uintptr_t)addr;
}

/* release register list id of channel ch; a failure is only logged */
static inline void t33_rel(u32 ch, u32 id, u32 line, const char *fn)
{
	int r = tisp_rcfg_module_release(ch & 0xff, id);

	if (r < 0)
		printk("%s, %d: release failed %d\n", fn, line, r);
}

/* tisp_defog_deinit: state words 1 (+ word 2), 4, 5 are kfree'd, the state itself, the weight table (vfree) */
static inline int t33_defog_free(u32 *info, u32 *weight, u32 ch)
{
	u32 *p;

	if (info[ch]) {
		p = t33_dw(info[ch]);
		if (p[1]) {
			private_kfree(p[1]);
			p[1] = 0;
			p[2] = 0;
		}
		if (p[4]) {
			private_kfree(p[4]);
			p[4] = 0;
		}
		if (p[5]) {
			private_kfree(p[5]);
			p[5] = 0;
		}
		private_kfree(info[ch]);
		info[ch] = 0;
	}
	if (weight[ch]) {
		private_vfree(weight[ch]);
		weight[ch] = 0;
	}
	t33_rel(ch, 12, 2053, "tisp_defog_deinit");
	return 0;
}

/* tisp_gamma_deinit: words 1, 3, 2, 5, 4 vfree'd (not cleared), then the state */
static inline int t33_gamma_free(u32 *para, u32 ch)
{
	u32 *p;

	if (para[ch]) {
		p = t33_dw(para[ch]);
		if (p[1])
			private_vfree(p[1]);
		if (p[3])
			private_vfree(p[3]);
		if (p[2])
			private_vfree(p[2]);
		if (p[5])
			private_vfree(p[5]);
		if (p[4])
			private_vfree(p[4]);
		private_vfree(para[ch]);
		para[ch] = 0;
	}
	t33_rel(ch, 11, 625, "tisp_gamma_deinit");
	return 0;
}

/* tisp_lce_deinit: clear the mode flags / counters and the frame flag, release lists 16 and 37 */
static inline void t33_lce_free(u8 *modeflag, u8 *modecounter, u8 *frameflag, u32 ch)
{
	ch &= 0xff;
	memset(modeflag, 0, 16);
	memset(modecounter, 0, 16);
	memset(frameflag, 0, 4);
	t33_rel(ch, 16, 3218, "tisp_lce_deinit");
	t33_rel(ch, 37, 3223, "tisp_lce_deinit");
}

/* tisp_lsc_deinit */
static inline void t33_lsc_free(u32 *para, u32 ch)
{
	u32 *p, *q;
	u32 k;

	ch &= 0xff;
	if (para[ch]) {
		p = t33_dw(para[ch]);
		if (p[4]) {
			private_kfree(p[4]);
			p[4] = 0;
		}
		if (p[1]) {
			private_vfree(p[1]);
			p[1] = 0;
		}
		if (p[2]) {
			q = t33_dw(p[2]);
			for (k = 6; k <= 12; k++)
				if (q[k])
					private_vfree(q[k]);
			private_vfree(p[2]);
			p[2] = 0;
		}
		if (p[3]) {
			private_vfree(p[3]);
			p[3] = 0;
		}
		private_vfree(para[ch]);
		para[ch] = 0;
	}
	t33_rel(ch, 6, 1330, "tisp_lsc_deinit");
}

/* tisp_ae_deinit */
static inline void t33_ae_free(u32 *ctx, u32 ch)
{
	u32 *p;

	ch &= 0xff;
	if (!ctx[ch])
		return;
	tisp_normal_ae_deinit(ch);
	p = t33_dw(ctx[ch]);
	if (p[9]) {
		private_kfree(p[9]);
		p[9] = 0;
	}
	if (p[6]) {
		private_kfree(p[6]);
		p[6] = 0;
	}
	t33_rel(ch, 30, 314, "tisp_ae_deinit");
	t33_rel(ch, 5, 319, "tisp_ae_deinit");
	system_irq_func_set(ch, 5, 0);
	system_irq_func_set(ch, 4, 0);
	tisp_event_set_cb(ch, 1, 0);
}

/* tisp_af_deinit */
static inline void t33_af_free(u32 *info, u32 *buf, u32 ch)
{
	u32 *p;

	if (info[ch]) {
		private_vfree(info[ch]);
		info[ch] = 0;
	}
	if (buf[ch]) {
		p = t33_dw(buf[ch]);
		if (p[1]) {
			private_kfree(p[1]);
			p[1] = 0;
		}
		private_vfree(buf[ch]);
		buf[ch] = 0;
	}
	t33_rel(ch, 31, 1153, "tisp_af_deinit");
}

/* tisp_awb_deinit */
static inline void t33_awb_free(u32 *ct, u32 *info, u32 *stat, u32 ch)
{
	u32 *p;
	u32 k;

	ch &= 0xff;
	if (ct[ch]) {
		private_kfree(ct[ch]);
		ct[ch] = 0;
	}
	if (info[ch]) {
		p = t33_dw(info[ch]);
		for (k = 1; k <= 4; k++)
			if (p[k]) {
				private_kfree(p[k]);
				p[k] = 0;
			}
		private_kfree(info[ch]);
		info[ch] = 0;
	}
	if (stat[ch]) {
		p = t33_dw(stat[ch]);
		if (p[1]) {
			private_kfree(p[1]);
			p[1] = 0;
		}
		private_kfree(stat[ch]);
		stat[ch] = 0;
	}
	t33_rel(ch, 7, 1419, "tisp_awb_deinit");
	t33_rel(ch, 29, 1424, "tisp_awb_deinit");
}

/* tisp_process_deinit: tisp_event_exit, a failure is logged */
static inline void t33_process_exit(void)
{
	int r = tisp_event_exit();

	if (r)
		isp_printf(2, "event exit failed %d\n", "tisp_process_deinit", 536, r);
}

/* tisp_process_init: does nothing */
static inline void t33_process_nop(void)
{
}

/* tisp_rcfg_deinit: frees the buffer the info points to, then the info */
static inline void t33_rcfg_free(u32 *buf_info)
{
	u32 *p;

	if (*buf_info) {
		p = t33_dw(*buf_info);
		if (p[0]) {
			private_kfree(p[0]);
			p[0] = 0;
		}
		private_vfree(*buf_info);
		*buf_info = 0;
	}
}

/* tisp_top_deinit: release list 33 */
static inline void t33_top_release(u32 ch)
{
	t33_rel(ch, 33, 328, "tisp_top_deinit");
}

#endif /* TX_ISP_T33_R11H_H */
