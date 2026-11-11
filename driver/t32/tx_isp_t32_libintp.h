/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core interrupt top half and dispatch of the ISP library.
 *
 *   t32_lib_intp_handler   lib_tisp_intp_handler 0xac2c0: read and acknowledge the six ISP core interrupt status
 *                          words (st[0..5]; st[5] also acknowledged at 0x2202c, st[2] only when nonzero), count
 *                          the set bits (per bit of st[1], st[2], st[5]; bit 0 of st[3] / st[0]; bits 4 / 5 of
 *                          st[4]), copy the 24-byte status to out; returns out
 *   t32_lib_intp_process   lib_tisp_intp_process 0xac538: for the 96 interrupt bits (st[0], st[3], st[4]) call
 *                          the registered handler of each set bit with (ch & 0xff); bits 0..31 also count in
 *                          cnt0[]; returns 1, or the last handler result that is not 1
 *   t32_lib_intp_static_clear  tisp_intp_static_clear 0xacd50: clear the counters (beyond vendor, #281)
 *
 * The vendor .bss block t32_data_e3cd0 .. e3efc is struct t32_intp_glob; the handler table t32_data_e3b50
 * (96 entries, set through system_irq_func_set) is a separate array here.
 * Hooks: struct t32_lib_hooks (tx_isp_t32_libwrap.h).
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C, PR #15) by tests/t32_oracle (t32_libintp_oracle).
 */
#ifndef TX_ISP_T32_LIBINTP_H
#define TX_ISP_T32_LIBINTP_H

#include "tx_isp_t32_libwrap.h"
#ifndef __KERNEL__
#include <string.h>
#endif

#define T32_INTP_WORDS		6
#define T32_INTP_BITS		96

typedef u32 (*t32_intp_fn)(u32 ch);

/* vendor .bss from t32_data_e3cd0 */
struct t32_intp_glob {
	u32 cnt_st4[2];		/* e3cd0: st[4] bit 4 / bit 5 */
	u32 st[T32_INTP_WORDS];	/* e3cd8: status of the last interrupt */
	u32 cnt0[32];		/* e3cf0: handled bits of st[0] */
	u32 cnt1[32];		/* e3d70: bits of st[1] */
	u32 cnt_b0[2];		/* e3df0: st[3] bit 0 / st[0] bit 0 */
	u32 cnt5[32];		/* e3df8: bits of st[5] */
	u32 cnt2[32];		/* e3e78: bits of st[2] */
	u32 n2;			/* e3ef8: interrupts with st[2] != 0 */
};

/* status / acknowledge register of st[i]: status at ack + 4 */
static const u32 t32_intp_ack[T32_INTP_WORDS] = { 0x30024, 0x30034, 0x30064, 0x300a4, 0x300b4, 0x300d4 };
#define T32_INTP_ACK5_EXTRA	0x2202cU

static inline void t32_intp_count(u32 *cnt, u32 bits)
{
	unsigned int i;

	for (i = 0; i < 32; i++)
		cnt[i] += (bits >> i) & 1;
}

static inline u32 t32_intp_ack_word(const struct t32_lib_hooks *h, struct t32_intp_glob *g, unsigned int i)
{
	g->st[i] = h->sys_read(h->ctx, t32_intp_ack[i] + 4);
	return h->sys_write(h->ctx, t32_intp_ack[i], g->st[i]);
}

static inline u32 *t32_lib_intp_handler(const struct t32_lib_hooks *h, struct t32_intp_glob *g, u32 *out)
{
	memset(g->st, 0, sizeof(g->st));
	t32_intp_ack_word(h, g, 0);
	t32_intp_ack_word(h, g, 1);
	t32_intp_ack_word(h, g, 3);
	t32_intp_ack_word(h, g, 4);
	t32_intp_ack_word(h, g, 5);
	h->sys_write(h->ctx, T32_INTP_ACK5_EXTRA, g->st[5]);
	g->st[2] = h->sys_read(h->ctx, t32_intp_ack[2] + 4);
	if (g->st[2]) {
		h->sys_write(h->ctx, t32_intp_ack[2], g->st[2]);
		g->n2++;
		t32_intp_count(g->cnt2, g->st[2]);
	}
	t32_intp_count(g->cnt1, g->st[1]);
	if (g->st[3] & 1)
		g->cnt_b0[0]++;
	if (g->st[0] & 1)
		g->cnt_b0[1]++;
	if (g->st[4] & 0x10)
		g->cnt_st4[0]++;
	if (g->st[4] & 0x20)
		g->cnt_st4[1]++;
	if (g->st[5])
		t32_intp_count(g->cnt5, g->st[5]);
	memcpy(out, g->st, sizeof(g->st));
	return out;
}

static inline u32 t32_lib_intp_process(struct t32_intp_glob *g, t32_intp_fn const *handler, u32 ch)
{
	unsigned int i;
	u32 word, r, ret = 1;

	ch &= 0xff;
	for (i = 0; i < T32_INTP_BITS; i++) {
		word = i < 32 ? g->st[0] : i < 64 ? g->st[3] : g->st[4];
		if (!(word & (1U << (i & 31))) || !handler[i])
			continue;
		if (i < 32)
			g->cnt0[i]++;
		r = handler[i](ch);
		if (r != 1)
			ret = r;
	}
	return ret;
}

/*
 * t32_lib_intp_static_clear  tisp_intp_static_clear 0xacd50: n2 = 0, then the counters cnt2, cnt5,
 * cnt_b0, cnt1 and cnt0 (in this order) are cleared.  BEYOND VENDOR (#281): the vendor "clears" them
 * with memcpy(dst, NULL, len), a read from address 0 (oops); ours zeroes the same bytes (memset).
 * cnt_st4 and st stay unchanged, as in the vendor.
 */
static inline void t32_lib_intp_static_clear(struct t32_intp_glob *g)
{
	g->n2 = 0;
	memset(g->cnt2, 0, sizeof(g->cnt2));
	memset(g->cnt5, 0, sizeof(g->cnt5));
	memset(g->cnt_b0, 0, sizeof(g->cnt_b0));
	memset(g->cnt1, 0, sizeof(g->cnt1));
	memset(g->cnt0, 0, sizeof(g->cnt0));
}

#endif
