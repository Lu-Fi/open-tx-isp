// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable isp_csi_cmd_set / isp_save_cmd_set (driver/t32/tx_isp_t32_ispcmd.h)
 * against the vendor functions from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @
 * 2e2e06bc, built for the host with env.c).  Random worlds built twice (subdev / private NULL,
 * error or valid (save: valid private only, the vendor crashes on NULL), user text "reset",
 * "get_vb", "isp_reg_reset", "w" with random key / value words or noise, lengths around the
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
struct lg { unsigned int n; u32 e[MAXL][4]; u8 sb[64]; };
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
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(2, a0, a1, 0); return 0; }
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
static void o_log(u32 m) { isp_printf(1, m == 0 ? T32O_PTR(t32_data_c48a8) : m == 1 ? T32O_PTR(t32_data_c48c0) : T32O_PTR(t32_data_ccae8), 0, 0); }
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

static const char *const words[] = { "dbgmode", "items", "interval", "vinum", "dbg", "intervals", "x", "" };
static const char *const vals[] = { "0x1f", "0X20", "123", "0", "abc", "0x" };
static void text(char *t)
{
	u32 k = rnd() % 8, n, i;

	t[0] = 0;
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
	for (i = 0; i < 64; i++)
		t32_data_d9660[i] = rnd();
	FN = rnd() % 2;
	FILE_ = W;
	P32(W + T32_FILE_PRIV, W + 0x100);
	SD = W + 0x200;
	P = W + 0x400;
	/* save: an invalid subdev or a NULL private block crashes the vendor, so only csi gets them */
	P32(W + 0x100 + T32_SEQ_PRIV, FN || rnd() % 16 ? SD : rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff);
	P32(SD + T32_SD_PRIV, FN || rnd() % 12 ? P : rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff);
	P32(P + T32_CSI_NLANES, rnd() % 5);
	P32(P + T32_CSI_RESTART, rnd() % 2 ? 0 : rnd());
	text(t);
	UBUF = W + 0x2000;
	i = strlen(t);
	memcpy(T32O_HOST(UBUF), t, i + 1);
	if (FN)
		LEN = rnd() % 4 ? i + 1 : i ? i : 1;
	else
		LEN = rnd() % 3 ? i + 1 : rnd() % 2 ? 1 + rnd() % 64 : 28 + rnd() % 10;
	if (FN && strchr(t, ' ') && LEN != i + 1)
		LEN = i + 1;	/* the words must end inside the copy */
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x9e3779b1u) | 1;
	rs = save;
}

static u64 ours(u32 f, u32 u, u32 n, u32 a3)
{
	(void)a3;
	return (u32)(FN ? t32_isp_save_cmd_set(&ops, f, u, n) : t32_isp_csi_cmd_set(&ops, f, u, n));
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
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))(FN ? isp_save_cmd_set : isp_csi_cmd_set), FILE_, UBUF, LEN, rnd(), 0);
		memcpy(lg.sb, t32_data_d9660, 64);
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_ispcmd_oracle: MISMATCH seed %08x fn %u: %d/%d log %u/%u\n", seed, FN, r, rr, lg_ours.n, lg.n);
		}
	}
	printf("t32_ispcmd_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
