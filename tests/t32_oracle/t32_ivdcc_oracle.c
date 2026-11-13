// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our IVDC AOV complete (driver/t32/tx_isp_t32_ivdc_aov.h)
 * against the unmodified vendor ivdc_complete_module (Matt Davis' recovered
 * C, PR #15 @ 2e2e06bc).  A random image (IVDC subdev, private data, a
 * register window, the 1708-byte save buffer with sparse zero addresses and
 * random step words), random modes (-2..6) and NULL / error pointers run
 * through ours, are restored and run through the vendor; compared: every
 * byte (registers included) and the ordered isp_printf log (level, format
 * text, every argument, %s by its text).  Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ivdc_aov.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
char t32_data_be40c[] = "ivdc_complete_module";
char t32_data_c52d8[] = "[%s %d] AOV complete IVDC is NULL\n";
char t32_data_c52fc[] = "[%s %d] AOV resume IVDC mem is NULL\n";
char t32_data_c5324[] = "[%d]IVDC_VPU_CTRL not support! vpu0:%#x, vpu1:%#x\n";
char t32_data_c5358[] = "[%s %d] Enter the complete ... \n";
u32 t32_data_d9014, t32_data_d98b0;

u64 ivdc_complete_module(u32, u32, u32, u32);

static u32 rng = 0x85ebca6bu;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 16
static struct { unsigned int n; u32 e[MAXE][8]; } EV[2];
static int side;
static u32 hash(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

/* both sides: level, format text, then each conversion (%s by its text) */
int isp_printf(unsigned int level, const char *fmt, ...)
{
	unsigned int n = EV[side].n++, k = 3;
	const char *f;
	va_list ap;

	va_start(ap, fmt);
	if (n < MAXE) {
		EV[side].e[n][0] = 9; EV[side].e[n][1] = level;
		EV[side].e[n][2] = hash((const u8 *)fmt, (u32)strlen(fmt));
		for (f = fmt; *f && k < 8; f++) {
			if (*f != '%')
				continue;
			while (f[1] && !strchr("sdxuc", f[1]))
				f++;
			f++;
			if (*f == 's') {
				unsigned long p = va_arg(ap, unsigned long);
				const char *s = (const char *)T32O_HOST((u32)p);

				EV[side].e[n][k++] = hash((const u8 *)s, (u32)strlen(s));
			} else {
				EV[side].e[n][k++] = va_arg(ap, unsigned int);
			}
		}
	}
	va_end(ap);
	return 0;
}

#define O_SD	0x0000u
#define O_PRIV	0x0200u
#define O_REG	0x0400u		/* 0x700 register window */
#define O_SAVE	0x0c00u		/* 1708 bytes */
#define IMGSZ	0x1300u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

static int fail(const char *what, unsigned int c)
{
	printf("t32_ivdcc_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, n = 40000, hit[3] = { 0 };
	struct t32_ivdc d;

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		u8 *sd = img + O_SD, *priv = img + O_PRIV, *sv = img + O_SAVE;
		u32 sdw = T32O_PTR(sd), pw = T32O_PTR(priv), svw = T32O_PTR(sv);
		int mode = rnd() % 8 ? (int)(rnd() % 6) : (int)(rnd() % 3) - 2;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		for (i = 0; i < 4; i++) {
			u32 base = i ? 1544U + 64U * (i - 1) : 24U;

			if (rnd() % 3 == 0)
				t32_g_wr32(sv, base, 0);
			if (rnd() % 3 == 0)
				t32_g_wr32(sv, base + 4, 0);
		}
		t32_g_wr32(sv, 128, (rnd() & 0x10077u) ^ (rnd() % 4 ? 0 : rnd() & 0xfffeff88u));
		if (rnd() % 4 == 0)	/* field a 0, field b 7 */
			t32_g_wr32(sv, 128, (t32_g_rd32(sv, 128) & ~0x77u) | 0x70u);
		if (rnd() % 20 == 0)
			sdw = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		if (rnd() % 20 == 0)
			pw = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		if (rnd() % 20 == 0)
			svw = 0;
		t32_g_wr32(sd, T32_IVDC_SD_PRIV, pw);
		t32_g_wr32(priv, T32_IVDC_PRIV_REGS, T32O_PTR(img + O_REG));
		t32_data_d9014 = (u32)mode;
		t32_data_d98b0 = svw;
		memset(&d, 0, sizeof(d));
		d.mode = (u32)mode;
		d.save_buf = T32O_HOST(svw);
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			EV[side].n = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				t32_ivdc_complete_module(&d, T32O_HOST(sdw));
				memcpy(save1, img, IMGSZ);
			} else {
				t32o_call(R(ivdc_complete_module), sdw, 0, 0, 0, 0);
			}
		}
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("isp_printf log", c);
		hit[memcmp(img + O_REG, save0 + O_REG, 0x700) ? 0 : EV[0].n ? 1 : 2]++;
	}
	printf("t32_ivdcc_oracle: %u cases, 0 mismatches; registers written / printed only / silent: %u %u %u\n",
	       n, hit[0], hit[1], hit[2]);
	return 0;
}
