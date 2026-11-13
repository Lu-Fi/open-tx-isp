// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: frame-source probe (driver/t32/tx_isp_t32_txfs.h)
 * against the vendor tx_isp_fs_probe (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc, symbols renamed op3_* by op3_ren.sh).  Inputs: PM / fast-start
 * globals, scripted kmalloc / subdev_init results, 0..4 pads (the subdev
 * init stub sets the count and the pad array) with random index / enable
 * bytes.  The callbacks are shared symbols (ours alias the vendor's).
 * Compared: the event log, the frame source / channel / pad bytes, the ops
 * table, f3370, d9950, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txfs.h"

#define NCASE	20000
#define ARENA	0x2000u
#define O_FS	0x000u
#define O_PDEV	0x180u
#define O_PD	0x1c0u
#define O_PADS	0x200u		/* 4 x 36 */
#define O_CHANS	0x400u		/* 4 x 816 */

u32 op3_t32_data_d9004, op3_t32_data_d9008, op3_t32_data_d9950;
u32 op3_t32_data_cf558[10], op3_t32_data_cf4ec[4], op3_t32_data_f3370[256];
u8 op3_t32_small_bss[64];
u32 t32_tx_isp_dev, t32_tx_g_d9008;
void t32_tx_isp_irq_handle(void) {}
void t32_tx_isp_irq_thread_handle(void) {}
static u32 iops[8];
char op3_t32_data_c6834[] = "Failed to allocate csi device\n";		/* data.c:9778 */
char op3_t32_data_c6854[] = "Failed to init isp module(%d.%d)\n";	/* data.c:9786 */
char op3_t32_data_c6878[] = "&q->mlock";				/* data.c:9795 */
char op3_t32_data_c6884[] = "&chan->mlock";				/* data.c:9798 */
u64 tx_isp_fs_probe(u32, u32, u32, u32);

#define OP3_FN(n, ours) \
	int ours(u32 sd); \
	int ours(u32 sd) { (void)sd; return 0; } \
	__asm__(".globl op3_" #n "\n.set op3_" #n ", " #ours);
OP3_FN(fs_suspend_module, t32_tx_fs_suspend_module)
OP3_FN(fs_resume_module, t32_tx_fs_resume_module)
OP3_FN(fs_prepare_module, t32_tx_fs_prepare_module)
OP3_FN(fs_complete_module, t32_tx_fs_complete_module)
int t32_tx_frame_chan_event(u32 pad, u32 ev, u32 arg) { (void)pad; (void)ev; (void)arg; return 0; }
__asm__(".globl op3_frame_chan_event\n.set op3_frame_chan_event, t32_tx_frame_chan_event");

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 km_fail, n_km, init_ret, npad;
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
	return (km_fail & (1u << k)) ? 0 : k ? A(O_CHANS) : A(O_FS);
}
static u32 init_rec(u32 p, u32 sd, u32 ops)
{
	op3_call("tx_isp_subdev_init", 3, p, sd, ops, 0);
	*(u16 *)T32O_HOST(sd + T32_FS_NPAD) = (u16)npad;
	*(u32 *)T32O_HOST(sd + T32_FS_PADS) = A(O_PADS);
	return init_ret;
}
static void mutex_rec(u32 l, const char *n, u32 k) { op3_call("private_raw_mutex_init", 3, l, fnv(n), k, 0); }
u64 op3_private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return km_rec(a0, a1); }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_tx_isp_subdev_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return init_rec(a0, a1, a2); }
u64 op3_private_raw_mutex_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; mutex_rec(a0, T32O_HOST(a1), a2); return 0; }
OP3_STUB(private_kfree, 1)
OP3_STUB(private_platform_set_drvdata, 2)
OP3_STUB(private_spin_lock_init, 1)
OP3_STUB(private_init_completion, 1)
OP3_STUB(private_init_waitqueue_head, 1)
OP3_STUB(tx_isp_frame_chan_deinit, 1)
u32 t32_tx_os_kmalloc(u32 n, u32 f) { return km_rec(n, f); }
void t32_tx_os_kfree(u32 p) { op3_call("private_kfree", 1, p, 0, 0, 0); }
void t32_tx_os_memset(u32 a, u32 c, u32 n) { memset(T32O_HOST(a), (int)c, n); }
void t32_tx_os_set_drvdata(u32 p, u32 d) { op3_call("private_platform_set_drvdata", 2, p, d, 0, 0); }
int t32_tx_os_subdev_init(u32 p, u32 sd, u32 ops) { return (int)init_rec(p, sd, ops); }
void t32_tx_os_spin_lock_init(u32 l) { op3_call("private_spin_lock_init", 1, l, 0, 0, 0); }
void t32_tx_os_raw_mutex_init(u32 l, const char *n, u32 k) { mutex_rec(l, n, k); }
void t32_tx_os_init_completion(u32 c) { op3_call("private_init_completion", 1, c, 0, 0, 0); }
void t32_tx_os_init_waitqueue_head(u32 q) { op3_call("private_init_waitqueue_head", 1, q, 0, 0, 0); }

static u8 tmpl[ARENA];
static u32 iops0[8], fast0[256];
static struct t32_tx_fs_env env;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a1; (void)a2; (void)a3; (void)a4; return (u32)t32_tx_fs_probe(&env, a0); }

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 iops[8], fast[256], g, ret;
};
static struct snap sv, so;

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_km = 0;
	memcpy(iops, iops0, sizeof(iops));
	op3_t32_data_cf558[4] = T32O_PTR(iops);
	memcpy(op3_t32_data_f3370, fast0, sizeof(fast0));
}

int main(void)
{
	unsigned int i, k;
	u32 base, pm, fs, g0;

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		u32 m = op3_rnd();

		for (k = 0; k < ARENA; k += 4)
			*(u32 *)(tmpl + k) = op3_rnd();
		*(u32 *)(tmpl + O_PDEV + 88) = base + O_PD;
		for (k = 0; k < 4; k++) {
			tmpl[O_PADS + k * T32_FS_PAD_BYTES + 4] = (u8)op3_rnd();
			tmpl[O_PADS + k * T32_FS_PAD_BYTES + 5] = (op3_rnd() & 1) ? 0 : (u8)op3_rnd();
		}
		for (k = 0; k < 8; k++)
			iops0[k] = op3_rnd();
		for (k = 0; k < 256; k++)
			fast0[k] = op3_rnd();
		pm = (m & 1) ? 0 : op3_rnd();
		fs = (m & 2) ? 0 : op3_rnd();
		km_fail = (m & 0x1c) == 0 ? 1 : 0;
		init_ret = (m & 0x60) ? 0 : op3_rnd();
		npad = (m >> 8) % 5;
		g0 = op3_rnd();

		setup();
		op3_t32_data_d9004 = pm; op3_t32_data_d9008 = fs; op3_t32_data_d9950 = g0;
		sv.ret = OP3_VC(tx_isp_fs_probe, base + O_PDEV, 0, 0, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		memcpy(sv.iops, iops, sizeof(iops));
		memcpy(sv.fast, op3_t32_data_f3370, sizeof(fast0));
		sv.g = op3_t32_data_d9950;

		setup();
		env.pm_on = pm; env.fast_start = fs;
		env.lock_key = T32O_PTR(op3_t32_small_bss); env.ops = T32O_PTR(op3_t32_data_cf558);
		env.cfg = T32O_PTR(op3_t32_data_cf4ec); env.fast_chans = T32O_PTR(op3_t32_data_f3370); env.fs = g0;
		so.ret = OP3_VC(ours, base + O_PDEV, 0, 0, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		memcpy(so.iops, iops, sizeof(iops));
		memcpy(so.fast, op3_t32_data_f3370, sizeof(fast0));
		so.g = env.fs;

		if (op3_log_cmp("fs_probe", &sv.log, &so.log))
			return 1;
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA) || memcmp(sv.iops, so.iops, sizeof(sv.iops)) ||
		    memcmp(sv.fast, so.fast, sizeof(sv.fast)) || sv.g != so.g) {
			u32 j;

			for (j = 0; j < ARENA && sv.w[j] == so.w[j]; j++)
				;
			fprintf(stderr, "fs_probe: ret %x/%x, first world diff 0x%x\n", sv.ret, so.ret, j);
			return 1;
		}
	}
	printf("t32_txfs_oracle: %u cases equal\n", NCASE);
	return 0;
}
