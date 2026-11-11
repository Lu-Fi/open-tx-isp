// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our ISP core interrupt masks and bin info / path getters (driver/t32/tx_isp_t32_libmisc.h)
 * against Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: every register write (address, value, order) and the return value of lib_tisp_intp_*;
 * the whole info / path block (random canaries around the written fields), the snprintf call (destination,
 * size, format) and the return value of lib_tisp_get_bin_info / _get_bin_path.  Channel state (dc284) and bin
 * names (da850) random per case, channel 0 / 1 with random upper bits (the vendor uses ch & 0xff).
 *
 * snprintf is replaced by a recording stub for the whole program (the vendor format "PRJ007" has no
 * conversion; the stub copies like snprintf for such a format and refuses any other).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libmisc.h"

u64 lib_tisp_intp_enable(u32, u32, u32, u32);
u64 lib_tisp_intp_disable(u32, u32, u32, u32);
u64 lib_tisp_intp_clear(u32, u32, u32, u32);
u64 lib_tisp_get_bin_info(u32, u32, u32, u32);
u64 lib_tisp_get_bin_path(u32, u32, u32, u32);
extern char t32_data_c8064[8], t32_data_c8070[8];

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define INFO	1024
#define PATH	1024

static u32 rs = 0x6b43a9b5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 sn_n, sn_dst, sn_size, sn_fmt, sn_bad;
u64 t32o_snprintf_stub(u32 a0, u32 a1, u32 a2, u32 a3) __asm__("snprintf");
u64 t32o_snprintf_stub(u32 a0, u32 a1, u32 a2, u32 a3)
{
	const char *f = (const char *)(uintptr_t)a2;
	u32 len = (u32)strlen(f), k;

	(void)a3;
	sn_n++;
	sn_dst = a0;
	sn_size = a1;
	sn_fmt = a2;
	if (strchr(f, '%'))
		sn_bad++;
	if (a1) {
		k = len < a1 - 1 ? len : a1 - 1;
		memcpy((void *)(uintptr_t)a0, f, k);
		((char *)(uintptr_t)a0)[k] = 0;
	}
	return len;
}

static struct { u32 reg, val; } ow[16];
static unsigned int on;
static u32 o_write(void *c, u32 reg, u32 val)
{
	(void)c;
	if (on < 16) {
		ow[on].reg = reg;
		ow[on].val = val;
	}
	on++;
	return 0;
}

static unsigned int compared, failed;

static void cmp_regs(const char *what, u32 vr, u32 or_)
{
	unsigned int i, bad = vr != or_ || t32o_log.n != on;

	for (i = 0; !bad && i < on; i++)
		bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
	compared++;
	if (bad && failed++ < 10)
		printf("MISMATCH %s: ret %x/%x writes %u/%u\n", what, vr, or_, t32o_log.n, on);
	t32o_reset();
	on = 0;
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_lib_hooks h;

	memset(&h, 0, sizeof(h));
	h.sys_write = o_write;
	t32o_reset();
	cmp_regs("intp_enable", (u32)t32o_call(F(lib_tisp_intp_enable), 0, 0, 0, 0, 0), t32_lib_intp_enable(&h));
	cmp_regs("intp_disable", (u32)t32o_call(F(lib_tisp_intp_disable), 0, 0, 0, 0, 0), t32_lib_intp_disable(&h));
	cmp_regs("intp_clear", (u32)t32o_call(F(lib_tisp_intp_clear), 0, 0, 0, 0, 0), t32_lib_intp_clear(&h));

	for (c = 0; c < n; c++) {
		u8 *vb, *ob, *vs = t32_data_dc284;
		static u8 st[1808], nm[704];
		u32 r, o, v_n, v_dst, v_size, v_fmt, ch = (rnd() & ~0xffu) | (c & 1), bad;

		t32o_reset();
		vb = t32o_alloc(INFO);
		ob = t32o_alloc(INFO);
		for (i = 0; i < INFO; i++)
			vb[i] = ob[i] = (u8)rnd();
		for (i = 0; i < sizeof(st); i++)
			vs[i] = st[i] = (u8)rnd();
		for (i = 0; i < sizeof(nm); i++)
			t32_data_da850[i] = nm[i] = (u8)rnd();
		sn_n = sn_dst = sn_size = sn_fmt = 0;
		if (c & 2) {
			r = (u32)t32o_call(F(lib_tisp_get_bin_info), rnd(), T32O_PTR(vb), rnd(), rnd(), 0);
			v_n = sn_n; v_dst = sn_dst - T32O_PTR(vb); v_size = sn_size; v_fmt = sn_fmt;
			sn_n = sn_dst = sn_size = sn_fmt = 0;
			o = t32_lib_get_bin_info(ob, t32_data_c8064, t32_data_c8070);
			bad = v_n != sn_n || v_dst != sn_dst - T32O_PTR(ob) || v_size != sn_size || v_fmt != sn_fmt;
		} else {
			r = (u32)t32o_call(F(lib_tisp_get_bin_path), ch, T32O_PTR(vb), rnd(), rnd(), 0) - T32O_PTR(vb);
			o = t32_lib_get_bin_path(st, nm, ch, ob) - T32O_PTR(ob);
			bad = 0;
		}
		bad |= r != o || memcmp(vb, ob, INFO) || memcmp(vs, st, sizeof(st)) ||
		       memcmp(t32_data_da850, nm, sizeof(nm)) || sn_bad;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH %s case %u ch %x: ret %x/%x\n", c & 2 ? "get_bin_info" : "get_bin_path", c, ch, r, o);
	}
	printf("t32_libmisc_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
