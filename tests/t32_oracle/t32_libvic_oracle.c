// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: lib_tisp_vic_stream_on / _stop, lib_tisp_vic_intp_enable / _disable and
 * lib_tisp_get_ipc_cache_data (driver/t32/tx_isp_t32_libwrap.h, group 5) against Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value (low word), every register write (address, value, order), the register
 * read addresses.  Inputs: random channels (all 32 bits and low bytes 0..255), random register contents.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libwrap.h"

u64 lib_tisp_vic_stream_on(u32, u32, u32, u32);
u64 lib_tisp_vic_stop(u32, u32, u32, u32);
u64 lib_tisp_vic_intp_enable(u32, u32, u32, u32);
u64 lib_tisp_vic_intp_disable(u32, u32, u32, u32);
u64 lib_tisp_get_ipc_cache_data(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

static u32 rs = 0x9e3779b9u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 v_rd[4], v_nr, o_rd[4], o_nr, regval[2];

static u32 rd(u32 reg)
{
	v_rd[v_nr++ & 3] = reg;
	return regval[reg & 1] ^ reg;
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
static u32 o_read(void *c, u32 reg)
{
	(void)c;
	o_rd[o_nr++ & 3] = reg;
	return regval[reg & 1] ^ reg;
}

static unsigned int compared, failed;

static void cmp(const char *what, u32 vr, u32 or_, u32 a0, u32 a1)
{
	unsigned int i, bad = vr != or_ || t32o_log.n != on || v_nr != o_nr || memcmp(v_rd, o_rd, sizeof(v_rd));

	for (i = 0; !bad && i < on; i++)
		bad = t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val;
	compared++;
	if (bad && failed++ < 10)
		printf("MISMATCH %s(%x, %x): ret %x/%x writes %u/%u\n", what, a0, a1, vr, or_, t32o_log.n, on);
	t32o_reset();
	on = v_nr = o_nr = 0;
	memset(v_rd, 0, sizeof(v_rd));
	memset(o_rd, 0, sizeof(o_rd));
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_lib_hooks h;

	memset(&h, 0, sizeof(h));
	h.sys_write = o_write;
	h.sys_read = o_read;
	t32o_rd_hook = rd;
	t32o_reset();
	for (c = 0; c < n; c++) {
		u32 ch = c % 2 ? rnd() : rnd() & 0xff, r;

		regval[0] = rnd();
		regval[1] = rnd();
		r = (u32)t32o_call(F(lib_tisp_vic_stream_on), ch, 0, 0, 0, 0);
		cmp("vic_stream_on", r, t32_lib_vic_stream_on(&h), ch, 0);
		r = (u32)t32o_call(F(lib_tisp_vic_stop), ch, 0, 0, 0, 0);
		cmp("vic_stop", r, t32_lib_vic_stop(&h), ch, 0);
		r = (u32)t32o_call(F(lib_tisp_vic_intp_enable), ch, 0, 0, 0, 0);
		cmp("vic_intp_enable", r, t32_lib_vic_intp_enable(&h, ch), ch, 0);
		r = (u32)t32o_call(F(lib_tisp_vic_intp_disable), ch, 0, 0, 0, 0);
		cmp("vic_intp_disable", r, t32_lib_vic_intp_disable(&h, ch), ch, 0);
		r = (u32)t32o_call(F(lib_tisp_get_ipc_cache_data), ch, 0, 0, 0, 0);
		cmp("get_ipc_cache_data", r, t32_lib_get_ipc_cache_data(&h), ch, 0);
	}
	printf("t32_libvic_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
