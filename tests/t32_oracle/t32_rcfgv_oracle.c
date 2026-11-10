// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the register-list vendor flow of tx_isp_t32_rcfgv.h
 * (tisp_rcfg_init, _deinit, _reg_init, tisp_single_lsc_rcfg_reg_init,
 * tisp_rcfg_cfg_trig, tisp_single_lsc_rcfg_cfg_trig, tisp_single_lsc_reg_write
 * / _lut_write) against the vendor functions (Matt Davis' recovered C, PR #15
 * @ 2e2e06bc; the two LSC writers against env.c's disassembly-based copies).
 *
 * Each side allocates from its own arena with the same bump allocator, both
 * pre-filled with the same garbage, so every allocation has the same offset.
 * Per run: pipes 1 / other, init of channel 0 and a random set of channels
 * 1..3, random LSC writes, cfg_trig / lsc_cfg_trig of random channels with
 * random shadow-mode bytes, deinit.  Compared after every call: return value
 * (addresses as arena offsets; not for reg_init and the init of a channel other than 0), both arenas word by word (a word that holds
 * an address or physical address must differ exactly by the arena distance),
 * the shadow-mode byte, every register write, kmalloc sizes/flags, kfree
 * order, cache-size and dma_sync calls, printf count.
 *
 * Build-time renames (fix_op11_rcfg.sh): env.c also defines
 * tisp_single_lsc_rcfg_cfg_trig (stub) and private_dma_cache_sync (silent);
 * the reference copies use ref_tisp_single_lsc_rcfg_cfg_trig and
 * op11_dma_cache_sync.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_rcfgv.h"

extern u64 tisp_rcfg_init(u32, u32, u32, u32), tisp_rcfg_deinit(u32, u32, u32, u32),
	   tisp_rcfg_reg_init(u32, u32, u32, u32), tisp_single_lsc_rcfg_reg_init(u32, u32, u32, u32),
	   tisp_rcfg_cfg_trig(u32, u32, u32, u32), ref_tisp_single_lsc_rcfg_cfg_trig(u32, u32, u32, u32),
	   tisp_single_lsc_reg_write(u32, u32, u32, u32), tisp_single_lsc_reg_lut_write(u32, u32, u32, u32);
char t32_data_c79d0[4] __attribute__((weak)) = "f";

static u32 rs = 0x7c3a9e15u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define AR (512u << 10)
struct arena { u8 *base; u32 used; };
static struct arena av, ao;
static void *bump(struct arena *a, u32 size)
{
	void *p;

	if (a->used + size > AR)
		return NULL;
	p = a->base + a->used;
	a->used += (size + 15) & ~15u;
	return p;
}

/* hook log, the same shape on both sides */
#define LOGN 64
struct hlog { unsigned int n; u32 e[LOGN][3]; };
static struct hlog vl, ol;
static void hrec(struct hlog *l, u32 a, u32 b, u32 c)
{
	if (l->n < LOGN) {
		l->e[l->n][0] = a; l->e[l->n][1] = b; l->e[l->n][2] = c;
	}
	l->n++;
}
enum { K_KMALLOC = 1, K_KFREE, K_CSZ, K_SYNC, K_PRINT };

/* offset of an arena address (or its KSEG0 physical alias), else the value itself */
static u32 aoff(const struct arena *a, u32 v)
{
	u32 b = T32O_PTR(a->base);

	if (v - b < AR)
		return v - b + 0x10000000u;
	if (v - (b + T32_RCFGV_KSEG0) < AR)
		return v - b - T32_RCFGV_KSEG0 + 0x20000000u;
	return v;
}

/* vendor stubs */
u64 private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	hrec(&vl, K_KMALLOC, a0, a1);
	return T32O_PTR(bump(&av, a0));
}
u64 private_kfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	hrec(&vl, K_KFREE, aoff(&av, a0), 0);
	return 0;
}
u64 lib_tisp_dma_cache_32(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	hrec(&vl, K_CSZ, a0, 0);
	return (a0 + 31) & ~31u;
}
u64 op11_dma_cache_sync(u32 a0, u32 a1, u32 a2, u32 a3)
{
	hrec(&vl, K_SYNC, aoff(&av, a1), a2);
	if (a0 || a3)
		hrec(&vl, 0xdead, a0, a3);
	return 0;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	hrec(&vl, K_PRINT, a0, a1 == T32O_PTR(t32_data_c79d0));
	return 0;
}

/* ours */
static struct { unsigned int n; struct { u32 reg, val; } w[64]; } owr;
static int fail_alloc_o;
static void *o_kmalloc(u32 size, u32 flags)
{
	hrec(&ol, K_KMALLOC, size, flags);
	if (fail_alloc_o && --fail_alloc_o == 0)
		return NULL;
	return bump(&ao, size);
}
static void o_kfree(void *p) { hrec(&ol, K_KFREE, aoff(&ao, T32O_PTR(p)), 0); }
static u32 o_csz(u32 size) { hrec(&ol, K_CSZ, size, 0); return (size + 31) & ~31u; }
static void o_sync(u32 addr, u32 size) { hrec(&ol, K_SYNC, aoff(&ao, addr), size); }
static void o_wr(u32 reg, u32 val)
{
	if (owr.n < 64) {
		owr.w[owr.n].reg = reg;
		owr.w[owr.n].val = val;
	}
	owr.n++;
}
static void o_print(void) { hrec(&ol, K_PRINT, 2, 1); }
static const struct t32_rcfgv_ops ops = {
	.kmalloc = o_kmalloc, .kfree = o_kfree, .cache_size = o_csz, .dma_sync = o_sync,
	.reg_write = o_wr, .print_nomem = o_print,
};
static struct t32_rcfgv r = { .ops = &ops };

static const char *what;
static unsigned int run, step, nwords;
static int check(u32 rv, u32 ro)
{
	unsigned int i;
	const u32 *wv = (const u32 *)av.base, *wo = (const u32 *)ao.base;

	if (aoff(&av, rv) != aoff(&ao, ro)) {
		printf("t32_rcfgv_oracle: run %u step %u %s: return vendor 0x%x ours 0x%x (arenas 0x%x 0x%x)\n", run, step, what, rv, ro, T32O_PTR(av.base), T32O_PTR(ao.base));
		return 1;
	}
	if (av.used != ao.used || vl.n != ol.n || memcmp(&vl, &ol, sizeof(vl))) {
		printf("t32_rcfgv_oracle: run %u step %u %s: allocations %u/%u, hook log %u/%u %s\n", run, step, what,
		       av.used, ao.used, vl.n, ol.n, memcmp(&vl, &ol, sizeof(vl)) ? "differs" : "equal");
		return 1;
	}
	if (t32_data_d03a0[0] != r.shadow || aoff(&av, t32_data_da844[0]) != aoff(&ao, T32O_PTR(r.root))) {
		printf("t32_rcfgv_oracle: run %u step %u %s: shadow byte %u/%u or root differs\n", run, step, what,
		       t32_data_d03a0[0], r.shadow);
		return 1;
	}
	if (t32o_log.n != owr.n) {
		printf("t32_rcfgv_oracle: run %u step %u %s: writes %u/%u\n", run, step, what, t32o_log.n, owr.n);
		return 1;
	}
	for (i = 0; i < owr.n; i++)
		if (t32o_log.w[i].reg != owr.w[i].reg || aoff(&av, t32o_log.w[i].val) != aoff(&ao, owr.w[i].val)) {
			printf("t32_rcfgv_oracle: run %u step %u %s write %u: vendor 0x%x=0x%x ours 0x%x=0x%x\n", run, step,
			       what, i, t32o_log.w[i].reg, t32o_log.w[i].val, owr.w[i].reg, owr.w[i].val);
			return 1;
		}
	for (i = 0; i < av.used / 4; i++)
		if (wv[i] != wo[i] && aoff(&av, wv[i]) != aoff(&ao, wo[i])) {
			printf("t32_rcfgv_oracle: run %u step %u %s: arena word %u vendor 0x%x ours 0x%x\n", run, step, what,
			       i, wv[i], wo[i]);
			return 1;
		}
	nwords += av.used / 4;
	return 0;
}

#define CALL(f, a0, a1) (u32)t32o_call((t32o_fn)(void (*)(void))(f), a0, a1, rnd(), rnd(), 0)

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 300;
	unsigned int cmp = 0, k, refused = 0;
	u8 *ip;

	for (run = 0; run < n; run++) {
		u32 pipes = rnd() % 2 ? 1 : (rnd() % 3 ? 2 : rnd()), live = 1, rv, ro, ch;
		int fail = run % 10 == 9 ? 1 + (int)(rnd() % 3) : 0;

		t32o_reset();
		av.base = t32o_alloc(AR);
		ao.base = t32o_alloc(AR);
		av.used = ao.used = 0;
		for (k = 0; k < AR; k++)
			av.base[k] = ao.base[k] = (u8)(k * 0x9d + (k >> 8) + run);
		ip = t32o_alloc(1024);
		memcpy(ip + 900, &pipes, 4);
		t32_data_d03a0[0] = r.shadow = (u8)rnd();
		t32_data_da844[0] = 0;
		r.root = NULL;
		for (step = 0; step < 40; step++) {
			memset(&vl, 0, sizeof(vl));
			memset(&ol, 0, sizeof(ol));
			memset(&owr, 0, sizeof(owr));
			t32o_log.n = 0;
			if (step == 0) {
				what = "init ch0";
				if (fail) {	/* every 10th run: a failed kmalloc; the vendor then memsets NULL (not run), ours refuses */
					fail_alloc_o = fail;
					ro = (u32)t32_rcfgv_init(&r, 0, pipes);
					fail_alloc_o = 0;
					refused++;
					if (ro != (u32)-1 || ol.n != (unsigned int)fail + 1 + (fail > 1) ||
					    ol.e[ol.n - 1][0] != K_PRINT) {
						printf("t32_rcfgv_oracle: run %u failed kmalloc %d: not refused\n", run, fail);
						return 1;
					}
					break;
				}
				rv = CALL(tisp_rcfg_init, 0, T32O_PTR(ip));
				ro = (u32)t32_rcfgv_init(&r, 0, pipes);
			} else if (step < 4) {
				ch = step;
				if (rnd() % 3 == 0)
					continue;
				what = "init chN";
				rv = CALL(tisp_rcfg_init, ch, T32O_PTR(ip));
				ro = (u32)t32_rcfgv_init(&r, ch, pipes);
				live |= 1u << ch;
				rv = ro;	/* the vendor's v0 here is a left-over address register (see the header) */
			} else if (step == 39) {
				what = "deinit";
				rv = CALL(tisp_rcfg_deinit, rnd(), rnd());
				ro = (u32)t32_rcfgv_deinit(&r);
			} else {
				u32 kind = rnd() % 6, idx, val, off;

				do
					ch = rnd() % 4;
				while (!(live >> ch & 1));
				if (rnd() % 4 == 0) {	/* the shadow byte as other code leaves it */
					t32_data_d03a0[0] = r.shadow = (u8)(rnd() % 4 ? rnd() % 2 : rnd());
				}
				idx = rnd() % 3000;
				val = rnd();
				off = rnd() % 600;
				switch (kind) {
				case 0:
				case 1:
					what = "lsc_reg_write";
					if (r.root->virt[ch][T32_RCFGV_SLOT_LSC] == 0)
						continue;
					rv = (u32)tisp_single_lsc_reg_write(ch, idx, val, rnd());
					t32_rcfgv_lsc_reg_write(&r, ch, idx, val);
					ro = rv;
					break;
				case 2:
					what = "lsc_reg_lut_write";
					if (r.root->virt[ch][T32_RCFGV_SLOT_LSC] == 0)
						continue;
					rv = (u32)tisp_single_lsc_reg_lut_write(ch, idx, val, off);
					t32_rcfgv_lsc_reg_lut_write(&r, ch, idx, val, off);
					ro = rv;
					break;
				case 3:
					what = "cfg_trig";
					if (r.root->virt[ch][T32_RCFGV_SLOT_MAIN] == 0 && t32_data_d03a0[0] == 1)
						continue;
					rv = CALL(tisp_rcfg_cfg_trig, ch, rnd());
					ro = (u32)t32_rcfgv_cfg_trig(&r, ch);
					break;
				case 4:
					what = "lsc_cfg_trig";
					rv = CALL(ref_tisp_single_lsc_rcfg_cfg_trig, ch, rnd());
					ro = (u32)t32_rcfgv_lsc_cfg_trig(&r, ch);
					break;
				default: {	/* reg_init again on an existing list (headers over written values) */
					u32 slot = rnd() % 2 ? T32_RCFGV_SLOT_MAIN : T32_RCFGV_SLOT_LSC;
					u32 *lv = T32O_HOST(*(u32 *)T32O_HOST(t32_data_da844[0] + 8 + ch * 256 + slot * 4));

					if (!lv)
						continue;
					what = slot == T32_RCFGV_SLOT_MAIN ? "reg_init" : "lsc_reg_init";
					rv = CALL(slot == T32_RCFGV_SLOT_MAIN ? tisp_rcfg_reg_init : tisp_single_lsc_rcfg_reg_init,
						  ch, rnd());
					if (slot == T32_RCFGV_SLOT_MAIN)
						t32_rcfgv_main_reg_init(t32_rcfgv_list(&r, ch, slot));
					else
						t32_rcfgv_lsc_reg_init(t32_rcfgv_list(&r, ch, slot));
					ro = rv = 0;	/* void in ours; the vendor's v0 is a left-over address register */
					break;
				}
				}
			}
			if (check(rv, ro))
				return 1;
			cmp++;
		}
	}
	printf("t32_rcfgv_oracle: %u calls compared (%u runs, %u refused on a failed kmalloc; %u arena words), 0 mismatches\n",
	       cmp, n, refused, nwords);
	return 0;
}
