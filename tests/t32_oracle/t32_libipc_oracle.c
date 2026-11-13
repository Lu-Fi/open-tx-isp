// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our IPC memory size / placement (driver/t32/tx_isp_t32_libipc.h) against Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc; REFFIX dc074_big.sh points both refs at the full
 * 528-byte t32o7_data_dc074 below).
 *
 * Compared bit-exact: return value (get: always; set: on the LDC load path), every callee call in order with
 * its arguments (sizes, base, channel, state pointer), the whole dc074 block, the configured byte.
 * The printk traces are counted and ordered against the other calls (their source line is not compared).
 * Inputs: channel 0..5 with random upper bits, sensor number 0 / 1 / 2 / random, LDC flag 0 / 1 / random,
 * random block words and callee results.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libipc.h"

u64 lib_tisp_get_isp_ipc_memory_space(u32, u32, u32, u32);
u64 lib_tisp_set_isp_ipc_memory_space(u32, u32, u32, u32);
extern u8 t32_data_dd674[4];

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

u8 t32o7_data_dc074[528] __attribute__((aligned(4)));
char t32_data_bf1d4[4] = "f", t32_data_bf1b0[4] = "f", t32_data_c8328[4] = "f", t32_data_c8350[4] = "f";

static u32 rs = 0x299f31d0u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct ev { u32 id, a[2], pr; };
#define NE 8
static struct ev ve[NE], oe[NE];
static unsigned int vn, on;
static u32 rets[4];

/* printk is env.c's (counts t32o_log.printfs): every call carries the trace count before it */
static u32 o_pr;
static void vadd(u32 id, u32 a0, u32 a1) { if (vn < NE) ve[vn] = (struct ev){ id, { a0, a1 }, t32o_log.printfs }; vn++; }
static void oadd(u32 id, u32 a0, u32 a1) { if (on < NE) oe[on] = (struct ev){ id, { a0, a1 }, o_pr }; on++; }

u64 tisp_top_ipc_get_mem_size(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; vadd(2, a0, a1); return rets[0]; }
u64 lib_tisp_ldc_get_mem_size(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; vadd(3, a0, a1); return rets[1]; }
u64 tisp_top_ipc_set_mem_cfg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vadd(4, a0, 0); return rnd(); }
u64 tisp_ldc_load_reg_cfg(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	vadd(5, a0, a1 - T32O_PTR(t32_data_dc284));
	return rets[2];
}

static u8 st[1808];
static void o_trace(void *c, u32 line) { (void)c; (void)line; o_pr++; }
static u32 o_tget(void *c, u32 w, u32 c0) { (void)c; oadd(2, w, c0); return rets[0]; }
static u32 o_lget(void *c, u32 w0, u32 w1) { (void)c; oadd(3, w0, w1); return rets[1]; }
static u32 o_tset(void *c, u32 b) { (void)c; oadd(4, b, 0); return 0; }
static u32 o_lload(void *c, u32 ch, u8 *s) { (void)c; oadd(5, ch, (u32)(s - st)); return rets[2]; }

int main(void)
{
	unsigned int c, i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_ipc_hooks h = { NULL, o_trace, o_tget, o_lget, o_tset, o_lload };
	static u8 mdns[528], conf[4];

	for (c = 0; c < n; c++) {
		u32 ch = (rnd() & ~0xffu) | (rnd() % 6), k = rnd() % 5, sn, r, o, base = rnd(), bad;
		u8 *cfg_v, cfg_o[8];
		int get = c & 1, has_ret = 1;

		t32o_reset();
		vn = on = o_pr = 0;
		memset(ve, 0, sizeof(ve));
		memset(oe, 0, sizeof(oe));
		for (i = 0; i < 528; i++)
			t32o7_data_dc074[i] = mdns[i] = (u8)rnd();
		for (i = 0; i < sizeof(st); i++)
			t32_data_dc284[i] = st[i] = (u8)rnd();
		sn = k < 3 ? k : k == 3 ? (ch & 0xff) + 1 : rnd();
		memcpy(t32_data_dc284 + 900, &sn, 4);
		memcpy(st + 900, &sn, 4);
		t32_data_dd674[0] = conf[0] = (u8)rnd();
		for (i = 0; i < 4; i++)
			rets[i] = rnd();
		cfg_v = t32o_alloc(8);
		for (i = 0; i < 8; i++)
			cfg_v[i] = cfg_o[i] = (u8)rnd();
		k = rnd() % 3;
		cfg_v[2] = cfg_o[2] = k == 0 ? 0 : k == 1 ? 1 : (u8)rnd();
		k = rnd() % 3;
		t32o7_data_dc074[76 * (ch & 0xff) + 10] = mdns[76 * (ch & 0xff) + 10] = k == 0 ? 0 : k == 1 ? 1 : (u8)rnd();
		if (get) {
			r = (u32)t32o_call(F(lib_tisp_get_isp_ipc_memory_space), ch, T32O_PTR(cfg_v), rnd(), rnd(), 0);
			o = t32_lib_get_isp_ipc_memory_space(&h, mdns, st, ch, cfg_o);
		} else {
			r = (u32)t32o_call(F(lib_tisp_set_isp_ipc_memory_space), ch, base, rnd(), rnd(), 0);
			o = t32_lib_set_isp_ipc_memory_space(&h, mdns, st, conf, ch, base);
			has_ret = mdns[76 * (ch & 0xff) + 10] == 1 && (ch & 0xff) == sn - 1;
		}
		bad = (has_ret && r != o) || vn != on || memcmp(ve, oe, sizeof(ve)) || memcmp(t32o7_data_dc074, mdns, 528) ||
		      memcmp(t32_data_dc284, st, sizeof(st)) || t32_data_dd674[0] != conf[0] || t32o_log.printfs != o_pr || memcmp(cfg_v, cfg_o, 8);
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH %s case %u ch %x sn %x: ret %x/%x calls %u/%u\n", get ? "get" : "set", c, ch, sn, r, o,
			       vn, on);
	}
	printf("t32_libipc_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
