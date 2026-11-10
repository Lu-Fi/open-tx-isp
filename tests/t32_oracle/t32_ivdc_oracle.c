// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our IVDC core / module power-management glue
 * (driver/t32/tx_isp_t32_ivdc.h) against the unmodified vendor functions
 * ivdc_core_ops_init, ivdc_{prepare,resume,resume_early,suspend,slake}_module,
 * ivdc_isp_info_clear, ivdc_enable_irq / _disable_irq and the empty callbacks
 * and ivdc_core_ops_ioctl (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 *
 * One memory image (subdev, private data, registers, clock list, save
 * buffer, a decoy register block) and the IVDC globals start random per case;
 * ours runs, the image is restored, the vendor runs.  Compared: every byte of
 * the image, every global, the CPM clock-gate word (a page mapped at
 * 0xb0000000 as on the T32's KSEG1), the return values and one ordered log of
 * every OS call (lock / unlock with lock address and flags, get_isp_clka /
 * clkv, clk_disable_unprepare, msleep, udelay, the irq enable / disable
 * callbacks, isp_printf with level and the vendor line / mode argument).
 * udelay clears the busy bit after a scripted number of calls (several polls).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ivdc.h"

#define WEAK __attribute__((weak))
#define R(f)	((t32o_fn)(void (*)(void))(f))

/* vendor globals (.bss / .data) and printk strings (content irrelevant) */
WEAK u32 t32_data_d9014, t32_data_d9894, t32_data_d9890, t32_data_d98b0, t32_data_d98b4;
WEAK u32 t32_data_d98a0, t32_data_d98a4, t32_data_d98a8, t32_data_d98ac;
WEAK u32 t32_data_d98e8, t32_data_d98ec, t32_data_d98f0, t32_data_d98f4;
WEAK u32 t32_data_cf2dc, t32_data_cf2e0, t32_data_cf2e4, t32_data_d989c;
WEAK char t32_data_be390[4], t32_data_be3b4[4], t32_data_be3c8[4], t32_data_be3e4[4], t32_data_be3f8[4], t32_data_be454[4];
WEAK char t32_data_c537c[4], t32_data_c53a0[4], t32_data_c53c4[4], t32_data_c5b5c[4], t32_data_c5b80[4];
WEAK char t32_data_c5b9c[4], t32_data_c5bb8[4], t32_data_c5bd8[4], t32_data_c5bfc[4];

u64 ivdc_core_ops_init(u32, u32, u32, u32);
u64 ivdc_prepare_module(u32, u32, u32, u32);
u64 ivdc_resume_module(u32, u32, u32, u32);
u64 ivdc_resume_early_module(u32, u32, u32, u32);
u64 ivdc_suspend_module(u32, u32, u32, u32);
u64 ivdc_slake_module(u32, u32, u32, u32);
u64 ivdc_isp_info_clear(u32, u32, u32, u32);
u64 ivdc_enable_irq(u32, u32, u32, u32);
u64 ivdc_disable_irq(u32, u32, u32, u32);
u64 ivdc_core_ops_ioctl(u32, u32, u32, u32);
int32_t ivdc_link_setup(void);
int32_t ivdc_misc_open(void);
int32_t ivdc_misc_release(void);
int32_t ivdc_misc_read(void);
int32_t ivdc_video_s_stream(void);

static u32 rng = 0x2468ace1u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

/* ---- ordered event log ---- */
#define MAXE 64
enum { E_LOCK = 1, E_UNLOCK, E_CLKA, E_CLKV, E_CLKDIS, E_MSLEEP, E_UDELAY, E_IRQEN, E_IRQDIS, E_PRINTF };
static struct { unsigned int n; u32 e[MAXE][4]; } EV[2];
static int side;
static u32 sc_flags, sc_busy;	/* lock flags, udelay calls until the busy bit clears */
static u32 nudelay;
static u8 *busy_reg;
static void ev(u32 t, u32 a, u32 b, u32 c)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = t; EV[side].e[n][1] = a; EV[side].e[n][2] = b; EV[side].e[n][3] = c;
	}
}

/* ours */
static void o_lock(u8 *l, u32 *f) { ev(E_LOCK, T32O_PTR(l), 0, 0); *f = sc_flags; }
static void o_unlock(u8 *l, u32 f) { ev(E_UNLOCK, T32O_PTR(l), f, 0); }
static void o_clka(void) { ev(E_CLKA, 0, 0, 0); }
static void o_clkv(void) { ev(E_CLKV, 0, 0, 0); }
static void o_clkdis(u32 c) { ev(E_CLKDIS, c, 0, 0); }
static void o_msleep(u32 ms) { ev(E_MSLEEP, ms, 0, 0); }
static void udelay_common(u32 us)
{
	if (nudelay < 4)
		ev(E_UDELAY, us, 0, 0);
	if (++nudelay == sc_busy)
		t32_g_wr32(busy_reg, 0, t32_g_rd32(busy_reg, 0) & ~1u);
	if (nudelay > 100000)	/* runaway: stop the case (both sides would loop) */
		t32_g_wr32(busy_reg, 0, 0);
}
static void o_udelay(u32 us) { udelay_common(us); }
static const struct t32_ivdc_os os = {
	.cpm = (volatile u8 *)(uintptr_t)0xb0000000u,
	.lock = o_lock, .unlock = o_unlock, .clka = o_clka, .clkv = o_clkv,
	.clk_disable_unprepare = o_clkdis, .msleep = o_msleep, .udelay = o_udelay,
};

/* vendor (the env.c lock stubs are weak) */
u64 __private_spin_lock_irqsave(u32 l, u32 f, u32 a2, u32 a3) { (void)a2; (void)a3; ev(E_LOCK, l, 0, 0); *(u32 *)T32O_HOST(f) = sc_flags; return 0; }
u64 private_spin_unlock_irqrestore(u32 l, u32 f, u32 a2, u32 a3) { (void)a2; (void)a3; ev(E_UNLOCK, l, f, 0); return 0; }
u64 get_isp_clka(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; ev(E_CLKA, 0, 0, 0); return 0; }
u64 get_isp_clkv(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; ev(E_CLKV, 0, 0, 0); return 0; }
u64 private_clk_disable_unprepare(u32 c, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(E_CLKDIS, c, 0, 0); return 0; }
u64 msleep(u32 ms, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(E_MSLEEP, ms, 0, 0); return 0; }
u64 __udelay(u32 us, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; udelay_common(us); return 0; }

/* irq enable / disable callbacks of the subdev */
static void o_irqen(u8 *dev, u32 z) { ev(E_IRQEN, T32O_PTR(dev), z, 0); }
static void o_irqdis(u8 *dev, u32 z) { ev(E_IRQDIS, T32O_PTR(dev), z, 0); }
static u64 r_irqen(u32 dev, u32 z, u32 a2, u32 a3) { (void)a2; (void)a3; ev(E_IRQEN, dev, z, 0); return 0; }
static u64 r_irqdis(u32 dev, u32 z, u32 a2, u32 a3) { (void)a2; (void)a3; ev(E_IRQDIS, dev, z, 0); return 0; }

static u32 sc_sdret;
static int o_sdop(u8 *sd, void *a) { ev(E_IRQEN + 100, T32O_PTR(sd), T32O_PTR(a), 0); return (int)sc_sdret; }
static u64 r_sdop(u32 sd, u32 a, u32 a2, u32 a3) { (void)a2; (void)a3; ev(E_IRQEN + 100, sd, a, 0); return sc_sdret; }

/* isp_printf: level, the mode argument of the core start print, the vendor line argument */
int isp_printf(unsigned int level, const char *fmt, ...)
{
	va_list ap;
	unsigned long a2, a3;
	int noline, mode;

	va_start(ap, fmt);
	a2 = va_arg(ap, unsigned long);
	a3 = va_arg(ap, unsigned long);
	va_end(ap);
	if (side) {
		noline = fmt == t32_data_c5b9c || fmt == t32_data_c5bb8;
		mode = fmt == t32_data_c5bb8;
	} else {
		noline = 0;
		mode = !strcmp(fmt, "ivdc mode %d\n");
	}
	ev(E_PRINTF, level, mode ? (u32)a2 : 0, noline ? 0 : (u32)a3);
	return 0;
}

/* ---- image ---- */
#define O_SD	0x0000u
#define O_PRIV	0x0200u
#define O_REGS	0x0400u		/* 0x800 */
#define O_CLKS	0x0c00u
#define O_SAVE	0x0d00u		/* 0x700 */
#define O_IREGS	0x1400u		/* register block of the isp_info_clear private data, 0x100 */
#define O_IPRIV	0x1500u
#define IMGSZ	0x1700u
static u8 *img, save0[IMGSZ], save1[IMGSZ];

struct glob { u32 w[17]; };
static void glob_ours(const struct t32_ivdc *d, struct glob *g)
{
	g->w[0] = d->mode; g->w[1] = d->started_mode; g->w[2] = T32O_PTR(d->info_priv); g->w[3] = T32O_PTR(d->save_buf);
	g->w[4] = d->d98b4;
	memcpy(&g->w[5], d->saved, 16);
	memcpy(&g->w[9], d->save_src, 16);
	memcpy(&g->w[13], d->flag, 12);
	g->w[16] = d->ioc_flag;
}
static void glob_vendor(struct glob *g)
{
	g->w[0] = t32_data_d9014; g->w[1] = t32_data_d9894; g->w[2] = t32_data_d9890; g->w[3] = t32_data_d98b0;
	g->w[4] = t32_data_d98b4;
	g->w[5] = t32_data_d98a0; g->w[6] = t32_data_d98a4; g->w[7] = t32_data_d98a8; g->w[8] = t32_data_d98ac;
	g->w[9] = t32_data_d98e8; g->w[10] = t32_data_d98ec; g->w[11] = t32_data_d98f0; g->w[12] = t32_data_d98f4;
	g->w[13] = t32_data_cf2dc; g->w[14] = t32_data_cf2e0; g->w[15] = t32_data_cf2e4;
	g->w[16] = t32_data_d989c;
}
static void glob_set_vendor(const struct glob *g)
{
	t32_data_d9014 = g->w[0]; t32_data_d9894 = g->w[1]; t32_data_d9890 = g->w[2]; t32_data_d98b0 = g->w[3];
	t32_data_d98b4 = g->w[4];
	t32_data_d98a0 = g->w[5]; t32_data_d98a4 = g->w[6]; t32_data_d98a8 = g->w[7]; t32_data_d98ac = g->w[8];
	t32_data_d98e8 = g->w[9]; t32_data_d98ec = g->w[10]; t32_data_d98f0 = g->w[11]; t32_data_d98f4 = g->w[12];
	t32_data_cf2dc = g->w[13]; t32_data_cf2e0 = g->w[14]; t32_data_cf2e4 = g->w[15];
	t32_data_d989c = g->w[16];
}

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_ivdc_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

static u32 pick_ptr(u32 good)
{
	switch (rnd() % 10) {
	case 0: return 0;
	case 1: return rnd() % 2 ? 0xfffff001u : 0xfffff001u + rnd() % 0xfff;	/* ERR_PTR, -4095 .. -1 */
	default: return good;
	}
}

#define NOPS 11
int main(void)
{
	static const u32 states[] = { 0, 1, 2, 3, 4, 0x80000000u };
	unsigned int c, i, n = 66000, eff[NOPS] = { 0 };
	volatile u32 *cpm;
	u32 cpm0;

	if (mmap((void *)(uintptr_t)0xb0000000u, 4096, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != (void *)(uintptr_t)0xb0000000u) {
		printf("t32_ivdc_oracle: SKIP (cannot map the CPM page at 0xb0000000)\n");
		return 0;
	}
	cpm = (volatile u32 *)(uintptr_t)(0xb0000000u + T32_CPM_CLKGATE);
	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % NOPS;
		struct t32_ivdc d;
		struct glob g0, gr, go;
		u32 sdw, ro = 0, rr = 0, on, cpm_o = 0, nud[2], ioc;
		u8 *sd = img + O_SD, *priv = img + O_PRIV;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		cpm0 = rnd();
		sdw = pick_ptr(T32O_PTR(sd));
		t32_g_wr32(sd, T32_IVDC_SD_PRIV, pick_ptr(T32O_PTR(priv)));
		if (op >= 7 && op <= 8)		/* irq enable / disable use the private data unchecked */
			t32_g_wr32(sd, T32_IVDC_SD_PRIV, T32O_PTR(priv)), sdw = T32O_PTR(sd);
		t32_g_wr32(priv, T32_IVDC_PRIV_REGS, T32O_PTR(img + O_REGS));
		t32_g_wr32(priv, T32_IVDC_PRIV_STATE, states[rnd() % 6]);
		t32_g_wr32(sd, T32_IVDC_SD_CLKS, T32O_PTR(img + O_CLKS));
		t32_g_wr32(sd, T32_IVDC_SD_CLK_NUM, rnd() % 8 == 0 ? 0xffffffffu : rnd() % 6);
		t32_g_wr32(sd, T32_IVDC_SD_IRQ_ON, rnd() % 3);
		t32_g_wr32(img + O_IPRIV, T32_IVDC_PRIV_REGS, T32O_PTR(img + O_IREGS));
		for (i = 0; i < 17; i++)
			g0.w[i] = rnd();
		g0.w[0] = rnd() % 4 ? rnd() % 6 : rnd();
		if (rnd() % 3 == 0)
			g0.w[1] = g0.w[0];
		g0.w[2] = pick_ptr(T32O_PTR(img + O_IPRIV));
		g0.w[3] = rnd() % 5 ? T32O_PTR(img + O_SAVE) : 0;
		on = rnd() % 2;
		t32_g_wr32(img + O_SAVE + 0x6f0, 0, on);	/* the "on" word of core_ops_init */
		sc_flags = rnd();
		sc_sdret = rnd() % 3 == 0 ? (u32)-515 : rnd() % 2 ? 0 : rnd();
		ioc = (u32[]){ T32_IVDC_IOC_INIT, T32_IVDC_IOC_MODE, T32_IVDC_IOC_FLAG_CLR, T32_IVDC_IOC_FLAG_SET,
			       0x1000001u, 0x1000004u, 0x1000010u, 0 }[rnd() % 8];
		if (op == 10 && ioc == T32_IVDC_IOC_INIT && sdw && !t32_ivdc_valid(T32O_HOST(sdw)))
			sdw = T32O_PTR(sd);	/* the vendor dereferences an ERR_PTR subdev here */
		t32_g_wr32(sd, T32_IVDC_SD_OPS, T32O_PTR(img + O_IPRIV + 0x100));
		t32_g_wr32(img + O_IPRIV + 0x100, 0, rnd() % 4 ? T32O_PTR(img + O_IPRIV + 0x120) : 0);
		sc_busy = 1 + rnd() % 700;
		busy_reg = img + O_REGS;
		t32_g_wr32(sd, T32_IVDC_SD_IRQ_EN, 0);	/* set per side, cleared after */
		t32_g_wr32(sd, T32_IVDC_SD_IRQ_DIS, 0);
		t32_g_wr32(img + O_IPRIV + 0x120, 4, 0);
		memcpy(save0, img, IMGSZ);

		{
			u32 m = rnd();

			for (side = 0; side < 2; side++) {
				EV[side].n = 0;
				nudelay = 0;
				memcpy(img, save0, IMGSZ);
				*cpm = cpm0;
				t32_g_wr32(sd, T32_IVDC_SD_IRQ_EN, (m & 1) ? 0 : side ? T32O_PTR(r_irqen) : T32O_PTR(o_irqen));
				t32_g_wr32(img + O_IPRIV + 0x120, 4, (m & 4) ? 0 : side ? T32O_PTR(r_sdop) : T32O_PTR(o_sdop));
				t32_g_wr32(sd, T32_IVDC_SD_IRQ_DIS, (m & 2) ? 0 : side ? T32O_PTR(r_irqdis) : T32O_PTR(o_irqdis));
				if (!side) {
					d.mode = g0.w[0]; d.started_mode = g0.w[1]; d.info_priv = T32O_HOST(g0.w[2]);
					d.save_buf = T32O_HOST(g0.w[3]); d.d98b4 = g0.w[4];
					memcpy(d.saved, &g0.w[5], 16); memcpy(d.save_src, &g0.w[9], 16); memcpy(d.flag, &g0.w[13], 12); d.ioc_flag = g0.w[16];
					d.os = &os;
					switch (op) {
					case 0: ro = (u32)t32_ivdc_core_ops_init(&d, T32O_HOST(sdw), (const u32 *)(img + O_SAVE + 0x6f0)); break;
					case 1: ro = (u32)t32_ivdc_prepare_module(&d, T32O_HOST(sdw)); break;
					case 2: ro = (u32)t32_ivdc_resume_module(&d, T32O_HOST(sdw)); break;
					case 3: ro = (u32)t32_ivdc_resume_early_module(&d, T32O_HOST(sdw)); break;
					case 4: ro = (u32)t32_ivdc_suspend_module(&d, T32O_HOST(sdw)); break;
					case 5: ro = (u32)t32_ivdc_slake_module(&d, T32O_HOST(sdw)); break;
					case 6: ro = (u32)t32_ivdc_isp_info_clear(&d); break;
					case 7: t32_ivdc_enable_irq(&d, T32O_HOST(sdw)); break;
					case 8: t32_ivdc_disable_irq(&d, T32O_HOST(sdw)); break;
					case 10: ro = (u32)t32_ivdc_core_ops_ioctl(&d, T32O_HOST(sdw), ioc, T32O_HOST(T32O_PTR(img + O_SAVE))); break;
					default:
						ro = (u32)(t32_ivdc_link_setup() | t32_ivdc_misc_open() << 1 | t32_ivdc_misc_release() << 2 |
							   t32_ivdc_misc_read() << 3 | t32_ivdc_video_s_stream() << 4);
						break;
					}
					glob_ours(&d, &go);
				} else {
					glob_set_vendor(&g0);
					switch (op) {
					case 0: rr = (u32)t32o_call(R(ivdc_core_ops_init), sdw, T32O_PTR(img + O_SAVE + 0x6f0), 0, 0, 0); break;
					case 1: rr = (u32)t32o_call(R(ivdc_prepare_module), sdw, 0, 0, 0, 0); break;
					case 2: rr = (u32)t32o_call(R(ivdc_resume_module), sdw, 0, 0, 0, 0); break;
					case 3: rr = (u32)t32o_call(R(ivdc_resume_early_module), sdw, 0, 0, 0, 0); break;
					case 4: rr = (u32)t32o_call(R(ivdc_suspend_module), sdw, 0, 0, 0, 0); break;
					case 5: rr = (u32)t32o_call(R(ivdc_slake_module), sdw, 0, 0, 0, 0); break;
					case 6: rr = (u32)t32o_call(R(ivdc_isp_info_clear), 0, 0, 0, 0, 0); break;
					case 7: t32o_call(R(ivdc_enable_irq), sdw, 0, 0, 0, 0); break;
					case 8: t32o_call(R(ivdc_disable_irq), sdw, 0, 0, 0, 0); break;
					case 10: rr = (u32)t32o_call(R(ivdc_core_ops_ioctl), sdw, ioc, T32O_PTR(img + O_SAVE), 0, 0); break;
					default:
						rr = (u32)((u32)t32o_call(R(ivdc_link_setup), 1, 2, 3, 4, 0) |
							   (u32)t32o_call(R(ivdc_misc_open), 1, 2, 3, 4, 0) << 1 |
							   (u32)t32o_call(R(ivdc_misc_release), 1, 2, 3, 4, 0) << 2 |
							   (u32)t32o_call(R(ivdc_misc_read), 1, 2, 3, 4, 0) << 3 |
							   (u32)t32o_call(R(ivdc_video_s_stream), 1, 2, 3, 4, 0) << 4);
						break;
					}
					glob_vendor(&gr);
				}
				t32_g_wr32(sd, T32_IVDC_SD_IRQ_EN, 0);
				t32_g_wr32(sd, T32_IVDC_SD_IRQ_DIS, 0);
				t32_g_wr32(img + O_IPRIV + 0x120, 4, 0);
				nud[side] = nudelay;
				if (!side) {
					memcpy(save1, img, IMGSZ);
					cpm_o = *cpm;
				}
			}
		}
		if (op != 7 && op != 8 && ro != rr)
			return fail("return value", c, op);
		if (memcmp(go.w, gr.w, sizeof(go.w)))
			return fail("globals", c, op);
		if (cpm_o != *cpm)
			return fail("CPM clock gate", c, op);
		if (memcmp(img, save1, IMGSZ)) {
			for (i = 0; i < IMGSZ && img[i] == save1[i]; i++)
				;
			printf("  first difference at image +0x%x\n", i);
			return fail("memory image", c, op);
		}
		if (nud[0] != nud[1])
			return fail("udelay count", c, op);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("OS call log", c, op);
		if (EV[0].n || memcmp(img, save0, IMGSZ) || memcmp(&g0, &go, sizeof(g0)))
			eff[op]++;
	}
	printf("t32_ivdc_oracle: %u cases, 0 mismatches; cases with an effect per op:", n);
	for (i = 0; i < NOPS; i++)
		printf(" %u", eff[i]);
	printf("\n");
	return 0;
}
