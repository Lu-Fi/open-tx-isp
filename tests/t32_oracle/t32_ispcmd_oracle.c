// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable isp_csi_cmd_set / isp_save_cmd_set / isp_vic_cmd_set (tx_isp_t32_ispcmd.h)
 * against the vendor functions from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @
 * 2e2e06bc, built for the host with env.c).  Random worlds built twice (subdev / private NULL,
 * error or valid (save: valid private only, the vendor crashes on NULL), user text "reset",
 * "get_vb", "isp_reg_reset", "w" with random key / value words, "snapraw", "mode", "help" or noise, lengths around the
 * 32 B buffer limit, failing kmalloc / copy_from_user, 0..4 CSI lanes); compared: return value,
 * call log (seq_printf, isp_printf format, kmalloc / kfree, copy_from_user, lock / unlock flags,
 * csi_phy_start, msleep, stop_reset, strsep tokens, kstrtouint string / base / target), the
 * static buffer d9660 and every byte of the world.
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ispcmd.h"

u64 isp_csi_cmd_set(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_save_cmd_set(u32 a0, u32 a1, u32 a2, u32 a3);
u64 isp_vic_cmd_set(u32 a0, u32 a1, u32 a2, u32 a3);
extern u8 t32_data_d9328[];
extern u32 t32_data_ced78[], mips_dma_map_ops;
extern u32 t32_data_c3acc[], t32_data_c3aec[], t32_data_bd81c[], t32_data_c3b00[], t32_data_c3b20[], t32_data_c3b7c[];
extern u32 t32_data_c3b9c[], t32_data_c3bb8[], t32_data_c3be0[], t32_data_c3c00[], t32_data_c3c04[], t32_data_c3c0c[];
extern u32 t32_data_c3c14[], t32_data_c3c20[], t32_data_c3c5c[], t32_data_c3ca8[], t32_data_c3cc4[], t32_data_c3cf8[];
extern u32 t32_data_c3d04[], t32_data_c3d50[];
extern u8 t32_data_d9660[];
extern u32 t32_data_c4888[], t32_data_c48a8[], t32_data_c48c0[], t32_data_ccae8[];

#define WSZ	0x3000u
#define MAXL	64
static u32 rs = 0xc3d5eu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 ss;
static u32 srnd(void)
{
	ss ^= ss << 13; ss ^= ss >> 17; ss ^= ss << 5;
	return ss;
}
struct lg { unsigned int n; u32 e[MAXL][4]; u8 sb[64], sb2[64]; u32 rawmode; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W, FILE_, UBUF, LEN, FN;
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b; lg.e[lg.n][3] = c;
	}
	lg.n++;
}
static void P32(u32 a, u32 v) { memcpy(T32O_HOST(a), &v, 4); }
static u32 G32(u32 a) { u32 v; memcpy(&v, T32O_HOST(a), 4); return v; }
u64 seq_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(1, a0, a1, 0); return srnd(); }
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7)
{
	u32 d = 0;

	(void)a5; (void)a6;
	if (a1 == T32O_PTR(t32_data_c3aec) || a1 == T32O_PTR(t32_data_c3be0))
		d = a2;
	else if (a1 == T32O_PTR(t32_data_c3b00))
		d = a3 ^ (a4 << 12);
	else if (a1 == T32O_PTR(t32_data_c3b20))
		d = a2 ^ (a3 << 4) ^ (a4 << 16) ^ (a7 * 7);
	L(2, a0, a1, d);
	return 0;
}
u64 private_seq_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(13, a0, a1, 0); return srnd(); }
u64 simple_strtoull(u32 a0, u32 a1, u32 a2, u32 a3)
{
	char *e;
	unsigned long long v = strtoull((const char *)T32O_HOST(a0), &e, 0);

	(void)a3;
	if (a1)
		P32(a1, a0 + (u32)(e - (char *)T32O_HOST(a0)));
	L(14, a0, a1 ? G32(a1) : 0, a2);
	return v;
}
u64 lib_tisp_vic_save_raw(u32 a0, u32 a1, u32 a2, u32 a3) { L(15, a0, a1, a2 ^ (a3 << 8)); return srnd(); }
static u64 r_sync(u32 a0, u32 a1, u32 a2, u32 a3) { L(16, a0 ^ a1, a2, a3); return srnd(); }
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	L(3, a0, a1, 0);
	return srnd() % 6 ? (u32)(uintptr_t)t32o_alloc(a0) : 0;
}
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(4, a0, 0, 0); return 0; }
u64 private_copy_from_user(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	L(5, a0, a1, a2);
	memcpy(T32O_HOST(a0), T32O_HOST(a1), a2);
	return srnd() % 8 ? 0 : 1 + srnd() % a2;
}
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3) { u32 f = srnd(); (void)a2; (void)a3; L(6, a0, G32(a1), f); P32(a1, f); return 0; }
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(7, a0, a1, 0); return 0; }
u64 csi_phy_start(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(8, a0, a1, 0); return srnd(); }
u64 private_msleep(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(9, a0, 0, 0); return 0; }
u64 lib_tisp_func_stop_reset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(10, 0, 0, 0); return srnd(); }
u64 o_strsep_sym(u32 a0, u32 a1, u32 a2, u32 a3) __asm__("strsep");
u64 o_strsep_sym(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 c = G32(a0);
	char *s, *d = (char *)T32O_HOST(a1);

	(void)a2; (void)a3;
	if (!c) {
		L(11, 0, 0, (u8)d[0]);
		return 0;
	}
	s = (char *)T32O_HOST(c);
	{
		size_t n = strcspn(s, d);

		if (s[n]) {
			s[n] = 0;
			P32(a0, c + n + 1);
		} else {
			P32(a0, 0);
		}
	}
	L(11, c, G32(a0), (u8)d[0]);
	return c;
}
u64 kstrtouint(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(12, a0, a1, a2); P32(a2, srnd()); return srnd(); }

static void o_se(u32 seq) { seq_printf(seq, T32O_PTR(t32_data_c4888), 0, 0); }
static const u32 *const vfmt[19] = { 0, t32_data_c3aec, t32_data_c3b00, t32_data_c3b20, t32_data_c3bb8, t32_data_c3be0,
	t32_data_c3c00, t32_data_c3c04, t32_data_c3c0c, t32_data_c3c14, t32_data_c3c20, t32_data_c3c5c, t32_data_c3ca8,
	t32_data_c3cc4, t32_data_c3cf8, t32_data_c3d04, t32_data_c3d50, t32_data_c3cc4, t32_data_c3c00 };
static void o_vseq(u32 seq, u32 m)
{
	if (m == 0)
		seq_printf(seq, T32O_PTR(t32_data_c3acc), 0, 0);
	else
		private_seq_printf(seq, m == 1 ? T32O_PTR(t32_data_c3b7c) : T32O_PTR(t32_data_c3b9c), 0, 0);
}
static void o_vpr(u32 l, u32 m, u32 a, u32 b, u32 c, u32 d)
{
	u32 f = T32O_PTR(vfmt[m]);

	if (m == 2)
		isp_printf(l, f, T32O_PTR(t32_data_bd81c), a, b, 0, 0, 0);
	else if (m == 3)
		isp_printf(l, f, a, b, c, 0, 0, d);
	else
		isp_printf(l, f, a, 0, 0, 0, 0, 0);
}
static u32 o_stou(u32 s, u32 *e) { return (u32)simple_strtoull(s, e ? T32O_PTR(e) : 0, 0, 0); }
static void o_sraw(u32 c, u32 r, u32 n, u32 m) { lib_tisp_vic_save_raw(c, r, n, m); }
static u32 o_dsync(void) { return G32(mips_dma_map_ops + T32_DMA_SYNC_FOR_CPU); }
static void o_sync(u32 fn, u32 hd, u32 n, u32 dir) { ((u64 (*)(u32, u32, u32, u32))(uintptr_t)fn)(0, hd, n, dir); }
static void o_log(u32 m) { isp_printf(1, m == 0 ? T32O_PTR(t32_data_c48a8) : m == 1 ? T32O_PTR(t32_data_c48c0) : T32O_PTR(t32_data_ccae8), 0, 0, 0, 0, 0, 0); }
static u32 o_km(u32 n, u32 g) { return (u32)private_kmalloc(n, g, 0, 0); }
static void o_kf(u32 p) { private_kfree(p, 0, 0, 0); }
static u32 o_cfu(u32 d, u32 s, u32 n) { return (u32)private_copy_from_user(d, s, n, 0); }
static void o_lk(u32 l, u32 *f) { __private_spin_lock_irqsave(l, T32O_PTR(f), 0, 0); }
static void o_ul(u32 l, u32 f) { private_spin_unlock_irqrestore(l, f, 0, 0); }
static void o_ps(u32 c, u32 l) { csi_phy_start(c, l, 0, 0); }
static void o_ms(u32 ms) { private_msleep(ms, 0, 0, 0); }
static void o_sr(void) { lib_tisp_func_stop_reset(0, 0, 0, 0); }
static u32 o_ss(u32 *c, const char *d) { return (u32)o_strsep_sym(T32O_PTR(c), T32O_PTR(d), 0, 0); }
static void o_ku(u32 s, u32 b, u32 r) { kstrtouint(s, b, r, 0); }
static const struct t32_cmd_ops ops = { o_se, o_log, o_km, o_kf, o_cfu, o_lk, o_ul, o_ps, o_ms, o_sr, o_ss, o_ku,
					t32_data_d9660 };
static const struct t32_viccmd_ops vops = { o_vseq, o_vpr, o_km, o_kf, o_cfu, o_stou, o_sraw, o_dsync, o_sync,
					      t32_data_d9328, t32_data_ced78 };

static const char *const words[] = { "dbgmode", "items", "interval", "vinum", "dbg", "intervals", "x", "" };
static const char *const vals[] = { "0x1f", "0X20", "123", "0", "abc", "0x" };
static void text(char *t)
{
	u32 k = rnd() % 8, n, i;

	t[0] = 0;
	if (FN == 2) {
		static const char *const vt[] = { "snapraw 1 2 3", "snapraw 0 1 4", "snapraw 4 3 3", "snapraw 5 1 1", "snapraw 2 0 9",
			"snapraw 3", "mode 1", "mode 0", "mode 2", "mode 3", "mode 0x10", "help", "hel", "snap", "x" };

		strcat(t, vt[rnd() % 15]);
		if (rnd() % 4 == 0) {
			t[8] = '0' + rnd() % 6;
		}
		return;
	}
	if (FN == 0) {
		strcat(t, k < 4 ? "reset" : k < 5 ? "rese" : k < 6 ? "resetx" : "rst");
	} else if (k < 1) {
		strcat(t, rnd() % 2 ? "get_vb" : "get_v");
	} else if (k < 2) {
		strcat(t, rnd() % 2 ? "isp_reg_reset" : "isp_reg_rese");
	} else if (k < 7) {
		strcat(t, rnd() % 6 ? "w " : "x ");
		n = rnd() % 5;
		for (i = 0; i < n; i++) {
			u32 w = rnd() % 8;

			strcat(t, words[w]);
			strcat(t, " ");
			if (w < 4 || rnd() % 2) {
				strcat(t, vals[rnd() % 6]);
				strcat(t, " ");
			}
		}
		if (strlen(t) && rnd() % 2)
			t[strlen(t) - 1] = 0;	/* the vendor needs a value after a key: last word is a value or noise */
	}
}

static void build(u32 seed)
{
	u32 save = rs, i, SD, P;
	char t[128];

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4)
		P32(W + i, rnd());
	for (i = 0; i < 64; i++) {
		t32_data_d9660[i] = rnd();
		t32_data_d9328[i] = rnd();
	}
	t32_data_ced78[0] = rnd() % 4 ? rnd() % 4 : rnd();
	mips_dma_map_ops = W + 0x2800;
	P32(W + 0x2800 + T32_DMA_SYNC_FOR_CPU, rnd() % 3 ? T32O_PTR(r_sync) : 0);
	for (i = 0; i < 5; i++) {
		P32(W + 0x400 + T32_VIC_CH_W + T32_VIC_CH_SIZE * i, rnd() % 6 ? 64 + rnd() % 4000 : rnd() % 2 ? 3840 + rnd() % 2 : rnd());
		P32(W + 0x400 + T32_VIC_CH_H + T32_VIC_CH_SIZE * i, rnd() % 6 ? 64 + rnd() % 3000 : rnd());
	}
	P32(W + 0x400 + T32_VIC_RAWBUF, rnd() % 6 ? rnd() | 1 : 0);

	FN = rnd() % 3;
	FILE_ = W;
	P32(W + T32_FILE_PRIV, W + 0x100);
	SD = W + 0x200;
	P = W + 0x400;
	/* save: an invalid subdev or a NULL private block crashes the vendor, so only csi gets them */
	P32(W + 0x100 + T32_SEQ_PRIV, FN == 1 || rnd() % 16 ? SD : rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff);
	P32(SD + T32_SD_PRIV, FN == 1 || rnd() % 12 ? P : rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff);
	P32(P + T32_CSI_NLANES, rnd() % 5);
	P32(P + T32_CSI_RESTART, rnd() % 2 ? 0 : rnd());
	text(t);
	UBUF = W + 0x2000;
	i = strlen(t);
	memcpy(T32O_HOST(UBUF), t, i + 1);
	if (FN == 1)
		LEN = rnd() % 4 ? i + 1 : i ? i : 1;
	else
		LEN = rnd() % 3 ? i + 1 : rnd() % 2 ? 1 + rnd() % 64 : 28 + rnd() % 10;
	if (FN == 2 && rnd() % 4 == 0) {	/* stale text in the static buffer behind a short copy */
		memcpy(t32_data_d9328, t, i + 1);
		LEN = 8;
	}
	if (FN == 1 && strchr(t, ' ') && LEN != i + 1)
		LEN = i + 1;	/* the words must end inside the copy */
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x9e3779b1u) | 1;
	rs = save;
}

static u64 ours(u32 f, u32 u, u32 n, u32 a3)
{
	(void)a3;
	return (u32)(FN == 2 ? t32_isp_vic_cmd_set(&vops, f, u, n) : FN ? t32_isp_save_cmd_set(&ops, f, u, n) :
		     t32_isp_csi_cmd_set(&ops, f, u, n));
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;

	for (it = 0; it < 20000; it++) {
		u32 seed = rnd();
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ours, FILE_, UBUF, LEN, 0, 0);
		memcpy(lg.sb, t32_data_d9660, 64);
		memcpy(lg.sb2, t32_data_d9328, 64);
		lg.rawmode = t32_data_ced78[0];
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))(FN == 2 ? isp_vic_cmd_set : FN ? isp_save_cmd_set : isp_csi_cmd_set), FILE_, UBUF, LEN, rnd(), 0);
		memcpy(lg.sb, t32_data_d9660, 64);
		memcpy(lg.sb2, t32_data_d9328, 64);
		lg.rawmode = t32_data_ced78[0];
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispcmd_oracle: MISMATCH seed %08x fn %u: %d/%d log %u/%u\n", seed, FN, r, rr, lg_ours.n, lg.n);
			if (getenv("T32O_DBG") && failed == 1) {
				unsigned int k;

				for (k = 0; k < lg.n && k < MAXL; k++)
					printf("  %2u: %u %08x %08x %08x | %u %08x %08x %08x\n", k, lg_ours.e[k][0], lg_ours.e[k][1],
					       lg_ours.e[k][2], lg_ours.e[k][3], lg.e[k][0], lg.e[k][1], lg.e[k][2], lg.e[k][3]);
				printf("  text '%.40s' len %u\n", (char *)T32O_HOST(UBUF), LEN);
				printf("  b7c %08x b9c %08x w3 %u buf %08x\n", T32O_PTR(t32_data_c3b7c), T32O_PTR(t32_data_c3b9c), G32(W + 0x400 + 288 + 3 * 104), G32(W + 0x400 + 872));
			}
		}
	}
	printf("t32_ispcmd_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
