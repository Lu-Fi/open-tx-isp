// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, WDR phase 5: our readable port (driver/t32/tx_isp_t32_wdr.h)
 * against the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc): tisp_wdr_process_init, process_deinit, init, deinit (init with
 * the vendor's malloc_cfg, params_hard_refresh, default / direct register
 * refresh and log2 as callees).
 *
 * Every OS call is logged on both sides in order (kmalloc size, kfree
 * pointer, irq / event callback numbers) and compared; our allocations come
 * from our own heap and are paired with the vendor's by order: pointers are
 * compared through that pairing (our virt_to_phys hook returns the bus address
 * of the paired vendor buffer), the contents of every pair byte for byte
 * (the record by its 7 words: the host struct is wider than the vendor's 28
 * bytes).  Also compared: the tuning arena of all 4 channels (parameter block
 * in it), the records of the channels, the WDR globals (proc / lut buffers,
 * grid, flags) and every register write.  Preset states (deinit,
 * process_init) use token pointers that are equal on both sides.
 * Not fed: a failed allocation (the vendor writes through NULL).
 * Exit 0 = all equal.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_wdr.h"

#define WEAK __attribute__((weak))
#define FR	64u
#define CAN	0xc5
#define ARN	(1u << 16)
#define TSZ	(T32_WDR_TUNE_PAR + T32_WDR_PAR_SZ + 64)

WEAK u32 t32_data_e3b40[4];
WEAK u32 t32_data_e3a84[5];
WEAK u8 t32_data_e3a98[4], t32_data_e3a80[4], t32_data_e3b10[4];
WEAK u32 t32_data_e3ad4[1], t32_data_e3ad8[1], t32_data_e3adc[1], t32_data_e3ae0[1], t32_data_e3ae4[1],
	t32_data_e3ae8[1], t32_data_e3aec[1], t32_data_e3af0[1];
WEAK u32 t32_data_e3af4[1], t32_data_e3af8[1], t32_data_e3afc[1], t32_data_e3b00[1], t32_data_e3b04[1], t32_data_e3b08[1];
static u32 *const VPROC[8] = { t32_data_e3ad4, t32_data_e3ad8, t32_data_e3adc, t32_data_e3ae0, t32_data_e3ae4,
	t32_data_e3ae8, t32_data_e3aec, t32_data_e3af0 };
static u32 *const VLUT[6] = { t32_data_e3af4, t32_data_e3af8, t32_data_e3afc, t32_data_e3b00, t32_data_e3b04,
	t32_data_e3b08 };
WEAK u16 t32_data_e3b30[5];
WEAK char t32_data_ca6e8[4] = "f", t32_data_ca708[4] = "f", t32_data_c2e1c[16] = "tisp_wdr_init";
WEAK const u32 t32_data_c2dc8[14] = { 0x001f0000U, 0x005f003fU, 0x00bf007fU, 0x017f00ffU, 0x027f01ffU, 0x037f02ffU,
	0x047f03ffU, 0x057f04ffU, 0x067f05ffU, 0x077f06ffU, 0x087f07ffU, 0x097f08ffU, 0x0a7f09ffU, 0x00000affU };
WEAK const u32 t32_data_c2e00[7] = { 0x05050505U, 0x07070606U, 0x07070707U, 0x07070707U, 0x07070707U, 0x07070707U,
	0x00000707U };
/* only their addresses are handed to the callback setters */
WEAK u64 tisp_wdr_interrupt_static(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
WEAK u64 tisp_wdr_process(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

u64 tisp_wdr_process_init(u32, u32, u32, u32);
u64 tisp_wdr_process_deinit(u32, u32, u32, u32);
u64 tisp_wdr_init(u32, u32, u32, u32);
u64 tisp_wdr_deinit(u32, u32, u32, u32);

#define FN(x) ((t32o_fn)(void (*)(void))(x))

enum { F_PIN, F_PDE, F_INI, F_DEI, F_N };
static const char *const fname[F_N] = { "process_init", "process_deinit", "init", "deinit" };

static u32 rs = 0xa54ff5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 rb(u32 n) { return rnd() % n; }

/* ---- call logs: K = kmalloc (a = size, p = pointer), F = kfree, I = irq set, E = event cb ---- */
#define NEV 64
struct ev { char t; u32 a, b; uintptr_t p; };
static struct ev VE[NEV], OE[NEV];
static u32 NVE, NOE, NVA;
static u8 *VAL[NEV];	/* the vendor's allocations in order */
static void vlog(char t, u32 a, u32 b, uintptr_t p) { if (NVE < NEV) { VE[NVE].t = t; VE[NVE].a = a; VE[NVE].b = b; VE[NVE].p = p; } NVE++; }
static void olog(char t, u32 a, u32 b, uintptr_t p) { if (NOE < NEV) { OE[NOE].t = t; OE[NOE].a = a; OE[NOE].b = b; OE[NOE].p = p; } NOE++; }

u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u8 *p = t32o_alloc(a0);

	(void)a2; (void)a3;
	if (p)
		memset(p, 0xe7, a0);	/* kmalloc memory is not zeroed */
	vlog('K', a0, a1, (uintptr_t)p);
	if (NVA < NEV) VAL[NVA] = p;
	NVA++;
	return T32O_PTR(p);
}
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	vlog('F', 0, 0, a0);
	return 0;
}
u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	vlog('I', a0, a1, a2 == T32O_PTR((void *)tisp_wdr_interrupt_static));
	return 0;
}
u64 tisp_event_set_cb(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	vlog('E', a0, a1, a2 == T32O_PTR((void *)tisp_wdr_process));
	return 0;
}

/* ---- our heap, paired with the vendor allocations by order ---- */
static u8 OH[1u << 17] __attribute__((aligned(64)));
static u32 ohu, NOA;
static struct { u8 *p; u32 n; } OAL[NEV];
static u8 *o_kmalloc(u32 size, u32 gfp)
{
	u8 *p;

	ohu = (ohu + 63u) & ~63u;
	p = OH + ohu;
	ohu += size;
	memset(p, 0xe7, size);
	if (NOA < NEV) { OAL[NOA].p = p; OAL[NOA].n = size; }
	NOA++;
	olog('K', size == sizeof(struct t32_wdr_rec) ? 28 : size, gfp, (uintptr_t)p);
	return p;
}

static u8 IMG[ARN], OA[ARN] __attribute__((aligned(16)));
static u8 *VA;
static u32 TUNE[4], TOT;
static struct t32_wdr_st ST;
static struct t32_wdr_rec REC[4];
static u32 *VREC[4];

/* our pointer -> the vendor's equivalent (allocation pair, preset record, arena), else unchanged */
static uintptr_t o2v(const void *q)
{
	uintptr_t p = (uintptr_t)q;
	u32 i;

	if (!q)
		return 0;
	for (i = 0; i < NOA && i < NEV && i < NVA; i++)
		if (p >= (uintptr_t)OAL[i].p && p < (uintptr_t)OAL[i].p + (OAL[i].n ? OAL[i].n : 1))
			return (uintptr_t)VAL[i] + (p - (uintptr_t)OAL[i].p);
	for (i = 0; i < 4; i++)
		if (q == (const void *)&REC[i])
			return (uintptr_t)VREC[i];
	if (p >= (uintptr_t)OA && p < (uintptr_t)OA + ARN)
		return (uintptr_t)VA + (p - (uintptr_t)OA);
	return p;
}

static struct { u32 reg, val; } W[256];
static u32 NW;
static void o_wr(u32 reg, u32 val) { if (NW < 256) { W[NW].reg = reg; W[NW].val = val; } NW++; }
static u32 o_rd(u32 reg) { return reg; }
static u32 o_v2p(const u8 *p) { return (u32)o2v(p) + 0x80000000u; }
static void o_kfree(void *p) { olog('F', 0, 0, o2v(p)); }
static void o_error(u32 line) { olog('P', line, 0, 0); }
static void o_irq(u32 ch, u32 irq) { olog('I', ch, irq, 1); }
static void o_evcb(u32 ch, u32 ev) { olog('E', ch, ev, 1); }
static const struct t32_wdr_ops OPS = { .reg_write = o_wr, .reg_read = o_rd, .kmalloc = o_kmalloc, .virt_to_phys = o_v2p,
	.kfree = o_kfree, .error = o_error, .irq_func_set = o_irq, .event_set_cb = o_evcb };

static u32 F, CH, SZ[2], SOFF, cur, PRESET[4];
static void lay_begin(void) { cur = 0; memset(IMG, CAN, sizeof(IMG)); }
static u32 lay(u32 n)
{
	u32 i, o;

	cur = (cur + FR + 7u) & ~7u;
	o = cur;
	for (i = 0; i < n; i++)
		IMG[o + i] = (u8)(rnd() >> 9);
	cur += n;
	TOT = cur + FR;
	return o;
}
static u32 token(void) { return rb(3) == 0 ? 0 : 0x10000000u | (rnd() & 0x0ffffff0u); }

static void gen(void)
{
	u32 i;

	lay_begin();
	for (i = 0; i < 4; i++) {
		TUNE[i] = lay(TSZ);
		IMG[TUNE[i] + T32_WDR_TUNE_PAR + 1831] = rb(2) ? 1 : (u8)rb(3);
	}
	SOFF = lay(8);
	CH = rb(4);
	switch (rb(4)) {
	case 0: SZ[0] = 1920; SZ[1] = 1080; break;
	case 1: SZ[0] = rb(16); SZ[1] = rb(16); break;
	case 2: SZ[0] = 15 * rb(300) + rb(3); SZ[1] = 15 * rb(200) + rb(3); break;
	default: SZ[0] = rb(5000); SZ[1] = rb(5000); break;
	}
	memcpy(IMG + SOFF, SZ, 8);
}

/* identical starting state on both sides: preset records (or none), token buffers */
static void setup(void)
{
	u32 i, k;

	memset(&ST, 0, sizeof(ST));
	ST.ops = &OPS;
	NVE = NOE = NOA = NVA = ohu = NW = 0;
	for (i = 0; i < 4; i++) {
		t32_data_dd0a4[i] = T32O_PTR(VA + TUNE[i]);
		ST.tune[i] = T32O_PTR(OA + TUNE[i]);
		VREC[i] = t32o_alloc(32);
		t32_data_e3b40[i] = 0;
		ST.rec[i] = 0;
		PRESET[i] = 0;
		if ((F == F_DEI || F == F_PIN || F == F_PDE) && rb(2)) {
			for (k = 0; k < 7; k++)
				VREC[i][k] = token();
			VREC[i][3] = T32O_PTR(VA + TUNE[i] + T32_WDR_TUNE_PAR);
			REC[i].nbuf = VREC[i][0];
			REC[i].buf = T32O_HOST(VREC[i][1]);
			REC[i].buf_phys = VREC[i][2];
			REC[i].par = OA + TUNE[i] + T32_WDR_TUNE_PAR;
			REC[i].ext = T32O_HOST(VREC[i][4]);
			REC[i].hw = T32O_HOST(VREC[i][5]);
			REC[i].work = T32O_HOST(VREC[i][6]);
			t32_data_e3b40[i] = T32O_PTR(VREC[i]);
			ST.rec[i] = &REC[i];
			PRESET[i] = 1;
		}
	}
	if (F == F_DEI && rb(3) == 0) {	/* only the channel to free is left: the shared buffers go too */
		for (i = 0; i < 4; i++)
			if (i != CH) { t32_data_e3b40[i] = 0; ST.rec[i] = 0; PRESET[i] = 0; }
	}
	for (i = 0; i < 8; i++)
		*VPROC[i] = T32O_PTR(ST.proc[i] = (F == F_INI && rb(2)) ? 0 : T32O_HOST(token()));
	for (i = 0; i < 6; i++)
		*VLUT[i] = T32O_PTR(ST.lut[i] = T32O_HOST(token()));
	for (i = 0; i < 5; i++)
		t32_data_e3b30[i] = ST.grid[i] = (u16)rnd();
	t32_data_e3a98[0] = ST.fusion_on = (u8)rnd();
	t32_data_e3a80[0] = ST.inited = (u8)rnd();
	t32_data_e3b10[0] = ST.nbuf = (u8)rnd();
}

static int rec_words_differ(const u32 *v, const struct t32_wdr_rec *r)
{
	return v[0] != r->nbuf || v[1] != (u32)o2v(r->buf) || v[2] != r->buf_phys || v[3] != (u32)o2v(r->par) ||
	       v[4] != (u32)o2v(r->ext) || v[5] != (u32)o2v(r->hw) || v[6] != (u32)o2v(r->work);
}

static int differ(void)
{
	u32 i;

	if (NVE != NOE || NVE > NEV || NVA != NOA || NW != t32o_log.n || memcmp(VA, OA, TOT))
		return 1;
	for (i = 0; i < NVE; i++) {
		if (VE[i].t != OE[i].t || VE[i].a != OE[i].a || VE[i].b != OE[i].b)
			return 2;
		if (VE[i].t == 'F' ? VE[i].p != OE[i].p : VE[i].t != 'K' && VE[i].p != OE[i].p)
			return 3;
	}
	for (i = 0; i < NW; i++)
		if (W[i].reg != t32o_log.w[i].reg || W[i].val != t32o_log.w[i].val)
			return 4;
	for (i = 0; i < NOA; i++) {	/* contents of the paired allocations */
		const u8 *v = VAL[i];
		const struct t32_wdr_rec *r = (const struct t32_wdr_rec *)OAL[i].p;

		if (OAL[i].n == sizeof(struct t32_wdr_rec)) {
			if (rec_words_differ((const u32 *)v, r))
				return 5;
		} else if (memcmp(v, OAL[i].p, OAL[i].n)) {
			return 6;
		}
	}
	for (i = 0; i < 4; i++) {
		if (t32_data_e3b40[i] != (u32)o2v(ST.rec[i]))
			return 7;
		if (PRESET[i] && rec_words_differ(VREC[i], &REC[i]))	/* also a freed one: cleared before the kfree */
			return 8;
	}
	for (i = 0; i < 8; i++)
		if (*VPROC[i] != (u32)o2v(ST.proc[i]))
			return 10;
	for (i = 0; i < 6; i++)
		if (*VLUT[i] != (u32)o2v(ST.lut[i]))
			return 11;
	for (i = 0; i < 5; i++)
		if (t32_data_e3b30[i] != ST.grid[i])
			return 12;
	if (t32_data_e3a98[0] != ST.fusion_on || t32_data_e3a80[0] != ST.inited || t32_data_e3b10[0] != ST.nbuf)
		return 13;
	return 0;
}

int main(void)
{
	static u32 hit[F_N];
	u32 n, cases = 0, bad = 0;
	u64 rv, ro;

	for (n = 0; n < 8000; n++) {
		int d;

		F = n % F_N;
		gen();
		t32o_reset();
		VA = t32o_alloc(ARN);
		memcpy(VA, IMG, TOT);
		memcpy(OA, IMG, TOT);
		setup();
		t32o_arg5 = t32o_arg6 = t32o_arg7 = 0;
		switch (F) {
		case F_PIN: rv = t32o_call(FN(tisp_wdr_process_init), 0, 0, 0, 0, 0); t32_wdr_process_init(&ST); ro = rv; break;
		case F_PDE: rv = t32o_call(FN(tisp_wdr_process_deinit), 0, 0, 0, 0, 0); t32_wdr_process_deinit(&ST); ro = rv; break;
		case F_INI:
			rv = t32o_call(FN(tisp_wdr_init), CH, T32O_PTR(VA + SOFF), 0, 0, 0);
			ro = (u32)t32_wdr_init(&ST, CH, (const u32 *)(OA + SOFF));
			break;
		default:
			rv = t32o_call(FN(tisp_wdr_deinit), CH, 0, 0, 0, 0);
			ro = (u32)t32_wdr_deinit(&ST, CH);
			break;
		}
		cases++;
		hit[F]++;
		d = differ();
		if (!d && (u32)rv != (u32)ro)
			d = 20;
		if (d && bad++ < 5)
			fprintf(stderr, "t32_wdr5_oracle: %s case %u differs (%d; events %u / %u, writes %u / %u)\n", fname[F], n, d,
				NVE, NOE, t32o_log.n, NW);
	}
	for (n = 0; n < F_N; n++)
		if (!hit[n])
			bad++;
	printf("t32_wdr5_oracle: %u cases, %u mismatches\n", cases, bad);
	return bad ? 1 : 0;
}
