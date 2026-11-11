// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our core reset (driver/t32/tx_isp_t32_libcore.h: lib_tisp_core_reset) against Matt Davis'
 * mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: every callee call in order (hook, arguments, state pointers mapped to the state index), the
 * .bss bytes f334c / f3358[4] / f335c (random start values), every list link (next / prev of every node and every
 * free / done list head, mapped to symbolic ids incl. LIST_POISON), the return value.
 * Inputs: single-camera word 1 / 0 / random, per state: enabled 0 / random, frame drop 0 / random, owner 0..2,
 * free and done lists with 0..3 buffers each (well-formed lists, shared node pool), the free list head NULL, an
 * empty done list whose prev is a spare node.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libcore.h"

u64 lib_tisp_core_reset(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))
#define NST	3
#define NNODE	12
#define STW	284

u8 t32_data_f334c[4], t32_data_f3358[4], t32_data_f335c[4];

static u32 rs = 0x51d2a7c3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { C_RESET = 1, C_ICLR, C_SINGLE, C_DROP, C_FCLR, C_EN, C_FWR, C_LDC, C_RCFG };
struct call { u32 id, a[4]; };
#define NC 32
static struct call vc[NC], oc[NC];
static unsigned int vn, on;
static u32 drop[NST], ret_rcfg;
static u8 *core_v, *st_v, *nodes_v;
static struct t32_core_chn st_o[NST];
static struct t32_core_fbuf nodes_o[NNODE];

static void rec(struct call *t, unsigned int *n, u32 id, u32 a0, u32 a1, u32 a2, u32 a3)
{
	if (*n < NC)
		t[*n] = (struct call){ id, { a0, a1, a2, a3 } };
	(*n)++;
}
#define VREC(...) rec(vc, &vn, __VA_ARGS__)
#define OREC(...) rec(oc, &on, __VA_ARGS__)

/* symbolic id of a vendor pointer: node k -> 1 + k, free head i -> 100 + i, done head -> 200 + i */
static u32 v_id(u32 p)
{
	u32 i;

	for (i = 0; i < NNODE; i++)
		if (p == T32O_PTR(nodes_v) + 16 * i)
			return 1 + i;
	for (i = 0; i < NST; i++) {
		if (p == T32O_PTR(st_v) + STW * i + 212)
			return 100 + i;
		if (p == T32O_PTR(st_v) + STW * i + 220)
			return 200 + i;
		if (p == T32O_PTR(st_v) + STW * i)
			return 300 + i;
	}
	return p;
}
static u32 o_id(const void *q)
{
	u32 i;

	for (i = 0; i < NNODE; i++)
		if (q == &nodes_o[i])
			return 1 + i;
	for (i = 0; i < NST; i++) {
		if (q == &st_o[i].free)
			return 100 + i;
		if (q == &st_o[i].done)
			return 200 + i;
		if (q == &st_o[i])
			return 300 + i;
	}
	return (u32)(uintptr_t)q;
}
static u32 *vw(u8 *b, u32 off) { return (u32 *)(b + off); }

/* vendor side callees */
u64 tisp_reset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; VREC(C_RESET, a0 == T32O_PTR(core_v), 0, 0, 0); return rnd(); }
u64 lib_tisp_intp_clear(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; VREC(C_ICLR, 0, 0, 0, 0); return rnd(); }
u64 __ispcore_single_cam_check_frame_drop(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; VREC(C_SINGLE, a0, 0, 0, 0); return rnd(); }
u64 __ispcore_get_frame_drop(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 i = v_id(a0) - 300;

	(void)a1; (void)a2; (void)a3;
	VREC(C_DROP, i, 0, 0, 0);
	return i < NST ? drop[i] : 0;
}
u64 tisp_msca_addr_fifo_clear_reg_byindex(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; VREC(C_FCLR, a0, 0, 0, 0); return rnd(); }
u64 tisp_msca_chx_en(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; VREC(C_EN, a0, a1, a2, 0); return rnd(); }
u64 tisp_msca_addr_fifo_write(u32 a0, u32 a1, u32 a2, u32 a3) { VREC(C_FWR, a0, a1, a2, a3); return rnd(); }
u64 tisp_ldc_reg_reflash(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; VREC(C_LDC, a0, 0, 0, 0); return rnd(); }
u64 tisp_rcfg_cfg_trig(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; VREC(C_RCFG, a0, 0, 0, 0); return ret_rcfg; }

/* our hooks */
static void o_reset(void *c, void *core) { (void)c; OREC(C_RESET, core == (void *)core_v, 0, 0, 0); }
static void o_iclr(void *c) { (void)c; OREC(C_ICLR, 0, 0, 0, 0); }
static void o_single(void *c, u32 a) { (void)c; OREC(C_SINGLE, a, 0, 0, 0); }
static u32 o_drop(void *c, struct t32_core_chn *st)
{
	u32 i = o_id(st) - 300;

	(void)c;
	OREC(C_DROP, i, 0, 0, 0);
	return i < NST ? drop[i] : 0;
}
static void o_fclr(void *c, u32 a) { (void)c; OREC(C_FCLR, a, 0, 0, 0); }
static void o_en(void *c, u32 s, u32 ch, u32 en) { (void)c; OREC(C_EN, s, ch, en, 0); }
static void o_fwr(void *c, u32 s, u32 ch, u32 a0, u32 a1) { (void)c; OREC(C_FWR, s, ch, a0, a1); }
static void o_ldc(void *c, u32 a) { (void)c; OREC(C_LDC, a, 0, 0, 0); }
static u32 o_rcfg(void *c, u32 a) { (void)c; OREC(C_RCFG, a, 0, 0, 0); return ret_rcfg; }

/* append node k to the list with head offset hoff of state i on both sides */
static void add_tail(u32 i, u32 hoff, u32 k)
{
	u8 *hv = st_v + STW * i + hoff, *nv = nodes_v + 16 * k, *pv;
	struct t32_core_list *ho = hoff == 212 ? &st_o[i].free : &st_o[i].done, *no = &nodes_o[k].list;

	pv = (u8 *)(uintptr_t)*vw(hv, 4);
	*vw(nv, 0) = T32O_PTR(hv);
	*vw(nv, 4) = T32O_PTR(pv);
	*vw(pv, 0) = T32O_PTR(nv);
	*vw(hv, 4) = T32O_PTR(nv);
	no->next = ho;
	no->prev = ho->prev;
	ho->prev->next = no;
	ho->prev = no;
}

static int links_equal(void)
{
	u32 i;

	for (i = 0; i < NNODE; i++)
		if (v_id(*vw(nodes_v + 16 * i, 0)) != o_id(nodes_o[i].list.next) ||
		    v_id(*vw(nodes_v + 16 * i, 4)) != o_id(nodes_o[i].list.prev))
			return 0;
	for (i = 0; i < NST; i++)
		if (v_id(*vw(st_v + STW * i, 212)) != o_id(st_o[i].free.next) ||
		    v_id(*vw(st_v + STW * i, 216)) != o_id(st_o[i].free.prev) ||
		    v_id(*vw(st_v + STW * i, 220)) != o_id(st_o[i].done.next) ||
		    v_id(*vw(st_v + STW * i, 224)) != o_id(st_o[i].done.prev))
			return 0;
	return 1;
}

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000;
	unsigned int compared = 0, failed = 0;
	struct t32_core_hooks h = { NULL, o_reset, o_iclr, o_single, o_drop, o_fclr, o_en, o_fwr, o_ldc, o_rcfg };
	struct t32_core_glob og;

	for (c = 0; c < n; c++) {
		u32 single, r, o, k, nk = 0, bad;

		t32o_reset();
		vn = on = 0;
		memset(vc, 0, sizeof(vc));
		memset(oc, 0, sizeof(oc));
		core_v = t32o_alloc(1200);
		st_v = t32o_alloc(STW * NST);
		nodes_v = t32o_alloc(16 * NNODE);
		for (i = 0; i < 1200; i++)
			core_v[i] = (u8)rnd();
		for (i = 0; i < STW * NST; i++)
			st_v[i] = (u8)rnd();
		single = c % 7 == 0 ? 1 : (c % 7 == 1 ? rnd() : 0);
		*vw(core_v, 1196) = single;
		*vw(core_v, 1132) = T32O_PTR(st_v);
		for (i = 0; i < NNODE; i++) {
			*vw(nodes_v + 16 * i, 0) = rnd() | 1;
			*vw(nodes_v + 16 * i, 4) = rnd() | 1;
			nodes_o[i].list.next = (void *)(uintptr_t)*vw(nodes_v + 16 * i, 0);
			nodes_o[i].list.prev = (void *)(uintptr_t)*vw(nodes_v + 16 * i, 4);
			nodes_o[i].addr0 = *vw(nodes_v + 16 * i, 8) = rnd();
			nodes_o[i].addr1 = *vw(nodes_v + 16 * i, 12) = rnd();
		}
		for (i = 0; i < NST; i++) {
			u8 *s = st_v + STW * i;

			st_o[i].owner = *vw(s, 116) = rnd() % NST;
			st_o[i].enabled = *vw(s, 208) = c % 5 == 0 ? 0 : (rnd() % 4 ? rnd() | 1 : 0);
			*vw(s, 212) = *vw(s, 216) = T32O_PTR(s + 212);
			*vw(s, 220) = *vw(s, 224) = T32O_PTR(s + 220);
			st_o[i].free.next = st_o[i].free.prev = &st_o[i].free;
			st_o[i].done.next = st_o[i].done.prev = &st_o[i].done;
			for (k = rnd() % 4; k && nk < NNODE; k--)
				add_tail(i, 212, nk++);
			for (k = rnd() % 4 ? 0 : rnd() % 4; k && nk < NNODE; k--)
				add_tail(i, 220, nk++);
			if (c % 11 == 0 && st_o[i].free.next == &st_o[i].free) {	/* NULL free head (vendor check) */
				*vw(s, 212) = 0;
				st_o[i].free.next = NULL;
			}
			drop[i] = rnd() % 3 ? 0 : rnd();
		}
		/* inconsistent empty done list (prev not the head): the vendor appends after done.prev unchecked */
		for (i = 0; i < NST && nk < NNODE; i++)
			if (c % 6 == 0 && st_o[i].done.next == &st_o[i].done) {
				*vw(st_v + STW * i, 224) = T32O_PTR(nodes_v) + 16 * (NNODE - 1);
				st_o[i].done.prev = &nodes_o[NNODE - 1].list;
				break;
			}
		memcpy(t32_data_f334c, (u32[]){ rnd() }, 4);
		memcpy(t32_data_f3358, (u32[]){ rnd() }, 4);
		memcpy(t32_data_f335c, (u32[]){ rnd() }, 4);
		og.sensor = t32_data_f334c[0];
		memcpy(og.flags, t32_data_f3358, 4);
		og.flag5c = t32_data_f335c[0];
		ret_rcfg = rnd();

		r = (u32)t32o_call(F(lib_tisp_core_reset), T32O_PTR(core_v), rnd(), rnd(), rnd(), 0);
		o = t32_lib_core_reset(&h, &og, core_v, single, st_o);
		bad = r != o || vn != on || vn > NC || memcmp(vc, oc, sizeof(vc)) || !links_equal() ||
		      og.sensor != t32_data_f334c[0] || memcmp(og.flags, t32_data_f3358, 4) || og.flag5c != t32_data_f335c[0];
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH case %u single %x: ret %x/%x calls %u/%u links %d\n", c, single, r, o, vn, on,
			       links_equal());
	}
	printf("t32_libcore_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
