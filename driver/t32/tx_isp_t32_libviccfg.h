/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP library VIC input configuration.
 *
 *   t32_lib_vic_cfg  lib_tisp_vic_cfg 0xad1ac (a0: channel = a0 & 0xff, attr: sensor input attributes):
 *                    lib_tisp_vic_reset(a0, attr), then by the interface type (attr word 20) the VIC registers of
 *                    the channel (base (ch + 128) << 12, direct system_reg_write):
 *                    1 DVP:  state mode 2, sync / polarity / timing words, bus width -> state word 4 and the line
 *                            stride in 32-bit units, the DVP control word from attr words 68 .. 124, crop / blank
 *                            halfwords, the YUV / RAW / BT path (attr word 120, e3f00 word 3, channel);  returns 0.
 *                    2 MIPI: state mode 1, the data format (e3f00 word 2: RAW 0x5100.., YUV 0x5200.., ...) and
 *                            attr words 24 / 28 give the input control word and state word 4, attr bytes 36 / 37
 *                            (== 2) add bits 1 / 0, image size / crop / frame size, start sequence 2, 4, 1;
 *                            returns 0, or -1 for an unknown format / sub mode (the registers are still written).
 *                    5 / 4 / 3 BT.601 / BT.656 / BT.1120 like modes (VIC mode 4 / 0 / 1): fixed control words,
 *                            attr word 24 must be 0 (mode 3: 0 or 1), else print + -1; mode 4 also writes the
 *                            absolute words 464 / 468 (vendor: no channel base); start 2 and a readback for the print.
 *                    other: print, -1.
 *     Every isp_printf of the vendor is the log hook (level, source line); its other arguments are not ported.
 *
 * Vendor behaviour kept: the channel is not range-checked; DVP bus widths other than 0 / 1 / 2 / 7 give a stride
 * of 0 and keep state word 4; a bad MIPI format still writes the registers and returns -1.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libviccfg_oracle).
 */
#ifndef TX_ISP_T32_LIBVICCFG_H
#define TX_ISP_T32_LIBVICCFG_H

#ifndef __KERNEL__
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

#define T32_VC_PAR_SIZE		904U
#define T32_VC_MIPI_ERR		0xffffffffU

/* VIC register offsets from the channel base */
#define T32_VC_START		16U
#define T32_VC_SIZE		20U
#define T32_VC_MODE		28U
#define T32_VC_CTRL		32U
#define T32_VC_DVP_WIDTH	36U
#define T32_VC_FRAME		40U
#define T32_VC_CROP		60U
#define T32_VC_STRIDE		256U
#define T32_VC_DVP_CROP0	260U
#define T32_VC_DVP_CROP1	264U
#define T32_VC_DVP_CTRL		268U
#define T32_VC_DVP_HS		272U
#define T32_VC_PATH		416U
#define T32_VC_PATH1		420U
#define T32_VC_PATH2		424U
#define T32_VC_PATH3		428U
#define T32_VC_PATH4		432U
#define T32_VC_PATH5		436U
#define T32_VC_BT		512U
#define T32_VC_BT1		516U

struct t32_vc_hooks {
	void *ctx;
	void (*reset)(void *ctx, u32 a0, const u8 *attr);	/* lib_tisp_vic_reset */
	void (*wr)(void *ctx, u32 reg, u32 val);		/* system_reg_write */
	u32 (*rd)(void *ctx, u32 reg);				/* system_reg_read */
	void (*log)(void *ctx, u32 level, u32 line);		/* isp_printf */
};

struct t32_vc_env {
	u32 **state;		/* e3f20[]: VIC state, word 0 mode, word 1 bus code */
	const u32 *fmt;		/* e3f00: 4 words per channel (width, height, MIPI format, DVP path) */
	const u8 *par;		/* dc284: 904 B per channel, words 0 / 1 = sensor width / height */
};

static inline u32 t32_vc_rd32(const u8 *p, u32 o)
{
	u32 v;

	memcpy(&v, p + o, 4);
	return v;
}

static inline u32 t32_vc_rd16(const u8 *p, u32 o)
{
	u16 v;

	memcpy(&v, p + o, 2);
	return v;
}

/* attr word 20 == 1 */
static inline u32 t32_vc_dvp(const struct t32_vc_hooks *h, const struct t32_vc_env *e, u32 ch, const u8 *a)
{
	const u32 *f = e->fmt + 4 * ch;
	u32 *st = e->state[ch];
	u32 r = (ch + 128) << 12, bus = t32_vc_rd32(a, 128), m = 0, v, path = t32_vc_rd32(a, 120);

	h->log(h->ctx, 0, 152);
	st[0] = 2;
	if (t32_vc_rd32(a, 24) == 1) {
		h->log(h->ctx, 1, 155);
		h->wr(h->ctx, r + T32_VC_CTRL, 0x20000);
		h->wr(h->ctx, r + T32_VC_PATH, 0x100010);
	} else {
		h->log(h->ctx, 0, 159);
		h->wr(h->ctx, r + T32_VC_PATH1, 0xa000a);
	}
	if (bus == 0 || bus == 1 || bus == 2 || bus == 7) {	/* 8 / 10 / 12 / 16 bit */
		st[1] = bus == 7 ? 4 : bus;
		m = bus == 7 ? 16 : 8 + 2 * bus;
	}
	v = m * t32_vc_rd32(a, 48);
	h->wr(h->ctx, r + T32_VC_STRIDE, (v >> 5) + ((v & 0x1f) != 0));
	h->wr(h->ctx, r + T32_VC_MODE, 2);
	h->wr(h->ctx, r + T32_VC_DVP_WIDTH, bus);
	h->wr(h->ctx, r + T32_VC_SIZE, f[0] << 16 | f[1]);
	h->wr(h->ctx, r + T32_VC_DVP_CTRL, t32_vc_rd32(a, 68) << 25 | t32_vc_rd32(a, 72) << 24 | t32_vc_rd32(a, 124) |
	      t32_vc_rd32(a, 76) << 23 | t32_vc_rd32(a, 96) << 22 | t32_vc_rd32(a, 100) << 20 |
	      t32_vc_rd32(a, 104) << 18 | t32_vc_rd32(a, 108) << 12 | t32_vc_rd32(a, 112) << 8 | path << 4 |
	      t32_vc_rd32(a, 116) << 2);
	h->wr(h->ctx, r + T32_VC_DVP_HS, t32_vc_rd32(a, 48) << 16 | t32_vc_rd16(a, 80));
	for (v = 0; v < 3; v++)
		h->wr(h->ctx, r + T32_VC_DVP_HS + 4 + 4 * v, t32_vc_rd16(a, 84 + 4 * v));
	if (path == 0) {
		u32 bt = 0x20040;

		if (f[3] == 2) {
			h->wr(h->ctx, r + T32_VC_BT, 0);
			h->wr(h->ctx, r + T32_VC_BT1, 0);
		} else if (f[3] == 1) {
			h->wr(h->ctx, r + T32_VC_BT, 0x80000011);
			h->wr(h->ctx, r + T32_VC_BT1, 0);
			bt = 0x20000;
		} else if (f[3] != 3 || ch == 0) {
			h->wr(h->ctx, r + T32_VC_BT, 0x80010001);
			if (ch < 2)
				h->wr(h->ctx, r + T32_VC_BT1, ch ? 32 : 16);
		}
		h->wr(h->ctx, r + T32_VC_PATH3, bt);
		h->wr(h->ctx, r + T32_VC_PATH2, 17472);
	} else if (path == 1 || path == 2) {
		v = path == 1 ? 17424 : 17440;
		h->wr(h->ctx, r + T32_VC_PATH3, v);
		h->wr(h->ctx, r + T32_VC_PATH2, v);
	} else {
		h->log(h->ctx, 2, 238);
	}
	h->wr(h->ctx, r + T32_VC_PATH4, 16);
	h->wr(h->ctx, r + T32_VC_PATH, path << 4 | t32_vc_rd32(a, 124));
	h->wr(h->ctx, r + T32_VC_DVP_CROP0, t32_vc_rd16(a, 86) << 16 | t32_vc_rd16(a, 82));
	h->wr(h->ctx, r + T32_VC_DVP_CROP1, t32_vc_rd16(a, 94) << 16 | t32_vc_rd16(a, 90));
	return 0;
}

/* MIPI input control word (and state word 4) of format fmt; *ret = -1 on an unknown format / sub mode */
static inline u32 t32_vc_mipi_ctrl(const struct t32_vc_hooks *h, u32 *st, u32 fmt, const u8 *a, u32 *ret)
{
	u32 sub = t32_vc_rd32(a, 24), lanes = t32_vc_rd32(a, 28);

	if ((fmt >= 20992 && fmt < 20996) || (fmt >= 21248 && fmt < 21264)) {	/* YUV */
		st[1] = 0;
		if (sub == 3 || sub == 4)
			return sub == 4 ? 0x100000 : 0;
		h->log(h->ctx, 1, 284);
	} else if ((fmt >= 20996 && fmt < 21000) || (fmt >= 21264 && fmt < 21280)) {	/* RAW */
		st[1] = 1;
		if (sub == 0 || sub == 1)
			return (lanes == 2 ? 0x30000 : 0x20000) | sub << 20;
		h->log(h->ctx, 1, lanes == 2 ? 315 : 324);
	} else if ((fmt >= 21000 && fmt < 21004) || (fmt >= 21280 && fmt < 21296)) {
		st[1] = 2;
		return lanes == 2 ? 0x50000 : 0x40000;
	} else if (fmt - 20736 < 4) {
		return 0xc0000;
	} else {
		h->log(h->ctx, 1, 364);
	}
	*ret = T32_VC_MIPI_ERR;
	return 0;
}

/* attr word 20 == 2 */
static inline u32 t32_vc_mipi(const struct t32_vc_hooks *h, const struct t32_vc_env *e, u32 ch, const u8 *a)
{
	const u8 *p = e->par + ch * T32_VC_PAR_SIZE;
	u32 r = (ch + 128) << 12, ret = 0, c, fw = t32_vc_rd16(a, 34) << 16;

	h->log(h->ctx, 0, 251);
	e->state[ch][0] = 1;
	h->wr(h->ctx, r + T32_VC_MODE, 3);
	c = t32_vc_mipi_ctrl(h, e->state[ch], e->fmt[4 * ch + 2], a, &ret);
	if (a[36] == 2)
		c |= 2;
	if (a[37] == 2)
		c |= 1;
	if (fw)
		h->wr(h->ctx, r + T32_VC_FRAME, fw + t32_vc_rd32(p, 0));
	if (t32_vc_rd16(a, 32))
		h->wr(h->ctx, r + T32_VC_CROP, t32_vc_rd16(a, 32));
	h->wr(h->ctx, r + T32_VC_FRAME, fw + t32_vc_rd32(p, 0));
	h->wr(h->ctx, r + T32_VC_CTRL, t32_vc_rd32(a, 40) << 31 | c);
	h->wr(h->ctx, r + T32_VC_SIZE, t32_vc_rd32(p, 0) << 16 | t32_vc_rd32(p, 4));
	h->wr(h->ctx, r + T32_VC_START, 2);
	h->wr(h->ctx, r + T32_VC_START, 4);
	h->wr(h->ctx, r + T32_VC_START, 1);
	h->wr(h->ctx, r + T32_VC_PATH1, 0x100010);
	h->wr(h->ctx, r + T32_VC_PATH3, 16912);
	h->wr(h->ctx, r + T32_VC_PATH4, 16);
	h->wr(h->ctx, r + T32_VC_PATH5, 0);
	h->wr(h->ctx, r + T32_VC_PATH2, 17472);
	return ret;
}

/* attr word 20 == 3, 4, 5: VIC mode 1, 0, 4 */
static inline u32 t32_vc_bt(const struct t32_vc_hooks *h, const struct t32_vc_env *e, u32 ch, const u8 *a, u32 type)
{
	static const u16 line[3][3] = { { 441, 454, 470 }, { 418, 436, 434 }, { 400, 414, 412 } };	/* go, bad, done */
	static const u8 mode[3] = { 1, 0, 4 };
	const u8 *p = e->par + ch * T32_VC_PAR_SIZE;
	const u16 *l = line[type - 3];
	u32 r = (ch + 128) << 12, sub = t32_vc_rd32(a, 24);

	h->log(h->ctx, 0, l[0]);
	h->wr(h->ctx, r + T32_VC_MODE, mode[type - 3]);
	if (sub != 0 && (type != 3 || sub != 1)) {
		h->log(h->ctx, 0, l[1]);
		return T32_VC_MIPI_ERR;
	}
	h->wr(h->ctx, r + T32_VC_SIZE, t32_vc_rd32(p, 0) << 16 | t32_vc_rd32(p, 4));
	if (type != 5)
		h->wr(h->ctx, r + T32_VC_MODE, mode[type - 3]);
	h->wr(h->ctx, r + T32_VC_CTRL, type != 3 ? 0x800c0000 : sub ? 0x88060820 : 0x800c8000);
	h->wr(h->ctx, r + T32_VC_FRAME, t32_vc_rd32(p, 0) << 1 | (type == 3 ? 0x100000 : 0));
	if (type == 3) {
		static const u32 bt1120[5][2] = { { 60, 48 }, { 44, 440 }, { 48, 0x1402d0 }, { 52, 0x50014 },
						  { 56, 0x2d00014 } };
		unsigned int i;

		for (i = 0; i < 5; i++)
			h->wr(h->ctx, r + bt1120[i][0], bt1120[i][1]);
		h->wr(h->ctx, r + T32_VC_PATH, 0);
	}
	h->wr(h->ctx, r + T32_VC_PATH1, 0x100010);
	h->wr(h->ctx, r + T32_VC_PATH3, 17412);
	if (type == 4) {
		h->wr(h->ctx, 464, 512);	/* vendor: absolute, no channel base */
		h->wr(h->ctx, 468, 512);
	}
	h->wr(h->ctx, r + T32_VC_START, 2);
	h->rd(h->ctx, r + T32_VC_START);
	h->log(h->ctx, 0, l[2]);
	return 0;
}

static inline u32 t32_lib_vic_cfg(const struct t32_vc_hooks *h, const struct t32_vc_env *e, u32 a0, const u8 *attr)
{
	u32 ch = a0 & 0xff, type;

	h->reset(h->ctx, a0, attr);
	type = t32_vc_rd32(attr, 20);
	if (type == 1)
		return t32_vc_dvp(h, e, ch, attr);
	if (type == 2)
		return t32_vc_mipi(h, e, ch, attr);
	if (type >= 3 && type <= 5)
		return t32_vc_bt(h, e, ch, attr, type);
	h->log(h->ctx, 1, 472);
	return T32_VC_MIPI_ERR;
}

#endif
