// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our readable ispcore_pad_event_handle (driver/t32/tx_isp_t32_isppad.h)
 * against the vendor function from Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15
 * @ 2e2e06bc, built for the host with env.c; the event jump table c312c holds the OEM code
 * addresses).  Random worlds built twice (event 0x3000000..9 / random, pad active / state /
 * flags, subdev / private block valid or not (event 2), sensor mode, stop / started / stream-off
 * flags, frame channel with real ready / queue / free lists of 0..3 buffers, format request with
 * crop / scaler / frame crop on or off, pixel format found or not, drop pattern, pre-buffer
 * counters); compared: return value, the ordered call log of every hook (arguments, printf
 * values, the 52 B channel attribute, lock flags), all bss words and every byte of the world and
 * the allocation pool.  Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_isppad.h"

u64 ispcore_pad_event_handle(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 t32_data_c312c[], t32_data_d9014[], t32_data_d9008[], t32_data_f3354[], t32_data_d8ce0[];
extern u32 t32_data_f3340[], t32_data_f333c[], t32_data_f3774[], t32_data_f3778[], t32_data_f3750[];
extern u32 t32_data_f374c[], t32_data_f3808[], t32_data_f3804[], t32_data_f3810[], t32_data_f380c[];
extern u32 t32_data_ccdf8[], t32_data_cce1c[], t32_data_cce64[], t32_data_ccebc[], t32_data_ccfa4[];
extern u32 t32_data_ccf18[], t32_data_ccf38[], t32_data_ccf64[], t32_data_ccf7c[];
extern u32 t32_data_c3370[], t32_data_c3354[], t32_data_c3338[];

#define WSZ	0x10000u
#define MAXL	48
static u32 rs = 0x9ad3e1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u32 ss;
static u32 srnd(void)
{
	ss ^= ss << 13; ss ^= ss >> 17; ss ^= ss << 5;
	return ss;
}
struct lg { unsigned int n; u32 e[MAXL][4]; u32 bss[13]; };
static struct lg lg, lg_ours;
static u8 *world, snap[WSZ];
static u32 W, PAD, EV, DATA, A3;
static void L(u32 id, u32 a, u32 b, u32 c)
{
	if (lg.n < MAXL) {
		lg.e[lg.n][0] = id; lg.e[lg.n][1] = a; lg.e[lg.n][2] = b; lg.e[lg.n][3] = c;
	}
	lg.n++;
}
static void P32(u32 a, u32 v) { memcpy(T32O_HOST(a), &v, 4); }
static u32 hash(u32 a, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ ((u8 *)T32O_HOST(a))[i]) * 16777619u;
	return h;
}

/* the vendor callees: log, return a seeded value */
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 s16, u32 s20, u32 s24)
{
	u32 f = a1, h;

	t32o_log.printfs++;
	if (f == T32O_PTR(t32_data_ccf18))		/* event 3 "no buffer": a2 / a3 are leftovers */
		h = 0;
	else if (f == T32O_PTR(t32_data_ccdf8) || f == T32O_PTR(t32_data_ccf7c))
		h = a2 ^ a3 * 3 ^ s16 * 5 ^ s20 * 7;
	else if (f == T32O_PTR(t32_data_cce1c) || f == T32O_PTR(t32_data_ccfa4))
		h = a2 ^ a3 * 3 ^ s16 * 5;
	else if (f == T32O_PTR(t32_data_cce64) || f == T32O_PTR(t32_data_ccebc))
		h = a2 ^ a3 * 3 ^ s16 * 5 ^ s20 * 7 ^ s24 * 11;
	else
		h = a2 ^ a3 * 3;
	L(1, a0, f, h);
	return srnd();
}
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(2, a0, 0, 0); P32(a1, srnd()); return srnd(); }
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(3, a0, a1, 0); return srnd(); }
u64 private_init_completion(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(4, a0, 0, 0); return srnd(); }
u64 lib_tisp_msca_channel_attr_set(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(5, a0, hash(a1, 52), 0); return srnd(); }
u64 tisp_msca_channel_start(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(6, a0, 0, 0); return srnd(); }
u64 lib_tisp_msca_chx_stream_on(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(7, a0, 0, 0); return srnd(); }
u64 lib_tisp_api_set_msca_dma_stride(u32 a0, u32 a1, u32 a2, u32 a3) { L(8, a0, a1, a2 ^ a3 << 7); return srnd(); }
u64 tisp_msca_chx_en(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; L(9, a0, a1, a2); return srnd(); }
u64 tisp_msca_addr_fifo_write(u32 a0, u32 a1, u32 a2, u32 a3) { L(10, a0, a1, a2 ^ (a3 << 1)); return srnd(); }
u64 __ispcore_get_frame_drop(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(11, a0, 0, 0); return srnd() % 3 == 0; }
u64 lib_tisp_stream_on(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; L(12, 0, 0, 0); return srnd(); }
u64 ispcore_frame_channel_streamoff(u32 a0, u32 a1, u32 a2, u32 a3) { L(13, a0, a1, a2 ^ a3 << 3); return srnd(); }
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; L(14, a0, a1, 0); return W + 0x8000; }
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; L(15, a0, 0, 0); return srnd(); }

static void o_pr(u32 l, u32 f, u32 a2, u32 a3, u32 s16, u32 s20, u32 s24) { isp_printf(l, f, a2, a3, s16, s20, s24); }
static void o_lk(u32 l, u32 *f) { __private_spin_lock_irqsave(l, T32O_PTR(f), 0, 0); }
static void o_ul(u32 l, u32 f) { private_spin_unlock_irqrestore(l, f, 0, 0); }
static void o_ic(u32 c) { private_init_completion(c, 0, 0, 0); }
static void o_as(u32 p, u32 *a) { lib_tisp_msca_channel_attr_set(p, T32O_PTR(a), 0, 0); }
static void o_cs(u32 n) { tisp_msca_channel_start(n, 0, 0, 0); }
static void o_so(u32 n) { lib_tisp_msca_chx_stream_on(n, 0, 0, 0); }
static void o_ds(u32 p, u32 c, u32 v, u32 z) { lib_tisp_api_set_msca_dma_stride(p, c, v, z); }
static void o_ce(u32 p, u32 c, u32 e) { tisp_msca_chx_en(p, c, e, 0); }
static void o_fw(u32 p, u32 s, u32 a, u32 b) { tisp_msca_addr_fifo_write(p, s, a, b); }
static u32 o_gd(u32 f) { return (u32)__ispcore_get_frame_drop(f, 0, 0, 0); }
static void o_on(void) { lib_tisp_stream_on(0, 0, 0, 0); }
static u32 o_sf(u32 a, u32 b, u32 c, u32 d) { return (u32)ispcore_frame_channel_streamoff(a, b, c, d); }
static u32 o_km(u32 s, u32 f) { return (u32)private_kmalloc(s, f, 0, 0); }
static void o_kf(u32 p) { private_kfree(p, 0, 0, 0); }
static struct t32_pe_glob glob;
static const struct t32_pe_ops ops = { o_pr, o_lk, o_ul, o_ic, o_as, o_cs, o_so, o_ds, o_ce, o_fw, o_gd, o_on, o_sf, o_km, o_kf, &glob };

static u32 pick(u32 n) { return rnd() % n; }

/* a real list at head: 0..3 entries from pool (64 B apart) */
static void list(u32 head, u32 pool, int nonempty)
{
	u32 n = nonempty ? 1 + pick(3) : pick(4), prev = head, i, e;

	for (i = 0; i < n; i++) {
		e = pool + 0x40 * i;
		P32(prev, e);
		P32(e + 4, prev);
		prev = e;
	}
	P32(prev, head);
	P32(head + 4, prev);
}

static void build(u32 seed)
{
	u32 save = rs, i, sd, priv, fc, mode;
	static const u32 evs[] = { 0x3000001, 0x3000002, 0x3000003, 0x3000004, 0x3000005, 0x3000006, 0x3000007, 0x3000008 };

	t32o_reset();
	world = t32o_alloc(WSZ);
	W = T32O_PTR(world);
	rs = seed | 1;
	for (i = 0; i < WSZ; i += 4)
		P32(W + i, rnd());
	PAD = W;
	sd = W + 0x100;
	priv = W + 0x400;
	fc = W + 0x1800;
	DATA = W + 0x2000;
	A3 = rnd();
	EV = pick(10) ? evs[pick(8)] : pick(2) ? 0x3000000 + pick(10) : rnd();
	world[T32_PE_ACTIVE] = pick(8) ? 1 + pick(3) : 0;
	world[T32_PE_STATE] = pick(4) ? 3 : pick(6);
	P32(PAD + T32_PE_FLAGS, pick(4) ? rnd() & ~0x20U : rnd());
	P32(PAD, sd);
	P32(PAD + T32_PE_FC, EV == 0x3000002 || EV == 0x3000003 || pick(16) ? fc : 0);	/* events 2 / 3 do not check it */
	P32(sd + T32_IS_PRIV, priv);
	if (EV == 0x3000002 && pick(6) == 0) {	/* invalid subdev / private block */
		i = pick(4);
		P32(i < 2 ? PAD : sd + T32_IS_PRIV, i & 1 ? 0 : 0xfffff000U + pick(0x1000));
	}
	mode = pick(2) ? 1 : pick(4);
	P32(priv + T32_FD_MODE, mode);
	P32(priv + T32_IS_STOP, pick(3) ? 0 : rnd());
	P32(priv + T32_IS_STARTED, pick(2) ? 0 : rnd());
	P32(priv + T32_IS_STREAMOFF, pick(3) ? 1 : pick(3));
	P32(fc + T32_FC_FRAMES, pick(8) ? pick(12) : pick(2) ? (u32)-2 - pick(3) : rnd() % 64);
	P32(fc + T32_FC_SLOT, pick(4) ? pick(2) : pick(4));
	P32(fc + T32_FC_NBUF, pick(5) ? pick(2) : pick(4));
	P32(fc + T32_FC_STREAM, pick(4) ? pick(4) : pick(2) ? 4 : rnd());
	P32(fc + T32_FC_SRC, W + 0x1a00);
	P32(W + 0x1a00 + 1148, pick(3) ? pick(2) : rnd());
	P32(fc + T32_FC_POOL, pick(3) ? W + 0x8000 : 0);
	P32(fc + 260, pick(2) ? 0 : rnd());
	list(fc + T32_FC_DROPQ, W + 0x3000, EV == 0x3000003 && mode != 1);
	list(fc + T32_FC_QUEUE, W + 0x3400, EV == 0x3000003 && mode == 1);
	list(fc + T32_FC_FREEQ, W + 0x3800, EV == 0x3000005 && mode != 1);
	for (i = 0; i < 116; i += 4)
		P32(DATA + i, rnd() % 4 ? rnd() % 0x2000 : rnd());
	world[0x2000 + 52] = pick(2) ? 0 : 1 + pick(255);
	world[0x2000 + 72] = pick(2) ? 0 : 1 + pick(255);
	world[0x2000 + 92] = pick(2) ? 0 : 1 + pick(255);
	P32(DATA, pick(4) ? pick(8) : rnd());
	for (i = 0; i < 88; i++)
		t32_data_d8ce0[i] = rnd() % 4 ? rnd() % 64 : rnd();
	if (pick(4))
		P32(DATA + 12, t32_data_d8ce0[11 * pick(8) + 8]);
	else
		P32(DATA + 12, pick(2) ? rnd() % 64 : rnd());
	t32_data_d9014[0] = pick(4) ? pick(6) : rnd();
	t32_data_d9008[0] = pick(3) ? 1 + pick(2) : 0;
	t32_data_f3354[0] = rnd();
	t32_data_f3774[0] = pick(6);
	t32_data_f3340[0] = pick(2) ? t32_data_f3774[0] - 1 : pick(6);
	t32_data_f3778[0] = pick(6);
	t32_data_f333c[0] = pick(2) ? t32_data_f3778[0] - 1 : pick(6);
	t32_data_f3750[0] = rnd();
	t32_data_f374c[0] = rnd();
	t32_data_f3808[0] = pick(5);
	t32_data_f3804[0] = pick(5);
	t32_data_f3810[0] = rnd();
	t32_data_f380c[0] = rnd();
	if ((EV == 0x3000005 || EV == 0x3000001) && pick(16) == 0)
		DATA = 0;
	memset(&lg, 0, sizeof(lg));
	ss = (seed ^ 0x9e3779b1u) | 1;
	rs = save;
}

static u64 ours(u32 pad, u32 ev, u32 data, u32 a3)
{
	return (u32)t32_ispcore_pad_event_handle(&ops, pad, ev, data, a3);
}

static void fin(void)
{
	lg.bss[0] = t32_data_d9014[0];
	lg.bss[1] = t32_data_d9008[0];
	lg.bss[2] = t32_data_f3354[0];
	lg.bss[3] = t32_data_f3340[0];
	lg.bss[4] = t32_data_f333c[0];
	lg.bss[5] = t32_data_f3774[0];
	lg.bss[6] = t32_data_f3778[0];
	lg.bss[7] = t32_data_f3750[0];
	lg.bss[8] = t32_data_f374c[0];
	lg.bss[9] = t32_data_f3808[0];
	lg.bss[10] = t32_data_f3804[0];
	lg.bss[11] = hash(T32O_PTR(t32_data_d8ce0), 352);
	lg.bss[12] = t32o_log.printfs;
}

int main(void)
{
	unsigned int it, compared = 0, failed = 0;
	struct t32_pe_glob g = {
		t32_data_d9014, t32_data_d9008, t32_data_f3354,
		{ t32_data_f3340, t32_data_f333c }, { t32_data_f3774, t32_data_f3778 }, { t32_data_f3750, t32_data_f374c },
		{ t32_data_f3808, t32_data_f3804 }, { t32_data_f3810, t32_data_f380c },
		T32O_PTR(t32_data_d8ce0), T32O_PTR(t32_data_c312c),
		{ T32O_PTR(t32_data_ccdf8), T32O_PTR(t32_data_cce1c), T32O_PTR(t32_data_ccebc), T32O_PTR(t32_data_cce64),
		  T32O_PTR(t32_data_ccfa4), T32O_PTR(t32_data_ccf18), T32O_PTR(t32_data_ccf38), T32O_PTR(t32_data_ccf64),
		  T32O_PTR(t32_data_ccf7c) },
		{ T32O_PTR(t32_data_c3370), T32O_PTR(t32_data_c3354), T32O_PTR(t32_data_c3338) },
	};

	glob = g;
	for (it = 0; it < 40000; it++) {
		u32 seed = rnd();
		int r, rr;

		build(seed);
		r = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ours, PAD, EV, DATA, A3, 0);
		fin();
		lg_ours = lg;
		memcpy(snap, world, WSZ);
		build(seed);
		rr = (int)(u32)t32o_call((t32o_fn)(void (*)(void))ispcore_pad_event_handle, PAD, EV, DATA, A3, 0);
		fin();
		compared++;
		if (r != rr || memcmp(&lg, &lg_ours, sizeof(lg)) || memcmp(snap, world, WSZ)) {
			if (failed++ < 5)
				printf("t32_isppad_oracle: MISMATCH seed %08x ev %x: %d/%d log %u/%u\n", seed, EV, r, rr, lg_ours.n, lg.n);
			if (getenv("T32O_DBG") && failed < 6) {
				unsigned int j;

				for (j = 0; j < MAXL && j < lg.n; j++)
					if (memcmp(lg.e[j], lg_ours.e[j], 16))
						printf("  log %u: id %u/%u %x/%x %x/%x %x/%x\n", j, lg_ours.e[j][0], lg.e[j][0],
						       lg_ours.e[j][1], lg.e[j][1], lg_ours.e[j][2], lg.e[j][2], lg_ours.e[j][3], lg.e[j][3]);
				for (j = 0; j < WSZ; j++)
					if (snap[j] != world[j])
						printf("  world +%x: %02x/%02x\n", j, snap[j], world[j]);
				for (j = 0; j < 13; j++)
					if (lg.bss[j] != lg_ours.bss[j])
						printf("  bss %u: %x/%x\n", j, lg_ours.bss[j], lg.bss[j]);
			}
		}
	}
	printf("t32_isppad_oracle: %u cases compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
