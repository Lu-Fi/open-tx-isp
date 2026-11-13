// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_load_parameters of driver/t32/tx_isp_t32_loadpar.h against the unmodified
 * vendor tisp_load_parameters (0x278b8; Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Callees are recorders on both sides: lib_tisp_is_file_exist / tisp_read_bin_file (path text, ch,
 * index; results from per case bit lists), tisp_lut_params_copy (ch, sel), private_vmalloc (sizes,
 * a NULL at a chosen call, patterned contents), isp_printf (level, text, function, line) and the
 * printk count (env.c).  snprintf of the reference is the host one.
 * Per case 1..3 calls on the same request and globals (LUT info allocated once, counts grow).
 * Compared after every call: return value, the whole request (1200 B), the common block da850 and
 * the channel state dc284 (both channels), the tuning images (0xf238 B each), dc274 and the 44 B LUT
 * info (table pointers mapped to allocation numbers), the call logs.  Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_loadpar.h"

u64 tisp_load_parameters(u32, u32, u32, u32);

#define WEAK __attribute__((weak))
#define NCH 2
#define INFON 1200
#define NBIN 16
#define BINN 0xf238u	/* vendor memcpy length (not the header macro: mutations of it must show) */

/* reference data objects: rodata text (data.c, c81a4 / c8260.. / c82e8 / bf1f8), the bin table dd0b4 */
char t32_data_c8260[] = "/etc/sensor/%s-PRJ007-day.bin";
char t32_data_c8280[] = "/etc/sensor/%s-PRJ007-night.bin";
char t32_data_c82a0[] = "/etc/sensor/%s-PRJ007-day-wdr.bin";
char t32_data_c82c4[] = "/etc/sensor/%s-PRJ007-night-wdr.bin";
char t32_data_c82e8[] = "[ISP][%s, %d]: The BIN file used did not load successfully \n";
extern char t32_data_c81a4[];	/* env.c */
static const char vm_err[] = "[%s %d]: vmalloc error !!!\n";	/* data.c c81a4 */
char t32_data_bf1f8[] = "tisp_load_parameters";
u32 t32_data_dd0b4[NBIN];

/* call log */
#define NLOG 96
struct ent { u32 k, a, b, c; char s[132]; };
struct lg { unsigned int n; struct ent e[NLOG]; };
static struct lg lv, lo;
static void add(struct lg *l, u32 k, u32 a, u32 b, u32 c, const char *s)
{
	struct ent *e;

	if (l->n >= NLOG)
		return;
	e = &l->e[l->n++];
	memset(e, 0, sizeof(*e));
	e->k = k; e->a = a; e->b = b; e->c = c;
	if (s)
		snprintf(e->s, sizeof(e->s), "%s", s);
}

/* per case behaviour: file exists / read result per call number, vmalloc NULL at call number */
static u32 exist_bits, read_bits, null_at;
struct side { unsigned int nex, nrd, nal; void *al[NLOG]; };
static struct side sv, so;

static void *alloc_pat(struct side *s, u32 size)
{
	unsigned int no = s->nal++;
	u8 *p;
	u32 i;

	if (no == null_at && size != 44)	/* the LUT info itself: the vendor would use NULL (not compared) */
		return NULL;
	p = t32o_alloc(size ? size : 4);
	for (i = 0; i < size; i++)
		p[i] = (u8)(no * 37 + i * 11 + 5);
	if (no < NLOG)
		s->al[no] = p;
	return p;
}

static u32 map_ptr(const struct side *s, u32 v)
{
	unsigned int i;

	if (!v)
		return 0;
	for (i = 0; i < s->nal && i < NLOG; i++)
		if (s->al[i] && T32O_PTR(s->al[i]) == v)
			return 0x1000 + i;
	return 0xdead0000u;
}

u64 lib_tisp_is_file_exist(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	add(&lv, 1, 0, 0, 0, T32O_HOST(a0));
	return exist_bits >> (sv.nex++ & 31) & 1;
}

u64 tisp_read_bin_file(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	add(&lv, 2, a0, a2, 0, T32O_HOST(a1));
	return (read_bits >> (sv.nrd++ & 31) & 1) ? (u32)-1 : 0;
}

u64 tisp_lut_params_copy(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	add(&lv, 3, a0, a1, 0, NULL);
	return 0;
}

u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	add(&lv, 4, a0, 0, 0, NULL);
	return T32O_PTR(alloc_pat(&sv, a0));
}

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	add(&lv, 5, a0, a3, a1 == T32O_PTR(t32_data_c81a4), NULL);
	add(&lv, 6, 0, 0, 0, T32O_HOST(a2));
	return 0;
}

/* BEYOND VENDOR (#281) bookkeeping of our side: night bin missing in mode 0, a guarded NULL allocation */
static u32 cur_mode, night_miss, guard_hit, null44, otab;
static int o_exist(const char *p)
{
	int r;

	add(&lo, 1, 0, 0, 0, p);
	r = exist_bits >> (so.nex++ & 31) & 1;
	if (!r && !cur_mode && strstr(p, "-PRJ007-night.bin"))
		night_miss = 1;
	return r;
}

static int o_read(u32 ch, const char *p, u32 i)
{
	add(&lo, 2, ch, i, 0, p);
	return (read_bits >> (so.nrd++ & 31) & 1) ? -1 : 0;
}

static void o_lut(u32 ch, u32 sel)
{
	add(&lo, 3, ch, sel, 0, NULL);
}

static void *o_vmalloc(u32 n)
{
	void *p;

	add(&lo, 4, n, 0, 0, NULL);
	if (n == 44 && null44) {	/* LUT info allocation fails (ours only: the vendor would use NULL) */
		so.nal++;
		guard_hit = 1;
		return NULL;
	}
	p = alloc_pat(&so, n);
	if (n != 44 && otab++ >= 2 && !p)
		guard_hit = 1;		/* one of the last five tables */
	return p;
}

static unsigned int o_printk_n;
static void o_printk(const char *fmt, const char *func, u32 line)
{
	o_printk_n++;
	if (strcmp(fmt, t32_data_c82e8) || strcmp(func, t32_data_bf1f8) || line != 1436)
		o_printk_n += 1000;
}

static void o_isp_printf(u32 lvl, const char *fmt, const char *func, u32 line)
{
	add(&lo, 5, lvl, line, !strcmp(fmt, vm_err), NULL);
	add(&lo, 6, 0, 0, 0, func);
}

static const struct t32_loadpar_ops ops = { o_exist, o_read, o_lut, o_vmalloc, o_printk, o_isp_printf };

static u32 rs = 0x10ad9a5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* our copies of the globals */
static u8 com_o[704], chs_o[1808], lsz_o[112];
static u32 tun_o[4], lut_o[4];

static int cmp_lut(u32 vv, u32 ov)
{
	u8 a[44], b[44];
	u32 i, w;

	if (map_ptr(&sv, vv) != map_ptr(&so, ov))
		return 1;
	if (!vv)
		return 0;
	memcpy(a, T32O_HOST(vv), 44);
	memcpy(b, T32O_HOST(ov), 44);
	for (i = 16; i < 44; i += 4) {
		memcpy(&w, a + i, 4); w = map_ptr(&sv, w); memcpy(a + i, &w, 4);
		memcpy(&w, b + i, 4); w = map_ptr(&so, w); memcpy(b + i, &w, 4);
	}
	return memcmp(a, b, 44) != 0;
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 6000, failed = 0, calls = 0;
	unsigned int ok = 0, nm[3] = { 0, 0, 0 }, nguard = 0, nnight = 0;

	for (c = 0; c < n && !failed; c++) {
		u8 *iv = NULL, io[INFON];
		u32 ch = (rnd() & 1) | (rnd() % 4 == 0 ? rnd() & ~0xffu : 0), k, ncall = 1 + rnd() % 3, j;
		const struct t32_loadpar_glb g = { com_o, chs_o, tun_o, t32_data_dd0b4, lut_o, (const u32 *)lsz_o };
		u32 tv[NCH];

		t32o_reset();
		memset(&sv, 0, sizeof(sv)); memset(&so, 0, sizeof(so));
		exist_bits = rnd() & rnd() ? rnd() : rnd() | rnd();
		read_bits = rnd() % 3 ? rnd() & rnd() & rnd() : rnd();
		null_at = rnd() % 4 == 0 ? rnd() % 12 : 999;
		for (k = 0; k < sizeof(com_o); k++)
			t32_data_da850[k] = com_o[k] = (u8)rnd();
		t32_data_da850[224] = com_o[224] = (u8)(rnd() % 4);
		for (k = 0; k < NCH; k++)
			if (rnd() & 1)
				t32_data_da850[k * 336] = com_o[k * 336] = 0;
		for (k = 0; k < sizeof(chs_o); k++)
			t32_data_dc284[k] = chs_o[k] = (u8)rnd();
		for (k = 0; k < 28; k++) {
			u32 sz = rnd() % 5 == 0 ? 0 : rnd() % 600;

			if (sz == 44)
				sz = 45;
			memcpy(t32_data_dc1a4 + 4 * k, &sz, 4);
			memcpy(lsz_o + 4 * k, &sz, 4);
		}
		for (k = 0; k < NBIN; k++) {
			u8 *b = t32o_alloc(BINN);
			u32 i, s = rnd();

			for (i = 0; i < BINN; i += 4)
				memcpy(b + i, &s, 4), s = s * 1103515245u + 12345u;
			t32_data_dd0b4[k] = T32O_PTR(b);
		}
		for (k = 0; k < NCH; k++) {
			u8 *a = t32o_alloc(BINN), *b = t32o_alloc(BINN);

			memset(a, 0x33, BINN); memset(b, 0x33, BINN);
			t32_data_dd0a4[k] = T32O_PTR(a); tun_o[k] = T32O_PTR(b);
			tv[k] = T32O_PTR(a);
			t32_data_dc274[k] = lut_o[k] = 0;
			if (rnd() & 1) {	/* LUT info from an earlier call, not mapped (0xdead0000 both sides) */
				u8 *x = t32o_alloc(44), *y = t32o_alloc(44);
				for (j = 0; j < 44; j++)
					x[j] = y[j] = (u8)rnd();
				t32_data_dc274[k] = T32O_PTR(x); lut_o[k] = T32O_PTR(y);
			}
		}
		iv = t32o_alloc(INFON);
		for (k = 0; k < INFON; k++)
			iv[k] = io[k] = (u8)rnd();
		{
			u32 m = rnd() % 10, nl = rnd() % 28;

			m = m < 5 ? 0 : m < 9 ? 1 : rnd() % 4 ? 2 + rnd() % 3 : rnd();
			memcpy(iv + 48, &m, 4); memcpy(io + 48, &m, 4);
			for (k = 0; k < nl; k++)
				iv[20 + k] = io[20 + k] = (u8)('a' + rnd() % 26);
			if (rnd() % 8)
				iv[20 + nl] = io[20 + nl] = 0;
			iv[124] = io[124] = (u8)(rnd() % 8 ? rnd() % 6 : rnd() % 9);
			iv[125] = io[125] = (u8)(rnd() % 6 ? rnd() % 4 : rnd() % 8);
			for (k = 0; k < 4; k++)
				iv[126 + k] = io[126 + k] = (u8)(rnd() % 3 ? rnd() % 2 : rnd());
			for (k = 0; k < 8; k++)	/* bin paths for mode 1 */
				iv[132 + 128 * k + (rnd() % 3 ? rnd() % 60 : rnd() % 128)] = 0,
				io[132 + 128 * k] = io[132 + 128 * k], memcpy(io, iv, INFON);
			nm[m == 0 ? 0 : m == 1 ? 1 : 2]++;
		}
		for (j = 0; j < ncall; j++) {
			unsigned int pv = t32o_log.printfs;
			int rv, ro;
			u32 bad = 0;

			lv.n = lo.n = 0;
			o_printk_n = 0;
			memcpy(&cur_mode, io + 48, 4);
			night_miss = guard_hit = otab = 0;
			null44 = !lut_o[ch & 0xff] && rnd() % 8 == 0;
			rv = (int)(u32)t32o_call((t32o_fn)(void (*)(void))tisp_load_parameters, ch, T32O_PTR(iv), 0, 0, 0);
			ro = t32_load_parameters(&g, &ops, ch, io);
			calls++;
			if (guard_hit) {
				/*
				 * BEYOND VENDOR (#281): ours stops with -1 at the failed allocation; up to it the
				 * call log equals the vendor's (prefix), the vendor goes on with the NULL.  The
				 * case ends here (later calls would start from different LUT tables).
				 */
				if (ro != -1 || lo.n > lv.n || memcmp(lv.e, lo.e, lo.n * sizeof(lo.e[0])) ||
				    lo.e[lo.n - 1].k != 4) {
					printf("MISMATCH case %u call %u: allocation guard (beyond vendor) ret %d/%d logs %u/%u\n",
					       c, j, rv, ro, lv.n, lo.n);
					failed++;
				}
				nguard++;
				break;
			}
			if (night_miss) {
				/* BEYOND VENDOR (#281): the vendor marks the missing night bin as loaded, ours clears it */
				u32 off = (ch & 0xff) * 904 + 127;

				if (t32_data_dc284[off] != 1 || chs_o[off] != 0) {
					printf("MISMATCH case %u call %u: night flag (beyond vendor) %u/%u\n", c, j,
					       t32_data_dc284[off], chs_o[off]);
					failed++;
					break;
				}
				t32_data_dc284[off] = 0;
				nnight++;
			}
			if (rv == 0)
				ok++;
			if (rv != ro) bad |= 1;
			if (memcmp(iv, io, INFON)) bad |= 2;
			if (memcmp(t32_data_da850, com_o, sizeof(com_o))) bad |= 4;
			if (memcmp(t32_data_dc284, chs_o, sizeof(chs_o))) bad |= 8;
			for (k = 0; k < NCH; k++) {
				if (t32_data_dd0a4[k] != tv[k] ||
				    memcmp(T32O_HOST(t32_data_dd0a4[k]), T32O_HOST(tun_o[k]), BINN)) bad |= 16;
				if (cmp_lut(t32_data_dc274[k], lut_o[k])) bad |= 32;
			}
			if (lv.n != lo.n || memcmp(lv.e, lo.e, lv.n * sizeof(lv.e[0]))) bad |= 64;
			if (t32o_log.printfs - pv != o_printk_n) bad |= 128;
			if (bad) {
				printf("MISMATCH case %u call %u bad %#x ret %d/%d mode %u logs %u/%u\n", c, j, bad, rv, ro,
				       io[48], lv.n, lo.n);
				for (k = 0; k < INFON; k++)
					if (iv[k] != io[k]) { printf("  info +%u %02x/%02x\n", k, iv[k], io[k]); break; }
				for (k = 0; k < lv.n || k < lo.n; k++)
					if (k >= lv.n || k >= lo.n || memcmp(&lv.e[k], &lo.e[k], sizeof(lv.e[0]))) {
						printf("  log #%u v %u %x %x '%s' o %u %x %x '%s'\n", k, lv.e[k].k, lv.e[k].a,
						       lv.e[k].b, lv.e[k].s, lo.e[k].k, lo.e[k].a, lo.e[k].b, lo.e[k].s);
						break;
					}
				failed++;
				break;
			}
		}
	}
	printf("t32_loadpar_oracle: %u cases (mode 0/1/other %u/%u/%u), %u calls (%u loaded) compared, %u mismatches;"
	       " beyond vendor (#281): %u allocation guards, %u night flags\n",
	       c, nm[0], nm[1], nm[2], calls, ok, failed, nguard, nnight);
	if (!nguard || !nnight)
		return 1;
	return failed ? 1 : 0;
}
