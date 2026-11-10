// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the LDC attribute API of driver/t32/tx_isp_t32_ldcattr.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc):
 * tisp_online_ldc_get_attr, tisp_online_ldc_set_attr,
 * tisp_offline_ldc_set_attr.
 *
 * Compared: return values, the copy_from_user / isp_printf calls (format,
 * %s, line), every system_reg_ldc_write (register, value, order), all 3240
 * bytes of the online context, the 1116 bytes of the offline state, and the
 * get_attr output (the pointer as an offset into the context).
 * Inputs: random contexts / user blocks (tables with negative s16), enable
 * byte 0 / 1 / other, mode words 0 / 1 / other, copy failures, channel 0 / 1.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_ldcattr.h"

u64 tisp_online_ldc_get_attr(u32, u32, u32, u32);
u64 tisp_online_ldc_set_attr(u32, u32, u32, u32);
u64 tisp_offline_ldc_set_attr(u32, u32, u32, u32);

#define CALL(f, a, b) t32o_call((t32o_fn)(void (*)(void))(f), a, b, 0, 0, 0)
enum { F_CFU = 1, F_PRINTF, F_LDCW };

u32 t32_data_e3614[4];
u8 t32_data_e3624[1120] __attribute__((aligned(4)));
__asm__(".globl t32_data_e3638\n.set t32_data_e3638, t32_data_e3624 + 0x14");
char t32_data_c2adc[] = "tisp_online_ldc_set_attr";
char t32_data_c2ac0[] = "tisp_offline_ldc_set_attr";
char t32_data_ca6c8[] = T32_LDC_COPY_FAIL_FMT;

static u32 cfu_fail;
static u8 *user_v, *user_o;	/* the same user block, at two addresses */

u64 private_copy_from_user(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	t32o8_rec(&t32o8_v, F_CFU, 0, a1, a2, 0);
	if (cfu_fail)
		return cfu_fail;
	memcpy(T32O_HOST(a0), T32O_HOST(a1), a2);
	return 0;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 w[2] = { a2, a3 };

	t32o8_rec_fmt(&t32o8_v, F_PRINTF, a0, T32O_HOST(a1), w, 2);
	return 0;
}
u64 system_reg_ldc_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_LDCW, a0, a1, 0, 0);
	return 0;
}

static unsigned long o_cfu(void *to, const void *from, unsigned long n)
{
	t32o8_rec(&t32o8_o, F_CFU, 0, from == user_o ? T32O_PTR(user_v) : T32O_PTR(from), (u32)n, 0);
	if (cfu_fail)
		return cfu_fail;
	memcpy(to, from, n);
	return 0;
}
static int o_printf(int level, const char *fmt, ...)
{
	u32 w[4];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 4);
	va_end(ap);
	t32o8_rec_fmt(&t32o8_o, F_PRINTF, (u32)level, fmt, w, n);
	return 0;
}
static void o_ldcw(u32 reg, u32 val) { t32o8_rec(&t32o8_o, F_LDCW, reg, val, 0, 0); }
static const struct t32_ldcattr_ops ops = { o_cfu, o_printf, o_ldcw };

static u32 rs = 0x1dca77e5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u16 pick16(void)
{
	return (rnd() % 3) ? (u16)(rnd() & 1) : (u16)rnd();
}

int main(void)
{
	unsigned int c, k, failed = 0, compared = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 6000;
	static u8 off_o[1120];

	for (c = 0; c < n; c++) {
		u32 which = c % 3, ch = rnd() & 1, vr = 0, orr = 0;
		struct t32_ldc_ctx *cv, *co;
		u8 *apiv, *apio;
		int bad;

		t32o_reset();
		t32o8_clear();
		cv = t32o_alloc(sizeof(*cv)); co = t32o_alloc(sizeof(*co));
		user_v = t32o_alloc(1200); user_o = t32o_alloc(1200);
		apiv = t32o_alloc(64); apio = t32o_alloc(64);
		for (k = 0; k < sizeof(*cv); k++)
			((u8 *)cv)[k] = ((u8 *)co)[k] = (u8)rnd();
		for (k = 0; k < 1200; k++)
			user_v[k] = user_o[k] = (u8)rnd();
		for (k = 0; k < 1120; k++)
			t32_data_e3624[k] = off_o[k] = (u8)rnd();
		for (k = 0; k < 64; k++)
			apiv[k] = apio[k] = (u8)rnd();
		t32_data_e3614[ch] = T32O_PTR(cv);
		t32_data_e3614[ch ^ 1] = 0;
		t32_data_dc284[ch * 904 + 40] = (rnd() % 3) ? (u8)(rnd() & 1) : (u8)rnd();
		cfu_fail = (rnd() % 5) ? 0 : 1 + rnd() % 100;
		switch (which) {
		case 0: {
			struct t32_ldc_online_attr oo;
			u8 ov[8];

			memcpy(&oo, apio, 8);
			memcpy(ov, apiv, 8);
			vr = (u32)CALL(tisp_online_ldc_get_attr, ch, T32O_PTR(apiv));
			orr = (u32)t32_online_ldc_get_attr(co, &oo);
			memcpy(apio, &oo, 8);
			/* the pointer: as offset into each side's context */
			*(u32 *)(apiv + 4) -= T32O_PTR(cv);
			*(u32 *)(apio + 4) -= T32O_PTR(co);
			(void)ov;
			break;
		}
		case 1: {
			struct t32_ldc_online_set in;

			*(u16 *)(apiv + 0) = *(u16 *)(apio + 0) = pick16();
			*(u16 *)(apiv + 2) = *(u16 *)(apio + 2) = pick16();
			*(u32 *)(apiv + 8) = T32O_PTR(user_v);
			memcpy(&in, apio, sizeof(in));
			in.param = T32O_PTR(user_o);
			vr = (u32)CALL(tisp_online_ldc_set_attr, ch, T32O_PTR(apiv));
			orr = (u32)t32_online_ldc_set_attr(&ops, co, t32_data_dc284[ch * 904 + 40], &in);
			*(u32 *)(apio + 8) = T32O_PTR(user_v);
			break;
		}
		default:
			*(u32 *)(apiv + 24) = T32O_PTR(user_v);
			*(u32 *)(apio + 24) = T32O_PTR(user_o);
			vr = (u32)CALL(tisp_offline_ldc_set_attr, T32O_PTR(apiv), 0);
			orr = (u32)t32_offline_ldc_set_attr(&ops, off_o, apio);
			*(u32 *)(apio + 24) = T32O_PTR(user_v);
			break;
		}
		bad = t32o8_cmp("ldcattr", failed < 8) || vr != orr || memcmp(cv, co, sizeof(*cv)) ||
		      memcmp(t32_data_e3624, off_o, 1120) || memcmp(apiv, apio, 64);
		compared++;
		if (bad && failed++ < 8)
			printf("MISMATCH case %u ch %u: ret %08x / %08x ctx %d off %d api %d\n", which, ch, vr, orr,
			       memcmp(cv, co, sizeof(*cv)) != 0, memcmp(t32_data_e3624, off_o, 1120) != 0,
			       memcmp(apiv, apio, 64) != 0);
	}
	printf("t32_ldcattr_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
