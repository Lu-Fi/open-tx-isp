// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the debug-print helpers of driver/t32/tx_isp_t32_dbg.h against the
 * recovered vendor C (Matt Davis, PR #15 @ 2e2e06bc): dump_vic_reg, dump_csi_reg,
 * check_vic_error / check_csi_error (the endless loops, cut after N passes by
 * t32dbg_pass_stop in the build-time copy of the reference, fix_dbg.sh),
 * ISPWARNING, print_array_awb_8/16/32, the five /proc open wrappers and the empty
 * debug stubs.  A random register image runs through both; compared are the ordered
 * logs of isp_printf (level, format text, the arguments the format consumes) and
 * printk (formatted text), PDE_DATA / private_single_open_size calls, return values,
 * and that the images are untouched.  The format texts of the reference come from the
 * vendor .rodata.str (t32_dbg_str.inc, made by data_str.py).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"

struct va_format { const char *fmt; va_list *va; };
int t32dbg_printk(const char *fmt, ...);
#define T32_DBG_PRINTK t32dbg_printk
#include "tx_isp_t32_dbg.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
#include "t32_dbg_str.inc"
u32 t32_data_d9348, t32_data_d9680;

/* ---- recorders ---- */
#define MAXE 4096
struct ev { u32 kind, level; char text[100]; u32 a[2]; u32 na; };
static struct ev EV[2][MAXE];
static unsigned int nev[2];
static int side;
static int pass_left;

static void put(u32 kind, u32 level, const char *t, u32 a0, u32 a1, u32 na)
{
	struct ev *e;

	if (nev[side] >= MAXE) { nev[side]++; return; }
	e = &EV[side][nev[side]++];
	e->kind = kind; e->level = level; e->na = na;
	snprintf(e->text, sizeof(e->text), "%s", t);
	e->a[0] = a0; e->a[1] = a1;
}

static unsigned int nconv(const char *f)
{
	unsigned int n = 0;

	for (; *f; f++)
		if (*f == '%') { if (f[1] == '%') f++; else n++; }
	return n;
}

int isp_printf(unsigned int level, const char *fmt, ...)
{
	va_list ap;
	u32 a[2], n = nconv(fmt);

	va_start(ap, fmt);
	a[0] = va_arg(ap, u32);
	a[1] = va_arg(ap, u32);
	va_end(ap);
	put(1, level, fmt, a[0], a[1], n > 2 ? 2 : n);
	return 0;
}

int t32dbg_printk(const char *fmt, ...)
{
	va_list ap;
	char b[128];

	va_start(ap, fmt);
	vsnprintf(b, sizeof(b), fmt, ap);	/* "%d," etc. with one int; texts without % ignore it */
	va_end(ap);
	put(2, 0, b, 0, 0, 0);
	return 0;
}
void t32dbg_dump_stack(void) { put(3, 0, "", 0, 0, 0); }
int t32dbg_pass_stop(void) { return pass_left-- <= 0; }

static u32 pde_ret, so_a[4];
u64 PDE_DATA(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	put(4, a0, "pde", 0, 0, 0);
	return pde_ret;
}
u64 private_single_open_size(u32 a0, u32 a1, u32 a2, u32 a3)
{
	put(5, a0, "open", 0, 0, 0);
	so_a[0] = a0; so_a[1] = a1; so_a[2] = a2; so_a[3] = a3;
	return a0 ^ a1 ^ a2 ^ a3 ^ 0x5a5a;
}
/* the show routines are only addresses here (not ported in this block) */
char isp_vic_frd_show[8], isp_csi_show[8], ldc_cmd_show[8], isp_framesource_show[8], isp_info_show[8];

u64 dump_vic_reg(u32, u32, u32, u32), check_vic_error(u32, u32, u32, u32);
u64 dump_csi_reg(u32, u32, u32, u32), check_csi_error(u32, u32, u32, u32);
u64 ISPWARNING(u32, u32, u32, u32), print_array_awb_32(u32, u32, u32, u32);
u64 print_array_awb_16(u32, u32, u32, u32), print_array_awb_8(u32, u32, u32, u32);
u64 dump_isp_vic_frd_open(u32, u32, u32, u32), dump_isp_csi_open(u32, u32, u32, u32);
u64 dump_ldc_frd_open(u32, u32, u32, u32), dump_isp_framesource_open(u32, u32, u32, u32);
u64 dump_isp_info_open(u32, u32, u32, u32);
int tisp_gib_print_para(void), tisp_lsc_print_para(void), tisp_gamma_print_para(void), tisp_ccm_print_para(void);
int tisp_bcsh_print_para(void), tisp_csc_print_para(void), tisp_csccr_print_para(void);
void tisp_clm_print_struct(void);
u64 tmo_params_print(u32, u32, u32, u32), tisp_ldc_debug(u32, u32, u32, u32), dump_msca_regs(u32, u32, u32, u32);

static u32 rs = 0xd1b9c0a3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static void fill(void *p, u32 n)
{
	u32 i;

	for (i = 0; i < n; i++)
		((u8 *)p)[i] = rnd() >> 11;
}

static int cmp_logs(const char *what, unsigned int c)
{
	unsigned int i;
	const struct ev *x, *y;

	if (nev[0] != nev[1] || nev[0] > MAXE) {
		printf("t32_dbg_oracle: case %u %s: %u vs %u logged calls\n", c, what, nev[0], nev[1]);
		return 1;
	}
	for (i = 0; i < nev[0]; i++) {
		x = &EV[0][i]; y = &EV[1][i];
		if (x->kind != y->kind || x->level != y->level || strcmp(x->text, y->text) || x->na != y->na ||
		    (x->na > 0 && x->a[0] != y->a[0]) || (x->na > 1 && x->a[1] != y->a[1])) {
			printf("t32_dbg_oracle: case %u %s call %u: vendor kind %u level %u \"%s\" %08x %08x / ours kind %u level %u \"%s\" %08x %08x\n",
			       c, what, i, x->kind, x->level, x->text, x->a[0], x->a[1],
			       y->kind, y->level, y->text, y->a[0], y->a[1]);
			return 1;
		}
	}
	return 0;
}
static void begin(void) { nev[0] = nev[1] = 0; side = 0; }

static void *os_pde(void *inode) { return (void *)(uintptr_t)PDE_DATA(T32O_PTR(inode), 0, 0, 0); }
static int os_open(void *file, void *show, void *data, size_t size)
{
	return (int)private_single_open_size(T32O_PTR(file), T32O_PTR(show), T32O_PTR(data), (u32)size);
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, calls = 0, k;
	static const struct t32_dbg_os os = { os_pde, os_open };

	for (c = 0; c < n; c++) {
		u8 *dev = t32o_alloc(512), *regs = t32o_alloc(512), *img;
		u8 dev0[512], regs0[512], *arr = t32o_alloc(1024), arr0[1024];
		u32 regsw = T32O_PTR(regs), r, rows, cols, sh, cnt, pass;

		t32o_reset();
		dev = t32o_alloc(512); regs = t32o_alloc(512); arr = t32o_alloc(1024);
		regsw = T32O_PTR(regs);
		fill(dev, 512); fill(regs, 512); fill(arr, 1024);
		memcpy(dev + T32_DBG_REGS, &regsw, 4);
		if (c % 5 == 0) { memset(regs + T32_CSI_ERR1, 0, 4); }
		if (c % 7 == 0) { memset(regs + T32_CSI_ERR2, 0, 4); }
		if (c % 11 == 0) { memset(regs + T32_CSI_ERR1, 0, 8); }
		memcpy(dev0, dev, 512); memcpy(regs0, regs, 512); memcpy(arr0, arr, 1024);
		(void)img;

		/* dump_vic_reg */
		begin();
		t32o_call(R(dump_vic_reg), T32O_PTR(dev), rnd(), rnd(), rnd(), 0);
		side = 1; t32_dump_vic_reg(dev);
		if (nev[0] != 109 || cmp_logs("dump_vic_reg", c)) return 1;
		calls++;

		/* check_vic_error: N passes of the endless loop */
		pass = 1 + rnd() % 3;
		t32_data_d9348 = T32O_PTR(dev);
		begin(); pass_left = (int)pass;
		t32o_call(R(check_vic_error), rnd(), rnd(), rnd(), rnd(), 0);
		side = 1; for (k = 0; k < pass; k++) t32_check_vic_error_pass(&t32_data_d9348);
		if (nev[0] != 109 * pass || cmp_logs("check_vic_error", c)) return 1;
		calls++;

		/* dump_csi_reg */
		begin();
		t32o_call(R(dump_csi_reg), T32O_PTR(dev), rnd(), rnd(), rnd(), 0);
		side = 1; t32_dump_csi_reg(dev);
		if (nev[0] != 15 || cmp_logs("dump_csi_reg", c)) return 1;
		calls++;

		/* check_csi_error */
		pass = 1 + rnd() % 3;
		t32_data_d9680 = T32O_PTR(dev);
		begin(); pass_left = (int)pass;
		t32o_call(R(check_csi_error), rnd(), rnd(), rnd(), rnd(), 0);
		side = 1; for (k = 0; k < pass; k++) t32_check_csi_error_pass(&t32_data_d9680);
		if (cmp_logs("check_csi_error", c)) return 1;
		calls++;

		/* ISPWARNING (rows x cols words >> shift) */
		rows = rnd() % 6; cols = rnd() % 8; sh = rnd() % 70;
		begin();
		t32o_call(R(ISPWARNING), T32O_PTR(arr), rows, cols, sh, 0);
		side = 1; t32_array_print((const u32 *)arr, (int)rows, cols, sh);
		if (nev[0] != rows * (cols + 1) + 1 || cmp_logs("ISPWARNING", c)) return 1;
		calls++;

		/* print_array_awb_32 / 16 / 8 */
		cnt = (rnd() % 40) - 3;
		begin(); t32o_call(R(print_array_awb_32), T32O_PTR(arr), cnt, 0, 0, 0);
		side = 1; t32_print_array_awb_32((const u32 *)arr, (int)cnt);
		if (cmp_logs("print_array_awb_32", c)) return 1;
		begin(); t32o_call(R(print_array_awb_16), T32O_PTR(arr), cnt, 0, 0, 0);
		side = 1; t32_print_array_awb_16((const u16 *)arr, (int)cnt);
		if (cmp_logs("print_array_awb_16", c)) return 1;
		begin(); t32o_call(R(print_array_awb_8), T32O_PTR(arr), cnt, 0, 0, 0);
		side = 1; t32_print_array_awb_8(arr, (int)cnt);
		if (cmp_logs("print_array_awb_8", c)) return 1;
		calls += 3;

		/* the five open wrappers */
		{
			static const struct { u64 (*f)(u32, u32, u32, u32); void *show; size_t size; } o[5] = {
				{ dump_isp_vic_frd_open, isp_vic_frd_show, T32_DUMP_VIC_FRD_SIZE },
				{ dump_isp_csi_open, isp_csi_show, T32_DUMP_CSI_SIZE },
				{ dump_ldc_frd_open, ldc_cmd_show, T32_DUMP_LDC_SIZE },
				{ dump_isp_framesource_open, isp_framesource_show, T32_DUMP_FRAMESOURCE_SIZE },
				{ dump_isp_info_open, isp_info_show, T32_DUMP_INFO_SIZE },
			};
			for (k = 0; k < 5; k++) {
				u32 inode = rnd(), file = rnd(), rv;
				u32 ov[4];
				u64 rr;

				pde_ret = rnd();
				begin();
				rr = t32o_call(R(o[k].f), inode, file, rnd(), rnd(), 0);
				memcpy(ov, so_a, sizeof(ov));
				side = 1;
				r = (u32)t32_dump_open(&os, (void *)(uintptr_t)inode, (void *)(uintptr_t)file, o[k].show, o[k].size);
				rv = (u32)rr;
				if (cmp_logs("open wrapper", c) || r != rv || memcmp(ov, so_a, sizeof(ov))) {
					printf("t32_dbg_oracle: case %u open wrapper %u: ret %08x/%08x args differ %d\n", c, k, rv, r, memcmp(ov, so_a, sizeof(ov)) != 0);
					return 1;
				}
				calls++;
			}
		}

		/* empty stubs: nothing logged, result 0 */
		begin();
		if (tisp_gib_print_para() || tisp_lsc_print_para() || tisp_gamma_print_para() || tisp_ccm_print_para() ||
		    tisp_bcsh_print_para() || tisp_csc_print_para() || tisp_csccr_print_para() ||
		    t32o_call(R(tmo_params_print), rnd(), rnd(), rnd(), rnd(), 0) ||
		    t32o_call(R(tisp_ldc_debug), rnd(), rnd(), rnd(), rnd(), 0) ||
		    t32o_call(R(dump_msca_regs), rnd(), rnd(), rnd(), rnd(), 0) || nev[0]) {
			printf("t32_dbg_oracle: case %u: a vendor debug stub did something\n", c);
			return 1;
		}
		tisp_clm_print_struct();
		if (t32_tisp_print_para_stub() != 0 || nev[0]) { printf("t32_dbg_oracle: stub\n"); return 1; }
		t32_tisp_clm_print_struct();
		calls += 11;

		if (memcmp(dev0, dev, 512) || memcmp(regs0, regs, 512) || memcmp(arr0, arr, 1024)) {
			printf("t32_dbg_oracle: case %u: an image changed\n", c);
			return 1;
		}
	}
	printf("t32_dbg_oracle: %u calls compared (%u random register images), 0 mismatches\n", calls, n);
	return 0;
}
