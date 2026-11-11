// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the per-frame AWB chain of the driver (t32_awb_init, then
 * per frame t32_awb_frame = tisp_awb_interrupt_static + tisp_awb_process,
 * driver/t32/tx_isp_t32_awbinit.h) against the whole vendor chain (Matt
 * Davis' recovered C, PR #15 @ 2e2e06bc: tisp_awb_init with params_refresh /
 * show_para_update / set_gain, tisp_awb_interrupt_static with
 * get_statistics, tisp_awb_process with long_par_update, spec_calculate,
 * long_alogrithm (ct detection, gray world, ct cal) and set_gain, every
 * fixed point helper a reference).
 *
 * Per case: an IQ bin AWB section (as is, or with the zone grid and a few
 * switches varied), the sensor size; both inits (stubs / hooks as in
 * t32_awbinit_oracle); then F frames: four synthetic statistics buffers
 * (0x7000 B; per zone the ten fields of both groups packed as
 * t32_awb_get_statistics reads them, from a scene: grey with a colour cast
 * drifting between frames, noise, dark / bright zones), the buffer index of
 * 0x1608c and the spec registers 0x16190.. from a register model.  Ours runs
 * first; a frame ours refuses (the long algorithm would read past one of its
 * tables) ends the case and is counted.  Compared per frame: every register
 * write (address / value / order), the events (channel, 8 words: the event
 * 11 colour temperature), the completions, the frame counter, every byte of
 * the par section, the run state and the hardware shadow.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_awbinit.h"

#define SECB	6740u
#define BUFB	T32_AWB_STAT_BUF_BYTES
extern u32 t32_data_e16a4[4], t32_data_e16b4[4], t32_data_e16c4[16], t32_data_e1694[4];
extern u8 t32_data_f0900[0x10000];
u64 tisp_awb_init(u32 a0, u32 a1, u32 a2, u32 a3), tisp_awb_interrupt_static(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_process(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x2c9e51u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* vendor-side stubs of the init (see t32_awbinit_oracle) */
u64 tisp_awb_malloc_cfg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 tisp_awb_set_hardware_param(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 tisp_awb_set_riscv_param(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 tisp_event_set_cb(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

static void *o_alloc(void *p, u32 size) { (void)p; return t32o_alloc(size); }
static void o_nop1(void *p, u32 ch) { (void)p; (void)ch; }
static void o_nop2(void *p, u32 ch, u32 v) { (void)p; (void)ch; (void)v; }
static void o_print(void *p, int ok) { (void)p; (void)ok; }
static const struct t32_awb_init_ops IOPS = { 0, o_alloc, o_nop1, o_nop1, o_nop2, o_nop2, o_print };

/* ---- register model: buffer index and the spec_calculate registers, per frame ---- */
static u32 frame, regv[3];
static u32 rd_model(u32 reg)
{
	if (reg == 0x1608c)
		return frame & 3;
	if (reg >= 0x16190 && reg <= 0x16198)
		return regv[(reg - 0x16190) / 4];
	return (reg * 0x9e3779b1u) ^ frame;
}
static u32 o_rd(void *p, u32 reg) { (void)p; return rd_model(reg); }

/* ---- the scene: one zone's ten fields packed as t32_awb_get_statistics unpacks them ---- */
static void put_zone(u32 *p, u32 *hi, int half, const u32 *f)
{
	/* f: g1 n_r, den, n_b, p, w, g0 n_r, den, n_b, p, w */
	p[0] = (f[0] & 0x3fffff) | (f[1] & 0x3ff) << 22;
	p[1] = (f[1] >> 10 & 0xfff) | (f[2] & 0xfffff) << 12;
	p[2] = (f[2] >> 20 & 3) | (f[3] & 0x3fffff) << 2 | (f[4] & 0xff) << 24;
	p[3] = (f[4] >> 8 & 0x3f) | (f[5] & 0x3fffff) << 6 | (f[6] & 0xf) << 28;
	p[4] = (f[6] >> 4 & 0x3ffff) | (f[7] & 0x3fff) << 18;
	p[5] = (f[7] >> 14 & 0xff) | (f[8] & 0x3fffff) << 8 | (f[9] & 3) << 30;
	*hi = (*hi & (half ? 0xffffu : 0xffff0000u)) | ((f[9] >> 2) & 0xfff) << (half ? 16 : 0);
}

static void scene(u8 *buf, const struct t32_awb_par *par, u32 cr, u32 cb, u32 noise, u32 dark)
{
	u32 w = par->zones_w, h = par->zones_h, q = w >> 2, rem = w & 3;
	u32 gwords = (q * 26 + rem * 6 + (rem ? 2 : 0)) & 0xffff, row_base = 2, x, y, k;
	u32 *b = (u32 *)buf;

	memset(buf, 0, BUFB);
	for (y = 0; y < h && y < 32; y++) {
		for (x = 0; x + 4 <= (w & ~3U) && x < 32; x += 4) {
			u32 *g = b + ((row_base + (x >> 2) * 26) & 0xffff);

			if ((u32)(g + 26 - b) > BUFB / 4)
				break;
			for (k = 0; k < 4; k++) {
				u32 f[10], lum = (dark && rnd() % dark == 0) ? rnd() % 64 : 256 + rnd() % 4096, j;

				for (j = 0; j < 2; j++) {
					u32 den = lum * (64 + (noise ? rnd() % noise : 0));
					u32 rr = cr + (noise ? rnd() % (noise + 1) : 0), bb = cb + (noise ? rnd() % (noise + 1) : 0);

					f[5 * j + 1] = den & 0x3fffff;
					f[5 * j + 0] = (u32)((unsigned long long)den * rr / 1024) & 0x3fffff;
					f[5 * j + 2] = (u32)((unsigned long long)den * bb / 1024) & 0x3fffff;
					f[5 * j + 3] = (den / 2) & 0x3fffff;
					f[5 * j + 4] = (64 + rnd() % 4000) & 0x3fff;
				}
				put_zone(g + 6 * k, g + 24 + (k >> 1), k & 1, f);
			}
		}
		row_base = (gwords + row_base) & 0xffff;
	}
}

static unsigned int ncases, nframes, failed, nrefused, nwrites, ncts;

static int cmpb(const char *what, const u8 *v, const u8 *o, u32 n, const char *tag, u32 f)
{
	u32 i;

	for (i = 0; i < n; i++)
		if (v[i] != o[i]) {
			if (failed++ < 6)
				printf("t32_awbchain_oracle: FAIL %s frame %u: %s byte %u vendor %02x ours %02x\n", tag, f, what, i,
				       v[i], o[i]);
			return 1;
		}
	return 0;
}

static int run_case(const char *tag, const u8 *sec, u32 w_, u32 h_)
{
	static u8 osec[SECB];
	static struct t32_awb_ctd d;
	static struct t32_awb_proc p;
	struct t32_stats_write ow[T32_AWB_MAX_WRITES] = { { 0, 0 } };
	struct t32_awb_init ai;
	u8 *base, *info, *bufs, *vctx, *einfo;
	u32 f, nfr = 8 + rnd() % 24, cr = 600 + rnd() % 1200, cb = 600 + rnd() % 1200, noise = rnd() % 3 ? rnd() % 64 : 0;
	u32 dark = rnd() % 3 ? 0 : 2 + rnd() % 8, i;
	u16 ocnt = 0;
	int n;

	t32o_reset();
	t32o_rd_hook = rd_model;
	base = t32o_alloc(0x8000);
	info = t32o_alloc(64);
	bufs = t32o_alloc(4 * BUFB);
	einfo = t32o_alloc(16);
	memcpy(base + T32_AWBI_PAR_OFF, sec, SECB);
	t32_wr32(info, 0, w_);
	t32_wr32(info, 4, h_);
	t32_data_dd0a4[0] = T32O_PTR(base);
	t32_data_d03a0[0] = 0;
	t32_data_f0900[0] = 0;
	t32_wr32(einfo, 4, T32O_PTR(bufs));	/* e1694[ch]: the statistics buffers (tisp_awb_malloc_cfg) */
	t32_data_e1694[0] = T32O_PTR(einfo);
	t32_data_e1690[0] = t32_data_e1690[1] = 0;
	frame = 0;
	t32o_call((t32o_fn)(void (*)(void))tisp_awb_init, 0, T32O_PTR(info), 0, 0, 0);
	vctx = T32O_HOST(t32_data_e16b4[0]);
	memcpy(osec, sec, SECB);
	n = t32_awb_init(&ai, 0, osec, w_, h_, 0, &IOPS, ow);
	ncases++;
	if (n < 0 || (unsigned int)n != t32o_log.n || cmpb("init st", T32O_HOST(t32_ld32(vctx, 4)), ai.a.st->raw,
							    T32_AWBI_ST_BYTES, tag, 0))
		return 1;
	for (f = 0; f < nfr; f++) {
		int rc;

		frame = f;
		if (f && rnd() % 5 == 0) {	/* the scene's colour cast moves */
			cr = cr * (900 + rnd() % 250) / 1024 + 1;
			cb = cb * (900 + rnd() % 250) / 1024 + 1;
		}
		for (i = 0; i < 3; i++)
			regv[i] = rnd() % 3 ? rnd() & 0xffff : rnd();
		scene(bufs + (f & 3) * BUFB, ai.a.par, cr, cb, noise, dark);
		memset(&d, 0, sizeof(d));
		rc = t32_awb_frame(&ai, &d, o_rd, NULL, bufs, &ocnt, &p);
		if (rc) {
			nrefused++;
			return 0;
		}
		t32o_log.n = 0;
		t32o_ev_n = 0;
		memset(t32o_evlog, 0, sizeof(t32o_evlog));
		t32o_complete_n = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_awb_interrupt_static, 0, 0, 0, 0, 0);
		t32o_call((t32o_fn)(void (*)(void))tisp_awb_process, 0, 0, 0, 0, 0);
		nframes++;
		if (t32o_log.n != p.nw) {
			if (failed++ < 6)
				printf("t32_awbchain_oracle: FAIL %s frame %u: writes %u / %u\n", tag, f, t32o_log.n, p.nw);
			return 1;
		}
		for (i = 0; i < p.nw; i++)
			if (t32o_log.w[i].reg != p.w[i].reg || t32o_log.w[i].val != p.w[i].val) {
				if (failed++ < 6)
					printf("t32_awbchain_oracle: FAIL %s frame %u: write %u vendor %x=%x ours %x=%x\n", tag, f,
					       i, t32o_log.w[i].reg, t32o_log.w[i].val, p.w[i].reg, p.w[i].val);
				return 1;
			}
		nwrites += p.nw;
		/* events: 12 from the interrupt (pushed by the caller on our side), then 11 with the ct */
		if (t32o_ev_n != 2 || t32o_evlog[0][3] != T32_AWB_EV_STATS || t32o_evlog[1][3] != p.ev.type ||
		    t32o_evlog[1][5] != p.ev.ct || t32o_evlog[1][6] != p.ev.arg ||
		    t32o_complete_n != (u32)p.complete ||
		    t32_st16(t32_data_e1690, 0) != ocnt) {
			if (failed++ < 6)
				printf("t32_awbchain_oracle: FAIL %s frame %u: events %u (%x %x ct %u / %u), completions %u, counter %u / %u\n",
				       tag, f, t32o_ev_n, t32o_evlog[0][3], t32o_evlog[1][3], t32o_evlog[1][5], p.ev.ct,
				       t32o_complete_n, t32_st16(t32_data_e1690, 0), ocnt);
			return 1;
		}
		ncts += p.ev.ct != 0;
		if (cmpb("par", base + T32_AWBI_PAR_OFF, osec, SECB, tag, f) ||
		    cmpb("st", T32O_HOST(t32_ld32(vctx, 4)), ai.a.st->raw, T32_AWBI_ST_BYTES, tag, f) ||
		    cmpb("hw", T32O_HOST(t32_ld32(vctx, 12)), ai.a.hw->raw, T32_AWBI_HW_BYTES, tag, f))
			return 1;
	}
	return 0;
}

/*
 * Synthetic AWB sections, independent of T32_IQ_DIR (openimp #355: without bins no chain case ran, so
 * e.g. the mutant st->zt2[i] = 2 passed in a checkout without them; same fix as #333 for the AE chain).
 * Fixed seed, fixed count (T32O_NSYNTH, default 48); the first mismatch ends the run.  A zero section
 * with random tables from +3200 and zone grid / divisors like t32_awbinit_oracle (sane for
 * params_refresh), the gray world / spec switches of the bin variants, in every second case a random
 * run of the section below the grid fields.
 */
static void synth_cases(unsigned int n)
{
	static u8 sec[SECB];
	unsigned int c, k;

	for (c = 0; c < n && !failed; c++) {
		memset(sec, 0, sizeof(sec));
		for (k = 0; k < SECB; k++)
			sec[k] = (u8)rnd();
		t32_wr16(sec, 3184, rnd() % 64);
		t32_wr16(sec, 3186, 1 + rnd() % 40);
		t32_wr16(sec, 3188, rnd() % 64);
		t32_wr16(sec, 3190, 1 + rnd() % 40);
		t32_wr16(sec, 3448, rnd() % 3);
		t32_wr16(sec, 3450, rnd() % 3);
		sec[5704] = (u8)(rnd() % 3);
		run_case("synthetic", sec, c & 1 ? 2560 : 1920, c & 1 ? 1440 : 1080);
	}
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int nvar = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 6, nbins = 0, r;
	static u8 sec[SECB], b[1u << 20];
	struct dirent **ents;
	int ne, ie;

	synth_cases(getenv("T32O_NSYNTH") ? (unsigned)atoi(getenv("T32O_NSYNTH")) : 48);
	ne = failed || !dir ? 0 : scandir(dir, &ents, NULL, alphasort);
	for (ie = 0; ie < ne && !failed; ie++) {
		char path[1024];
		size_t l = strlen(ents[ie]->d_name), bytes;
		u32 off, len = 0;
		FILE *fp;

		if (l >= 4 && !strcmp(ents[ie]->d_name + l - 4, ".bin")) {
			snprintf(path, sizeof(path), "%s/%s", dir, ents[ie]->d_name);
			fp = fopen(path, "rb");
			if (fp) {
				bytes = fread(b, 1, sizeof(b), fp);
				fclose(fp);
				off = t32_iq_section(b, (u32)bytes, T32_IQ_SEC_AWB, &len);
				if (off && len >= SECB) {
					nbins++;
					for (r = 0; r < nvar; r++) {
						memcpy(sec, b + off, SECB);
						if (r >= 2) {	/* the gray world / spec switches */
							t32_wr16(sec, 3448, rnd() % 3);
							t32_wr16(sec, 3450, rnd() % 3);
							sec[5704] = (u8)(rnd() % 3);
						}
						run_case(ents[ie]->d_name, sec, r & 1 ? 2560 : 1920, r & 1 ? 1440 : 1080);
					}
				}
			}
		}
		free(ents[ie]);
	}
	for (; ie < ne; ie++)
		free(ents[ie]);
	if (ne > 0)
		free(ents);
	printf("t32_awbchain_oracle: IQ bins %u, %u cases, %u frames, %u writes, %u frames with ct, %u refused (ours), %u mismatches\n",
	       nbins, ncases, nframes, nwrites, ncts, nrefused, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awbchain_oracle: IQ-bin part: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
