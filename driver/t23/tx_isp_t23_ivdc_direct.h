/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T23 IVDC direct mode, frame channel 0 (openimp #767): the register values
 * the vendor module writes to the IVDC block (0x13200000), as pure helpers
 * shared with tests/t23_ivdc_direct_host.c.  Written from the decompiled
 * vendor flow of tx-isp-t23.ko (ISVP T23 1.3.0), no vendor code copied:
 *
 *   ivdc_core_ops_init @0x1c2c0      ISP 0xd060 = 1; direct: ISP 0x1c top
 *                                    bits = 0b101, IVDC 0x0 = 1 and, while
 *                                    bit 0 stays set, 0x70 = 0x88 = 0x30
 *   ivdc_pad_event_handle @0x1cdac
 *     0x3000002 (channel 0 format)   geometry, see t23_ivdc_geometry()
 *     0x3000007 (channel 0 reqbufs)  DDR ring, see t23_ivdc_ring()
 *     0x3000003 (channel 0 stream on) irq on (0x40 = 0x244fc0ff), 0x4 = 2,
 *                                    0x8 = 0xc
 *     0x3000004 (stream off)         irq off
 *   tisp_msca_chx_cfg_load @0x29d18  channel 0 strides 0xd140 = 0xd160 =
 *                                    0x1000 (direct_mode > 0)
 *   ispcore_pad_event_handle @0x79a40 channel 0 QBUF: MSCA address FIFO
 *                                    Y 0x80000000, UV 0x81000000
 *   ispcore_interrupt_service_routine @0x7b6b4  core status bit 0x1000
 *                                    (frame start) completes a channel 0
 *                                    buffer; bit 0 drains the channel 0
 *                                    FIFO back into itself
 *
 * Checked against the QEMU register trace of the vendor stack
 * (tests/t23/vendor-1.3.0-ivdc-regs.txt).
 */
#ifndef TX_ISP_T23_IVDC_DIRECT_H
#define TX_ISP_T23_IVDC_DIRECT_H

#define T23_IVDC_Y_ADDR		0x80000000U
#define T23_IVDC_UV_ADDR	0x81000000U
#define T23_IVDC_STRIDE		0x1000U
#define T23_IVDC_IRQ_MASK	0x244fc0ffU
#define T23_IVDC_MAX_REGS	20

struct t23_ivdc_reg {
	unsigned int off;
	unsigned int val;
};

/* Channel 0 format (pad event 0x3000002).  mode_now: the current value of
 * IVDC 0x24 (written as 0x388 only when its bits 10:8 are clear).
 * *threshold_line: ivdc_threshold_line, 0 = half the height (set here, as
 * the vendor keeps it).  Returns the number of writes in out[], in order. */
static inline int t23_ivdc_geometry(unsigned int w, unsigned int h,
				    unsigned int mode_now,
				    unsigned int *threshold_line,
				    struct t23_ivdc_reg *out)
{
	int n = 0;
	unsigned int hi = (h & 0xfffU) << 16;

#define T23_IVDC_W(o, v) do { out[n].off = (o); out[n].val = (v); n++; } while (0)
	T23_IVDC_W(0x10, (w & 0xfffU) | hi);
	T23_IVDC_W(0x14, ((h >> 1) & 0xfffU) << 16 | ((w + 7U) & 0xff8U));
	T23_IVDC_W(0x18, ((h + 7U) & 0xff8U) << 16 | ((w + 15U) & 0xff0U));
	T23_IVDC_W(0x1c, ((w + 31U) & 0xfe0U) | hi);
	T23_IVDC_W(0x20, ((w + 255U) >> 8) << 16 | ((w + 255U) & 0xffffff00U));
	if (!(mode_now & 0x700U))
		T23_IVDC_W(0x24, 0x388U);
	T23_IVDC_W(0x60, 0x702U);
	T23_IVDC_W(0x64, 7U);
	T23_IVDC_W(0x84, 1U);
	T23_IVDC_W(0x68, 0x10000U);
	T23_IVDC_W(0x6c, 0x3000001U);
	if (!*threshold_line)
		*threshold_line = h >> 1;
	T23_IVDC_W(0xa4, *threshold_line);
	T23_IVDC_W(0xa8, h - *threshold_line);
	T23_IVDC_W(0x3c, 0x10000000U);
	T23_IVDC_W(0x48, 0x20U);
	T23_IVDC_W(0x4c, 0x20U);
	T23_IVDC_W(0x50, 0x20U);
	T23_IVDC_W(0x1c0, h << 16 | w);
	T23_IVDC_W(0x9c, 0x80U);
#undef T23_IVDC_W
	return n;
}

/* Bytes of the DDR ring for a w x h channel 0 with *mem_line lines
 * (ivdc_mem_line; 0 = a quarter of the height, set here as the vendor
 * does, which also sizes that case as h * 3 * stride / 8). */
static inline unsigned int t23_ivdc_ring_bytes(unsigned int w, unsigned int h,
					       unsigned int *mem_line)
{
	unsigned int aw = (w + 255U) & 0xffffff00U;

	if (!*mem_line) {
		*mem_line = h >> 2;
		return h * 3U * aw >> 3;
	}
	return *mem_line * 3U * aw >> 1;
}

/* Channel 0 reqbufs (pad event 0x3000007) with the ring at paddr.
 * *direct_mode: 1 for widths up to 3072; wider pictures switch it to 2
 * (vendor).  Returns the number of writes, -1 when direct_mode is not 1
 * for a narrow picture (vendor: -EINVAL). */
static inline int t23_ivdc_ring(unsigned int w, unsigned int mem_line,
				unsigned int paddr, int *direct_mode,
				struct t23_ivdc_reg *out)
{
	unsigned int aw = (w + 255U) & 0xffffff00U;
	unsigned int line = mem_line, half = mem_line / 2U;
	int n = 0;

	if (w <= 0xc00U) {
		if (*direct_mode != 1)
			return -1;
		out[n].off = 0x28; out[n].val = 0; n++;
	} else {
		*direct_mode = 2;
		out[n].off = 0x28; out[n].val = 0x48000U / aw; n++;
	}
	if (line > 0xf00U)
		line = 0xf00U;
	if (half > 0xf00U)
		half = 0xf00U;
	out[n].off = 0x2c; out[n].val = 0; n++;
	out[n].off = 0x30; out[n].val = half << 16 | line; n++;
	out[n].off = 0x34; out[n].val = paddr; n++;
	out[n].off = 0x38; out[n].val = aw * line + paddr; n++;
	return n;
}

/*
 * openimp #800: ISP top register 0x1c bits 31:29 = 0b101 route the MSCA
 * channel 0 output into the IVDC.  The core start (tisp_init order) writes
 * 0x1c = 0 / 8; the vendor re-applies the bits with ivdc_core_ops_init(sd, 1)
 * at every ISP enable and drops its state to 2 at every disable.  Without
 * the bits the MSCA writes channel 0 to the IVDC addresses as real bus
 * addresses: 0x80000000 / 0x81000000 alias to DDR 0 / 16 MB on a 32 MB part
 * (kernel text, exception vector).  Returns 1 when the init must run
 * (again): direct mode on and the bits are not set, or the state is not 3.
 */
#define T23_IVDC_TOP_MASK	0xe0000000U
#define T23_IVDC_TOP_DIRECT	0xa0000000U

static inline int t23_ivdc_needs_init(int direct_mode, int state,
				      unsigned int top1c)
{
	if (direct_mode <= 0)
		return 0;
	return state != 3 || (top1c & T23_IVDC_TOP_MASK) != T23_IVDC_TOP_DIRECT;
}

/*
 * #800 (beyond vendor, safety): the value the core start writes to ISP 0x1c.
 * The vendor writes 0 / 8 in tisp_init and sets the direct bits only later
 * in ivdc_core_ops_init(sd, 1); in between the core runs with channel 0
 * still enabled from the last session (msca_keep_enabled), so one frame
 * there goes to DDR 0 (kernel vectors).  In direct mode 1 the core start
 * keeps the direct bits in the same write.
 */
static inline unsigned int t23_ivdc_core_top1c(int direct_mode, unsigned int v)
{
	if (direct_mode != 1)
		return v;
	return (v & ~T23_IVDC_TOP_MASK) | T23_IVDC_TOP_DIRECT;
}

/* 1 when an IVDC address would reach a non-direct MSCA (the DDR alias case) */
static inline int t23_ivdc_tag_unsafe(int direct_mode, unsigned int top1c)
{
	return direct_mode > 0 &&
	       (top1c & T23_IVDC_TOP_MASK) != T23_IVDC_TOP_DIRECT;
}

#endif
