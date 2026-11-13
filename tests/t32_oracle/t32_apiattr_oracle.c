// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our setters with logic (driver/t32/tx_isp_t32_api_attr.h)
 * against the vendor tisp_set_csc_attr, tisp_s_ccm_attr, tisp_s_awb_attr,
 * tisp_s_module_ratio_attr, tisp_set_wdr_output_mode, tisp_set_frame_drop,
 * tisp_set_fps, tisp_set_ae_minmax and tisp_s_raw_rw_control_internal from
 * Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Callees, tisp_reg_read/_write and the sensor fps entry are recording stubs
 * (ours reaches the same stubs through the ops table).  Compared: callee
 * order, every defined argument, the bytes behind stack pointers (AWB mode
 * copy, identity CCM, sensor info block), register reads/writes (address,
 * value, order) with d03a0 = 0, 1 and 2, the number of error prints, the
 * return value, every byte of the user buffer, the attribute block *f0904
 * (all channels) and the AE block.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_attr.h"

extern u32 t32_data_f0904[4];
extern u8 t32_data_dd2d4[0x400];

static u32 rs = 0x2b9d17u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define NREC 16
struct rec { const char *fn; unsigned int n, bl; u32 a[4]; u8 b[128]; };
static struct rec lg[NREC];
static unsigned int nlg;
static u32 stub_ret, top_word, fps_new, sens_ret;
static u8 *buf;

static struct rec *rec(const char *fn, unsigned int n, const u32 *a, const void *blob, unsigned int bl)
{
	static struct rec dummy;
	struct rec *r = nlg < NREC ? &lg[nlg] : &dummy;
	unsigned int i;

	memset(r, 0, sizeof(*r));
	r->fn = fn;
	r->n = n;
	for (i = 0; i < n; i++)
		r->a[i] = a[i];
	if (blob) {
		memcpy(r->b, blob, bl);
		r->bl = bl;
	}
	nlg++;
	return r;
}

#define STUB(name, n) \
	u64 name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ u32 a[4] = { a0, a1, a2, a3 }; rec(#name, n, a, 0, 0); return stub_ret + nlg; }
/* a1 points to bl bytes on the caller's stack: log the bytes, not the address */
#define STUBB(name, bl) \
	u64 name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ u32 a[4] = { a0 }; (void)a2; (void)a3; rec(#name, 1, a, T32O_HOST(a1), bl); return stub_ret + nlg; }
u64 tisp_csc_api_set(u32, u32, u32, u32);
u64 tisp_bcsh_refresh_by_csc(u32, u32, u32, u32);
u64 tisp_clm_refresh_by_csc(u32, u32, u32, u32);
u64 tisp_ccm_api_set(u32, u32, u32, u32);
u64 tisp_bcsh_api_set_ccm(u32, u32, u32, u32);
u64 tisp_awb_set_mode(u32, u32, u32, u32);
u64 tisp_awb_set_start(u32, u32, u32, u32);
u64 tisp_awb_set_frz(u32, u32, u32, u32);
u64 tisp_awb_set_ct(u32, u32, u32, u32);
u64 tisp_s_sdns_ratio(u32, u32, u32, u32);
u64 tisp_s_mdns_ratio(u32, u32, u32, u32);
u64 tisp_tmo_api_strength_ctrl(u32, u32, u32, u32);
u64 tisp_s_dpc_ratio(u32, u32, u32, u32);
u64 tisp_s_defog_ratio(u32, u32, u32, u32);
u64 tisp_wdr_api_set_output_mode(u32, u32, u32, u32);
u64 tisp_ae_api_set_fps(u32, u32, u32, u32);
u64 tisp_ae_clac_deflicker_cfg(u32, u32, u32, u32);
u64 tisp_reg_read(u32, u32, u32, u32);
u64 tisp_reg_write(u32, u32, u32, u32);
STUB(tisp_csc_api_set, 2)
STUB(tisp_bcsh_refresh_by_csc, 1)
STUB(tisp_clm_refresh_by_csc, 1)
STUB(tisp_ccm_api_set, 2)
STUBB(tisp_awb_set_mode, 12)
STUB(tisp_awb_set_start, 2)
STUB(tisp_awb_set_frz, 2)
STUB(tisp_awb_set_ct, 2)
STUB(tisp_s_sdns_ratio, 2)
STUB(tisp_s_mdns_ratio, 2)
STUB(tisp_tmo_api_strength_ctrl, 2)
STUB(tisp_s_dpc_ratio, 2)
STUB(tisp_s_defog_ratio, 2)
STUB(tisp_wdr_api_set_output_mode, 2)
STUB(tisp_ae_clac_deflicker_cfg, 1)
/* the user buffer goes by address, the identity CCM is a stack block */
u64 tisp_bcsh_api_set_ccm(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 a[4] = { a0, a1 };

	(void)a2; (void)a3;
	if (a1 == T32O_PTR(buf))
		rec("tisp_bcsh_api_set_ccm", 2, a, 0, 0);
	else
		rec("tisp_bcsh_api_set_ccm", 1, a, T32O_HOST(a1), 40);
	return stub_ret + nlg;
}
/* the AE may change the fps word in place */
u64 tisp_ae_api_set_fps(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 a[4] = { a0 };

	(void)a2; (void)a3;
	rec("tisp_ae_api_set_fps", 1, a, T32O_HOST(a1), 4);
	if (fps_new)
		memcpy(T32O_HOST(a1), &fps_new, 4);
	return stub_ret + nlg;
}
static u64 sensor_fps(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 a[4] = { a0 };

	(void)a2; (void)a3;
	rec("sensor_fps", 1, a, T32O_HOST(a1), 128);
	return sens_ret;
}
u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 a[4] = { a0, a1 };

	(void)a2; (void)a3;
	rec("tisp_reg_read", 2, a, 0, 0);
	return top_word;
}
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 a[4] = { a0, a1, a2 };

	(void)a3;
	rec("tisp_reg_write", 3, a, 0, 0);
	return stub_ret;
}

/* ---- our ops ---- */
#define P(p)	T32O_PTR(p)
static u32 o_rd(u32 c, u32 r) { return (u32)tisp_reg_read(c, r, 0, 0); }
static void o_wr(u32 c, u32 r, u32 v) { tisp_reg_write(c, r, v, 0); }
static void o_warn(void) { t32o_log.printfs++; }
static int o_csc(u32 c, const void *b) { return (int)tisp_csc_api_set(c, P(b), 0, 0); }
static int o_bcsc(u32 c) { return (int)tisp_bcsh_refresh_by_csc(c, 0, 0, 0); }
static int o_ccsc(u32 c) { return (int)tisp_clm_refresh_by_csc(c, 0, 0, 0); }
static int o_ccm(u32 c, const void *b) { return (int)tisp_ccm_api_set(c, P(b), 0, 0); }
static int o_bccm(u32 c, const void *m)
{
	u32 a[4] = { c, P(m) };

	if (m == buf)
		rec("tisp_bcsh_api_set_ccm", 2, a, 0, 0);
	else
		rec("tisp_bcsh_api_set_ccm", 1, a, m, 40);
	return (int)(stub_ret + nlg);
}
static int o_mode(u32 c, const u32 *m) { u32 a[4] = { c }; rec("tisp_awb_set_mode", 1, a, m, 12); return (int)(stub_ret + nlg); }
static int o_start(u32 c, const void *s) { return (int)tisp_awb_set_start(c, P(s), 0, 0); }
static int o_frz(u32 c, u32 f) { return (int)tisp_awb_set_frz(c, f, 0, 0); }
static int o_ct(u32 c, const void *s) { return (int)tisp_awb_set_ct(c, P(s), 0, 0); }
static int o_sdns(u32 c, u32 r) { return (int)tisp_s_sdns_ratio(c, r, 0, 0); }
static int o_mdns(u32 c, u32 r) { return (int)tisp_s_mdns_ratio(c, r, 0, 0); }
static int o_tmo(u32 c, u32 r) { return (int)tisp_tmo_api_strength_ctrl(c, r, 0, 0); }
static int o_dpc(u32 c, u32 r) { return (int)tisp_s_dpc_ratio(c, r, 0, 0); }
static int o_dfg(u32 c, u32 r) { return (int)tisp_s_defog_ratio(c, r, 0, 0); }
static int o_wdr(u32 c, u32 m) { return (int)tisp_wdr_api_set_output_mode(c, m, 0, 0); }
static int o_fps(u32 c, u32 *f)
{
	u32 a[4] = { c };

	rec("tisp_ae_api_set_fps", 1, a, f, 4);
	if (fps_new)
		*f = fps_new;
	return (int)(stub_ret + nlg);
}
static int o_sens(u32 c, u32 fps, u8 *info)
{
	static u8 low[128];	/* our info block is on the host stack: hand the entry a copy below 4 GiB */
	u32 fn;
	int r;

	memcpy(&fn, t32_data_dd2d4 + c * 232 + 144, 4);
	memcpy(low, info, 128);
	r = (int)((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(fps, P(low), 0, 0);
	memcpy(info, low, 128);
	return r;
}
static int o_dfl(u32 c) { return (int)tisp_ae_clac_deflicker_cfg(c, 0, 0, 0); }

static const struct t32_api_attr_ops ops = {
	.reg_read = o_rd, .reg_write = o_wr, .warn = o_warn, .csc_api_set = o_csc,
	.bcsh_refresh_by_csc = o_bcsc, .clm_refresh_by_csc = o_ccsc, .ccm_api_set = o_ccm,
	.bcsh_api_set_ccm = o_bccm, .awb_set_mode = o_mode, .awb_set_start = o_start,
	.awb_set_frz = o_frz, .awb_set_ct = o_ct, .sdns_ratio = o_sdns, .mdns_ratio = o_mdns,
	.tmo_api_strength_ctrl = o_tmo, .dpc_ratio = o_dpc, .defog_ratio = o_dfg,
	.wdr_api_set_output_mode = o_wdr, .ae_api_set_fps = o_fps, .sensor_set_fps = o_sens,
	.ae_clac_deflicker_cfg = o_dfl,
};

#define REFS(X) \
	X(tisp_set_csc_attr) X(tisp_s_ccm_attr) X(tisp_s_awb_attr) X(tisp_s_module_ratio_attr) \
	X(tisp_set_wdr_output_mode) X(tisp_set_frame_drop) X(tisp_set_fps) X(tisp_set_ae_minmax) \
	X(tisp_s_raw_rw_control_internal)
#define DECL(f) u64 f(u32, u32, u32, u32);
REFS(DECL)
#define ENT(f) { #f, (t32o_fn)(void (*)(void))f },
static const struct { const char *name; t32o_fn fn; } ent[] = { REFS(ENT) };
#define NENT (sizeof(ent) / sizeof(ent[0]))

#define BUFB	256
#define ATB	(4 * 1024 + 4 * 76 + 600)
static u8 *attr, buf0[BUFB], at0[ATB];

static int ours(unsigned int k, u32 a0, u32 a1, u32 a2)
{
	u8 sh = t32_data_d03a0[0];

	switch (k) {
	case 0: return t32_set_csc_attr(&ops, a0, buf);
	case 1: return t32_s_ccm_attr(&ops, sh, a0, buf);
	case 2: return t32_s_awb_attr(&ops, attr, a0, buf);
	case 3: return t32_s_module_ratio_attr(&ops, attr, a0, buf);
	case 4: return t32_set_wdr_output_mode(&ops, a0, buf);
	case 5: return t32_set_frame_drop(&ops, sh, a0, a1, buf);
	case 6: return t32_set_fps(&ops, a0, a1);
	case 7: return t32_set_ae_minmax(buf);
	case 8: return t32_s_raw_rw_control_internal(a0, buf);
	}
	(void)a2;
	return -1;
}

static unsigned int failed, compared;

static void run(unsigned int k, u32 a0, u32 a1, u8 mode)
{
	static struct rec vl[NREC];
	unsigned int vn, vp, i;
	u32 vr, a[4];
	int or_;
	static u8 vbuf[BUFB], vat[ATB];

	t32o_reset();
	buf = t32o_alloc(BUFB);
	attr = t32o_alloc(ATB);
	t32_data_f0904[0] = T32O_PTR(attr);
	if (k == 7)
		a0 = T32O_PTR(buf);	/* AE min/max: a0 is the AE block */
	{
		u32 fn = T32O_PTR((void *)sensor_fps);

		memcpy(t32_data_dd2d4 + (a0 & 3) * 232 + 144, &fn, 4);
	}
	t32_data_d03a0[0] = mode;
	stub_ret = rnd();
	sens_ret = (rnd() & 3) ? rnd() & 0x7fffffff : 0x80000000u | rnd();
	top_word = rnd();
	fps_new = (rnd() & 1) ? 0 : rnd();
	if (fps_new && !(fps_new & 0xffff))
		fps_new |= 1;	/* fps & 0xffff = 0: vendor div by zero, not compared */
	for (i = 0; i < BUFB; i++)
		buf0[i] = (u8)rnd();
	for (i = 0; i < ATB; i++)
		at0[i] = (u8)rnd();
	switch (k) {
	case 3:	/* module ratio: modes 0/1/other, ratio equal to the cache or not */
		for (i = 0; i < 5; i++) {
			u32 m = rnd() % 4, cm;
			u8 *c = at0 + (a0 & 3) * 128 + 1060 + i * 8;

			if (m == 3)
				m = rnd();	/* a mode other than 0/1 */
			cm = (rnd() & 1) ? m : rnd() % 3;
			memcpy(buf0 + i * 8, &m, 4);
			memcpy(c, &cm, 4);
			if (rnd() & 1)
				c[4] = buf0[i * 8 + 4];
		}
		break;
	case 2:	/* AWB start flag */
		a[0] = (rnd() & 1) ? 1 : rnd() % 3;
		memcpy(buf0 + 64, a, 4);
		break;
	case 4:	/* WDR mode */
		a[0] = (rnd() & 1) ? rnd() % 6 : rnd();
		memcpy(buf0, a, 4);
		break;
	case 5:	/* frame drop: enable 1 / other, lsize around 32 */
		a[0] = (rnd() & 1) ? 1 : rnd() % 3;
		memcpy(buf0, a, 4);
		if (rnd() & 1)
			buf0[4] = (u8)(28 + rnd() % 8);
		break;
	case 6:	/* fps word, den != 0 */
		if (!(a1 & 0xffff))
			a1 |= 1;
		break;
	}

	memcpy(buf, buf0, BUFB); memcpy(attr, at0, ATB);
	nlg = 0;
	vr = (u32)t32o_call(ent[k].fn, a0, (k == 5 || k == 6) ? a1 : T32O_PTR(buf), T32O_PTR(buf), 0, 0);
	vn = nlg; vp = t32o_log.printfs;
	memcpy(vl, lg, sizeof(vl));
	memcpy(vbuf, buf, BUFB); memcpy(vat, attr, ATB);

	memcpy(buf, buf0, BUFB); memcpy(attr, at0, ATB);
	nlg = 0; t32o_log.printfs = 0;
	or_ = ours(k, a0, a1, 0);
	compared++;
	if (vn != nlg || vp != t32o_log.printfs || vr != (u32)or_ || memcmp(vbuf, buf, BUFB) ||
	    memcmp(vat, attr, ATB))
		goto bad;
	for (i = 0; i < vn && i < NREC; i++)
		if (strcmp(vl[i].fn, lg[i].fn) || vl[i].n != lg[i].n || vl[i].bl != lg[i].bl ||
		    memcmp(vl[i].a, lg[i].a, sizeof(vl[i].a)) || memcmp(vl[i].b, lg[i].b, vl[i].bl))
			goto bad;
	return;
bad:
	if (failed++ < 8) {
		printf("MISMATCH %s a0=%08x a1=%08x d03a0=%u: calls %u/%u prints %u/%u ret %08x/%08x mem %d%d\n",
		       ent[k].name, a0, a1, mode, vn, nlg, vp, t32o_log.printfs, vr, (u32)or_,
		       !!memcmp(vbuf, buf, BUFB), !!memcmp(vat, attr, ATB));
		for (i = 0; i < vn && i < NREC; i++)
			printf("  vendor %s(%08x %08x %08x) blob %u\n", vl[i].fn, vl[i].a[0], vl[i].a[1], vl[i].a[2], vl[i].bl);
		for (i = 0; i < nlg && i < NREC; i++)
			printf("  ours   %s(%08x %08x %08x) blob %u\n", lg[i].fn, lg[i].a[0], lg[i].a[1], lg[i].a[2], lg[i].bl);
	}
}

int main(void)
{
	unsigned int c, k, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 10000;

	for (k = 0; k < NENT; k++)
		for (c = 0; c < n / 4 + 9; c++) {
			u32 a0 = rnd() & 3, a1 = rnd();

			if (k == 0 && (c & 1))
				a0 = rnd() & ~3u;	/* csc: the channel byte vs the full word */
			if (k == 5 && (c & 1))
				a1 &= 15;	/* frame drop output index (small: register map of the shadow run) */
			if (k == 3 && (c & 1))
				a0 |= 0x02000000u;	/* ch << 7 wraps to the same block: the byte channel vs the full word */
			if (k == 8)
				a0 &= 1;	/* raw rw: reads dc284 + ch * 904 (2 channels) */
			run(k, a0, a1, (u8)(c % 3));
		}
	printf("t32_apiattr_oracle: %u entry points, %u cases compared, %u mismatches\n", (unsigned)NENT, compared,
	       failed);
	return failed ? 1 : 0;
}
