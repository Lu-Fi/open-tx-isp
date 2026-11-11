// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: IVDC probe (driver/t32/tx_isp_t32_txivdc.h) against the
 * vendor tx_isp_ivdc_probe (Matt Davis' recovered C, PR #15 @ 2e2e06bc,
 * symbols renamed op3_* by op3_ren.sh).  Inputs: PM / fast-start / link
 * mode globals, scripted kmalloc / subdev_init (which also sets 0..3 in and
 * out pads) / misc_register results.  The IVDC callbacks are shared
 * symbols (ours alias the vendor's); the misc name is compared as a
 * string.  Compared: the event log, the device and pad bytes, the ops
 * table, the globals, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txivdc.h"

#define NCASE	30000
#define ARENA	0x1000u
#define O_DEV	0x000u
#define O_PDEV	0x200u
#define O_PD	0x300u
#define O_PADS	0x400u		/* 2 x 3 pads of 36 bytes */

u32 op3_t32_data_d9004, op3_t32_data_d9008, op3_t32_data_d9014, op3_t32_data_d98b0, op3_t32_data_d9890, op3_t32_data_d9894;
u32 op3_t32_data_cf274[10], op3_t32_data_cf208[4], op3_t32_data_cf19c[8];
u8 op3_t32_small_bss[64];
static u32 iops[8];
char op3_t32_data_c552c[] = "Failed to allocate ivdc device\n";	/* data.c:8560 */
char op3_t32_data_c554c[] = "[%s %d] IVDC malloc is NULL\n";		/* data.c:8568 */
char op3_t32_data_be440[] = "tx_isp_ivdc_probe";			/* data.c:1331 */
char op3_t32_data_c556c[] = "Failed to init isp module(%d.%d)\n";	/* data.c:8576 */
char op3_t32_data_c5590[] = "&ivdc->mlock";				/* data.c:8585 */
char op3_t32_data_c55a0[] = "misc-ivdc";				/* data.c:8589 */
char op3_t32_data_c55ac[] = "Failed to register isp-ivdc!\n";		/* data.c:8592 */
u64 tx_isp_ivdc_probe(u32, u32, u32, u32);

#define OP3_FN(n, ours) \
	int ours(u32 sd); \
	int ours(u32 sd) { (void)sd; return 0; } \
	__asm__(".globl op3_" #n "\n.set op3_" #n ", " #ours);
OP3_FN(ivdc_suspend_module, t32_tx_ivdc_suspend_module)
OP3_FN(ivdc_resume_early_module, t32_tx_ivdc_resume_early_module)
OP3_FN(ivdc_resume_module, t32_tx_ivdc_resume_module)
OP3_FN(ivdc_prepare_module, t32_tx_ivdc_prepare_module)
OP3_FN(ivdc_complete_module, t32_tx_ivdc_complete_module)
int t32_tx_ivdc_pad_event_handle(u32 pad, u32 ev, u32 arg) { (void)pad; (void)ev; (void)arg; return 0; }
__asm__(".globl op3_ivdc_pad_event_handle\n.set op3_ivdc_pad_event_handle, t32_tx_ivdc_pad_event_handle");

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 km_fail, n_km, init_ret, misc_ret, npad[2];
static u32 fnv(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}
static u32 km_rec(u32 n, u32 f)
{
	u32 k = n_km++;

	op3_call("private_kmalloc", 2, n, f, 0, 0);
	return (km_fail & (1u << k)) ? 0 : k ? A(0x800) : A(O_DEV);
}
static u32 init_rec(u32 p, u32 sd, u32 ops)
{
	op3_call("tx_isp_subdev_init", 3, p, sd, ops, 0);
	*(u16 *)T32O_HOST(sd + T32_IVDC_NPAD_IN - T32_IVDC_SD) = (u16)npad[0];
	*(u16 *)T32O_HOST(sd + T32_IVDC_NPAD_OUT - T32_IVDC_SD) = (u16)npad[1];
	*(u32 *)T32O_HOST(sd + T32_IVDC_PAD_IN - T32_IVDC_SD) = A(O_PADS);
	*(u32 *)T32O_HOST(sd + T32_IVDC_PAD_OUT - T32_IVDC_SD) = A(O_PADS + 3 * T32_PAD_BYTES);
	return init_ret;
}
static void mutex_rec(u32 l, const char *n, u32 k) { op3_call("private_raw_mutex_init", 3, l, fnv(n), k, 0); }
u64 op3_private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return km_rec(a0, a1); }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_tx_isp_subdev_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return init_rec(a0, a1, a2); }
u64 op3_private_raw_mutex_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; mutex_rec(a0, T32O_HOST(a1), a2); return 0; }
u64 op3_private_misc_register(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; op3_call("private_misc_register", 1, a0, 0, 0, 0); return misc_ret; }
OP3_STUB(kfree, 1)
OP3_STUB(private_platform_set_drvdata, 2)
OP3_STUB(private_spin_lock_init, 1)
OP3_STUB(private_init_completion, 1)
u32 t32_tx_os_kmalloc(u32 n, u32 f) { return km_rec(n, f); }
void t32_tx_os_kfree_plain(u32 p) { op3_call("kfree", 1, p, 0, 0, 0); }
void t32_tx_os_memset(u32 a, u32 c, u32 n) { memset(T32O_HOST(a), (int)c, n); }
void t32_tx_os_set_drvdata(u32 p, u32 d) { op3_call("private_platform_set_drvdata", 2, p, d, 0, 0); }
int t32_tx_os_subdev_init(u32 p, u32 sd, u32 ops) { return (int)init_rec(p, sd, ops); }
void t32_tx_os_spin_lock_init(u32 l) { op3_call("private_spin_lock_init", 1, l, 0, 0, 0); }
void t32_tx_os_raw_mutex_init(u32 l, const char *n, u32 k) { mutex_rec(l, n, k); }
void t32_tx_os_init_completion(u32 c) { op3_call("private_init_completion", 1, c, 0, 0, 0); }
int t32_tx_os_misc_register(u32 m) { return (int)op3_private_misc_register(m, 0, 0, 0); }

static u8 tmpl[ARENA];
static struct t32_tx_ivdc_env env;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a1; (void)a2; (void)a3; (void)a4; return (u32)t32_tx_ivdc_probe(&env, a0); }

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 iops[8], g[3], ret;
};
static struct snap sv, so;
static u32 iops0[8];

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_km = 0;
	memcpy(iops, iops0, sizeof(iops));
	op3_t32_data_cf274[4] = T32O_PTR(iops);
}

int main(void)
{
	unsigned int i, k;
	u32 base, pm, fs, lm, g0[3];

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		u32 m = op3_rnd();

		for (k = 0; k < ARENA; k += 4)
			*(u32 *)(tmpl + k) = op3_rnd();
		*(u32 *)(tmpl + O_PDEV + 88) = base + O_PD;
		for (k = 0; k < 8; k++)
			iops0[k] = op3_rnd();
		pm = (m & 1) ? 0 : op3_rnd();
		fs = (m & 2) ? 0 : op3_rnd();
		lm = op3_rnd();
		km_fail = (m & 0x1c) == 0 ? 1 : (m & 0x1c) == 4 ? 2 : 0;
		init_ret = (m & 0x60) ? 0 : op3_rnd();
		misc_ret = (m & 0x380) ? op3_rnd() & 0xff : (u32)-(int)(op3_rnd() % 20 + 1);
		npad[0] = (m >> 10) % 4;
		npad[1] = (m >> 12) % 4;
		for (k = 0; k < 3; k++)
			g0[k] = op3_rnd();

		setup();
		op3_t32_data_d9004 = pm; op3_t32_data_d9008 = fs; op3_t32_data_d9014 = lm;
		op3_t32_data_d98b0 = g0[0]; op3_t32_data_d9890 = g0[1]; op3_t32_data_d9894 = g0[2];
		sv.ret = OP3_VC(tx_isp_ivdc_probe, base + O_PDEV, 0, 0, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		memcpy(sv.iops, iops, sizeof(iops));
		sv.g[0] = op3_t32_data_d98b0; sv.g[1] = op3_t32_data_d9890; sv.g[2] = op3_t32_data_d9894;

		setup();
		env.pm_on = pm; env.fast_start = fs; env.link_mode = lm;
		env.lock_key = T32O_PTR(op3_t32_small_bss); env.ops = T32O_PTR(op3_t32_data_cf274);
		env.cfg = T32O_PTR(op3_t32_data_cf208); env.fops = T32O_PTR(op3_t32_data_cf19c);
		env.buf = g0[0]; env.dev = g0[1]; env.mode = g0[2];
		so.ret = OP3_VC(ours, base + O_PDEV, 0, 0, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		memcpy(so.iops, iops, sizeof(iops));
		so.g[0] = env.buf; so.g[1] = env.dev; so.g[2] = env.mode;
		if (*(u32 *)(so.w + 4) == T32_TX_A("misc-ivdc") && *(u32 *)(sv.w + 4) == T32O_PTR(op3_t32_data_c55a0))
			*(u32 *)(so.w + 4) = T32O_PTR(op3_t32_data_c55a0);

		if (op3_log_cmp("ivdc_probe", &sv.log, &so.log))
			return 1;
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA) || memcmp(sv.iops, so.iops, sizeof(sv.iops)) ||
		    memcmp(sv.g, so.g, sizeof(sv.g))) {
			fprintf(stderr, "ivdc_probe: ret %x/%x, world / ops / globals differ\n", sv.ret, so.ret);
			return 1;
		}
	}
	printf("t32_txivdc_oracle: %u cases equal\n", NCASE);
	return 0;
}
