// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_core_reset of driver/t32/tx_isp_t32_creset.h against
 * Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc).
 * Compared: the calls in order (lock object and flags, core reset, IPC
 * query, the completion waited for, isp_printf format and value), the three
 * flag bytes, every byte of the core image.  Inputs: mode 0 / 1 / other,
 * core +1196 = 1 / other, +4012 0 / set, reset already running or not,
 * IPC data 0 / set, wait results 0 / -512 / other.  Exit 0 = all equal.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_creset.h"

u64 tisp_core_reset(u32, u32, u32, u32);

enum { F_LOCK = 1, F_UNLOCK, F_RESET, F_IPC, F_WAIT, F_PRINTF };
u8 t32_data_f331c[0x50] __attribute__((aligned(4)));	/* the completion, then the three flag bytes */
__asm__(".globl t32_data_f335d\n.set t32_data_f335d, t32_data_f331c + 0x41");
__asm__(".globl t32_data_f335e\n.set t32_data_f335e, t32_data_f331c + 0x42");
__asm__(".globl t32_data_f335f\n.set t32_data_f335f, t32_data_f331c + 0x43");
char t32_data_ccca4[] = "Multi-camera setup required for this interface. !!! \n";	/* data.c strings */
char t32_data_cccdc[] = "Wait interrupted by signal.\n";
char t32_data_ccd14[] = "Wait interrupted by signal!!.\n";
char t32_data_cccfc[] = "Unexpected error: %d\n";

static struct t32o8_log *cur;
static u8 *core;
static u32 r_flags, r_ipc, r_wait;
static u32 off(u32 p) { return p - T32O_PTR(core) < 0x1000 ? p - T32O_PTR(core) : p == T32O_PTR(t32_data_f331c) ? 0xf331c : p; }

u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o8_rec(cur, F_LOCK, off(a0), 0, 0, 0);
	*(u32 *)T32O_HOST(a1) = r_flags;
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; t32o8_rec(cur, F_UNLOCK, off(a0), a1, 0, 0); return 0; }
u64 lib_tisp_core_reset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_RESET, off(a0), 0, 0, 0); return 0; }
u64 lib_tisp_get_ipc_cache_data(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_IPC, 0, 0, 0, 0); return r_ipc; }
u64 private_wait_for_completion_interruptible(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	t32o8_rec(cur, F_WAIT, off(a0), 0, 0, 0);
	return r_wait;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
	u32 w[5] = { a2, a3, a4, a5, a6 };

	t32o8_rec_fmt(cur, F_PRINTF, a0, T32O_HOST(a1), w, 5);
	return 0;
}

static void o_lock(void *l, u32 *f)
{
	static u32 tmp;

	__private_spin_lock_irqsave(T32O_PTR(l), T32O_PTR(&tmp), 0, 0);
	*f = tmp;
}
static void o_unlock(void *l, u32 f) { private_spin_unlock_irqrestore(T32O_PTR(l), f, 0, 0); }
static void o_reset(u8 *c) { lib_tisp_core_reset(T32O_PTR(c), 0, 0, 0); }
static int o_ipc(void) { return (int)lib_tisp_get_ipc_cache_data(0, 0, 0, 0); }
static int o_wait(void *d) { return (int)private_wait_for_completion_interruptible(T32O_PTR(d), 0, 0, 0); }
static int o_printf(int level, const char *fmt, ...)
{
	u32 w[6];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 6);
	va_end(ap);
	t32o8_rec_fmt(cur, F_PRINTF, (u32)level, fmt, w, n);
	return 0;
}
static const struct t32_creset_ops ops = { o_lock, o_unlock, o_reset, o_ipc, o_wait, o_printf };

static u32 rs = 0xc4e5e7u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}
static u8 pick8(void) { return (u8)((rnd() % 3) ? rnd() & 1 : rnd()); }

int main(void)
{
	unsigned int c, k, failed = 0;
	static u8 init[0x1000], vend[0x1000];

	t32o_reset();
	core = t32o_alloc(0x1000);
	for (c = 0; c < 30000; c++) {
		u32 mode = (rnd() % 3) ? rnd() & 1 : rnd();
		struct t32_creset_state st = { pick8(), pick8(), pick8(), t32_data_f331c };
		int bad;

		for (k = 0; k < 0x1000; k++)
			core[k] = (u8)rnd();
		*(u32 *)(core + 1196) = (rnd() & 1) ? 1 : rnd() % 3;
		*(u32 *)(core + 4012) = (rnd() & 1) ? 0 : rnd();
		r_flags = rnd();
		r_ipc = (rnd() & 1) ? 0 : rnd();
		r_wait = (rnd() % 3) ? 0 : (rnd() & 1) ? (u32)-512 : rnd();
		t32_data_f331c[0x41] = st.ipc_pending;
		t32_data_f331c[0x42] = st.reset_req;
		t32_data_f331c[0x43] = st.reset_running;
		memcpy(init, core, 0x1000);
		t32o8_clear();
		cur = &t32o8_v;
		t32o_call((t32o_fn)(void (*)(void))tisp_core_reset, T32O_PTR(core), mode, 0, 0, 0);
		memcpy(vend, core, 0x1000);
		memcpy(core, init, 0x1000);
		cur = &t32o8_o;
		t32_tisp_core_reset(&ops, &st, core, mode);
		bad = t32o8_cmp("creset", failed < 8) || memcmp(vend, core, 0x1000) || st.ipc_pending != t32_data_f331c[0x41] ||
		      st.reset_req != t32_data_f331c[0x42] || st.reset_running != t32_data_f331c[0x43];
		if (bad && failed++ < 8)
			printf("MISMATCH mode %u multi %u\n", mode, *(u32 *)(core + 1196));
	}
	printf("t32_creset_oracle: 30000 compared, %u mismatches\n", failed);
	return failed ? 1 : 0;
}
