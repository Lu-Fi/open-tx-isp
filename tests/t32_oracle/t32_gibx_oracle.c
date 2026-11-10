// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: GIB per-frame / API paths.  Runs the unmodified vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our readable
 * t32_gib_{again_update,again_s_update,param_array_get,param_array_set,
 * api_set_blc,api_get_blc,wdr_en,deinit} from driver/t32/tx_isp_t32_gib.h
 * and compares every register write (address, value, order), the return
 * values and every byte of the section, working block (+0x40..), gain block
 * and held levels both sides change.
 *
 * Vendor context (t32_data_e17c0[ch]): +0 section, +4 gain block, +8 working
 * block (its first 64 bytes are the 16 pointers into it), +12 the pointer
 * table copy, +16 held levels.  Both pointer tables are filled for the
 * Bayer row of the case.  Null contexts and null context fields (deinit)
 * are exercised too.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_gib.h"

#define SECB	T32_GIB_SEC_BYTES
#define CPB	T32_GIB_VALUES
#define HB	T32_GIB_HOLD_BYTES
#define GWB	24u
#define MAXW	T32_GIB_MAX_WRITES
#define SEC_OFF	26940u

/*
 * tisp_gib_api_set_blc passes these two addresses to printk when section
 * byte 0x220 is 1; the reference never reads the bytes.  Weak placeholders
 * (used only when env.c does not define them), no vendor data.
 */
__attribute__((weak)) u8 t32_data_c94a0[4];
__attribute__((weak)) u8 t32_data_bf4b0[4];
__attribute__((weak)) u64 printk(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}
__attribute__((weak)) u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	return 0;
}

extern u64 tisp_gib_again_update(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gib_again_s_update(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gib_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gib_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gib_api_set_blc(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gib_api_get_blc(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gib_wdr_en(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_gib_deinit(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x51ab37u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* our vfree: records the order of the freed pointers */
static void *freed[8];
static unsigned int nfreed;
static void recfree(void *p)
{
	if (nfreed < 8)
		freed[nfreed] = p;
	nfreed++;
}

static unsigned int compared, failed;

enum { C_AGAIN, C_AGAIN_S, C_GET, C_SET, C_SETBLC, C_GETBLC, C_WDR, C_DEINIT };

static void gen_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 0x3f);
	if (rnd() % 3 == 0)
		sec[4] = sec[5] = sec[6] = sec[7] = 0;	/* gain interpolation path */
	if (rnd() % 3 == 0)
		sec[0x223] = 0;
	else if (rnd() % 2)
		sec[0x223] = 1;
	if (rnd() % 2)
		sec[0x220] = (u8)(rnd() % 3);
	if (rnd() % 2)
		sec[0x221] = (u8)(rnd() % 3);
	if (rnd() % 2)
		memset(sec + 0x114, 0, 8);
}

static int run_case(int which, u32 ch, u32 gain, int nullctx, unsigned int nullmask,
		    const u8 *section, const u8 *cp0, const u8 *gw0, const u8 *hold0,
		    const u8 *arg, unsigned int row)
{
	u8 *tune_ref, *base_ref, *cp_ref, *gw_ref, *hold_ref, *ctx_ref, *ptab_ref, *buf;
	u32 *lp;
	u8 *base_our, *cp_our, *gw_our, *hold_our, *ptrow_our;
	struct t32_gib_ctx *our_ctx;
	u8 rsec[SECB], osec[SECB], rcp[CPB], ocp[CPB], rgw[GWB], ogw[GWB], rh[HB], oh[HB];
	u8 out_ref[SECB], out_our[SECB];
	u32 len_ref = 0, len_our = 0, ref_f[4] = { 0 }, our_f[4] = { 0 }, ref_slot = 0;
	void *expf[8];
	unsigned int nexp = 0;
	struct t32_front_write refw[MAXW], ow[MAXW];
	unsigned int ref_n, i;
	int our_n = 0, ref_rc = 0, our_rc = 0, slot_left = 0;
	const u8 *pt = t32_gib_ptr_tab[row];

	/* vendor side */
	t32o_reset();
	tune_ref = t32o_alloc(SEC_OFF + SECB + 64);
	cp_ref = t32o_alloc(CPB + 64);
	gw_ref = t32o_alloc(GWB + 64);
	hold_ref = t32o_alloc(HB + 64);
	ctx_ref = t32o_alloc(64);
	ptab_ref = t32o_alloc(64);
	buf = t32o_alloc(SECB + 64);
	lp = t32o_alloc(8);
	if (!tune_ref || !cp_ref || !gw_ref || !hold_ref || !ctx_ref || !ptab_ref || !buf || !lp) {
		fprintf(stderr, "t32_gibx_oracle: pool exhausted\n");
		return 2;
	}
	memset(tune_ref, 0, SEC_OFF + SECB + 64);
	base_ref = tune_ref + SEC_OFF;
	memcpy(base_ref, section, SECB);
	memcpy(cp_ref, cp0, CPB);
	for (i = 0; i < 16; i++) {
		((u32 *)cp_ref)[i] = T32O_PTR(cp_ref + pt[i]);
		((u32 *)ptab_ref)[i] = T32O_PTR(cp_ref + pt[i]);
	}
	memcpy(gw_ref, gw0, GWB);
	memcpy(hold_ref, hold0, HB);
	memset(ctx_ref, 0, 64);
	*(u32 *)(ctx_ref + 0) = T32O_PTR(base_ref);
	*(u32 *)(ctx_ref + 4) = (nullmask & 1) ? 0 : T32O_PTR(gw_ref);
	*(u32 *)(ctx_ref + 8) = (nullmask & 2) ? 0 : T32O_PTR(cp_ref);
	*(u32 *)(ctx_ref + 12) = (nullmask & 4) ? 0 : T32O_PTR(ptab_ref);
	*(u32 *)(ctx_ref + 16) = (nullmask & 8) ? 0 : T32O_PTR(hold_ref);
	t32_data_e17c0[ch] = nullctx ? 0 : T32O_PTR(ctx_ref);
	t32_data_dd0a4[ch] = T32O_PTR(tune_ref);
	memset(buf, 0xaa, SECB + 64);
	*lp = 0xdeadbeefu;

	switch (which) {
	case C_AGAIN:
		t32o_call((t32o_fn)(void (*)(void))tisp_gib_again_update, ch, gain, 0, 0, 0);
		break;
	case C_AGAIN_S:
		t32o_call((t32o_fn)(void (*)(void))tisp_gib_again_s_update, ch, gain, 0, 0, 0);
		break;
	case C_GET:
		t32o_call((t32o_fn)(void (*)(void))tisp_gib_param_array_get, ch, T32O_PTR(buf), T32O_PTR(lp), 0, 0);
		memcpy(out_ref, buf, SECB);
		len_ref = *lp;
		break;
	case C_SET:
		memcpy(buf, arg, SECB);
		t32o_call((t32o_fn)(void (*)(void))tisp_gib_param_array_set, ch, T32O_PTR(buf), 0, 0, 0);
		break;
	case C_SETBLC:
		memcpy(buf, arg, HB);
		ref_rc = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_gib_api_set_blc, ch, T32O_PTR(buf), 0, 0, 0);
		break;
	case C_GETBLC:
		ref_rc = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_gib_api_get_blc, ch, T32O_PTR(buf), 0, 0, 0);
		memcpy(out_ref, buf, HB);
		break;
	case C_WDR:
		t32o_call((t32o_fn)(void (*)(void))tisp_gib_wdr_en, ch, gain & 0xff, 0, 0, 0);
		break;
	case C_DEINIT:
		ref_rc = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_gib_deinit, ch, 0, 0, 0, 0);
		for (i = 0; i < 4; i++)
			ref_f[i] = *(u32 *)(ctx_ref + 4 + 4 * i);
		ref_slot = t32_data_e17c0[ch];
		break;
	}
	ref_n = t32o_log.n;
	if (ref_n > MAXW)
		return 2;
	memcpy(refw, t32o_log.w, ref_n * sizeof(refw[0]));
	memcpy(rsec, base_ref, SECB);
	memcpy(rcp, cp_ref, CPB);
	memcpy(rgw, gw_ref, GWB);
	memcpy(rh, hold_ref, HB);

	/* our side */
	t32o_reset();
	base_our = t32o_alloc(SECB + 64);
	cp_our = t32o_alloc(CPB + 64);
	gw_our = t32o_alloc(GWB + 64);
	hold_our = t32o_alloc(HB + 64);
	ptrow_our = t32o_alloc(32);
	our_ctx = t32o_alloc(sizeof(*our_ctx));
	if (!base_our || !cp_our || !gw_our || !hold_our || !ptrow_our || !our_ctx) {
		fprintf(stderr, "t32_gibx_oracle: pool exhausted\n");
		return 2;
	}
	memset(ptrow_our, 0, 32);
	memcpy(ptrow_our, pt, 16);
	memcpy(base_our, section, SECB);
	memcpy(cp_our, cp0, CPB);
	memcpy(gw_our, gw0, GWB);
	memcpy(hold_our, hold0, HB);
	our_ctx->sec = base_our;
	our_ctx->gw = (nullmask & 1) ? NULL : gw_our;
	our_ctx->cp = (nullmask & 2) ? NULL : cp_our;
	our_ctx->ptrow = (nullmask & 4) ? NULL : ptrow_our;
	our_ctx->hold = (nullmask & 8) ? NULL : hold_our;

	switch (which) {
	case C_AGAIN:
		our_n = t32_gib_again_update(our_ctx, (u8)ch, gain, ow);
		break;
	case C_AGAIN_S:
		our_n = t32_gib_again_s_update(our_ctx, (u8)ch, gain, ow);
		break;
	case C_GET: {
		u8 b[SECB];
		u32 l = 0xdeadbeefu;

		memset(b, 0xaa, SECB);
		our_rc = t32_gib_param_array_get(our_ctx, b, &l);
		memcpy(out_our, b, SECB);
		len_our = l;
		break;
	}
	case C_SET: {
		u8 b[SECB];

		memcpy(b, arg, SECB);
		our_n = t32_gib_param_array_set(our_ctx, (u8)ch, b, ow);
		break;
	}
	case C_SETBLC:
		our_n = t32_gib_api_set_blc(nullctx ? NULL : our_ctx, (u8)ch, arg, ow);
		our_rc = our_n < 0 ? -1 : 0;
		if (our_n < 0)
			our_n = 0;
		break;
	case C_GETBLC: {
		u8 b[HB];

		memset(b, 0xaa, HB);
		our_rc = t32_gib_api_get_blc(nullctx ? NULL : our_ctx, b);
		memcpy(out_our, b, HB);
		break;
	}
	case C_WDR:
		our_n = t32_gib_wdr_en(our_ctx, (u8)ch, (u8)(gain & 0xff), ow);
		break;
	case C_DEINIT: {
		struct t32_gib_ctx *slot = nullctx ? NULL : our_ctx;

		if (!nullctx) {
			if (our_ctx->gw)
				expf[nexp++] = our_ctx->gw;
			if (our_ctx->cp)
				expf[nexp++] = our_ctx->cp;
			if (our_ctx->ptrow)
				expf[nexp++] = our_ctx->ptrow;
			if (our_ctx->hold)
				expf[nexp++] = our_ctx->hold;
			expf[nexp++] = our_ctx;
		}
		nfreed = 0;
		our_rc = t32_gib_deinit(recfree, &slot);
		our_f[0] = our_ctx->gw ? 1 : 0;
		our_f[1] = our_ctx->cp ? 1 : 0;
		our_f[2] = our_ctx->ptrow ? 1 : 0;
		our_f[3] = our_ctx->hold ? 1 : 0;
		slot_left = slot != NULL;
		our_n = 0;
		break;
	}
	}
	memcpy(osec, base_our, SECB);
	memcpy(ocp, cp_our, CPB);
	memcpy(ogw, gw_our, GWB);
	memcpy(oh, hold_our, HB);

	compared++;
	if (our_n < 0 || (unsigned int)our_n != ref_n)
		goto bad;
	for (i = 0; i < ref_n; i++)
		if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
			goto bad;
	if (ref_rc != our_rc)
		goto bad;
	if (which == C_GET && (len_ref != len_our || memcmp(out_ref, out_our, SECB)))
		goto bad;
	if (which == C_GETBLC && memcmp(out_ref, out_our, HB))
		goto bad;
	if (which == C_DEINIT) {
		if (slot_left || (ref_slot != 0) != 0)
			goto bad;
		if (!nullctx) {
			if (ref_slot)
				goto bad;
			for (i = 0; i < 4; i++)
				if (ref_f[i] || our_f[i])
					goto bad;
		} else if (ref_slot) {
			goto bad;
		}
		if (nfreed != nexp)
			goto bad;
		for (i = 0; i < nexp; i++)
			if (freed[i] != expf[i])
				goto bad;
		return 0;
	}
	if (memcmp(rsec, osec, SECB) || memcmp(rcp + 0x40, ocp + 0x40, CPB - 0x40) ||
	    memcmp(rgw, ogw, GWB) || memcmp(rh, oh, HB))
		goto bad;
	return 0;
bad:
	if (failed++ < 5) {
		printf("MISMATCH case %d (null ctx %d mask %u): writes ref %u ours %d, rc ref %d ours %d\n",
		       which, nullctx, nullmask, ref_n, our_n, ref_rc, our_rc);
		for (i = 0; i < ref_n && (int)i < our_n; i++)
			if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val) {
				printf("  #%u vendor %04x=%08x ours %04x=%08x\n", i, refw[i].reg,
				       refw[i].val, ow[i].reg, ow[i].val);
				break;
			}
		for (i = 0; i < SECB; i++)
			if (rsec[i] != osec[i]) {
				printf("  sec+0x%x vendor %02x ours %02x\n", i, rsec[i], osec[i]);
				break;
			}
		for (i = 0x40; i < CPB; i++)
			if (rcp[i] != ocp[i]) {
				printf("  cp+0x%x vendor %02x ours %02x\n", i, rcp[i], ocp[i]);
				break;
			}
		for (i = 0; i < GWB; i++)
			if (rgw[i] != ogw[i]) {
				printf("  gw+0x%x vendor %02x ours %02x\n", i, rgw[i], ogw[i]);
				break;
			}
		for (i = 0; i < HB; i++)
			if (rh[i] != oh[i]) {
				printf("  hold+0x%x vendor %02x ours %02x\n", i, rh[i], oh[i]);
				break;
			}
		if (which == C_DEINIT)
			printf("  deinit: slot ref %08x ours %d, frees %u expected %u\n",
			       ref_slot, slot_left, nfreed, nexp);
	}
	return 1;
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c;

	for (c = 0; c < n && !failed; c++) {
		u8 sec[SECB], cp[CPB], gw[GWB], hold[HB], arg[SECB];
		u32 ch = rnd() & 3, gain = rnd();
		unsigned int i, row = rnd() % 11, mask;

		gen_section(sec);
		for (i = 0; i < CPB; i++)
			cp[i] = (u8)rnd();
		for (i = 0; i < GWB; i++)
			gw[i] = (u8)rnd();
		if (rnd() % 2) {			/* realistic gain words: small index */
			gw[2] = (u8)(rnd() % 4); gw[3] = 0;
			gw[6] = (u8)(rnd() % 4); gw[7] = 0;
			if (rnd() % 2)
				gain &= 0x0003ffffu;
		}
		if (rnd() % 8 == 0)
			gain = rnd() % 2 ? 0 : 0xffffffffu;
		if (rnd() % 2)				/* WDR flag 0 / 1 / other */
			gw[16] = (u8)(rnd() % 3);
		if (rnd() % 2)				/* WDR enable value 0 / 1 / other */
			gain = (gain & ~0xffu) | (rnd() % 3);
		for (i = 0; i < HB; i++)
			hold[i] = (u8)rnd();
		if (rnd() % 2) {
			hold[0] = 1;
			hold[1] = 0;
		} else if (rnd() % 2) {
			hold[0] = 0;
			hold[1] = 0;
		}
		for (i = 0; i < SECB; i++)
			arg[i] = (u8)rnd();
		if (rnd() % 2)
			arg[0x223] = (u8)(rnd() % 2);
		if (rnd() % 2)
			arg[0x220] = (u8)(rnd() % 3);
		if (rnd() % 3 == 0)
			arg[4] = arg[5] = arg[6] = arg[7] = 0;

		run_case(C_AGAIN, ch, gain, 0, 0, sec, cp, gw, hold, arg, row);
		run_case(C_AGAIN_S, ch, gain, 0, 0, sec, cp, gw, hold, arg, row);
		run_case(C_GET, ch, gain, 0, 0, sec, cp, gw, hold, arg, row);
		run_case(C_SET, ch, gain, 0, 0, sec, cp, gw, hold, arg, row);
		run_case(C_SETBLC, ch, gain, 0, 0, sec, cp, gw, hold, arg, row);
		run_case(C_GETBLC, ch, gain, 0, 0, sec, cp, gw, hold, arg, row);
		run_case(C_WDR, ch, gain, 0, 0, sec, cp, gw, hold, arg, row);
		mask = rnd() % 4 == 0 ? rnd() & 0xf : 0;
		run_case(C_DEINIT, ch, gain, 0, mask, sec, cp, gw, hold, arg, row);
		if (c % 16 == 5) {			/* no context for the channel */
			run_case(C_SETBLC, ch, gain, 1, 0, sec, cp, gw, hold, arg, row);
			run_case(C_GETBLC, ch, gain, 1, 0, sec, cp, gw, hold, arg, row);
			run_case(C_DEINIT, ch, gain, 1, 0, sec, cp, gw, hold, arg, row);
		}
	}

	printf("t32_gibx_oracle: random %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
