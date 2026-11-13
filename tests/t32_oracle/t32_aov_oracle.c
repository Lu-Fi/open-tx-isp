// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the AOV / IPC / defog sleep helpers of
 * driver/t32/tx_isp_t32_aov.h against Matt Davis' recovered vendor C (ref/,
 * PR #15 @ 2e2e06bc): tisp_aov_rcfg_sleep / _wake_up, tisp_ipc_sleep /
 * _wake_up, tsip_defog_sleep, tisp_isp_pipe_null_down_interrupt,
 * handle_aov_ispsleep (built through fix_op8_glue.sh for its ll/sc loop).
 *
 * Compared: every register read (address, order; both sides get the same
 * scripted values), every register write (address, value, order; d03a0 = 1:
 * the shadow index maps one to one to our address, t32o_reg_eq, and the
 * channel tag equals ours), the saved words, flags, the device image of
 * handle_aov_ispsleep (4096 bytes), the kernel calls and return values.
 * Inputs: random channel index / channel count (0, 1..4, 255), d03a0 0 / 1,
 * random register values, pending flag 0 / 1 / other, AOV mode 0 / 1 / other.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_aov.h"

u64 tisp_aov_rcfg_sleep(u32, u32, u32, u32);
u64 tisp_aov_rcfg_wake_up(u32, u32, u32, u32);
u64 tisp_ipc_sleep(u32, u32, u32, u32);
u64 tisp_ipc_wake_up(u32, u32, u32, u32);
u64 tsip_defog_sleep(u32, u32, u32, u32);
u64 tisp_isp_pipe_null_down_interrupt(u32, u32, u32, u32);
u64 handle_aov_ispsleep(u32, u32, u32, u32);

#define CALL(f, a) t32o_call((t32o_fn)(void (*)(void))(f), a, 0, 0, 0, 0)
enum { F_READ = 1, F_SLEEP, F_UP, F_WAKE };

/* vendor objects (.bss) */
u32 t32_data_dc608, t32_data_da840, t32_data_e14cc[6], t32_data_e20f0[11], t32_data_e3150;
u8 t32_data_e3158[4];

static u32 seed, nread_v, nread_o;
static u32 rval(u32 reg, u32 k)
{
	u32 x = seed ^ reg * 0x9e3779b1u ^ k * 0x85ebca6bu;

	x ^= x >> 15; x *= 0x2c1b3c6du; x ^= x >> 12;
	return x;
}

static u32 v_read(u32 reg)
{
	t32o8_rec(&t32o8_v, F_READ, reg, 0, 0, 0);
	return rval(reg, nread_v++);
}

static u8 *dev_v, *dev_o;
u64 lib_tisp_aov_sleep(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_SLEEP, a0, 0, 0, 0);
	return 0;
}
u64 up(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_UP, a0 - T32O_PTR(dev_v), 0, 0, 0);
	return 0;
}
u64 __wake_up(u32 a0, u32 a1, u32 a2, u32 a3)
{
	t32o8_rec(&t32o8_v, F_WAKE, a0, a1, a2, a3);
	return 0;
}

/* ours */
static struct { u32 reg, val, ch, tisp; } ow[64];
static unsigned int nw;
static u32 o_read(u32 reg)
{
	t32o8_rec(&t32o8_o, F_READ, reg, 0, 0, 0);
	return rval(reg, nread_o++);
}
static void o_write(u32 reg, u32 val) { ow[nw].reg = reg; ow[nw].val = val; ow[nw].ch = 0; ow[nw++].tisp = 0; }
static void o_twrite(u32 ch, u32 reg, u32 val) { ow[nw].reg = reg; ow[nw].val = val; ow[nw].ch = ch; ow[nw++].tisp = 1; }
static void o_sleep(int s) { t32o8_rec(&t32o8_o, F_SLEEP, (u32)s, 0, 0, 0); }
static void o_up(void *sem) { t32o8_rec(&t32o8_o, F_UP, (u32)((u8 *)sem - dev_o), 0, 0, 0); }
static void o_inc(u32 *v) { (*v)++; }
static void o_wake(void *q, u32 mode, int nr, void *key)
{
	t32o8_rec(&t32o8_o, F_WAKE, T32O_PTR(q), mode, (u32)nr, T32O_PTR(key));
}
static const struct t32_aov_ops ops = { o_read, o_write, o_twrite, o_sleep, o_up, o_inc, o_wake };

static u32 rs = 0xa0babe11u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static int writes_diff(u32 mode)
{
	unsigned int i;

	if (t32o_log.n != nw)
		return 1;
	for (i = 0; i < nw; i++) {
		u32 v = t32o_log.w[i].reg;

		if (t32o_log.w[i].val != ow[i].val || !t32o_reg_eq(v, ow[i].reg))
			return 1;
		if (mode && ow[i].tisp && (!(v & 0x80000000u) || (v >> 24 & 7) != (ow[i].ch & 7)))
			return 1;
	}
	return 0;
}

int main(void)
{
	unsigned int c, failed = 0, compared = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000;

	t32o_rd_hook = v_read;
	for (c = 0; c < n; c++) {
		u32 which = c % 7, mode = (rnd() & 1), vr = 0, orr = 0, k;
		u32 idx = (rnd() & 1) ? rnd() % 5 : rnd(), nch = (rnd() & 3) ? 1 + rnd() % 4 : rnd() % 256;
		static u32 o_saved, o_ipc[6], o_defog[11], o_val;
		static u8 o_pend;
		int bad;

		t32o_reset();
		t32o8_clear();
		nw = 0; nread_v = nread_o = 0; seed = rnd();
		t32_data_d03a0[0] = (u8)mode;
		t32_data_dc608 = nch;
		t32_data_da840 = o_saved = rnd();
		for (k = 0; k < 6; k++)
			t32_data_e14cc[k] = o_ipc[k] = rnd();
		for (k = 0; k < 11; k++)
			t32_data_e20f0[k] = o_defog[k] = rnd();
		t32_data_e3150 = o_val = rnd();
		t32_data_e3158[0] = o_pend = (rnd() % 3) ? (u8)(rnd() & 1) : (u8)rnd();
		dev_v = t32o_alloc(4096); dev_o = t32o_alloc(4096);
		for (k = 0; k < 4096; k++)
			dev_v[k] = dev_o[k] = (u8)rnd();
		*(u32 *)(dev_v + 1196) = *(u32 *)(dev_o + 1196) = (rnd() % 3) ? rnd() & 1 : rnd();
		*(u32 *)(dev_v + 4028) = *(u32 *)(dev_o + 4028) = rnd();
		switch (which) {
		case 0:
			vr = (u32)CALL(tisp_aov_rcfg_sleep, idx);
			orr = t32_aov_rcfg_sleep(&ops, &o_saved, idx, nch);
			break;
		case 1:
			vr = (u32)CALL(tisp_aov_rcfg_wake_up, 0);
			t32_aov_rcfg_wake_up(&ops, &o_saved);
			vr = orr = 0;	/* a tail call: v0 is system_reg_write's */
			break;
		case 2:
			vr = (u32)CALL(tisp_ipc_sleep, 0);
			orr = t32_ipc_sleep(&ops, o_ipc);
			break;
		case 3:
			CALL(tisp_ipc_wake_up, 0);
			t32_ipc_wake_up(&ops, o_ipc);
			break;
		case 4:
			vr = (u32)CALL(tsip_defog_sleep, 0);
			orr = t32_defog_sleep(&ops, o_defog, mode);
			break;
		case 5:
			vr = (u32)CALL(tisp_isp_pipe_null_down_interrupt, 0);
			orr = (u32)t32_isp_pipe_null_done(&ops, &o_pend, o_val);
			break;
		default:
			CALL(handle_aov_ispsleep, T32O_PTR(dev_v));
			t32_handle_aov_ispsleep(&ops, dev_o);
			break;
		}
		bad = t32o8_cmp("aov", failed < 8) || vr != orr || writes_diff(mode) || t32_data_da840 != o_saved ||
		      memcmp(t32_data_e14cc, o_ipc, sizeof(o_ipc)) || memcmp(t32_data_e20f0, o_defog, sizeof(o_defog)) ||
		      t32_data_e3158[0] != o_pend || memcmp(dev_v, dev_o, 4096);
		compared++;
		if (bad && failed++ < 8)
			printf("MISMATCH case %u (d03a0 %u idx %u nch %u): ret %08x / %08x, writes %u / %u\n", which, mode, idx,
			       nch, vr, orr, t32o_log.n, nw);
	}
	printf("t32_aov_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
