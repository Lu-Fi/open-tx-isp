/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 MSCA register access helpers: readable C of the vendor's small MSCA
 * leaf functions (address FIFO, IR FIFO, RAM port, shadow-RAM flag, work
 * state, DMA stride, channel enable).
 *
 * The vendor reaches the MSCA block two ways.  system_reg_write/read go to
 * the register directly.  tisp_reg_write/read (group, index, value) go to
 * the register when t32_data_d03a0 == 0 and into the register shadow when
 * it is 1; the shadow index of a register is a constant per register
 * (reg 0xf0130 + 0x100 * ch <-> index 52 + 32 * ch).  struct t32_ms_io models
 * both: shadow selects the form, writes are recorded as the vendor's callee
 * sees them (system_reg_write: address; tisp_reg_write in shadow mode:
 * 0x80000000 | group << 16 | index).
 *
 * Checked against the vendor functions (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_mscad_oracle).
 */
#ifndef TX_ISP_T32_MSCA_IO_H
#define TX_ISP_T32_MSCA_IO_H

#include "tx_isp_t32_tp.h"

struct t32_ms_io {
	u8 shadow;			/* t32_data_d03a0 */
	struct t32_front_write *w;	/* writes in order */
	unsigned int n;
	u32 (*rd)(u32 reg);		/* register read (address, or shadow word) */
};

#define T32_MS_SHADOW_IDX(grp, idx)	(0x80000000u | ((u32)(grp) << 16) | (idx))

/* system_reg_write */
static inline void t32_ms_sw(struct t32_ms_io *io, u32 reg, u32 val)
{
	T32_FWR(io->w, io->n, reg, val);
}

static inline u32 t32_ms_sr(struct t32_ms_io *io, u32 reg)
{
	return io->rd(reg);
}

/* tisp_reg_write(grp, shadow ? index : reg, val) */
static inline void t32_ms_tw(struct t32_ms_io *io, u32 grp, u32 idx, u32 reg, u32 val)
{
	t32_ms_sw(io, io->shadow ? T32_MS_SHADOW_IDX(grp, idx) : reg, val);
}

static inline u32 t32_ms_tr(struct t32_ms_io *io, u32 grp, u32 idx, u32 reg)
{
	return io->rd(io->shadow ? T32_MS_SHADOW_IDX(grp, idx) : reg);
}

/* tisp_msca_ram_write(a, b): two words to the RAM port, b first (the vendor passes a1 through unchanged) */
static inline void t32_msca_ram_write(struct t32_ms_io *io, u32 a, u32 b)
{
	t32_ms_sw(io, 0xf8004, b);
	t32_ms_sw(io, 0xf8004, a);
}

/*
 * tisp_msca_addr_fifo_clear_byindex: pulse the four FIFO clears of channel
 * ch (Y 0x144, UV 0x164, Y done 0x148, UV done 0x168).  The vendor does
 * nothing in shadow mode (it returns the mode value).
 */
static inline void t32_msca_addr_fifo_clear_byindex(struct t32_ms_io *io, u32 ch)
{
	u32 b = 0xf0000 + ((ch & 0xff) << 8);

	if (io->shadow)
		return;
	t32_ms_sw(io, b + 0x144, 1);
	t32_ms_sw(io, b + 0x164, 1);
	t32_ms_sw(io, b + 0x148, 1);
	t32_ms_sw(io, b + 0x168, 1);
}

/* tisp_msca_addr_fifo_clear_reg_byindex: the same four clears, no mode test */
static inline void t32_msca_addr_fifo_clear_reg_byindex(struct t32_ms_io *io, u32 ch)
{
	u32 b = 0xf0000 + ((ch & 0xff) << 8);

	t32_ms_sw(io, b + 0x144, 1);
	t32_ms_sw(io, b + 0x164, 1);
	t32_ms_sw(io, b + 0x148, 1);
	t32_ms_sw(io, b + 0x168, 1);
}

/* tisp_msca_addr_fifo_clear_all: the clears of the three channels (nothing in shadow mode) */
static inline void t32_msca_addr_fifo_clear_all(struct t32_ms_io *io)
{
	u32 ch;

	if (io->shadow)
		return;
	for (ch = 0; ch < 3; ch++) {
		u32 b = 0xf0000 + (ch << 8);

		t32_ms_sw(io, b + 0x144, 1);
		t32_ms_sw(io, b + 0x164, 1);
		t32_ms_sw(io, b + 0x148, 1);
		t32_ms_sw(io, b + 0x168, 1);
	}
}

/* tisp_msca_addr_fifo_write: push the Y (0x130) and UV (0x150) addresses */
static inline void t32_msca_addr_fifo_write(struct t32_ms_io *io, u32 grp, u32 ch, u32 y, u32 uv)
{
	u32 c = ch & 0xff;

	t32_ms_tw(io, grp & 0xff, c * 32 + 52, 0xf0130 + c * 0x100, y);
	t32_ms_tw(io, grp & 0xff, c * 32 + 60, 0xf0150 + c * 0x100, uv);
}

/*
 * tisp_msca_addr_fifo_read: the Y / UV address words (low 3 bits cleared).
 * Shadow mode reads the push words (index 52 / 60), direct mode the
 * status words 0x138 / 0x158.
 */
static inline void t32_msca_addr_fifo_read(struct t32_ms_io *io, u32 grp, u32 ch, u32 *y, u32 *uv)
{
	u32 c = ch & 0xff;

	if (io->shadow) {
		*y = t32_ms_tr(io, grp & 0xff, c * 32 + 52, 0) & ~7u;
		*uv = t32_ms_tr(io, grp & 0xff, c * 32 + 60, 0) & ~7u;
	} else {
		*y = t32_ms_sr(io, 0xf0138 + c * 0x100) & ~7u;
		*uv = t32_ms_sr(io, 0xf0158 + c * 0x100) & ~7u;
	}
}

/* tisp_msca_addr_fifo_state: 1 when the channel's FIFO is not empty (0 in shadow mode) */
static inline u32 t32_msca_addr_fifo_state(struct t32_ms_io *io, u32 ch)
{
	if (io->shadow)
		return 0;
	return (t32_ms_sr(io, 0xf013c + (ch & 0xff) * 0x100) & 1) ^ 1;
}

/* tisp_msca_work_state: bit n of the work status word */
static inline u32 t32_msca_work_state(struct t32_ms_io *io, u32 n)
{
	return (t32_ms_sr(io, 0xf0054) >> (n & 31)) & 1;
}

/* tisp_msca_ir_init */
static inline void t32_msca_ir_init(struct t32_ms_io *io, u32 a, u32 b, u32 len)
{
	t32_ms_sw(io, 0x20054, ((b & 0xff) << 16) | (a & 0xff));
	t32_ms_sw(io, 0x20060, len & 0xffff);
}

/* tisp_msca_addr_ir_fifo_read / _write */
static inline u32 t32_msca_addr_ir_fifo_read(struct t32_ms_io *io)
{
	return t32_ms_sr(io, 0x20068);
}

static inline void t32_msca_addr_ir_fifo_write(struct t32_ms_io *io, u32 v)
{
	t32_ms_sw(io, 0x20058, v);
}

/* tisp_msca_get_shdram_flag: top half of 0xf09e0 */
static inline u32 t32_msca_get_shdram_flag(struct t32_ms_io *io)
{
	return t32_ms_sr(io, 0xf09e0) >> 16;
}

/* lib_tisp_msca_chx_reg_stream_off: clear the channel's enable bit in 0xf0040 */
static inline void t32_msca_chx_reg_stream_off(struct t32_ms_io *io, u32 ch)
{
	t32_ms_sw(io, 0xf0040, t32_ms_sr(io, 0xf0040) & ~(1u << ((ch & 0xff) & 31)));
}

/* lib_tisp_get_msca_chx_en: channel enable bit (word 17 in shadow mode) */
static inline u32 t32_msca_get_chx_en(struct t32_ms_io *io, u32 grp, u32 ch)
{
	return (t32_ms_tr(io, grp & 0xff, 17, 0xf0040) & (1u << ((ch & 0xff) & 31))) != 0;
}

/* lib_tisp_api_set_msca_dma_stride: both strides = (y + uv + 15) & ~15 */
static inline void t32_msca_set_dma_stride(struct t32_ms_io *io, u32 grp, u32 ch, u32 a, u32 b)
{
	u32 c = ch & 0xff, s = (a + b + 15) & ~15u;

	t32_ms_tw(io, grp & 0xff, c * 32 + 56, 0xf0140 + c * 0x100, s);
	t32_ms_tw(io, grp & 0xff, (c + 2) * 32, 0xf0160 + c * 0x100, s);
}

/*
 * tisp_msca_set_shdram_flag(chn, op, cnt): 0xf09e0 is the shadow-RAM flag
 * (token 0x5a in bits 23:16).  op 0 rewrites the low half, op 2 also stamps
 * the token, op 1 selects the RAM port address from the channel's word at
 * +302 (w302, NULL when chn >= 4 or the channel is not set up) and counts
 * in *cnt.  Returns the number of isp_printf errors (0 or 1).
 */
static inline unsigned int t32_msca_set_shdram_flag(struct t32_ms_io *io, u32 chn, u32 op, const u16 *w302, u16 *cnt)
{
	u32 v = t32_ms_sr(io, 0xf09e0);

	op &= 0xff;
	if (op == 0) {
		t32_ms_sw(io, 0xf09e0, v & 0xffff);
	} else if (op == 2) {
		t32_ms_sw(io, 0xf09e0, (v & 0xffff) | 0x5a0000);
	} else if (op == 1) {
		if (chn >= 4 || !w302)
			return 1;
		t32_ms_sw(io, 0xf8004, 0x5a0000 | (*w302 & 0x1fff));
		t32_ms_sw(io, 0xf8004, 0xf09e0);
		if (cnt)
			(*cnt)++;
	} else {
		return 1;
	}
	return 0;
}

#endif /* TX_ISP_T32_MSCA_IO_H */
