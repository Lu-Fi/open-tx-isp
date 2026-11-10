/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP front end: colour space conversion (CSC), chroma range (CSCCR)
 * and the ISP top block, initialised from the IQ bin as the vendor tisp does
 * at AddSensor (S4).  Register writes in the vendor's order.
 *
 * Reverse engineered flow (PRJ007 driver): tisp_csc_init / _update_para /
 * _write_reg, tisp_csccr_init / _update_para / _write_reg, tisp_top_init and
 * tisp_top_reg_cfg.  Parameter blocks are IQ bin sections 9 (CSC, 104
 * bytes), 10 (CSCCR, 20 bytes) and 0 (top, 100 bytes; one byte per control
 * bit).  Checked against the vendor's writes in the QEMU trace (run v1, cv8001
 * PRJ007 bin, 3840x2160 SRGGB10): host test tx_isp_t32_front_test.
 *
 * Only the default parameter path is built (CSC/CSCCR taken from the bin, no
 * mode table, no day/night switch, single pipeline, no WDR).
 */
#ifndef TX_ISP_T32_FRONT_H
#define TX_ISP_T32_FRONT_H

#ifndef __KERNEL__
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

#define T32_FRONT_ID		0x0134da11U
#define T32_FRONT_MAX_WRITES	48

/* IQ bin section indexes (tx_isp_t32_iq.h walks the table) */
#define T32_IQ_SEC_TOP		0
#define T32_IQ_SEC_CSC		9
#define T32_IQ_SEC_CSCCR	10

#define T32_CSC_BYTES		104U
#define T32_CSCCR_BYTES		20U
#define T32_TOP_BYTES		100U

struct t32_front_write {
	u32 reg, val;
};

/* input_mbus_fmt of the vendor ABI (tx-isp-videodev.h) */
#define T32_MBUS_SBGGR8		0x5200U
#define T32_MBUS_SRGGB12	0x520bU

/*
 * isp_mbus_to_bayer, for the Bayer codes of 8, 10 and 12 bits (0x5200 ..
 * 0x520b): bit depth class (0, 1, 2) << 16 | colour order (BGGR 1, GBRG 3,
 * GRBG 2, RGGB 0).  -1 for codes the front end does not take here.
 */
static inline int t32_mbus_to_bayer(u32 code)
{
	static const u8 order[4] = { 1, 3, 2, 0 };

	if (code < T32_MBUS_SBGGR8 || code > T32_MBUS_SRGGB12)
		return -1;
	return (int)((((code - T32_MBUS_SBGGR8) >> 2) << 16) | order[(code - T32_MBUS_SBGGR8) & 3]);
}

static inline u32 t32_front_le32(const u8 *p)
{
	return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static inline int t32_front_s16(const u8 *p)
{
	return (short)(p[0] | p[1] << 8);
}

/* tisp_round_int64(x, sign, 6): arithmetic shift with the bit below rounding up */
static inline int t32_front_round6(int x)
{
	return (x >> 6) + ((x >> 5) & 1);
}

#define T32_FW(w, n, r, v) do { (w)[(n)].reg = (r); (w)[(n)].val = (v); (n)++; } while (0)

/*
 * The vendor loads the matrix of the selected mode (byte 0x5b of the section:
 * BT.601, BT.709, BT.2020, each full or limited range) into its working copy
 * at init, over the bin's own values; the bin supplies the mode and a few
 * offsets.  Rows are Y, Cb, Cr coefficients in Q16 (Y row sums to 65536),
 * then the offset and clip bytes of the mode.
 */
struct t32_csc_mode {
	int m[9];
	u8 tail[5];
};
static const struct t32_csc_mode t32_csc_modes[6] = {
	{ { 19595, 38470, 7471, -11058, -21710, 32768, 32768, -27439, -5329 }, { 0x80, 0x00, 0xff, 0x00, 0xff } },
	{ { 16829, 33039, 6416, -9714, -19071, 28784, 28784, -24103, -4681 }, { 0x80, 0x10, 0xeb, 0x10, 0xf0 } },
	{ { 13933, 46871, 4732, -7509, -25259, 32768, 32768, -29763, -3005 }, { 0x80, 0x00, 0xff, 0x00, 0xff } },
	{ { 11966, 40254, 4064, -6596, -22189, 28784, 28784, -26145, -2639 }, { 0x80, 0x10, 0xeb, 0x10, 0xf0 } },
	{ { 17216, 44433, 3886, -9151, -23617, 32768, 32768, -30133, -2635 }, { 0x80, 0x00, 0xff, 0x00, 0xff } },
	{ { 14786, 38160, 3338, -8038, -20746, 28784, 28784, -26469, -2315 }, { 0x80, 0x10, 0xeb, 0x10, 0xf0 } },
};

/*
 * tisp_csc_init: sec = IQ section 9 (104 bytes); 8 writes plus the block id, 0 for a mode above 5.
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_csc_oracle).
 */
static inline unsigned int t32_csc_init(const u8 *sec, struct t32_front_write *w)
{
	short p[14];	/* the vendor's csc_cp: s16 at byte offsets 0 .. 0x1a */
	u8 b[0x22];
	unsigned int n = 0, i;

	const struct t32_csc_mode *md;

	if (sec[0x5b] > 5)
		return 0;
	md = &t32_csc_modes[sec[0x5b]];
	for (i = 0; i < sizeof(b); i++)
		b[i] = 0;
	b[0] = sec[0x58];
	b[1] = sec[0x59];
	b[2] = sec[0x5a];
	p[0] = p[1] = 0;
	p[2] = (short)t32_front_s16(sec + 0x4c);
	p[3] = (short)t32_front_s16(sec + 0x4e);
	p[4] = (short)t32_front_s16(sec + 0x50);
	p[5] = (short)t32_front_round6(md->m[0]);
	p[6] = (short)t32_front_round6(md->m[1]);
	p[7] = (short)t32_front_round6(md->m[2]);
	p[8] = (short)-t32_front_round6(md->m[3]);
	p[9] = (short)-t32_front_round6(md->m[4]);
	p[10] = (short)t32_front_round6(md->m[5]);
	p[11] = (short)t32_front_round6(md->m[6]);
	p[12] = (short)-t32_front_round6(md->m[7]);
	p[13] = (short)-t32_front_round6(md->m[8]);
	for (i = 0; i < 5; i++)
		b[0x1d + i] = md->tail[i];

	T32_FW(w, n, 0xb040, (b[1] & 7U) << 4 | (b[2] & 7U) << 8 | (b[0] & 1U) |
	       ((u32)p[2] & 0x1ff) << 16);
	T32_FW(w, n, 0xb044, ((u32)p[4] & 0x1ff) << 12 | ((u32)p[3] & 0x1ff));
	T32_FW(w, n, 0xb048, ((u32)p[6] & 0x3ff) << 10 | ((u32)p[7] & 0xff) << 20 | ((u32)p[5] & 0x1ff));
	T32_FW(w, n, 0xb04c, ((u32)p[9] & 0x1ff) << 10 | ((u32)p[10] & 0x3ff) << 20 | ((u32)p[8] & 0x1ff));
	T32_FW(w, n, 0xb050, ((u32)p[12] & 0x1ff) << 10 | ((u32)p[13] & 0x1ff) << 20 | ((u32)p[11] & 0x3ff));
	T32_FW(w, n, 0xb054, b[0x1c] | (u32)b[0x1d] << 8);
	T32_FW(w, n, 0xb058, b[0x1e] | (u32)b[0x1f] << 8 | (u32)b[0x20] << 16 | (u32)b[0x21] << 24);
	T32_FW(w, n, 0xb010, 0xffffffff);
	T32_FW(w, n, 0xb000, T32_FRONT_ID);
	return n;
}

/* CSCCR range pairs per mode (default, stretch, compress): min/max Y in, out, min/max C in, out */
static const u8 t32_csccr_modes[3][8] = {
	{ 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0x00, 0xff },
	{ 0x10, 0xeb, 0x00, 0xff, 0x10, 0xf0, 0x00, 0xff },
	{ 0x00, 0xff, 0x10, 0xeb, 0x00, 0xff, 0x10, 0xf0 },
};

/*
 * tisp_csccr_init: sec = IQ section 10 (20 bytes, mode in byte 4); -1 for modes not built / bad ranges.
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_csccr_oracle).
 */
static inline int t32_csccr_init(const u8 *sec, struct t32_front_write *w)
{
	const u8 *m;
	int y_in, y_out, c_in, c_out, k_y, k_c;
	unsigned int n = 0;

	if (sec[4] > 2)
		return -1;
	m = t32_csccr_modes[sec[4]];
	if (m[1] < m[0] || m[3] < m[2] || m[5] < m[4] || m[7] < m[6] || m[1] == m[0] || m[5] == m[4])
		return -1;
	y_in = m[1] - m[0];
	y_out = m[3] - m[2];
	c_in = m[5] - m[4];
	c_out = m[7] - m[6];
	k_y = (y_out * 0x400 + y_in / 2) / y_in;
	k_c = (c_out * 0x400 + c_in / 2) / c_in;
	if (k_y > 0x7ff)
		k_y = 0x7ff;
	if (k_c > 0x7ff)
		k_c = 0x7ff;
	T32_FW(w, n, 0x14040, (u32)k_y & 0x7ff);
	T32_FW(w, n, 0x14044, ((u32)y_out & 0xff) << 8 | (u32)m[2] << 16 | m[0]);
	T32_FW(w, n, 0x14050, (u32)k_c & 0x7ff);
	T32_FW(w, n, 0x14054, ((u32)(m[7] - 0x80) & 0x1ff) << 16 | ((u32)(m[6] - 0x80) & 0x1ff));
	T32_FW(w, n, 0x14010, 0xffffffff);
	T32_FW(w, n, 0x14000, T32_FRONT_ID);
	return (int)n;
}

/*
 * tisp_top_init (+ tisp_top_reg_cfg): sec = IQ section 0 (100 bytes), bayer
 * from t32_mbus_to_bayer (colour order in bits 0..4, depth class in bits
 * 16..17), info36 = bit 0 of byte 36 of the sensor info the vendor passes
 * (bit 16 of the two Bayer registers).  Pipeline 0, single pipeline, direct
 * register path, no WDR (info byte 39 = 0).  The vendor maps the colour order
 * through a table (identity below 4, +4 up to 19, 0 above) and flags the
 * DE-IR variants (order >= 4).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_top_oracle).
 */
static inline unsigned int t32_top_init(const u8 *sec, u32 width, u32 height, u32 bayer, u32 info36,
					struct t32_front_write *w)
{
	u32 size = width << 16 | height, en = 0, ctl = 0, sta = 0;
	u32 idx = bayer & 0x1f, deir = idx < 4 ? 0 : 1;
	u32 order = idx < 4 ? idx : idx < 20 ? idx + 4 : 0;
	u32 depth = (bayer >> 16 & 3) << 8, bit16 = (info36 & 1) << 16;
	unsigned int n = 0, i;

	/* tisp_top_reg_cfg: module enables (+4), control (+0x24), state (+0x44) */
	for (i = 0; i < 32; i++) {
		en |= (u32)(sec[4 + i] & 1) << i;
		ctl |= (u32)(sec[0x24 + i] & 1) << i;
	}
	for (i = 0; i < 10; i++)
		sta |= (u32)sec[0x44 + i] << i;
	if (deir)
		sta |= 4;
	T32_FW(w, n, 0x70, size);
	T32_FW(w, n, 0x80, size);
	T32_FW(w, n, 0x78, deir << 20 | bit16 | depth | order);
	T32_FW(w, n, 0x88, deir << 20 | bit16 | depth);
	T32_FW(w, n, 0x48, 0xffffffff);
	T32_FW(w, n, 0x48, 0xfff7ffff);
	T32_FW(w, n, 0x100, 4);
	T32_FW(w, n, 0x40, en);
	T32_FW(w, n, 0x60, sta);
	T32_FW(w, n, 0x6c, ctl);
	return n;
}

/*
 * CSCCR per-frame paths (tisp_csccr_switch_params_write_reg, _sync_api_para,
 * _deinit, _set_uv_clip, _param_array_get/_set, _api_set/_get, _wake_up,
 * _dn_params_refresh).  The vendor context t32_data_e3120[ch] holds three
 * pointers: +0 the 20 byte parameter section (mode byte 4, ranges 5..12,
 * flags 14 source, 15 / 16 sync gates), +4 the derived register block and
 * +8 the 12 byte API block (u32 mode, 8 range bytes).  Aux layout used here:
 * u16 +0 Y gain, +2 Y in min, u16 +4 Y out range, +6 Y out min, u16 +8 C gain,
 * +10 / +11 C out min / max.
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15)
 * by tests/t32_oracle (t32_csccrx_oracle).
 */
struct t32_csccr_ctx {
	u8 *param;
	u8 *aux;
	u8 *api;
};

static inline u32 t32_csccr_le16(const u8 *p)
{
	return p[0] | (u32)p[1] << 8;
}

/* tisp_csccr_para_refresh_by_mode: the range table of the mode, modes above 2 leave the ranges alone */
static inline void t32_csccr_para_refresh_by_mode(u32 mode, u8 *ranges)
{
	if (mode <= 2)
		memcpy(ranges, t32_csccr_modes[mode], 8);
}

/*
 * tisp_csccr_update_para: derived block from the ranges of the parameter
 * section.  Open item: when the API block is not synced (gate byte 15 != 1)
 * the vendor may take the API ranges instead (seen with byte 16 = 0); the
 * selector is not built here, the oracle keeps both copies equal.
 */
static inline int t32_csccr_update_para(struct t32_csccr_ctx *ctx)
{
	const u8 *m = ctx->param + 5;
	u8 *a = ctx->aux;
	u32 y_in, y_out, c_in, c_out, ky, kc;

	if (m[1] < m[0] || m[3] < m[2] || m[5] < m[4] || m[7] < m[6] || m[1] == m[0] || m[5] == m[4])
		return -1;
	y_in = m[1] - m[0];
	y_out = m[3] - m[2];
	c_in = m[5] - m[4];
	c_out = m[7] - m[6];
	ky = (y_out * 0x400 + y_in / 2) / y_in;
	kc = (c_out * 0x400 + c_in / 2) / c_in;
	if (ky > 0x7ff)
		ky = 0x7ff;
	if (kc > 0x7ff)
		kc = 0x7ff;
	a[0] = ky & 0xff;
	a[1] = ky >> 8;
	a[2] = m[0];
	a[4] = y_out & 0xff;
	a[5] = y_out >> 8;
	a[6] = m[2];
	a[8] = kc & 0xff;
	a[9] = kc >> 8;
	a[10] = m[6];
	a[11] = m[7];
	return 0;
}

/* the three Y registers shared by tisp_csccr_write_reg and the switch variant */
static inline void t32_csccr_write_yc(const u8 *a, struct t32_front_write *w, unsigned int *n)
{
	T32_FW(w, *n, 0x14040, t32_csccr_le16(a) & 0x7ff);
	T32_FW(w, *n, 0x14044, ((t32_csccr_le16(a + 4) << 8) & 0xffff) | (u32)a[6] << 16 | a[2]);
	T32_FW(w, *n, 0x14050, t32_csccr_le16(a + 8) & 0x7ff);
}

/* tisp_csccr_write_reg (also tisp_csccr_wake_up) */
static inline void t32_csccr_write_reg(const struct t32_csccr_ctx *ctx, struct t32_front_write *w,
				       unsigned int *n)
{
	const u8 *a = ctx->aux;

	t32_csccr_write_yc(a, w, n);
	T32_FW(w, *n, 0x14054, ((u32)(a[11] - 0x80) & 0x1ff) << 16 | ((u32)(a[10] - 0x80) & 0x1ff));
	T32_FW(w, *n, 0x14010, 0xffffffff);
}

/* tisp_csccr_switch_params_write_reg: no UV clip register */
static inline int t32_csccr_switch_params_write_reg(const struct t32_csccr_ctx *ctx, u8 ch,
						    struct t32_front_write *w)
{
	unsigned int n = 0;

	(void)ch;
	t32_csccr_write_yc(ctx->aux, w, &n);
	T32_FW(w, n, 0x14010, 0xffffffff);
	return (int)n;
}

/* tisp_csccr_sync_api_para: dir 1 = parameters to API (gate byte 15), else API to parameters (gate byte 16) */
static inline void t32_csccr_sync_api_para(struct t32_csccr_ctx *ctx, u32 dir)
{
	u8 *p = ctx->param;
	u8 *a = ctx->api;

	if (dir == 1) {
		if (p[15] != 1)
			return;
		a[0] = p[4];
		a[1] = a[2] = a[3] = 0;
		memcpy(a + 4, p + 5, 8);
	} else {
		if (p[16] != 1)
			return;
		p[4] = a[0];
		memcpy(p + 5, a + 4, 8);
	}
}

/* tisp_csccr_set_uv_clip: dn = tisp_day_or_night_g_ctrl (0 day, 1 night, else error without effect) */
static inline void t32_csccr_set_uv_clip(u32 dn, u32 *uv, struct t32_front_write *w, unsigned int *n)
{
	if (dn == 0) {
		T32_FW(w, *n, 0x14054, 0x7fff80);
		T32_FW(w, *n, 0x14010, 0xffffffff);
		*uv = 0x7fff80;
	} else if (dn == 1) {
		T32_FW(w, *n, 0x14054, 0);
		T32_FW(w, *n, 0x14010, 0xffffffff);
		*uv = 0;
	}
}

/* tisp_csccr_param_array_get */
static inline int t32_csccr_param_array_get(const struct t32_csccr_ctx *ctx, u8 *buf, u32 *len)
{
	memcpy(buf, ctx->param, T32_CSCCR_BYTES);
	*len = T32_CSCCR_BYTES;
	return 0;
}

/* tisp_csccr_param_array_set */
static inline int t32_csccr_param_array_set(struct t32_csccr_ctx *ctx, const u8 *buf, u32 dn,
					    u32 *uv, struct t32_front_write *w)
{
	unsigned int n = 0;

	memcpy(ctx->param, buf, T32_CSCCR_BYTES);
	ctx->param[14] = 1;
	t32_csccr_para_refresh_by_mode(ctx->param[4], ctx->param + 5);
	t32_csccr_sync_api_para(ctx, 1);
	t32_csccr_update_para(ctx);
	t32_csccr_write_reg(ctx, w, &n);
	t32_csccr_set_uv_clip(dn, uv, w, &n);
	return (int)n;
}

/* tisp_csccr_api_set: buf = 12 byte API block */
static inline int t32_csccr_api_set(struct t32_csccr_ctx *ctx, const u8 *buf, struct t32_front_write *w)
{
	unsigned int n = 0;

	ctx->param[14] = 2;
	memcpy(ctx->api, buf, 12);
	t32_csccr_para_refresh_by_mode(t32_front_le32(buf), ctx->api + 4);
	t32_csccr_sync_api_para(ctx, 0);
	t32_csccr_update_para(ctx);
	t32_csccr_write_reg(ctx, w, &n);
	return (int)n;
}

/* tisp_csccr_api_get: -1 without a context */
static inline int t32_csccr_api_get(const struct t32_csccr_ctx *ctx, u8 *buf)
{
	if (!ctx)
		return -1;
	memcpy(buf, ctx->api, 12);
	return 0;
}

/* tisp_csccr_wake_up */
static inline int t32_csccr_wake_up(const struct t32_csccr_ctx *ctx, struct t32_front_write *w)
{
	unsigned int n = 0;

	t32_csccr_write_reg(ctx, w, &n);
	return (int)n;
}

/* tisp_csccr_dn_params_refresh: the section is used in place from the tuning base */
static inline int t32_csccr_dn_params_refresh(struct t32_csccr_ctx *ctx, u8 ch, u8 *tuning_base,
					      struct t32_front_write *w)
{
	ctx->param = tuning_base + 27592;
	ctx->param[14] = 1;
	t32_csccr_para_refresh_by_mode(ctx->param[4], ctx->param + 5);
	t32_csccr_sync_api_para(ctx, 1);
	t32_csccr_update_para(ctx);
	return t32_csccr_switch_params_write_reg(ctx, ch, w);
}

/* tisp_csccr_deinit: frees the aux and API blocks and the context, clears the slot */
static inline int t32_csccr_deinit(struct t32_csccr_ctx **slot, void (*vfree)(void *p))
{
	struct t32_csccr_ctx *ctx = *slot;

	if (!ctx)
		return 0;
	if (ctx->aux) {
		vfree(ctx->aux);
		ctx->aux = NULL;
	}
	if (ctx->api) {
		vfree(ctx->api);
		ctx->api = NULL;
	}
	vfree(ctx);
	*slot = NULL;
	return 0;
}

#undef T32_FW

#endif /* TX_ISP_T32_FRONT_H */
