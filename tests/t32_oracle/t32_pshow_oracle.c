// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: sinfo_show and isp_vic_frd_show of driver/t32/tx_isp_t32_pshow.h against the
 * recovered vendor C (Matt Davis, PR #15 @ 2e2e06bc).  sinfo_show: random slot (in use or not), file
 * index 0..15 and beyond, sensor subdev / i2c device / sensor info / driver present or NULL, random
 * value words (0, frame-rate words with a 0 denominator); isp_vic_frd_show: valid, NULL and error
 * pointers for the proc data and the VIC device, random counters and vic_err table.  Compared: the
 * seq_printf / isp_printf stream (t32_info_common.inc), lock / unlock / lib_tisp_vic_debug_info calls
 * with their arguments, the image bytes (the per-sensor error sums), the vic_err table, the return
 * value.  Exit 0 = all equal, 1 = first mismatch.
 */
#include "t32_oracle_env.h"
#include "tx_isp_t32_pshow.h"
#include "t32_info_common.inc"

/* sinfo_show: MIPS jump table of the 15 file indexes (OEM code addresses, data.c:375..389) */
u32 t32_data_bd550[15] = { 0x12c8, 0x12fc, 0x131c, 0x1350, 0x1380, 0x13a0, 0x13b0, 0x13e4, 0x1400, 0x1410,
			   0x1420, 0x1460, 0x148c, 0x149c, 0x14b4 };
u32 t32_data_ce270[8];		/* sinfo_lock (.bss) */
u32 t32_data_d934c[52];		/* vic_err: 4 sensors x 13 counters (.bss) */

u64 mutex_lock(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; put(100 + F_LOCK, a0, 0, 0, 0, ""); return 0; }
u64 mutex_unlock(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; put(100 + F_UNLOCK, a0, 0, 0, 0, ""); return 0; }
u64 lib_tisp_vic_debug_info(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; (void)a1; return gcall(F_VICDBG, a0, 0, NULL, 0); }
static void o_lock(void *p) { put(100 + F_LOCK, T32O_PTR(p), 0, 0, 0, ""); }
static void o_unlock(void *p) { put(100 + F_UNLOCK, T32O_PTR(p), 0, 0, 0, ""); }
static int o_vicdbg(void *m) { return (int)gcall(F_VICDBG, T32O_PTR(m), 0, NULL, 0); }
static const struct t32_pshow_ops ops = { o_seq, o_printf, o_lock, o_unlock, o_vicdbg };

u64 sinfo_show(u32, u32, u32, u32), isp_vic_frd_show(u32, u32, u32, u32);

#define SLOT_OFF	0x200u
#define I2C_OFF		ATTR_OFF
#define INFO_OFF	(ATTR_OFF + 0x140)
#define DRV_OFF		(ATTR_OFF + 0x280)
#define INFO24_OFF	(ATTR_OFF + 0x3c0)

static void sinfo_setup(void)
{
	u32 sd = (rnd() & 3) ? IMGW(ISP_OFF) : 0, k;

	w32(PRIV_OFF, IMGW(SLOT_OFF));
	w32(PRIV_OFF + 4, (rnd() & 7) ? rnd() % 15 : (rnd() & 1) ? 15 + (rnd() & 1) : rnd());
	img[SLOT_OFF] = (rnd() & 7) ? 1 + (u8)(rnd() % 255) : 0;
	w32(SLOT_OFF + 16, sd);
	w32(SLOT_OFF + 4, (rnd() & 3) ? IMGW(DRV_OFF) : 0);
	w32(DRV_OFF + 28, IMGW(NAME_OFF + 32 * (rnd() & 7)));
	w32(ISP_OFF + 880, (rnd() & 3) ? IMGW(I2C_OFF) : 0);
	w32(I2C_OFF, (rnd() & 3) ? IMGW(NAME_OFF + 32 * (rnd() & 7)) : 0);
	w32(ISP_OFF + 280, (rnd() & 3) ? IMGW(INFO_OFF) : 0);
	w32(INFO_OFF + 24, (rnd() & 3) ? IMGW(INFO24_OFF) : 0);
	for (k = 896; k <= 904; k += 4)
		if (rnd() & 1)
			w32(ISP_OFF + k, ((rnd() % 61) << 16) | ((rnd() & 3) ? 1 + (rnd() & 1) : 0));
}

static void vic_setup(unsigned int c)
{
	u32 i;

	for (i = 0; i < 52; i++)
		t32_data_d934c[i] = wgen(rnd);
	switch (c % 13) {
	case 0: w32(M_OFF + 72, 0); break;
	case 1: w32(M_OFF + 72, 0xfffff001u + rnd() % 0xfff); break;
	case 2: w32(PRIV_OFF + 280, 0); break;
	case 3: w32(PRIV_OFF + 280, 0xfffff001u + rnd() % 0xfff); break;
	default: break;
	}
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 20000, c, f, lines = 0, i;
	unsigned int hit_idx[16] = { 0 }, nbad = 0;
	static u32 err0[52];

	for (c = 0; c < n; c++) {
		for (f = 0; f < 2; f++) {
			u32 rv, ro;

			case_seed = rnd();
			build_image();
			if (f == 0)
				sinfo_setup();
			else
				vic_setup(c);
			memcpy(snap, img, IMG_SZ);
			reset_side(0);
			rv = (u32)t32o_call(R(f ? isp_vic_frd_show : sinfo_show), IMGW(M_OFF), 0, 0, 0, 0);
			memcpy(after0, img, IMG_SZ);
			memcpy(err0, t32_data_d934c, sizeof(err0));
			memcpy(img, snap, IMG_SZ);
			reset_side(1);
			ro = f ? (u32)t32_isp_vic_frd_show(&ops, img + M_OFF, t32_data_d934c)
			       : (u32)t32_sinfo_show(&ops, img + M_OFF, t32_data_ce270);
			if (cmp_logs("t32_pshow_oracle", c))
				{ printf("  (%s)\n", f ? "isp_vic_frd_show" : "sinfo_show"); return 1; }
			if (rv != ro || memcmp(img, after0, IMG_SZ) || memcmp(err0, t32_data_d934c, sizeof(err0))) {
				printf("t32_pshow_oracle: case %u %s: ret %x / %x, image %s\n", c, f ? "vic_frd" : "sinfo", rv, ro,
				       memcmp(img, after0, IMG_SZ) ? "differs" : "equal");
				return 1;
			}
			lines += nev[0];
			if (f == 0 && nev[0] > 2)
				hit_idx[r32(PRIV_OFF + 4) & 15]++;
			for (i = 0; f && i < nev[0]; i++)
				nbad += EV[0][i].fn == 2;
		}
	}
	printf("t32_pshow_oracle: %u cases x 2 functions, %u calls compared (sinfo lines per index:", n, lines);
	for (i = 0; i < 15; i++)
		printf(" %u", hit_idx[i]);
	printf("; vic invalid %u), 0 mismatches\n", nbad);
	return 0;
}
