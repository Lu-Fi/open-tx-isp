// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: interrupt glue (driver/t32/tx_isp_t32_txirq.h) against
 * the vendor tx_isp_enable_irq / tx_isp_disable_irq (0xd0d0 / 0xd0e8 and
 * 0xb977c / 0xb9800), tx_isp_request_irq, tx_isp_free_irq, tx_vic_enable_irq,
 * tx_vic_disable_irq, tx_ldc_enable_irq, tx_ldc_disable_irq (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc, symbols renamed op3_* by op3_ren.sh).
 *
 * Per case both sides start from the same random subdevice block (irq info
 * at +156, callbacks at +184 / +188 = recorders or 0), platform device,
 * scripted platform_get_irq / request_threaded_irq results and spin lock
 * flags.  Compared: the event log (hook and callback calls with their
 * arguments, prints), the subdevice and platform device bytes, the return
 * value of tx_isp_request_irq (the others are void).  The two function
 * pointers request_irq stores (+28 / +32) are compared through a map: the
 * vendor's tx_isp_enable_irq_d0d0 / _disable_irq_d0e8 <-> our
 * t32_tx_isp_irq_enable / _disable.
 * Exit 0 = all equal, 1 = mismatch, 2 = setup failure.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txirq.h"

#define NCASE	30000
#define SDB	512u

u32 op3_t32_data_d9348;		/* .bss: the VIC subdevice */
char op3_t32_data_be350[] = "tx_isp_request_irq";			/* data.c:1271 */
char op3_t32_data_c5290[] = "%s[%d] the parameters are invalid!\n";	/* data.c:8393 */
char op3_t32_data_c52b4[] = "%s[%d] Failed to request irq(%d).\n";	/* data.c:8402 */

u64 tx_isp_enable_irq_d0d0(u32, u32, u32, u32);
u64 tx_isp_disable_irq_d0e8(u32, u32, u32, u32);
u64 tx_isp_enable_irq_b977c(u32, u32, u32, u32);
u64 tx_isp_disable_irq_b9800(u32, u32, u32, u32);
u64 tx_isp_request_irq(u32, u32, u32, u32);
u64 tx_isp_free_irq(u32, u32, u32, u32);
u64 tx_vic_enable_irq(u32, u32, u32, u32);
u64 tx_vic_disable_irq(u32, u32, u32, u32);
u64 tx_ldc_enable_irq(u32, u32, u32, u32);
u64 tx_ldc_disable_irq(u32, u32, u32, u32);

/* ---- scripted hooks ---- */
static int nirq, failat, irqv[8], getirq_err;
static u32 req_ret, lock_flags;

static int get_irq_rec(u32 pdev, u32 i)
{
	op3_call("private_platform_get_irq", 2, pdev, i, 0, 0);
	return (int)i < nirq ? irqv[i] : getirq_err;
}

static u32 req_rec(u32 irq, u32 h, u32 t, u32 fl, u32 name, u32 dev)
{
	u32 a[6] = { irq, h, t, fl, name, dev };

	op3_callv("private_request_threaded_irq", 6, a);
	return (int)irq == irqv[failat < 0 ? 0 : failat] && failat >= 0 ? req_ret : 0;
}

static u32 lock_rec(u32 lock)
{
	op3_call("spin_lock_irqsave", 1, lock, 0, 0, 0);
	return lock_flags;
}

void op3_isp_irq_handle(void) {}
void op3_isp_irq_thread_handle(void) {}
void t32_tx_isp_irq_handle(void) __attribute__((alias("op3_isp_irq_handle")));
void t32_tx_isp_irq_thread_handle(void) __attribute__((alias("op3_isp_irq_thread_handle")));

OP3_STUB(private_enable_irq, 1)
OP3_STUB(private_disable_irq, 1)
OP3_STUB(private_free_irq, 2)
OP3_STUB(private_spin_lock_init, 1)
OP3_STUB(private_spin_unlock_irqrestore, 2)
OP3_STUB(lib_tisp_vic_intp_enable, 1)
OP3_STUB(lib_tisp_vic_intp_disable, 1)
u64 op3_private_platform_get_irq(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)get_irq_rec(a0, a1); }
u64 op3_private_request_threaded_irq(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);
u64 op3_private_request_threaded_irq(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) { return req_rec(a0, a1, a2, a3, a4, a5); }
u64 op3___private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	*(u32 *)T32O_HOST(a1) = lock_rec(a0);
	return 0;
}
u64 op3_tx_isp_enable_irq_d0d0(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_enable_irq_d0d0(a0, a1, a2, a3); }
u64 op3_tx_isp_disable_irq_d0e8(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_disable_irq_d0e8(a0, a1, a2, a3); }

void t32_tx_os_enable_irq(u32 irq) { op3_call("private_enable_irq", 1, irq, 0, 0, 0); }
void t32_tx_os_disable_irq(u32 irq) { op3_call("private_disable_irq", 1, irq, 0, 0, 0); }
void t32_tx_os_free_irq(u32 irq, u32 dev) { op3_call("private_free_irq", 2, irq, dev, 0, 0); }
int t32_tx_os_platform_get_irq(u32 pdev, u32 i) { return get_irq_rec(pdev, i); }
int t32_tx_os_request_threaded_irq(u32 irq, u32 h, u32 t, u32 fl, u32 name, u32 dev) { return (int)req_rec(irq, h, t, fl, name, dev); }
void t32_tx_os_spin_lock_init(u32 l) { op3_call("private_spin_lock_init", 1, l, 0, 0, 0); }
u32 t32_tx_os_spin_lock_irqsave(u32 l) { return lock_rec(l); }
void t32_tx_os_spin_unlock_irqrestore(u32 l, u32 f) { op3_call("private_spin_unlock_irqrestore", 2, l, f, 0, 0); }
void t32_tx_os_vic_intp_enable(u32 i) { op3_call("lib_tisp_vic_intp_enable", 1, i, 0, 0, 0); }
void t32_tx_os_vic_intp_disable(u32 i) { op3_call("lib_tisp_vic_intp_disable", 1, i, 0, 0, 0); }

/* the callbacks at +184 / +188 */
static void cb_en(u32 info, u32 index) { op3_call("cb_enable", 2, info, index, 0, 0); }
static void cb_dis(u32 info, u32 index) { op3_call("cb_disable", 2, info, index, 0, 0); }

/* ---- case state ---- */
static u8 sd0[SDB], pd0[16];
static u32 sd_kind, arg_idx, cb_mask;

struct snap {
	struct op3_log log;
	u8 sd[SDB], pd[16];
	u32 ret;
};
static struct snap sv, so;

enum { F_EN0, F_DIS0, F_REQ, F_FREE, F_CEN, F_CDIS, F_LEN, F_LDIS, F_VEN, F_VDIS, F_N };
static const char *const fname[F_N] = {
	"enable_irq_d0d0", "disable_irq_d0e8", "request_irq", "free_irq", "enable_irq_b977c",
	"disable_irq_b9800", "ldc_enable_irq", "ldc_disable_irq", "vic_enable_irq", "vic_disable_irq"
};

static u8 *sd, *pd;

static u32 setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	sd = t32o_alloc(SDB);
	pd = t32o_alloc(16);
	memcpy(sd, sd0, SDB);
	memcpy(pd, pd0, 16);
	*(u32 *)(sd + T32_SD_IRQ_ENABLE) = (cb_mask & 1) ? T32O_PTR(cb_en) : 0;
	*(u32 *)(sd + T32_SD_IRQ_DISABLE) = (cb_mask & 2) ? T32O_PTR(cb_dis) : 0;
	switch (sd_kind) {
	case 0: return 0;
	case 1: return (u32)-T32_MAX_ERRNO + (op3_rnd() % T32_MAX_ERRNO);
	default: return T32O_PTR(sd);
	}
}

static void take(struct snap *s, u32 ret, int ours)
{
	u32 k;

	s->log = op3_log;
	memcpy(s->sd, sd, SDB);
	memcpy(s->pd, pd, 16);
	s->ret = ret;
	if (ours)
		for (k = 0; k < SDB; k += 4) {
			u32 *w = (u32 *)(s->sd + k);

			if (*w == T32_TX_A(t32_tx_isp_irq_enable))
				*w = T32O_PTR(op3_tx_isp_enable_irq_d0d0);
			else if (*w == T32_TX_A(t32_tx_isp_irq_disable))
				*w = T32O_PTR(op3_tx_isp_disable_irq_d0e8);
		}
}

static int run_case(int f)
{
	u32 ret = 0, d, info, pa;

	d = setup();
	info = !d ? 0 : T32_IS_ERR(d) ? T32O_PTR(sd) + T32_SD_IRQ : d + T32_SD_IRQ;
	pa = (cb_mask & 4) ? 0 : T32O_PTR(pd);
	op3_t32_data_d9348 = d;
	switch (f) {
	case F_EN0: (void)OP3_VC(tx_isp_enable_irq_d0d0, T32O_PTR(sd), arg_idx % 3, 0, 0, 0); break;
	case F_DIS0: (void)OP3_VC(tx_isp_disable_irq_d0e8, T32O_PTR(sd), arg_idx % 3, 0, 0, 0); break;
	case F_REQ: ret = OP3_VC(tx_isp_request_irq, pa, info, 0, 0, 0); break;
	case F_FREE: (void)OP3_VC(tx_isp_free_irq, info, 0, 0, 0, 0); break;
	case F_CEN: (void)OP3_VC(tx_isp_enable_irq_b977c, T32O_PTR(sd), 0, 0, 0, 0); break;
	case F_CDIS: (void)OP3_VC(tx_isp_disable_irq_b9800, T32O_PTR(sd), 0, 0, 0, 0); break;
	case F_LEN: (void)OP3_VC(tx_ldc_enable_irq, d, 0, 0, 0, 0); break;
	case F_LDIS: (void)OP3_VC(tx_ldc_disable_irq, d, 0, 0, 0, 0); break;
	case F_VEN: (void)OP3_VC(tx_vic_enable_irq, arg_idx, 0, 0, 0, 0); break;
	case F_VDIS: (void)OP3_VC(tx_vic_disable_irq, arg_idx, 0, 0, 0, 0); break;
	}
	take(&sv, ret, 0);

	d = setup();
	info = !d ? 0 : T32_IS_ERR(d) ? T32O_PTR(sd) + T32_SD_IRQ : d + T32_SD_IRQ;
	pa = (cb_mask & 4) ? 0 : T32O_PTR(pd);
	ret = 0;
	switch (f) {
	case F_EN0: t32_tx_isp_irq_enable(T32O_PTR(sd), arg_idx % 3); break;
	case F_DIS0: t32_tx_isp_irq_disable(T32O_PTR(sd), arg_idx % 3); break;
	case F_REQ: ret = (u32)t32_tx_isp_request_irq(pa, info); break;
	case F_FREE: t32_tx_isp_free_irq(info); break;
	case F_CEN: t32_tx_sd_irq_enable(T32O_PTR(sd)); break;
	case F_CDIS: t32_tx_core_irq_disable(T32O_PTR(sd)); break;
	case F_LEN: t32_tx_ldc_irq_enable(d); break;
	case F_LDIS: t32_tx_ldc_irq_disable(d); break;
	case F_VEN: t32_tx_vic_irq_enable(d, arg_idx); break;
	case F_VDIS: t32_tx_vic_irq_disable(d, arg_idx); break;
	}
	take(&so, ret, 1);

	if (op3_log_cmp(fname[f], &sv.log, &so.log))
		return 1;
	if (sv.ret != so.ret || memcmp(sv.sd, so.sd, SDB) || memcmp(sv.pd, so.pd, 16)) {
		unsigned int k;

		for (k = 0; k < SDB && sv.sd[k] == so.sd[k]; k++)
			;
		fprintf(stderr, "%s: ret %x/%x, first sd byte diff %u\n", fname[f], sv.ret, so.ret, k);
		return 1;
	}
	return 0;
}

int main(void)
{
	unsigned int i, k, cnt[F_N] = { 0 };

	for (i = 0; i < NCASE; i++) {
		int f = (int)(i % F_N);
		u32 m = op3_rnd();

		for (k = 0; k < SDB; k++)
			sd0[k] = (u8)op3_rnd();
		for (k = 0; k < 16; k++)
			pd0[k] = (u8)op3_rnd();
		/* the state words the flows test: small values, 0, 1 */
		for (k = 0; k < 9; k++) {
			u32 *w = (u32 *)(sd0 + T32_SD_IRQ + 4 * k);

			switch (op3_rnd() & 3) {
			case 0: *w = 0; break;
			case 1: *w = 1; break;
			case 2: *w = op3_rnd() & 7; break;
			}
		}
		*(u32 *)(sd0 + T32_SD_IRQ + T32_IRQ_COUNT) = (m & 0x10) ? op3_rnd() % 9 - 2 : op3_rnd() % 4;
		sd_kind = (m & 3) == 0 ? 0 : (m & 3) == 1 ? 1 : 2;
		cb_mask = (m >> 2) & 3;
		if ((m & 0x70) == 0x70)
			cb_mask |= 4;		/* request_irq: no platform device */
		arg_idx = (m & 0x80) ? (u32)((int)(op3_rnd() % 7) - 2) : 0;
		nirq = (int)((m >> 8) % 7);
		for (k = 0; k < 8; k++)
			irqv[k] = (int)(op3_rnd() & 0x3ff) + 1;
		if (m & 0x4000)
			irqv[op3_rnd() % 4] = 0;
		failat = (m & 0x800) ? (int)(op3_rnd() % 4) : -1;
		req_ret = (m & 0x1000) ? (u32)-16 : op3_rnd() | 1;
		getirq_err = (m & 0x2000) ? -6 : -(int)(op3_rnd() % 100) - 1;
		lock_flags = op3_rnd();
		if (run_case(f))
			return 1;
		cnt[f]++;
	}
	printf("t32_txirq_oracle: %u cases equal (", NCASE);
	for (i = 0; i < F_N; i++)
		printf("%s%s %u", i ? ", " : "", fname[i], cnt[i]);
	printf(")\n");
	return 0;
}
