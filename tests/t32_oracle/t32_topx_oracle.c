// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: TOP per-frame / lifecycle paths.  Runs the unmodified
 * vendor functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc) and our
 * readable t32_top_* from driver/t32/tx_isp_t32_front.h from the same
 * initial state and compares every register write (address, value, order),
 * every state word the reference changes and the return values.  Exits 1 on
 * the first mismatch.
 *
 * Covered: tisp_top_api_dn_par_refresh, _deinit, _ipc_get_mem_size,
 * _ipc_set_mem_cfg, _ipc_init, _sleep (register path and direct path),
 * _wake_up, _param_array_set / _get, _frame_count.
 * Not covered: the direct register path (t32_data_d03a0 != 0) of the
 * writing functions; tisp_top_reg_cfg runs for channel 0 only with
 * deir = 0 (as proven by t32_top_oracle), with its section in both
 * e1484[0] and dd0a4[0].
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_front.h"

#define SECB		T32_TOP_BYTES
#define MAXW		64
#define WEAK		__attribute__((weak))

/*
 * Objects / glue the references use that env.c may not provide: zero .bss
 * state words set per case by this oracle (weak, an env.c definition wins).
 */
WEAK u32 t32_data_dc0a8;
WEAK u32 t32_data_dc074[16];
WEAK u32 t32_data_e1494[16];
WEAK u32 t32_data_f32d8[4];
WEAK u32 t32_data_f32f8[4];
WEAK u32 t32_data_f32e8[4];

/* unmodified vendor functions from ref/ */
extern u64 tisp_top_api_dn_par_refresh(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_top_deinit(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_top_ipc_get_mem_size(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_top_ipc_set_mem_cfg(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_top_ipc_init(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_top_sleep(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_top_wake_up(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_top_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_top_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3);
extern u64 tisp_top_frame_count(u32 a0, u32 a1, u32 a2, u32 a3);

/* MDNS APIs called by tisp_top_frame_count: record the call order */
static unsigned int seq[8], seqn, seq_ok;
WEAK u64 tisp_mdns_dn_params_api2(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (seqn < 8)
		seq[seqn++] = 0x200 | (a0 & 0xff);
	return 0;
}
WEAK u64 tisp_mdns_dn_params_api(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	if (seqn < 8)
		seq[seqn++] = 0x100 | (a0 & 0xff);
	return 0;
}
static void our_api2(u8 ch) { if (seqn < 8) seq[seqn++] = 0x200 | ch; }
static void our_api(u8 ch) { if (seqn < 8) seq[seqn++] = 0x100 | ch; }

/* register read of tisp_top_sleep: (ch, reg) -> hook(reg) */
WEAK u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a2; (void)a3;
	return t32o_rd_hook ? t32o_rd_hook(a1) : 0;
}

static u32 rs = 0x3c9e11u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared;

static void fail(const char *what)
{
	printf("t32_topx_oracle: MISMATCH %s (after %u compared)\n", what, compared);
	exit(1);
}

static void *must(void *p)
{
	if (!p) {
		printf("t32_topx_oracle: alloc failed\n");
		exit(2);
	}
	return p;
}

static void put32(u8 *p, u32 v)
{
	p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}

static u32 host_reg_read(u32 reg)
{
	return reg * 0x9e3779b9u + 7;
}

static void gen_section(u8 *sec)
{
	unsigned int i;

	for (i = 0; i < SECB; i++)
		sec[i] = (i % 3) ? (u8)rnd() : (u8)(rnd() & 1);
}

static unsigned int grab_ref(struct t32_front_write *refw)
{
	unsigned int n = t32o_log.n, i;

	if (n > MAXW) {
		printf("t32_topx_oracle: too many writes\n");
		exit(2);
	}
	for (i = 0; i < n; i++) {
		refw[i].reg = t32o_log.w[i].reg;
		refw[i].val = t32o_log.w[i].val;
	}
	return n;
}

static void cmp_writes(const char *what, const struct t32_front_write *refw, unsigned int ref_n,
		       const struct t32_front_write *ow, unsigned int our_n)
{
	unsigned int i;

	compared++;
	if (ref_n == our_n) {
		for (i = 0; i < ref_n; i++)
			if (refw[i].reg != ow[i].reg || refw[i].val != ow[i].val)
				break;
		if (i == ref_n)
			return;
	}
	printf("%s: writes ref %u ours %u\n", what, ref_n, our_n);
	for (i = 0; i < ref_n || i < our_n; i++)
		if (i >= ref_n || i >= our_n || refw[i].reg != ow[i].reg || refw[i].val != ow[i].val) {
			printf("  #%u vendor %05x=%08x ours %05x=%08x\n", i,
			       i < ref_n ? refw[i].reg : 0, i < ref_n ? refw[i].val : 0,
			       i < our_n ? ow[i].reg : 0, i < our_n ? ow[i].val : 0);
			break;
		}
	fail(what);
}

/* channel 0, single pipeline, register path: the state t32_top_oracle proves reg_cfg in */
static void reg_cfg_state(void)
{
	*(u32 *)(t32_data_dc284 + 900) = 1;
	t32_data_d03a0[0] = 0;
}

/*
 * Vendor state after tisp_top_init for channel 0 (section at dd0a4[0], info
 * as in t32_top_oracle); its writes are dropped.  Returns ours' deir.
 */
static u32 vendor_top_init(u8 *base, u32 bayer)
{
	u8 *info = must(t32o_alloc(64));
	u32 idx = bayer & 0x1f;

	memset(info, 0, 64);
	put32(info, 1920);
	put32(info + 4, 1080);
	put32(info + 8, bayer);
	info[36] = (u8)(rnd() & 1);
	info[39] = 0;
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32o_call((t32o_fn)(void (*)(void))tisp_top_init, 0, T32O_PTR(info), 0, 0, 0);
	t32o_log.n = 0;
	return idx < 4 ? 0 : 1;
}

static u32 rnd_bayer(void)
{
	if (rnd() % 2)
		return (u32)t32_mbus_to_bayer(T32_MBUS_SBGGR8 + rnd() % 12);
	return rnd() & 0x3ffff;
}

static void run_dn_par_refresh(const u8 *dn_sec, u8 inited)
{
	struct t32_front_write refw[MAXW], ow[MAXW];
	u8 *base_ref, *param_ref, *base_our, *param_our;
	unsigned int ref_n, our_n;
	u32 deir = 0;

	t32o_reset();
	reg_cfg_state();
	base_ref = must(t32o_alloc(SECB + 16));
	param_ref = must(t32o_alloc(SECB));
	memcpy(base_ref, dn_sec, SECB);
	memset(param_ref, 0xaa, SECB);
	if (inited)
		deir = vendor_top_init(base_ref, rnd_bayer());
	t32_data_e1484[0] = T32O_PTR(param_ref);
	t32_data_dd0a4[0] = T32O_PTR(base_ref);
	t32o_call((t32o_fn)(void (*)(void))tisp_top_api_dn_par_refresh, 0, 0, 0, 0, 0);
	ref_n = grab_ref(refw);

	base_our = must(t32o_alloc(SECB + 16));
	param_our = must(t32o_alloc(SECB));
	memcpy(base_our, dn_sec, SECB);
	memset(param_our, 0xaa, SECB);
	our_n = t32_top_api_dn_par_refresh(0, param_our, base_our, deir, inited, ow);
	if (memcmp(param_ref, param_our, SECB))
		fail("dn_par_refresh param");
	cmp_writes("dn_par_refresh", refw, ref_n, ow, our_n);
}

static void run_deinit(u32 ch)
{
	u8 ref_flag, our_flag;
	u32 ref_rc;
	int our_rc;

	t32o_reset();
	t32_data_e1460[ch] = 0xaa;
	ref_rc = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_top_deinit, ch, 0, 0, 0, 0);
	ref_flag = t32_data_e1460[ch];

	t32o_reset();
	t32_data_e1460[ch] = 0xaa;
	our_rc = t32_top_deinit((u8)ch, t32_data_e1460);
	our_flag = t32_data_e1460[ch];
	compared++;
	if (ref_flag != our_flag || ref_rc != (u32)our_rc)
		fail("deinit");
}

static void run_ipc_get_mem_size(u32 w, u32 h)
{
	u32 ref_v, ref_rc, our_v, our_mem = 0;

	t32o_reset();
	t32_data_dc0a8 = 0;
	ref_rc = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_top_ipc_get_mem_size, w, h, 0, 0, 0);
	ref_v = t32_data_dc0a8;

	our_v = t32_top_ipc_get_mem_size(w, h, &our_mem);
	compared++;
	if (ref_v != our_mem || ref_rc != our_v)
		fail("ipc_get_mem_size");
}

static void run_ipc_set_mem_cfg(u32 width, const u32 *cfg)
{
	struct t32_front_write refw[MAXW], ow[MAXW];
	u32 ours[14];
	unsigned int ref_n, our_n;

	t32o_reset();
	memcpy(t32_data_dc074, cfg, sizeof(ours));
	t32o_call((t32o_fn)(void (*)(void))tisp_top_ipc_set_mem_cfg, width, 0, 0, 0, 0);
	ref_n = grab_ref(refw);

	memcpy(ours, cfg, sizeof(ours));
	our_n = t32_top_ipc_set_mem_cfg(ours, width, ow);
	if (memcmp(ours, t32_data_dc074, sizeof(ours)))
		fail("ipc_set_mem_cfg config words");
	cmp_writes("ipc_set_mem_cfg", refw, ref_n, ow, our_n);
}

static void run_ipc_init(u32 state, const u8 *rnd_info, u32 w, u32 h, u32 b36, u32 b41)
{
	struct t32_front_write refw[MAXW], ow[MAXW];
	u8 *info;
	unsigned int ref_n, our_n;
	u32 ref_rc;

	t32o_reset();
	info = must(t32o_alloc(904));
	memcpy(info, rnd_info, 904);
	put32(info, w);
	put32(info + 4, h);
	info[36] = (u8)b36;
	info[41] = (u8)b41;
	put32(info + 900, state);
	t32_data_d03a0[0] = 0;
	ref_rc = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_top_ipc_init, T32O_PTR(info), 0, 0, 0, 0);
	ref_n = grab_ref(refw);

	our_n = t32_top_ipc_init(info, ow);
	if (ref_rc != 0)
		fail("ipc_init return");
	cmp_writes("ipc_init", refw, ref_n, ow, our_n);
}

static void run_sleep(u32 ch, u8 direct)
{
	u32 saved_our[14];
	unsigned int ref_n;

	t32o_reset();
	t32o_rd_hook = host_reg_read;
	memset(t32_data_e1494, 0, 56);
	t32_data_d03a0[0] = direct;
	t32o_call((t32o_fn)(void (*)(void))tisp_top_sleep, ch, 0, 0, 0, 0);
	ref_n = t32o_log.n;
	t32_data_d03a0[0] = 0;

	memset(saved_our, 0, sizeof(saved_our));
	t32_top_sleep((u8)ch, saved_our, direct, host_reg_read);
	compared++;
	if (ref_n || memcmp(t32_data_e1494, saved_our, sizeof(saved_our)))
		fail(direct ? "sleep saved registers (direct)" : "sleep saved registers");
}

static void run_wake(u32 ch)
{
	struct t32_front_write refw[MAXW], ow[MAXW];
	u32 rnd_saved[14];
	unsigned int ref_n, our_n, i;

	for (i = 0; i < 14; i++)
		rnd_saved[i] = rnd();
	t32o_reset();
	memcpy(t32_data_e1494, rnd_saved, 56);
	t32_data_d03a0[0] = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_top_wake_up, ch, 0, 0, 0, 0);
	ref_n = grab_ref(refw);

	our_n = t32_top_wake_up((u8)ch, rnd_saved, 0, ow);
	cmp_writes("wake_up", refw, ref_n, ow, our_n);
}

static void run_param_array_set(const u8 *buf, u8 inited)
{
	struct t32_front_write refw[MAXW], ow[MAXW];
	u8 *param_ref, *param_our, *src_ref, *dd_ref;
	unsigned int ref_n, our_n;
	u32 ref_rc, deir = 0;

	t32o_reset();
	reg_cfg_state();
	param_ref = must(t32o_alloc(SECB));
	src_ref = must(t32o_alloc(SECB));
	dd_ref = must(t32o_alloc(SECB + 16));
	memset(param_ref, 0xaa, SECB);
	memcpy(src_ref, buf, SECB);
	memcpy(dd_ref, buf, SECB);
	if (inited)
		deir = vendor_top_init(dd_ref, rnd_bayer());
	t32_data_e1484[0] = T32O_PTR(param_ref);
	t32_data_dd0a4[0] = T32O_PTR(dd_ref);
	ref_rc = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_top_param_array_set, 0,
				T32O_PTR(src_ref), 0, 0, 0);
	ref_n = grab_ref(refw);

	param_our = must(t32o_alloc(SECB));
	memset(param_our, 0xaa, SECB);
	our_n = t32_top_param_array_set(0, param_our, buf, deir, inited, ow);
	if (memcmp(param_ref, param_our, SECB))
		fail("param_array_set param");
	if (ref_rc != 0)
		fail("param_array_set return");
	cmp_writes("param_array_set", refw, ref_n, ow, our_n);
}

static void run_param_array_get(u32 ch, const u8 *buf)
{
	u8 *param_ref, *out_ref, *out_our;
	u32 *len_ref, len_our = 0, ref_rc;
	int our_rc;

	t32o_reset();
	param_ref = must(t32o_alloc(SECB));
	out_ref = must(t32o_alloc(SECB + 4));
	len_ref = must(t32o_alloc(4));
	memcpy(param_ref, buf, SECB);
	memset(out_ref, 0xaa, SECB + 4);
	*len_ref = 0;
	t32_data_e1484[ch] = T32O_PTR(param_ref);
	ref_rc = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_top_param_array_get, ch, T32O_PTR(out_ref),
				T32O_PTR(len_ref), 0, 0);

	out_our = must(t32o_alloc(SECB + 4));
	memset(out_our, 0xaa, SECB + 4);
	our_rc = t32_top_param_array_get((u8)ch, buf, out_our, &len_our);
	compared++;
	if (memcmp(out_ref, out_our, SECB + 4) || *len_ref != len_our || ref_rc != (u32)our_rc)
		fail("param_array_get");
}

static void run_frame_count(u32 ch, u32 count, u32 thr_cfg, u32 thr_api, u8 mdns_dn)
{
	struct t32_front_write refw[MAXW], ow[MAXW];
	unsigned int ref_n, our_n, ref_seqn;
	unsigned int ref_seq[8];
	u32 oc = count, ocfg = thr_cfg, oapi = thr_api;

	t32o_reset();
	t32_data_f32d8[ch] = count;
	t32_data_f32f8[ch] = thr_cfg;
	t32_data_f32e8[ch] = thr_api;
	t32_data_dc284[ch * 904 + 37] = mdns_dn;
	t32_data_d03a0[0] = 0;
	seqn = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_top_frame_count, ch, 0, 0, 0, 0);
	ref_n = grab_ref(refw);
	ref_seqn = seqn;
	memcpy(ref_seq, seq, sizeof(seq));

	seqn = 0;
	our_n = t32_top_frame_count((u8)ch, &oc, &ocfg, &oapi, mdns_dn, 0, our_api2, our_api, ow);
	if (oc != t32_data_f32d8[ch] || ocfg != t32_data_f32f8[ch] || oapi != t32_data_f32e8[ch])
		fail("frame_count state");
	if (seq_ok && (ref_seqn != seqn || memcmp(ref_seq, seq, ref_seqn * sizeof(seq[0]))))
		fail("frame_count mdns call order");
	cmp_writes("frame_count", refw, ref_n, ow, our_n);
}

static void probes(void)
{
	t32o_rd_hook = host_reg_read;
	if ((u32)tisp_reg_read(0, 0x90, 0, 0) != host_reg_read(0x90)) {
		printf("t32_topx_oracle: env tisp_reg_read is not (ch, reg) -> t32o_rd_hook(reg)\n");
		exit(2);
	}
	seqn = 0;
	tisp_mdns_dn_params_api(1, 0, 0, 0);
	seq_ok = seqn == 1;
	seqn = 0;
	if (!seq_ok)
		printf("t32_topx_oracle: env MDNS api stubs in use, call order not compared\n");
}

int main(void)
{
	const unsigned int n = getenv("T32O_NRAND") ?
		(unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, i;

	probes();
	for (c = 0; c < n; c++) {
		u8 sec[SECB], info[904];
		u32 cfg[14];
		u32 ch = rnd() & 3, ch2 = rnd() & 1;
		u32 count, v, thr_cfg, thr_api, st;

		gen_section(sec);
		run_dn_par_refresh(sec, (u8)(c & 1));
		run_deinit(ch);
		run_ipc_get_mem_size(rnd(), rnd());
		for (i = 0; i < 14; i++)
			cfg[i] = rnd();
		if (c & 1)
			cfg[13] &= 0x1fff;
		run_ipc_set_mem_cfg(rnd(), cfg);
		for (i = 0; i < 904; i++)
			info[i] = (u8)rnd();
		st = (rnd() % 8 < 6) ? rnd() % 5 : rnd();
		run_ipc_init(st, info, rnd() & (c % 3 ? 0xfff : 0xffffffff),
			     rnd() & (c % 5 ? 0xfff : 0xffffffff), rnd() & 0xff,
			     (rnd() % 3) ? 1 : (rnd() & 0xff));
		run_sleep(ch, (u8)(c & 1));
		run_wake(ch);
		gen_section(sec);
		run_param_array_set(sec, (u8)((c >> 1) & 1));
		gen_section(sec);
		run_param_array_get(ch, sec);

		count = (c % 11 == 0) ? 0xffffff : (rnd() & 0xffffff);
		v = (count + 1) & 0xffffff;
		thr_cfg = (rnd() % 3 == 0) ? v : (rnd() % 2 ? rnd() & 0xffffff : 0xffffffffu);
		thr_api = (rnd() % 3 == 0) ? v : (rnd() % 2 ? rnd() & 0xffffff : 0xffffffffu);
		run_frame_count(ch2, count, thr_cfg, thr_api, (u8)(rnd() & 1));
	}

	/* boundaries */
	run_frame_count(0, 0xffffff, 0, 0xffffffffu, 0);
	run_frame_count(0, 0xfffffe, 0xffffff, 0xffffff, 0);
	run_frame_count(1, 5, 6, 6, 1);
	run_frame_count(1, 5, 6, 6, 0);
	run_frame_count(0, 5, 0, 0, 0);
	run_frame_count(1, 5, 0xffffffffu, 6, 0);
	run_ipc_get_mem_size(0xffff, 0xffff);
	run_ipc_get_mem_size(0, 0);
	run_ipc_get_mem_size(0x10000, 0x10001);
	{
		u32 cfg[14];

		memset(cfg, 0, sizeof(cfg));
		cfg[13] = 0x1fff;
		run_ipc_set_mem_cfg(1920, cfg);
		cfg[13] = 0x2000;
		run_ipc_set_mem_cfg(1920, cfg);
	}
	{
		u8 info[904];

		memset(info, 0, sizeof(info));
		run_ipc_init(1, info, 1920, 1080, 1, 0);
		run_ipc_init(1, info, 1920, 1080, 0, 0);
		run_ipc_init(3, info, 1920, 1080, 0, 0);
		run_ipc_init(0, info, 1920, 1080, 0, 1);
		run_ipc_init(4, info, 1920, 1080, 0, 0);
		run_ipc_init(2, info, 1920, 1080, 0, 0);
	}

	printf("t32_topx_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
