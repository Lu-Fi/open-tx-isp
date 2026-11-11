// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_awb_init (driver/t32/tx_isp_t32_awbinit.h) against the
 * vendor tisp_awb_init (Matt Davis' recovered C, PR #15 @ 2e2e06bc) with its
 * state callees as references: tisp_awb_params_refresh,
 * tisp_awb_show_para_update, tisp_awb_set_gain (+ gain_reg, the fixed point
 * multiply, tisp_reg_write, system_reg_set_awb_trig).
 *
 * Stubs on the vendor side, hooks on ours, both logged into one call list:
 * private_kmalloc (size), tisp_awb_malloc_cfg (the statistics buffers, done
 * by t32_awb_stats_init, t32_awb_oracle), tisp_awb_set_hardware_param (the
 * same), tisp_awb_set_riscv_param (f0900[ch] == 1; not used by the driver),
 * system_irq_func_set (channel, irq, handler), tisp_event_set_cb (channel,
 * event, handler), isp_printf (count).
 *
 * Compared per case: the call list, every register write (address / value /
 * order), every byte of the five blocks (e16a4 table 192 B, the run state
 * 0x1d48c B, the mode block 28 B, the hardware shadow 4292 B, the context
 * 32 B with words 0..3 as the block addresses), the AWB section of the
 * tuning copy (6740 B, params_refresh writes into it), e16c4[ch] word 0 and
 * the decoy channels 1..3.  The d03a0 = 1 run (register shadow) must give
 * the same writes as the direct run.  Inputs: random sections, sizes up to
 * 17 bits (the vendor keeps u16), and section 2 of every IQ bin.  Our
 * allocation-failure paths (the vendor writes through NULL there) are run on
 * our side only and counted.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_iq.h"
#include "tx_isp_t32_awbinit.h"

#define SECB	6740u
extern u32 t32_data_e16a4[4], t32_data_e16b4[4], t32_data_e16c4[16];
extern u8 t32_data_f0900[0x10000];
u64 tisp_awb_init(u32 a0, u32 a1, u32 a2, u32 a3);
u64 tisp_awb_interrupt_static(u32 a0, u32 a1, u32 a2, u32 a3), tisp_awb_process(u32 a0, u32 a1, u32 a2, u32 a3);

static u32 rs = 0x5a17c3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* ---- the call list ---- */
#define NC 32
struct clog { unsigned int n; u32 e[NC][4]; };
static struct clog cl[2];
static int side;
static void crec(u32 k, u32 a, u32 b, u32 c)
{
	struct clog *l = &cl[side];

	if (l->n < NC) {
		l->e[l->n][0] = k; l->e[l->n][1] = a; l->e[l->n][2] = b; l->e[l->n][3] = c;
	}
	l->n++;
}

u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	crec(1, a0, a1, 0);
	return a0 ? T32O_PTR(t32o_alloc(a0)) : 0;
}
u64 tisp_awb_malloc_cfg(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; crec(2, a0, 0, 0); return 0; }
u64 tisp_awb_set_hardware_param(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; crec(3, a0, 0, 0); return 0; }
u64 tisp_awb_set_riscv_param(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; crec(4, a0, 0, 0); return 0; }
u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	crec(5, a0, a1, a2 == T32O_PTR((void *)tisp_awb_interrupt_static));
	return 0;
}
u64 tisp_event_set_cb(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	crec(6, a0, a1, a2 == T32O_PTR((void *)tisp_awb_process));
	return 0;
}

/* ours */
static int fail_at = -1, nalloc;
static void *o_alloc(void *p, u32 size)
{
	(void)p;
	crec(1, size, 0x24000c0, 0);
	if (nalloc++ == fail_at)
		return NULL;
	return t32o_alloc(size);
}
static void o_riscv(void *p, u32 ch) { (void)p; crec(4, ch, 0, 0); }
static void o_hw(void *p, u32 ch) { (void)p; crec(3, ch, 0, 0); }
static void o_irq(void *p, u32 ch, u32 irq) { (void)p; crec(5, ch, irq, 1); }
static void o_ev(void *p, u32 ch, u32 ev) { (void)p; crec(6, ch, ev, 1); }
static unsigned int oprints;
static void o_print(void *p, int ok) { (void)p; (void)ok; oprints++; }
static const struct t32_awb_init_ops OPS = { 0, o_alloc, o_riscv, o_hw, o_irq, o_ev, o_print };

static unsigned int compared, failed, refused, kmsizes;

static int cmpb(const char *what, const u8 *v, const u8 *o, u32 n, const char *tag)
{
	u32 i;

	for (i = 0; i < n; i++)
		if (v[i] != o[i]) {
			if (failed++ < 6)
				printf("t32_awbinit_oracle: FAIL %s: %s byte %u vendor %02x ours %02x\n", tag, what, i, v[i], o[i]);
			return 1;
		}
	return 0;
}

static struct t32o_log m0;

static int run_case(const char *tag, const u8 *sec, u32 w_, u32 h_, u8 riscv)
{
	static u8 osec[T32_AWBI_PAR_OFF + SECB];
	struct t32_stats_write ow[T32_AWB_MAX_WRITES] = { { 0, 0 } };
	struct t32_awb_init ai;
	u8 *base, *info, *vctx;
	unsigned int i, vprints;
	int n, bad = 0, dsm;

	for (dsm = 0; dsm < 2; dsm++) {
		t32o_reset();
		if (!dsm)
			t32o_decoy_arm();
		memset(cl, 0, sizeof(cl));
		base = t32o_alloc(0x8000);
		info = t32o_alloc(64);
		memcpy(base + T32_AWBI_PAR_OFF, sec, SECB);
		t32_wr32(info, 0, w_);
		t32_wr32(info, 4, h_);
		t32_data_dd0a4[0] = T32O_PTR(base);
		t32_data_d03a0[0] = (u8)dsm;
		t32_data_f0900[0] = riscv;
		t32_data_e16c4[0] = 0xdeadbeef;	/* cleared by the init */
		side = 0;
		t32o_call((t32o_fn)(void (*)(void))tisp_awb_init, 0, T32O_PTR(info), 0, 0, 0);
		if (dsm) {	/* the register shadow run: the same writes as the direct one, only the addresses differ */
			if (t32o_log.n != m0.n)
				bad = 1;
			for (i = 0; !bad && i < m0.n; i++)
				if (!t32o_reg_eq(t32o_log.w[i].reg, m0.w[i].reg) || t32o_log.w[i].val != m0.w[i].val)
					bad = 1;
			if (bad && failed++ < 6)
				printf("t32_awbinit_oracle: FAIL %s: shadow run writes differ\n", tag);
			return bad;
		}
		memcpy(&m0, &t32o_log, sizeof(m0));
		if (t32o_decoy_check()) {
			if (failed++ < 6)
				printf("t32_awbinit_oracle: FAIL %s: a decoy channel changed\n", tag);
			bad = 1;
		}
		vprints = t32o_log.printfs;
		vctx = T32O_HOST(t32_data_e16b4[0]);

		/* ours on a copy of the tuning base */
		memcpy(osec, base, T32_AWBI_PAR_OFF);
		memcpy(osec + T32_AWBI_PAR_OFF, sec, SECB);
		side = 1; nalloc = 0; fail_at = -1; oprints = 0;
		crec(2, 0, 0, 0);		/* tisp_awb_malloc_cfg: t32_awb_stats_init at stream-on */
		n = t32_awb_init(&ai, 0, osec + T32_AWBI_PAR_OFF, w_, h_, riscv, &OPS, ow);
		compared++;
		if (n < 0 || cl[0].n != cl[1].n || memcmp(cl[0].e, cl[1].e, sizeof(cl[0].e[0]) * (cl[0].n < NC ? cl[0].n : NC))) {
			if (failed++ < 6)
				printf("t32_awbinit_oracle: FAIL %s: calls %u / %u (rc %d)\n", tag, cl[0].n, cl[1].n, n);
			return 1;
		}
		for (i = 0; i < cl[0].n; i++)
			kmsizes += cl[0].e[i][0] == 1;
		if (vprints != oprints || (unsigned int)n != t32o_log.n) {
			if (failed++ < 6)
				printf("t32_awbinit_oracle: FAIL %s: prints %u / %u, writes %u / %d\n", tag, vprints, oprints,
				       t32o_log.n, n);
			return 1;
		}
		for (i = 0; i < (unsigned int)n; i++)
			if (t32o_log.w[i].reg != ow[i].reg || t32o_log.w[i].val != ow[i].val) {
				if (failed++ < 6)
					printf("t32_awbinit_oracle: FAIL %s: write %u vendor %x=%x ours %x=%x\n", tag, i,
					       t32o_log.w[i].reg, t32o_log.w[i].val, ow[i].reg, ow[i].val);
				return 1;
			}
		if (t32_ld32(vctx, 0) != t32_data_dd0a4[0] + T32_AWBI_PAR_OFF ||
		    t32_ld32(ai.ctx, 0) != T32O_PTR(osec + T32_AWBI_PAR_OFF) ||
		    t32_ld32(ai.ctx, 4) != T32O_PTR(ai.a.st) || t32_ld32(ai.ctx, 8) != T32O_PTR(ai.mode) ||
		    t32_ld32(ai.ctx, 12) != T32O_PTR(ai.a.hw) || t32_data_e16c4[0] != ai.proc_flag) {
			if (failed++ < 6)
				printf("t32_awbinit_oracle: FAIL %s: context words / e16c4\n", tag);
			return 1;
		}
		bad |= cmpb("ctx", vctx + 16, ai.ctx + 16, 16, tag);
		bad |= cmpb("tbl", T32O_HOST(t32_data_e16a4[0]), ai.tbl, T32_AWBI_TBL_BYTES, tag);
		bad |= cmpb("st", T32O_HOST(t32_ld32(vctx, 4)), ai.a.st->raw, T32_AWBI_ST_BYTES, tag);
		bad |= cmpb("mode", T32O_HOST(t32_ld32(vctx, 8)), ai.mode, T32_AWBI_MODE_BYTES, tag);
		bad |= cmpb("hw", T32O_HOST(t32_ld32(vctx, 12)), ai.a.hw->raw, T32_AWBI_HW_BYTES, tag);
		bad |= cmpb("par", base + T32_AWBI_PAR_OFF, osec + T32_AWBI_PAR_OFF, SECB, tag);
		if (bad)
			return 1;
	}
	return 0;
}

/* our refusals: an allocation fails (vendor: memset / writes through NULL) */
static void refusals(const u8 *sec)
{
	static u8 osec[T32_AWBI_PAR_OFF + SECB];
	struct t32_stats_write ow[T32_AWB_MAX_WRITES];
	struct t32_awb_init ai;
	int k, n;

	for (k = 0; k < 5; k++) {
		t32o_reset();
		memcpy(osec + T32_AWBI_PAR_OFF, sec, SECB);
		side = 1; memset(cl, 0, sizeof(cl)); nalloc = 0; fail_at = k; oprints = 0;
		n = t32_awb_init(&ai, 0, osec + T32_AWBI_PAR_OFF, 1920, 1080, 0, &OPS, ow);
		refused++;
		/* -ENOMEM; the printk only for the context */
		{
		static const unsigned int calls[5] = { 5, 2, 3, 5, 5 };	/* allocations up to the check that fails */

		if (n != -12 || cl[1].n != calls[k] || oprints != (k == 1)) {
			if (failed++ < 6)
				printf("t32_awbinit_oracle: FAIL refusal %d: rc %d calls %u prints %u\n", k, n, cl[1].n, oprints);
		}
		}
	}
}

int main(void)
{
	const char *dir = getenv("T32_IQ_DIR");
	int strict = getenv("T40_ORACLE_STRICT") && atoi(getenv("T40_ORACLE_STRICT"));
	unsigned int nrand = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 400, c, i, nbins = 0;
	static u8 sec[SECB], b[1u << 20];
	struct dirent **ents;
	int ne, ie;

	for (c = 0; c < nrand; c++) {
		u32 w = c % 4 ? 640 + rnd() % 3200 : rnd() & 0x1ffff, h = c % 4 ? 360 + rnd() % 2000 : rnd() & 0x1ffff;

		for (i = 0; i < SECB; i++)
			sec[i] = (u8)rnd();
		/* sane zone divisors for params_refresh in most cases (it loops until the quotient is below 127) */
		if (c % 8) {
			t32_wr16(sec, 3184, rnd() % 64); t32_wr16(sec, 3186, 1 + rnd() % 40);
			t32_wr16(sec, 3188, rnd() % 64); t32_wr16(sec, 3190, 1 + rnd() % 40);
		} else {
			t32_wr16(sec, 3186, 60 + rnd() % 4000); t32_wr16(sec, 3190, 60 + rnd() % 4000);
		}
		run_case("random", sec, w, h, (u8)(c % 5 == 0 ? 1 : c % 7 == 0 ? 2 : 0));
	}
	refusals(sec);
	ne = scandir(dir ? dir : "../external/ingenic-sdk/sensor-iq/t32", &ents, NULL, alphasort);
	for (ie = 0; ie < ne; ie++) {
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
					run_case(ents[ie]->d_name, b + off, 1920, 1080, 0);
					run_case(ents[ie]->d_name, b + off, 2560 + rnd() % 1280, 1440 + rnd() % 720, 0);
				}
			}
		}
		free(ents[ie]);
	}
	if (ne > 0)
		free(ents);
	printf("t32_awbinit_oracle: %u cases (%u IQ bins), %u allocations, %u refusals (ours only), %u mismatches\n",
	       compared, nbins, kmsizes, refused, failed);
	if (failed)
		return 1;
	if (!nbins) {
		printf("t32_awbinit_oracle: SKIP (set T32_IQ_DIR to a directory of T32 *.bin)\n");
		return strict ? 2 : 0;
	}
	return 0;
}
