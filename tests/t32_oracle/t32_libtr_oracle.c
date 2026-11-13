// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: lib_tisp_trig, lib_tisp_vic_ctrl, lib_tisp_vic_stop_byindex and lib_tisp_api_tmo_strength_ctrl
 * (driver/t32/tx_isp_t32_libwrap.h, group 3) against Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15
 * @ 2e2e06bc).
 *
 * Compared bit-exact: return value (low word), every register write (address, value, order), the TMO callee
 * call count and arguments.  Inputs: random channels (all 32 bits and low bytes), index 0..4 and random,
 * random values and strengths, callee return values.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libwrap.h"

u64 lib_tisp_trig(u32, u32, u32, u32);
u64 lib_tisp_vic_ctrl(u32, u32, u32, u32);
u64 lib_tisp_vic_stop_byindex(u32, u32, u32, u32);
u64 lib_tisp_api_tmo_strength_ctrl(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

static u32 rs = 0x9e3779b9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 ret_val, v_n, v_a[2], o_n, o_a[2];

u64 tisp_tmo_api_strength_ctrl(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	v_n++;
	v_a[0] = a0;
	v_a[1] = a1;
	return ret_val;
}

static struct { u32 reg, val; } ow[4];
static unsigned int on;
static u32 o_write(void *c, u32 reg, u32 val)
{
	(void)c;
	ow[on].reg = reg;
	ow[on++].val = val;
	return 0;
}
static u32 o_tmo(void *c, u32 ch, u32 s)
{
	(void)c;
	o_n++;
	o_a[0] = ch;
	o_a[1] = s;
	return ret_val;
}

static unsigned int compared, failed;

static void cmp(const char *what, u32 vr, u32 or_, u32 a0, u32 a1)
{
	unsigned int i, bad = vr != or_ || t32o_log.n != on || v_n != o_n || (v_n && memcmp(v_a, o_a, sizeof(v_a)));

	for (i = 0; !bad && i < on; i++)
		bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
	compared++;
	if (bad && failed++ < 10)
		printf("MISMATCH %s(%x, %x): ret %x/%x writes %u/%u\n", what, a0, a1, vr, or_, t32o_log.n, on);
	t32o_reset();
	on = v_n = o_n = 0;
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_lib_hooks h;

	memset(&h, 0, sizeof(h));
	h.sys_write = o_write;
	h.tmo_strength = o_tmo;
	t32o_reset();
	for (c = 0; c < n; c++) {
		u32 ch = c % 2 ? rnd() : rnd() & 0xff, v = rnd(), idx = c % 3 ? c % 5 : rnd(), r;

		ret_val = rnd();
		r = (u32)t32o_call(F(lib_tisp_trig), ch, v, 0, 0, 0);
		cmp("trig", r, t32_lib_trig(&h), ch, v);
		r = (u32)t32o_call(F(lib_tisp_vic_ctrl), ch, v, 0, 0, 0);
		cmp("vic_ctrl", r, t32_lib_vic_ctrl(&h, ch, v), ch, v);
		r = (u32)t32o_call(F(lib_tisp_vic_stop_byindex), idx, v, 0, 0, 0);
		cmp("vic_stop_byindex", r, t32_lib_vic_stop_byindex(&h, idx), idx, v);
		r = (u32)t32o_call(F(lib_tisp_api_tmo_strength_ctrl), ch, v, 0, 0, 0);
		cmp("tmo_strength_ctrl", r, t32_lib_tmo_strength_ctrl(&h, ch, v), ch, v);
	}
	printf("t32_libtr_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
