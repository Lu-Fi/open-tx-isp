/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 RISC-V co-processor glue of the tx-isp module (vendor tx_isp_riscv_*,
 * tx_isp_get_tiziano_para, tx_isp_get_riscv_night_mode): the 8 KiB firmware
 * window at the end of the reserved memory, the mailbox at 0xb2220000
 * (+0x00 control, +0x04 boot address, +0x08 command, +0x0c done flag,
 * +0xf0 bit 31 = hold in reset) and the "InitPara" block the RISC-V core
 * fills for the fast start (tiziano para, magic 0x55aaaa55 at +12520).
 *
 * Vendor globals: da798 -> struct t32_tx_riscv_mem (vmalloc'ed, 8 bytes),
 * da790 -> InitPara.  They live in struct t32_tx_riscv here.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txriscv_oracle): every hook call, print
 * and mailbox store in order, the objects and the return values.
 */
#ifndef TX_ISP_T32_TXRISCV_H
#define TX_ISP_T32_TXRISCV_H

#include "tx_isp_t32_txos.h"

#define T32_RISCV_MBOX		0xb2220000U
#define T32_RISCV_CTRL		(T32_RISCV_MBOX + 0x00)
#define T32_RISCV_BOOT		(T32_RISCV_MBOX + 0x04)
#define T32_RISCV_CMD		(T32_RISCV_MBOX + 0x08)
#define T32_RISCV_DONE		(T32_RISCV_MBOX + 0x0c)
#define T32_RISCV_ARG0		(T32_RISCV_MBOX + 0x10)
#define T32_RISCV_ARG1		(T32_RISCV_MBOX + 0x14)
#define T32_RISCV_ARG2		(T32_RISCV_MBOX + 0x18)
#define T32_RISCV_ARG3		(T32_RISCV_MBOX + 0x1c)
#define T32_RISCV_ARG4		(T32_RISCV_MBOX + 0x20)
#define T32_RISCV_ARG5		(T32_RISCV_MBOX + 0x24)
#define T32_RISCV_RESET		(T32_RISCV_MBOX + 0xf0)
#define T32_RISCV_HOLD		0x80000000U

#define T32_RISCV_FW_WIN	8192U		/* firmware window at the end of rmem */
#define T32_RISCV_FW_BYTES	2108U		/* vendor boot code (cfb64) */
#define T32_RISCV_KSEG1		0xa0000000U
#define T32_RISCV_BOOT_OFF	128U

/* commands (+0x08, low byte) */
#define T32_RISCV_CMD_RUN	1U		/* bits 8..31: 100000 */
#define T32_RISCV_CMD_STOP_VIC	2U
#define T32_RISCV_CMD_GPIO	3U

/* InitPara (da790) */
#define T32_TIZ_MAGIC_OFF	12520U
#define T32_TIZ_MAGIC		0x55aaaa55U
#define T32_TIZ_NIGHT_OFF	12600U
#define T32_TIZ_CH_STRIDE	0x4000U		/* per vinum block (vendor table bede0, data.c:1947) */
#define T32_TIZ_AE0		12524U		/* ae x3, then +12548 luma, ev, num */
#define T32_TIZ_LUMA		12548U
#define T32_TIZ_AWB		12560U		/* MfPara x6 */

struct t32_tx_riscv_mem {
	u32 vaddr;		/* KSEG1 address of the window */
	u32 paddr;
};

struct t32_tx_riscv {
	u32 mem;		/* vendor da798: struct t32_tx_riscv_mem *, 0 = not probed */
	u32 init_para;		/* vendor da790: InitPara, 0 = none */
	u32 rmem_base, rmem_size;
	u32 fw;			/* address of the 2108 byte boot code */
};

#ifdef __KERNEL__
#define t32_tx_os_fastpara_init(ch)		tiziano_fastpara_init(ch)
#define t32_tx_os_g_riscv_info(ch, w, p)	tisp_g_riscv_info(ch, w, p)
#else
void t32_tx_os_fastpara_init(u32 ch);
void t32_tx_os_g_riscv_info(u32 ch, u32 which, u32 para);
#endif

/* tx_isp_riscv_stop: hold the core in reset */
static inline int t32_tx_riscv_stop(void)
{
	t32_tx_wr(T32_RISCV_RESET, t32_tx_rd(T32_RISCV_RESET) | T32_RISCV_HOLD);
	return 0;
}

/* tx_isp_riscv_is_stop: 1 (and clear the flag) once the core reported done */
static inline int t32_tx_riscv_is_stop(void)
{
	if (t32_tx_rd(T32_RISCV_DONE) != 1)
		return 0;
	t32_tx_wr(T32_RISCV_DONE, 0);
	return 1;
}

/* tx_isp_riscv_is_run: fast start parameters valid? */
static inline int t32_tx_riscv_is_run(struct t32_tx_riscv *r, u32 ch)
{
	t32_tx_os_fastpara_init(ch & 0xff);
	if (!r->init_para) {
		t32_tx_os_printk("tx_isp_riscv_is_run error InitPara is null\n");
		return -(int)T32_EINVAL;
	}
	return t32_tx_ld(r->init_para, T32_TIZ_MAGIC_OFF) == T32_TIZ_MAGIC ? 0 : -(int)T32_EINVAL;
}

/* tx_isp_get_riscv_night_mode */
static inline int t32_tx_get_riscv_night_mode(struct t32_tx_riscv *r)
{
	if (!r->init_para) {
		t32_tx_os_printk("get_riscv_night_mode failed para is NULL\n");
		return -1;
	}
	return (int)t32_tx_ld(r->init_para, T32_TIZ_NIGHT_OFF);
}

/* tx_isp_riscv_stop_vic */
static inline int t32_tx_riscv_stop_vic(struct t32_tx_riscv *r, u32 arg)
{
	if (!r->mem) {
		t32_tx_os_isp_printf(1, "[%s %d] The RISC-V memory address cannot be empty.",
				     "tx_isp_riscv_stop_vic", 261);
		return -(int)T32_EINVAL;
	}
	t32_tx_wr(T32_RISCV_CMD, T32_RISCV_CMD_STOP_VIC | (arg & 0xffffff) << 8);
	return 0;
}

/* tx_isp_riscv_switch_gpio(port, pin, func, level, pull) */
static inline int t32_tx_riscv_switch_gpio(struct t32_tx_riscv *r, u32 port, u32 pin, u32 func,
					   u32 level, u32 pull)
{
	if (!r->mem) {
		t32_tx_os_isp_printf(1, "[%s %d] The RISC-V memory address cannot be empty.",
				     "tx_isp_riscv_switch_gpio", 237);
		return -(int)T32_EINVAL;
	}
	t32_tx_wr(T32_RISCV_CMD, T32_RISCV_CMD_GPIO | (pin & 0xff) << 8 | (level & 0xff) << 16 |
		  (port & 0xf) << 24 | (func & 3) << 28 | (pull & 3) << 30);
	return 0;
}

/* tx_isp_riscv_run: boot the core from the firmware window */
static inline int t32_tx_riscv_run(struct t32_tx_riscv *r)
{
	u32 paddr;

	if (!r->mem) {
		t32_tx_os_isp_printf(1, "[%s %d] RISCV address is empty.", "tx_isp_riscv_run", 146);
		return -(int)T32_EINVAL;
	}
	if (!t32_tx_ld(r->mem, 0) || !(paddr = t32_tx_ld(r->mem, 4))) {
		t32_tx_os_isp_printf(1, "[%s %d] The vaddr or paddr cannot be empty.", "tx_isp_riscv_run", 151);
		return -(int)T32_EINVAL;
	}
	t32_tx_wr(T32_RISCV_CTRL, t32_tx_rd(T32_RISCV_CTRL) & ~2U);
	t32_tx_wr(T32_RISCV_RESET, t32_tx_rd(T32_RISCV_RESET) | T32_RISCV_HOLD);
	t32_tx_wr(T32_RISCV_CTRL, 35);
	t32_tx_wr(T32_RISCV_BOOT, paddr + T32_RISCV_BOOT_OFF);
	t32_tx_wr(T32_RISCV_CMD, 0);
	t32_tx_wr(T32_RISCV_DONE, 0);
	t32_tx_wr(T32_RISCV_ARG4, 0);
	t32_tx_wr(T32_RISCV_ARG5, 0);
	t32_tx_wr(T32_RISCV_ARG0, 0);
	t32_tx_wr(T32_RISCV_ARG1, 0);
	t32_tx_wr(T32_RISCV_ARG2, 0xffffffffU);
	t32_tx_wr(T32_RISCV_ARG3, 0xffffffffU);
	t32_tx_os_msleep(2);
	t32_tx_wr(T32_RISCV_RESET, t32_tx_rd(T32_RISCV_RESET) & ~T32_RISCV_HOLD);
	t32_tx_wr(T32_RISCV_CMD, T32_RISCV_CMD_RUN | 100000U << 8);
	return 0;
}

/* tx_isp_riscv_probe: window at the end of rmem, boot code copied in */
static inline int t32_tx_riscv_probe(struct t32_tx_riscv *r)
{
	u32 m = t32_tx_os_vmalloc(sizeof(struct t32_tx_riscv_mem)), pa;

	r->mem = m;
	if (!m) {
		t32_tx_os_isp_printf(1, "[%s %d] RISC-V memory allocation failed.", "tx_isp_riscv_probe", 181);
		return -(int)T32_EINVAL;
	}
	t32_tx_os_memset(m, 0, sizeof(struct t32_tx_riscv_mem));
	pa = r->rmem_base + r->rmem_size - T32_RISCV_FW_WIN;
	t32_tx_st(r->mem, 0, pa | T32_RISCV_KSEG1);
	t32_tx_st(r->mem, 4, pa);
	t32_tx_os_isp_printf(1, "%s[%d]: paddr = 0x%08x\n", "tx_isp_riscv_probe", 204, pa);
	t32_tx_os_memset(t32_tx_ld(r->mem, 0), 0, T32_RISCV_FW_WIN);
	t32_tx_os_memcpy(t32_tx_ld(r->mem, 0), r->fw, T32_RISCV_FW_BYTES);
	return 0;
}

/* tx_isp_riscv_remove */
static inline int t32_tx_riscv_remove(struct t32_tx_riscv *r)
{
	t32_tx_riscv_stop();
	if (r->mem) {
		if (t32_tx_ld(r->mem, 0)) {
			t32_tx_st(r->mem, 0, 0);
			t32_tx_st(r->mem, 4, 0);
		}
		t32_tx_os_vfree(r->mem);
		r->mem = 0;
	}
	return 0;
}

/* tx_isp_set_tiziano_param: empty in the vendor module */
static inline int t32_tx_set_tiziano_param(void)
{
	return 0;
}

/*
 * tx_isp_get_tiziano_para(vinum).  Vendor: the "invalid vinum" print passes
 * no third argument (a stale register a3 is printed); ours prints vinum and
 * the oracle calls the vendor with a3 = vinum.
 */
static inline int t32_tx_get_tiziano_para(struct t32_tx_riscv *r, u32 ch)
{
	u32 p;

	if (ch >= 4) {
		t32_tx_os_printk("[CORE][%s, %d]: tx_isp_get_tiziano_para: vinum %d is invalid\n",
				 "tx_isp_get_tiziano_para", 71, ch);
		return -1;
	}
	if (!r->init_para)
		return 0;
	p = r->init_para + ch * T32_TIZ_CH_STRIDE;
	t32_tx_os_printk("vinum is %d, tiziano para ae:%d %d %d luma:%d ev:%d num:%d\n", ch,
			 t32_tx_ld(p, T32_TIZ_AE0), t32_tx_ld(p, T32_TIZ_AE0 + 4), t32_tx_ld(p, T32_TIZ_AE0 + 8),
			 t32_tx_ld(p, T32_TIZ_LUMA), t32_tx_ld(p, T32_TIZ_LUMA + 4), t32_tx_ld(p, T32_TIZ_LUMA + 8));
	t32_tx_os_printk("tiziano para awb: MfPara :%d %d %d %d %d %d\n",
			 t32_tx_ld(p, T32_TIZ_AWB), t32_tx_ld(p, T32_TIZ_AWB + 4), t32_tx_ld(p, T32_TIZ_AWB + 8),
			 t32_tx_ld(p, T32_TIZ_AWB + 12), t32_tx_ld(p, T32_TIZ_AWB + 16), t32_tx_ld(p, T32_TIZ_AWB + 20));
	t32_tx_os_g_riscv_info(ch, 1, p);
	return 0;
}

#endif
