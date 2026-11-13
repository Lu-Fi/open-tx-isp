// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: core one-shot tick (driver/t32/tx_isp_t32_txtick.h)
 * against the vendor tx_isp_core_oneshot_tick_isra_18 (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc, symbols renamed op3_* by op3_ren.sh).
 * Inputs: oneshot flag, 0..5 channels with started flags, 0..4 sensors,
 * per channel 0..3 ready buffers and 0..2 queued buffers (or a NULL queue
 * head), scripted lock flags.  Compared: the event log, every world byte
 * (lists, flags), the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txtick.h"

#define NCASE	30000
#define ARENA	0x3000u
#define O_SD	0x000u
#define O_CORE	0x100u		/* +4284 */
#define O_CHAN	0x1400u		/* 5 x 284 */
#define O_BUF	0x2000u		/* 32 nodes of 16 bytes */

u32 t32_tx_isp_dev, t32_tx_g_d9008;
void t32_tx_isp_irq_handle(void) {}
void t32_tx_isp_irq_thread_handle(void) {}
u64 tx_isp_core_oneshot_tick_isra_18(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 flags_v;
static u32 lock_rec(u32 l) { op3_call("spin_lock_irqsave", 1, l, 0, 0, 0); return flags_v; }
u64 op3___private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; *(u32 *)T32O_HOST(a1) = lock_rec(a0); return 0; }
OP3_STUB(private_spin_unlock_irqrestore, 2)
OP3_STUB(lib_tisp_msca_chx_stream_on, 1)
OP3_STUB(tisp_msca_addr_fifo_write, 4)
OP3_STUB(tisp_msca_chx_en, 3)
OP3_STUB(tisp_ldc_reg_reflash, 1)
OP3_STUB(tisp_rcfg_cfg_trig, 1)
u32 t32_tx_os_spin_lock_irqsave(u32 l) { return lock_rec(l); }
void t32_tx_os_spin_unlock_irqrestore(u32 l, u32 f) { op3_call("private_spin_unlock_irqrestore", 2, l, f, 0, 0); }
void t32_tx_os_msca_chx_stream_on(u32 c) { op3_call("lib_tisp_msca_chx_stream_on", 1, c, 0, 0, 0); }
void t32_tx_os_msca_addr_fifo_write(u32 a0, u32 c, u32 y, u32 uv) { op3_call("tisp_msca_addr_fifo_write", 4, a0, c, y, uv); }
void t32_tx_os_msca_chx_en(u32 a0, u32 c, u32 on) { op3_call("tisp_msca_chx_en", 3, a0, c, on, 0); }
void t32_tx_os_ldc_reg_reflash(u32 a0) { op3_call("tisp_ldc_reg_reflash", 1, a0, 0, 0, 0); }
void t32_tx_os_rcfg_cfg_trig(u32 a0) { op3_call("tisp_rcfg_cfg_trig", 1, a0, 0, 0, 0); }

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

/* a circular list at head (offset) with n nodes from the node pool */
static void mklist(u32 base, u32 head, u32 n, u32 *pool)
{
	u32 prev = head, k;

	for (k = 0; k < n; k++) {
		u32 node = O_BUF + 16 * (*pool)++;

		sw(prev, base + node);		/* prev->next (the head's next is +0) */
		sw(node + 4, base + prev);
		prev = node;
	}
	sw(prev, base + head);
	sw(head + 4, base + prev);
}

static void build(u32 base)
{
	u32 k, pool = 0, m = op3_rnd();

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	sw(O_SD + T32_SD_PRIV, base + O_CORE);
	sw(O_CORE + T32_CORE_ONESHOT, (m & 1) ? 1 : op3_rnd() % 3);
	sw(O_CORE + T32_CORE_CHANS, base + O_CHAN);
	sw(O_CORE + T32_CORE_NCHAN, (m >> 1) % 6);
	sw(O_CORE + T32_CORE_SNS_NUM, (m >> 4) % 5);
	for (k = 0; k < 5; k++) {
		u32 ch = O_CHAN + k * T32_CHAN_BYTES, r = op3_rnd();

		sw(ch + T32_CHAN_STARTED, (r & 1) ? 0 : op3_rnd());
		mklist(base, ch + T32_CHAN_READY, (r >> 1) % 4, &pool);
		if ((r & 0x30) == 0x30)
		{
			sw(ch + T32_CHAN_QUEUE, 0);	/* NULL next of the queue head, prev = the head (the vendor appends there) */
			sw(ch + T32_CHAN_QUEUE + 4, base + ch + T32_CHAN_QUEUE);
		}
		else
			mklist(base, ch + T32_CHAN_QUEUE, (r >> 4) % 3, &pool);
	}
	flags_v = op3_rnd();
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
}

static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a1; (void)a2; (void)a3; (void)a4; return (u32)t32_tx_core_oneshot_tick(a0); }

int main(void)
{
	unsigned int i;
	u32 base, rv, ro;
	struct op3_log lv;
	static u8 wv[ARENA];

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		build(base);
		setup();
		rv = OP3_VC(tx_isp_core_oneshot_tick_isra_18, base + O_SD, 0, 0, 0, 0);
		lv = op3_log;
		memcpy(wv, arena, ARENA);
		setup();
		ro = OP3_VC(ours, base + O_SD, 0, 0, 0, 0);
		if (op3_log_cmp("core_oneshot_tick", &lv, &op3_log))
			return 1;
		if (rv != ro || memcmp(wv, arena, ARENA)) {
			fprintf(stderr, "core_oneshot_tick: ret %x/%x or world differs\n", rv, ro);
			return 1;
		}
	}
	printf("t32_txtick_oracle: %u cases equal\n", NCASE);
	return 0;
}
