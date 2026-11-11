/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Shared part of the /proc show oracles (t32_info1..4, t32_pshow): one call log per side
 * (0 = vendor reference, 1 = ours) with every seq_printf / isp_printf (the format text and
 * the line it prints: %s arguments are read through, %ld is a 32-bit word as on the T32),
 * every getter call and its scalar arguments.  The getters fill their output buffers with
 * the same pseudo-random words on both sides (seeded by case, getter and call number), so a
 * read of a wrong field, a wrong buffer offset or a missing call shows in the printed text.
 * A device image (seq_file, proc data, ISP device, tuning, sensor attributes, names) lives
 * below 4 GiB; both sides start from the same snapshot and must leave the same bytes.
 * Included after the driver header by each oracle.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define R(f)	((t32o_fn)(void (*)(void))(f))
#include "t32_info_str.inc"

/* ---- log ---- */
#define MAXE 4096
struct ev { u32 fn, a[4]; char t[400]; };
static struct ev EV[2][MAXE];
static unsigned int nev[2];
static int side;

static void put(u32 fn, u32 a0, u32 a1, u32 a2, u32 a3, const char *t)
{
	struct ev *e;

	if (nev[side] >= MAXE) { nev[side]++; return; }
	e = &EV[side][nev[side]++];
	memset(e, 0, sizeof(*e));
	e->fn = fn; e->a[0] = a0; e->a[1] = a1; e->a[2] = a2; e->a[3] = a3;
	snprintf(e->t, sizeof(e->t), "%s", t);
}

/* ---- printf: argument collection (vendor: o32 words; ours: typed va_list) and one renderer ---- */
struct farg { u32 w; const char *s; };
#define MAXA 24
/* next conversion after *pp: returns the conversion char (0 at the end), *pl = number of 'l' */
static char conv(const char **pp, int *pl)
{
	const char *p = *pp;

	for (;;) {
		while (*p && *p != '%')
			p++;
		if (!*p) { *pp = p; return 0; }
		if (p[1] == '%') { p += 2; continue; }
		p++;
		*pl = 0;
		while (*p && strchr("-+ #0123456789.lhz", *p))
			*pl += *p++ == 'l';
		*pp = p + (*p != 0);
		return *p;
	}
}
static int collect_words(const char *fmt, va_list ap, struct farg *a)
{
	int n = 0, l;
	char c;

	while ((c = conv(&fmt, &l)) && n < MAXA) {
		a[n].w = va_arg(ap, u32);
		a[n].s = c == 's' ? (const char *)T32O_HOST(a[n].w) : NULL;
		n++;
	}
	return n;
}
static int collect_ours(const char *fmt, va_list ap, struct farg *a)
{
	int n = 0, l;
	char c;

	while ((c = conv(&fmt, &l)) && n < MAXA) {
		if (c == 's') {
			a[n].s = va_arg(ap, const char *);
			a[n].w = 0;
		} else {
			a[n].w = l ? (u32)va_arg(ap, long) : va_arg(ap, u32);
			a[n].s = NULL;
		}
		n++;
	}
	return n;
}
static void render(char *out, size_t sz, const char *fmt, const struct farg *a, int na)
{
	size_t o;
	int k = 0;
	const char *p = fmt;

	o = (size_t)snprintf(out, sz, "%s|", fmt);
	while (*p && o + 1 < sz) {
		char spec[16], tmp[260];
		int j = 0;

		if (*p != '%') { out[o++] = *p++; continue; }
		if (p[1] == '%') { out[o++] = '%'; p += 2; continue; }
		spec[j++] = *p++;
		while (*p && strchr("-+ #0123456789.lhz", *p)) {
			if (*p != 'l' && *p != 'h' && *p != 'z' && j < 12)
				spec[j++] = *p;
			p++;
		}
		if (!*p)
			break;
		spec[j++] = *p;
		spec[j] = 0;
		if (k >= na)
			snprintf(tmp, sizeof(tmp), "<?>");
		else if (*p == 's') {
			if (a[k].s) {
				snprintf(tmp, sizeof(tmp), "%.255s", a[k].s);
			} else {
				snprintf(tmp, sizeof(tmp), "(null)");
			}
		} else if (*p == 'd' || *p == 'i')
			snprintf(tmp, sizeof(tmp), spec, (int)a[k].w);
		else
			snprintf(tmp, sizeof(tmp), spec, (unsigned int)a[k].w);
		k++;
		p++;
		o += (size_t)snprintf(out + o, sz - o, "%s", tmp);
		if (o >= sz)
			o = sz - 1;
	}
	out[o < sz ? o : sz - 1] = 0;
}
static void put_fmt(u32 fn, u32 a0, const char *fmt, const struct farg *a, int na)
{
	char t[400];

	render(t, sizeof(t), fmt ? fmt : "(null)", a, na);
	put(fn, a0, 0, 0, 0, t);
}

/* vendor kernel calls (o32: a0, a1 = format, the words after it from a2 on) */
u64 private_seq_printf(u32 a0, u32 a1, ...)
{
	struct farg a[MAXA];
	va_list ap;
	int n;

	va_start(ap, a1);
	n = collect_words(T32O_HOST(a1), ap, a);
	va_end(ap);
	put_fmt(1, a0, T32O_HOST(a1), a, n);
	return 0x5e9;
}
u64 seq_printf(u32 a0, u32 a1, ...)
{
	struct farg a[MAXA];
	va_list ap;
	int n;

	va_start(ap, a1);
	n = collect_words(T32O_HOST(a1), ap, a);
	va_end(ap);
	put_fmt(1, a0, T32O_HOST(a1), a, n);
	return 0x5e9;
}
u64 isp_printf(u32 a0, u32 a1, ...)
{
	struct farg a[MAXA];
	va_list ap;
	int n;

	va_start(ap, a1);
	n = collect_words(T32O_HOST(a1), ap, a);
	va_end(ap);
	put_fmt(2, a0, T32O_HOST(a1), a, n);
	return 0;
}
/* ours */
static int o_seq(void *m, const char *fmt, ...)
{
	struct farg a[MAXA];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = collect_ours(fmt, ap, a);
	va_end(ap);
	put_fmt(1, T32O_PTR(m), fmt, a, n);
	return 0x5e9;
}
static __attribute__((unused)) int o_printf(int level, const char *fmt, ...)
{
	struct farg a[MAXA];
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = collect_ours(fmt, ap, a);
	va_end(ap);
	put_fmt(2, (u32)level, fmt, a, n);
	return 0;
}

/* ---- random ---- */
static u32 rs = 0x2545f491u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
/* a word that hits the vendor's compares often: 0..7, small, 16-bit, full, one byte, 0x80000000 */
static u32 wgen(u32 (*r)(void))
{
	u32 k = r() & 15;

	switch (k) {
	case 0: case 1: case 2: return 0;
	case 3: case 4: case 5: return r() & 7;
	case 6: case 7: return r() & 0xff;
	case 8: return r() & 0xff00;
	case 9: return r() & 0xffff;
	case 10: return 0x80000000u;
	case 11: return 3;
	default: return r();
	}
}
static void wfill(u8 *p, u32 n, u32 (*r)(void))
{
	u32 i, w;

	for (i = 0; i < n; i += 4) {
		w = wgen(r);
		memcpy(p + i, &w, n - i < 4 ? n - i : 4);
	}
}

/* getters: the same output on both sides for the n-th call of a getter in one case */
static u32 case_seed, gcnt[2][64], gs;
static u32 grnd(void)
{
	gs ^= gs << 13; gs ^= gs >> 17; gs ^= gs << 5;
	return gs;
}
static void gseed(u32 fn)
{
	gs = case_seed * 0x9e3779b1u ^ (fn + 1) * 0x85ebca6bu ^ (gcnt[side][fn]++ + 1) * 0xc2b2ae35u;
	if (!gs)
		gs = 1;
	grnd(); grnd();
}
/* record the call, fill n bytes, return a random value (often 0) */
static u32 gcall(u32 fn, u32 a0, u32 a1, void *p, u32 n)
{
	put(100 + fn, a0, a1, 0, 0, "");
	gseed(fn);
	if (p)
		wfill(p, n, grnd);
	return (grnd() & 3) ? 0 : wgen(grnd);
}

static void reset_side(int s)
{
	side = s;
	nev[s] = 0;
	memset(gcnt[s], 0, sizeof(gcnt[s]));
}

static int cmp_logs(const char *name, unsigned int c)
{
	unsigned int i;
	const struct ev *x, *y;

	if (nev[0] != nev[1] || nev[0] > MAXE) {
		printf("%s: case %u: %u vs %u logged calls\n", name, c, nev[0], nev[1]);
		for (i = 0; i < nev[0] && i < nev[1] && i < MAXE; i++)
			if (EV[0][i].fn != EV[1][i].fn || memcmp(EV[0][i].a, EV[1][i].a, sizeof(EV[0][i].a)) ||
			    strcmp(EV[0][i].t, EV[1][i].t)) {
				printf("  first diff at %u: vendor %u %08x \"%s\" / ours %u %08x \"%s\"\n", i, EV[0][i].fn,
				       EV[0][i].a[0], EV[0][i].t, EV[1][i].fn, EV[1][i].a[0], EV[1][i].t);
				break;
			}
		return 1;
	}
	for (i = 0; i < nev[0]; i++) {
		x = &EV[0][i]; y = &EV[1][i];
		if (x->fn != y->fn || memcmp(x->a, y->a, sizeof(x->a)) || strcmp(x->t, y->t)) {
			printf("%s: case %u call %u: vendor %u %08x %08x \"%s\" / ours %u %08x %08x \"%s\"\n", name, c, i,
			       x->fn, x->a[0], x->a[1], x->t, y->fn, y->a[0], y->a[1], y->t);
			return 1;
		}
	}
	return 0;
}

/* ---- getters (vendor o32 stubs and ours) ---- */
enum { F_GIB = 1, F_AWBST, F_AWBATTR, F_AEEXPR, F_AESC, F_BV, F_AT, F_EV, F_EXPL, F_STABLE, F_BIN, F_DN, F_TUN,
       F_FLIP, F_AF, F_STEP, F_OSD, F_MEMOPT, F_CL, F_DBG, F_WAIT, F_VICDBG, F_LOCK, F_UNLOCK };

/* device image below 4 GiB */
#define IMG_SZ		0x2000u
#define M_OFF		0x0000u		/* seq_file (+72 private) */
#define PRIV_OFF	0x0100u		/* proc data (+280 ISP device) */
#define ISP_OFF		0x0400u		/* ISP device, 0x800 bytes */
#define TUN_OFF		0x0c00u		/* tisp_get_tuning */
#define ATTR_OFF	0x0d00u		/* 8 sensor attributes of 0x140 bytes (+0 name) */
#define SPRIV_OFF	0x1700u		/* 8 sensor private objects of 0x80 bytes (+104 DFBOOT) */
#define NAME_OFF	0x1b00u		/* 8 names of 32 bytes */
static u8 *img, snap[IMG_SZ], after0[IMG_SZ];
#define IMGW(off)	(T32O_PTR(img) + (off))

static void w32(u32 off, u32 v) { memcpy(img + off, &v, 4); }
static u32 r32(u32 off) { u32 v; memcpy(&v, img + off, 4); return v; }

static void build_image(void)
{
	u32 i, j, o;

	if (!img)
		img = t32o_alloc(IMG_SZ);
	wfill(img, IMG_SZ, rnd);
	w32(M_OFF + 72, IMGW(PRIV_OFF));
	w32(PRIV_OFF + 280, IMGW(ISP_OFF));
	for (i = 0; i < 8; i++) {
		o = NAME_OFF + 32 * i;
		for (j = 0; j < 31; j++)
			img[o + j] = (u8)('A' + rnd() % 26);
		img[o + (rnd() % 31)] = 0;
		img[o + 31] = 0;
		w32(ATTR_OFF + 0x140 * i, IMGW(o));
		if ((rnd() & 31) == 0)
			w32(ATTR_OFF + 0x140 * i, 0);	/* no name: "(null)" */
		if (376 + 104 * i < 0x800)
			w32(ISP_OFF + 376 + 104 * i, IMGW(ATTR_OFF + 0x140 * i));
		if (324 + 104 * i + 56 < 0x800)
			w32(ISP_OFF + 324 + 104 * i + 56, IMGW(SPRIV_OFF + 0x80 * i));
	}
	for (i = 0; i < 8; i++)	/* channel state words: around the >= 4 bound */
		w32(ISP_OFF + 300 + 4 * i, (rnd() & 15) == 0 ? 0x80000000u + (rnd() & 7) : rnd() & 7);
}

#define STUB(name, fn, n) u64 name(u32 a0, u32 a1, u32 a2, u32 a3) \
	{ (void)a2; (void)a3; return gcall(fn, a0, 0, T32O_HOST(a1), n); }
STUB(tisp_gib_api_get_blc, F_GIB, 24)
STUB(tisp_g_awb_global_statis_attr, F_AWBST, 20)
STUB(tisp_g_awb_attr, F_AWBATTR, 76)
STUB(tisp_g_ae_exprinfo_attr, F_AEEXPR, 220)
STUB(tisp_g_ae_scence_attr, F_AESC, 40)
STUB(tisp_g_bv, F_BV, 4)
STUB(tisp_g_at_list, F_AT, 128)
STUB(tisp_g_ev_list, F_EV, 128)
STUB(tisp_g_ae_explist, F_EXPL, 324)
STUB(tisp_get_ae_stable, F_STABLE, 12)
STUB(lib_tisp_get_bin_path, F_BIN, 904)
STUB(tisp_g_hv_flip, F_FLIP, 960)
STUB(tisp_g_antiflick, F_AF, 8)
u64 tisp_day_or_night_g_ctrl(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	return gcall(F_DN, a0, 0, NULL, 0) & 3;
}
u64 tisp_get_tuning(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	gcall(F_TUN, 0, 0, NULL, 0);
	return IMGW(TUN_OFF);
}
u64 tisp_get_msca_osd_info(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return gcall(F_OSD, a0, 0, NULL, 0); }
u64 get_isp_memopt(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return gcall(F_MEMOPT, 0, 0, NULL, 0); }
u64 get_isp_cache_line(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return gcall(F_CL, 0, 0, NULL, 0); }
/* step list: a random number of words, a count (mostly below the 120 words of the buffer) */
static u32 do_step(u32 ch, u8 *buf, u8 *cnt)
{
	u32 k, i, r;
	u16 n;

	put(100 + F_STEP, ch, 0, 0, 0, "");
	gseed(F_STEP);
	k = grnd() % 121;
	for (i = 0; i < k; i++) {
		u32 w = wgen(grnd);

		memcpy(buf + 4 * i, &w, 4);
	}
	n = (u16)((grnd() & 3) ? grnd() % 8 : grnd() % 120);
	memcpy(cnt, &n, 2);
	r = grnd() & 3;
	return r == 3 ? 0xffffffffu : r == 2 ? 1 : 0;
}
u64 tisp_get_antiflicker_step(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return do_step(a0, T32O_HOST(a1), T32O_HOST(a2)); }
/* isp_debug_info: may change the item word (the vendor reads it again) */
static u32 do_dbg(u32 item, u32 m, u32 ch)
{
	put(100 + F_DBG, item, m, ch, 0, "");
	gseed(F_DBG);
	if ((grnd() & 3) == 0)
		w32(ISP_OFF + 748, r32(ISP_OFF + 748) ^ (1u << (grnd() & 31)));
	return (grnd() & 3) == 0 ? wgen(grnd) : 0;
}
u64 isp_debug_info(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return do_dbg(a0, a1, a2); }
u64 private_wait_for_completion_timeout(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	return gcall(F_WAIT, a0, a1, NULL, 0) ? 0 : 1;
}

#define OSTUB(oname, fn, n, T) static int oname(T ch, void *p) { return (int)gcall(fn, ch, 0, p, n); }
OSTUB(o_gib, F_GIB, 24, u32)
OSTUB(o_awbst, F_AWBST, 20, u32)
OSTUB(o_awbattr, F_AWBATTR, 76, u32)
OSTUB(o_aeexpr, F_AEEXPR, 220, u32)
OSTUB(o_aesc, F_AESC, 40, u32)
OSTUB(o_bv, F_BV, 4, u8)
OSTUB(o_at, F_AT, 128, u32)
OSTUB(o_ev, F_EV, 128, u32)
OSTUB(o_expl, F_EXPL, 324, u32)
OSTUB(o_stable, F_STABLE, 12, u8)
OSTUB(o_bin, F_BIN, 904, u8)
OSTUB(o_flip, F_FLIP, 960, u32)
OSTUB(o_af, F_AF, 8, u32)
static int o_dn(u8 ch) { return (int)(gcall(F_DN, ch, 0, NULL, 0) & 3); }
static u8 *o_tun(void) { gcall(F_TUN, 0, 0, NULL, 0); return img + TUN_OFF; }
static int o_step(u32 ch, void *buf, u16 *n) { return (int)do_step(ch, buf, (u8 *)n); }
static int o_osd(u8 ch) { return (int)gcall(F_OSD, ch, 0, NULL, 0); }
static u32 o_memopt(void) { return gcall(F_MEMOPT, 0, 0, NULL, 0); }
static int o_cl(void) { return (int)gcall(F_CL, 0, 0, NULL, 0); }
static int o_dbg(u32 item, void *m, u32 ch) { return (int)do_dbg(item, T32O_PTR(m), ch); }
static long o_wait(void *c, u32 ms) { return gcall(F_WAIT, T32O_PTR(c), ms, NULL, 0) ? 0 : 1; }
