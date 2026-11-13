// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tx_isp_t32_regmap.h (tisp_reg_map_get, tisp_reg_map_set)
 * against the vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Per case random channel word, register offset, message bytes (value),
 * t32_data_d03a0 = 0 / 1 / other, scripted register reads (0x40 with bit 11
 * equal / different to the value).  Compared: return value, every register
 * write (address, value, order), every hook call (system read address,
 * tisp_reg_read / _write arguments, mdns start / trig) in order with the
 * write count at the call, every byte of the message buffer and the reply
 * length.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_regmap.h"

extern u64 system_reg_write(u32, u32, u32, u32), system_reg_read(u32, u32, u32, u32);
extern u64 tisp_reg_map_get(u32, u32, u32, u32), tisp_reg_map_set(u32, u32, u32, u32);

static u32 rs = 0xbb67ae85u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define LOGN 8
struct hlog { unsigned int n; u32 e[LOGN][5]; };
static struct hlog vl, ol, *cur;
static void hrec(u32 kind, u32 a, u32 b, u32 c)
{
	if (cur->n < LOGN) {
		cur->e[cur->n][0] = kind; cur->e[cur->n][1] = a; cur->e[cur->n][2] = b;
		cur->e[cur->n][3] = c; cur->e[cur->n][4] = t32o_log.n;
	}
	cur->n++;
}
static u32 rd_val, tr_val;
static u32 rd_script(u32 reg) { hrec(1, reg, 0, 0); return rd_val; }
u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hrec(2, a0, a1, 0); return tr_val; }
u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; hrec(3, a0, a1, a2); return 0; }
u64 tisp_mdns_start(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(4, a0, 0, 0); return 0; }
u64 tisp_mdns_reg_trig(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; hrec(5, a0, 0, 0); return 0; }

static u32 o_sread(u32 a) { return (u32)system_reg_read(a, 0, 0, 0); }
static u32 o_swrite(u32 a, u32 v) { return (u32)system_reg_write(a, v, 0, 0); }
static u32 o_rread(u32 ch, u32 r) { return (u32)tisp_reg_read(ch, r, 0, 0); }
static u32 o_rwrite(u32 ch, u32 r, u32 v) { return (u32)tisp_reg_write(ch, r, v, 0); }
static void o_mstart(u32 ch) { tisp_mdns_start(ch, 0, 0, 0); }
static void o_mtrig(u32 ch) { tisp_mdns_reg_trig(ch, 0, 0, 0); }
static const struct t32_regmap_ops ops = { o_sread, o_swrite, o_rread, o_rwrite, o_mstart, o_mtrig };

#define MSGB 48

int main(void)
{
	u8 *vmsg = t32o_alloc(MSGB), *omsg = t32o_alloc(MSGB);
	u32 *vlen = t32o_alloc(4), *olen = t32o_alloc(4);
	unsigned long n, N = 20000, per[2] = { 0 }, mdns = 0;

	for (n = 0; n < N; n++) {
		struct t32_regmap m;
		int fn = (int)(rnd() & 1);
		u32 ch = rnd(), reg = rnd(), r = rnd() % 3, rv, ro, i, wn, w[8];
		u8 sh[4];

		sh[0] = r == 0 ? 0 : (r == 1 ? 1 : rnd() | 1);
		for (i = 0; i < MSGB; i++) vmsg[i] = omsg[i] = rnd();
		*vlen = *olen = rnd();
		{ u32 val; memcpy(&val, vmsg + T32_REGMAP_MSG_VAL, 4);
		  rd_val = rnd(); if (rnd() & 1) rd_val = (rd_val & ~0x800u) | (val & 0x800u); }
		tr_val = rnd();

		t32o_reset();
		t32o_rd_hook = rd_script;
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		m.ops = &ops; m.shadow = sh;
		ro = fn ? t32_reg_map_set(&m, ch, omsg) : t32_reg_map_get(&m, ch, reg, omsg, olen);
		wn = t32o_log.n;
		for (i = 0; i < wn && i < 4; i++) { w[2 * i] = t32o_log.w[i].reg; w[2 * i + 1] = t32o_log.w[i].val; }

		t32o_reset();
		t32o_rd_hook = rd_script;
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		t32_data_d03a0[0] = sh[0];
		rv = fn ? (u32)t32o_call((t32o_fn)(void (*)(void))tisp_reg_map_set, ch, T32O_PTR(vmsg), rnd(), rnd(), 0)
			: (u32)t32o_call((t32o_fn)(void (*)(void))tisp_reg_map_get, ch, reg, T32O_PTR(vmsg), T32O_PTR(vlen), 0);
		t32_data_d03a0[0] = 0;
		t32o_rd_hook = NULL;

		if (rv != ro || wn != t32o_log.n || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) ||
		    memcmp(vmsg, omsg, MSGB) || *vlen != *olen) {
			printf("t32_regmap_oracle: FAIL case %lu fn %d d03a0 %u: ret %#x/%#x writes %u/%u hooks %u/%u len %#x/%#x\n",
			       n, fn, sh[0], rv, ro, t32o_log.n, wn, vl.n, ol.n, *vlen, *olen);
			return 1;
		}
		for (i = 0; i < wn && i < 4; i++)
			if (w[2 * i] != t32o_log.w[i].reg || w[2 * i + 1] != t32o_log.w[i].val) {
				printf("t32_regmap_oracle: FAIL case %lu write %u\n", n, i);
				return 1;
			}
		for (i = 0; i < vl.n && i < LOGN; i++)
			mdns += vl.e[i][0] == 4;
		per[fn]++;
	}
	printf("t32_regmap_oracle: PASS %lu cases (get %lu, set %lu, mdns-wrapped writes %lu)\n", N, per[0], per[1], mdns);
	return 0;
}
