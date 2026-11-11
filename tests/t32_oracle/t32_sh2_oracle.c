// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_lib_tisp_vic_debug_info and t32_lib_tisp_debug_info of driver/t32/tx_isp_t32_show.h
 * against the vendor lib_tisp_vic_debug_info / lib_tisp_debug_info (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Random counters and attribute blocks (VIC blocks present or NULL), 0..2 sensors with random state, bin path
 * strings and default-path flags, tisp_awb_get_attr / tisp_ae_get_info hooks that fill a random prefix of their
 * record (the rest must stay the vendor's zero).  Compared: the print stream with every hook call (sensor) in
 * order, the result, the seq_file count and that no input changed.  Exit 0 = all equal, 1 = first mismatch.
 */
#include "t32_show_rec.h"
#include "tx_isp_t32_show.h"

u64 lib_tisp_vic_debug_info(u32, u32, u32, u32), lib_tisp_debug_info(u32, u32, u32, u32);
u32 t32_data_e3f20[2], t32_data_e3f58[2], t32_data_e3610[1], t32_data_e3cd0[2];
extern u32 t32_data_e3ef8[1];
extern u8 t32_data_e3e78[128], t32_data_e3df0[8], t32_data_e3cf0[128], t32_data_e3d70[128], t32_data_e3df8[128];

static u32 hook_seed;
static void hook_fill(u32 kind, u32 ch, u8 *rec, u32 size)
{
	u32 st = hook_seed ^ (kind * 0x9e3779b9u) ^ (ch * 0x85ebca6bu), i, len;
	char t[32];

	st |= 1;
	len = (st >> 3) % (size + 1);
	for (i = 0; i < len; i++) {
		st ^= st << 13; st ^= st >> 17; st ^= st << 5;
		rec[i] = (u8)(st >> 7);
	}
	snprintf(t, sizeof(t), "hook %u %u", kind, ch);
	sr_put(SR_HOOK, kind, ch, 0, t);
}
u64 tisp_awb_get_attr(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hook_fill(1, a0, T32O_HOST(a1), 76); return 0; }
u64 tisp_ae_get_info(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; hook_fill(2, a0, T32O_HOST(a1), 32); return 0; }
static void o_awb(u32 ch, u8 *rec) { hook_fill(1, ch, rec, 76); }
static void o_ae(u32 ch, u8 *rec) { hook_fill(2, ch, rec, 32); }

static void fill_str(u8 *p, u32 n)	/* printable text with its NUL inside n */
{
	u32 i, len = sr_rnd() % n;

	for (i = 0; i < len; i++)
		p[i] = (u8)(0x21 + sr_rnd() % 94);
	p[len] = 0;
}

static u32 snap_n;
static void snap_take(u8 *dst)
{
	u32 k = 0;
#define S(x) do { memcpy(dst + k, x, sizeof(x)); k += sizeof(x); } while (0)
	S(t32_data_e3f58); S(t32_data_e3610); S(t32_data_e3cd0); S(t32_data_e3ef8); S(t32_data_e3e78);
	S(t32_data_e3df0); S(t32_data_e3cf0); S(t32_data_e3d70); S(t32_data_e3df8); S(t32_data_dc284); S(t32_data_da850);
#undef S
	snap_n = k;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000, c, prints = 0, i, k;
	static u8 s0[4096], s1[4096];
	static const struct t32_dbgi_src src = {
		t32_data_e3ef8, t32_data_e3e78, t32_data_e3df0, t32_data_e3cf0, t32_data_e3d70, t32_data_e3610,
		(const u8 *)t32_data_e3cd0, t32_data_dc284, t32_data_da850, t32_data_e3df8, o_awb, o_ae,
	};

	for (c = 0; c < n; c++) {
		u8 *m, m0[128], m1[128], *vb[2], vb0[2][128];
		u32 r0, r1;

		/* ---- lib_tisp_vic_debug_info ---- */
		t32o_reset();
		m = t32o_alloc(128);
		sr_fill(m, 128);
		*(u32 *)(m + 12) = sr_rnd() % 4096;
		for (i = 0; i < 2; i++) {
			vb[i] = t32o_alloc(128);
			sr_fill(vb[i], 128);
			if (sr_rnd() & 3) ((u32 *)vb[i])[0] = sr_rnd() % 4;
			if (sr_rnd() & 3) ((u32 *)vb[i])[1] = sr_rnd() % 4;
			memcpy(vb0[i], vb[i], 128);
			t32_data_e3f20[i] = (sr_rnd() % 5 == 0) ? 0 : T32O_PTR(vb[i]);
			t32_data_e3f58[i] = sr_rnd();
		}
		memcpy(m0, m, 128);
		sr_begin();
		r0 = (u32)t32o_call(SR_R(lib_tisp_vic_debug_info), T32O_PTR(m), sr_rnd(), sr_rnd(), sr_rnd(), 0);
		memcpy(m1, m, 128); memcpy(m, m0, 128);
		sr_side = 1;
		r1 = (u32)t32_lib_tisp_vic_debug_info(m, t32_data_e3f20, t32_data_e3f58);
		if (sr_cmp("t32_sh2_oracle", c, "lib_tisp_vic_debug_info")) return 1;
		if (r0 != r1 || memcmp(m1, m, 128) || memcmp(vb0[0], vb[0], 128) || memcmp(vb0[1], vb[1], 128)) {
			printf("t32_sh2_oracle: case %u vic_debug_info: ret %08x/%08x\n", c, r0, r1);
			return 1;
		}
		prints += sr_n[0];

		/* ---- lib_tisp_debug_info ---- */
		t32o_reset();
		m = t32o_alloc(128);
		sr_fill(m, 128);
		*(u32 *)(m + 12) = sr_rnd() % 4096;
		t32_data_e3ef8[0] = sr_rnd(); t32_data_e3610[0] = sr_rnd();
		t32_data_e3cd0[0] = sr_rnd(); t32_data_e3cd0[1] = sr_rnd();
		sr_fill(t32_data_e3e78, 128); sr_fill(t32_data_e3df0, 8); sr_fill(t32_data_e3cf0, 128);
		sr_fill(t32_data_e3d70, 128); sr_fill(t32_data_e3df8, 128);
		sr_fill(t32_data_dc284, sizeof(t32_data_dc284));
		for (i = 0; i < 2; i++) {
			u8 *sn = t32_data_dc284 + 904 * i;

			for (k = 0; k < 6; k++) {
				u32 r = sr_rnd() % 4;

				sn[126 + k] = r == 0 ? 1 : r == 1 ? 0 : (u8)sr_rnd();
				fill_str(sn + 132 + 128 * k, 128);
			}
			fill_str(t32_data_da850 + 336 * i, 336);
		}
		*(u32 *)(t32_data_dc284 + 900) = c % 8 == 0 ? 0 : 1 + sr_rnd() % 2;
		hook_seed = sr_rnd();
		snap_take(s0);
		memcpy(m0, m, 128);
		sr_begin();
		r0 = (u32)t32o_call(SR_R(lib_tisp_debug_info), T32O_PTR(m), sr_rnd(), sr_rnd(), sr_rnd(), 0);
		memcpy(m1, m, 128); memcpy(m, m0, 128);
		sr_side = 1;
		r1 = (u32)t32_lib_tisp_debug_info(m, &src);
		snap_take(s1);
		if (sr_cmp("t32_sh2_oracle", c, "lib_tisp_debug_info")) return 1;
		if (r0 != r1 || memcmp(m1, m, 128) || memcmp(s0, s1, snap_n)) {
			printf("t32_sh2_oracle: case %u lib_tisp_debug_info: ret %08x/%08x\n", c, r0, r1);
			return 1;
		}
		prints += sr_n[0];
	}
	printf("t32_sh2_oracle: %u cases x 2 functions compared (%u prints / hook calls), 0 mismatches\n", n, prints);
	return 0;
}
