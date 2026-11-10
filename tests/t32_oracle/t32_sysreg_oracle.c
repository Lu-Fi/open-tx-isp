// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our system register access (driver/t32/tx_isp_t32_sysreg.h)
 * against the unmodified vendor functions system_reg_write / _read,
 * system_reg_ldc_write / _read and system_irq_func_set (Matt Davis' recovered C,
 * PR #15 @ 2e2e06bc; built as ref_<name>, see fix_glue_names.sh).
 * One memory image (two subdevs, two register blocks, irq table) starts random;
 * ours runs on it, the image is restored, the vendor runs; every byte of both
 * register blocks, both subdevs and the irq table and every return value must
 * be equal.  The two subdevs are each other's decoy.
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_sysreg.h"

#define WEAK __attribute__((weak))
#define RB	0x10000u	/* register block size the oracle maps per subdev */
#define SDB	0x120u

WEAK u32 t32_data_f3310[2];	/* ISP subdev pointer (.bss) */
WEAK u32 t32_data_d9944[2];	/* LDC subdev pointer (.bss); [1] = 0: the normalized reference reads it as void * */
WEAK u32 t32_data_e3b50[T32_IRQ_FUNC_N];	/* irq callback table (.bss, 0xe3b50 .. 0xe3cd0) */

u64 ref_system_reg_write(u32, u32, u32, u32);
u64 ref_system_reg_read(u32, u32, u32, u32);
void *ref_system_reg_ldc_write(int32_t, int32_t);
u64 ref_system_reg_ldc_read(u32, u32, u32, u32);
u64 ref_system_irq_func_set(u32, u32, u32, u32);

static u32 rng = 0x5eed1234u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

static u8 *img;		/* isp sd | ldc sd | isp regs | ldc regs */
static u32 imgsz;
static u8 *save0, *save1;

static int fail(const char *what, unsigned int c)
{
	printf("t32_sysreg_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	struct t32_sysreg s;
	u32 tab0[T32_IRQ_FUNC_N];
	unsigned int c, i, n = 20000;

	imgsz = 2 * SDB + 2 * RB;
	img = t32o_alloc(imgsz);
	save0 = malloc(imgsz);
	save1 = malloc(imgsz);
	for (c = 0; c < n; c++) {
		u32 op = c % 5, reg, val, ro = 0, rr = 0, idx = 0, fn = 0;
		u8 *isp = img, *ldc = img + SDB;

		for (i = 0; i < imgsz; i++)
			img[i] = (u8)rnd();
		t32_g_wr32(isp, T32_G_SD_BASE, T32_WORD(img + 2 * SDB));
		t32_g_wr32(ldc, T32_G_SD_BASE, T32_WORD(img + 2 * SDB + RB));
		for (i = 0; i < T32_IRQ_FUNC_N; i++)
			tab0[i] = rnd();
		reg = (rnd() % (RB / 4)) * 4;
		if (c < 40)	/* first / last word of the block */
			reg = (c & 1) ? RB - 4 : 0;
		val = rnd();
		if (c % 7 == 0)
			val = (c & 8) ? 0xffffffffu : 0;
		idx = rnd() % T32_IRQ_FUNC_N;
		if (c < 40)
			idx = (c & 2) ? T32_IRQ_FUNC_N - 1 : 0;
		fn = rnd();
		memcpy(save0, img, imgsz);

		/* ours */
		s.isp_sd = isp;
		s.ldc_sd = ldc;
		memcpy(s.irq_func, tab0, sizeof(tab0));
		switch (op) {
		case 0: ro = (u32)t32_system_reg_write(&s, reg, val); break;
		case 1: ro = t32_system_reg_read(&s, reg); break;
		case 2: t32_system_reg_ldc_write(&s, reg, val); break;
		case 3: ro = t32_system_reg_ldc_read(&s, reg); break;
		default: t32_system_irq_func_set(&s, idx, fn); break;
		}
		memcpy(save1, img, imgsz);

		/* vendor */
		memcpy(img, save0, imgsz);
		t32_data_f3310[0] = T32O_PTR(isp); t32_data_f3310[1] = 0;
		t32_data_d9944[0] = T32O_PTR(ldc); t32_data_d9944[1] = 0;
		memcpy(t32_data_e3b50, tab0, sizeof(tab0));
		switch (op) {
		case 0: rr = (u32)t32o_call((t32o_fn)(void (*)(void))ref_system_reg_write, reg, val, 0, 0, 0); break;
		case 1: rr = (u32)t32o_call((t32o_fn)(void (*)(void))ref_system_reg_read, reg, 0, 0, 0, 0); break;
		case 2: t32o_call((t32o_fn)(void (*)(void))ref_system_reg_ldc_write, reg, val, 0, 0, 0); break;
		case 3: rr = (u32)t32o_call((t32o_fn)(void (*)(void))ref_system_reg_ldc_read, reg, 0, 0, 0, 0); break;
		default: t32o_call((t32o_fn)(void (*)(void))ref_system_irq_func_set, rnd(), idx, fn, 0, 0); break;
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, imgsz))
			return fail("memory image (subdevs / register blocks)", c);
		if (memcmp(s.irq_func, t32_data_e3b50, sizeof(tab0)))
			return fail("irq callback table", c);
		if ((op == 0 || op == 2) && !memcmp(img, save0, imgsz) && val != t32_g_rd32(save0, 2 * SDB + (op ? RB : 0) + reg))
			return fail("write had no effect", c);
	}
	printf("t32_sysreg_oracle: %u cases, 0 mismatches\n", n);
	return 0;
}
