// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: VIC glue (driver/t32/tx_isp_t32_txvic.h) against the
 * vendor tx_isp_vic_start, _index_cal, _reset_isra_5, _activate_subdev,
 * _slake_subdev, _suspend_module, _resume_module, _save_raw (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc, symbols renamed op3_* by op3_ren.sh;
 * resume's tx_isp_vic_index_cal calls go to the vendor function).
 *
 * Per case one random world (subdevice NULL / ERR_PTR / valid, VIC device
 * NULL / ERR_PTR / valid, sensor count 0..6, input states 0..5, clocks,
 * armed GPIOs with levels, sensor mode, reset busy flag, scripted VIC /
 * GPIO results); both sides run on the low stack.  Compared: the event log
 * (locks, clocks, VIC / GPIO hooks with their message words, prints), every
 * world byte, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txvic.h"

#define NCASE	30000
#define ARENA	0x2000u
#define O_VIC	0x400u
#define O_CLK	0x200u
#define O_SNS	0x1000u

char op3_t32_data_bd7e8[] = "tx_isp_vic_index_cal";				/* data.c:541 */
char op3_t32_data_c3dac[] = "[ %s:%d ] Can not support this sensor num!!!\n";	/* data.c:7056 */
char op3_t32_data_beb9c[] = "tx_isp_vic_save_raw";				/* data.c:1802 */
char op3_t32_data_c6e18[] = "\n[%s %d] save raw error !!!\n";			/* data.c:10155 */
char op3_t32_data_bd800[] = "tx_isp_vic_resume_module";			/* data.c:547 */
char op3_t32_data_c3d6c[] = "\n [%s %d] VIC is null \n";			/* data.c:7040 */
char op3_t32_data_c3ddc[] = "\n [%s %d] Fail to vic suspend\n";		/* data.c:7068 */
char op3_t32_data_c3dfc[] = "[%s %d] Enter the resume ... \n";		/* data.c:7076 */
char op3_t32_data_bd7cc[] = "tx_isp_vic_suspend_module";			/* data.c:534 */
char op3_t32_data_c3d84[] = "switch";						/* data.c:7046 */
char op3_t32_data_c3d8c[] = "[%s %d] Enter the suspend ... \n";		/* data.c:7048 */

u64 tx_isp_vic_start(u32, u32, u32, u32);
u64 tx_isp_vic_index_cal(u32, u32, u32, u32);
u64 tx_isp_vic_reset_isra_5(u32, u32, u32, u32);
u64 tx_isp_vic_activate_subdev(u32, u32, u32, u32);
u64 tx_isp_vic_slake_subdev(u32, u32, u32, u32);
u64 tx_isp_vic_suspend_module(u32, u32, u32, u32);
u64 tx_isp_vic_resume_module(u32, u32, u32, u32);
u64 tx_isp_vic_save_raw(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 w(u32 p, u32 k) { return ((const u32 *)T32O_HOST(p))[k]; }
static u32 flags_v, init_ret[2], n_init, par_ret, dma_ret, raw_val[8];

static u32 fnv(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}

static u32 lock_rec(u32 l) { op3_call("spin_lock_irqsave", 1, l, 0, 0, 0); return flags_v; }
static u32 init_rec(u32 idx, u32 arg, u32 msg)
{
	u32 a[6] = { idx, arg, w(msg, 0), w(msg, 1), w(msg, 2), w(msg, 3) };

	op3_callv("lib_tisp_vic_init", 6, a);
	return init_ret[n_init++ & 1];
}
static u32 par_rec(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
	u32 a[6] = { a0, a1, a2, a3, a4, a5 };

	op3_callv("lib_tisp_vic_set_par", 6, a);
	return par_ret;
}
static u32 sstream_rec(u32 sd, u32 c) { return op3_call("vic_core_s_stream", 2, sd, w(c, 0), 0, 0); }
static u32 cinit_rec(u32 sd, u32 c) { return op3_call("vic_core_ops_init", 2, sd, w(c, 0), 0, 0); }
static void greq_rec(u32 g, const char *l) { op3_call("private_gpio_request", 2, g, fnv(l), 0, 0); }
static u32 desc_rec(u32 g) { op3_call("gpio_to_desc", 1, g, 0, 0, 0); return g * 3 + 1; }
static u32 raw_rec(u32 d) { op3_call("gpiod_get_raw_value", 1, d, 0, 0, 0); return raw_val[d & 7]; }

OP3_STUB(private_spin_unlock_irqrestore, 2)
OP3_STUB(private_mutex_lock, 1)
OP3_STUB(private_mutex_unlock, 1)
OP3_STUB(private_clk_prepare_enable, 1)
OP3_STUB(private_clk_disable_unprepare, 1)
OP3_STUB(lib_tisp_vic_reset, 0)
OP3_STUB(lib_tisp_vic_stream_on, 0)
OP3_STUB(lib_tisp_vic_deinit, 0)
OP3_STUB(lib_tisp_vic_ctrl, 2)
OP3_STUB(private_gpio_direction_output, 2)
OP3_STUB(private_gpio_free, 1)
u64 op3_lib_tisp_vic_dma_start(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	op3_call("lib_tisp_vic_dma_start", 0, 0, 0, 0, 0);
	return dma_ret;
}
u64 op3___private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; *(u32 *)T32O_HOST(a1) = lock_rec(a0); return 0; }
u64 op3_lib_tisp_vic_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return init_rec(a0, a1, a2); }
u64 op3_lib_tisp_vic_set_par(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);
u64 op3_lib_tisp_vic_set_par(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) { return par_rec(a0, a1, a2, a3, a4, a5); }
u64 op3_vic_core_s_stream(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return sstream_rec(a0, a1); }
u64 op3_vic_core_ops_init(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return cinit_rec(a0, a1); }
u64 op3_private_gpio_request(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; greq_rec(a0, T32O_HOST(a1)); return 0; }
u64 op3_gpio_to_desc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return desc_rec(a0); }
u64 op3_gpiod_get_raw_value(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return raw_rec(a0); }
u64 op3_memcpy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memcpy(T32O_HOST(a0), T32O_HOST(a1), a2); return a0; }
u64 op3_tx_isp_vic_index_cal(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_vic_index_cal(a0, a1, a2, a3); }

u32 t32_tx_os_spin_lock_irqsave(u32 l) { return lock_rec(l); }
void t32_tx_os_spin_unlock_irqrestore(u32 l, u32 f) { op3_call("private_spin_unlock_irqrestore", 2, l, f, 0, 0); }
void t32_tx_os_mutex_lock(u32 m) { op3_call("private_mutex_lock", 1, m, 0, 0, 0); }
void t32_tx_os_mutex_unlock(u32 m) { op3_call("private_mutex_unlock", 1, m, 0, 0, 0); }
void t32_tx_os_clk_prepare_enable(u32 c) { op3_call("private_clk_prepare_enable", 1, c, 0, 0, 0); }
void t32_tx_os_clk_disable_unprepare(u32 c) { op3_call("private_clk_disable_unprepare", 1, c, 0, 0, 0); }
void t32_tx_os_vic_reset(void) { op3_call("lib_tisp_vic_reset", 0, 0, 0, 0, 0); }
void t32_tx_os_vic_stream_on(void) { op3_call("lib_tisp_vic_stream_on", 0, 0, 0, 0, 0); }
void t32_tx_os_vic_deinit(void) { op3_call("lib_tisp_vic_deinit", 0, 0, 0, 0, 0); }
int t32_tx_os_vic_init(u32 i, u32 a, void *m) { return (int)init_rec(i, a, T32O_PTR(m)); }
void t32_tx_os_vic_ctrl(u32 a, u32 b) { op3_call("lib_tisp_vic_ctrl", 2, a, b, 0, 0); }
int t32_tx_os_vic_set_par(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) { return (int)par_rec(a0, a1, a2, a3, a4, a5); }
int t32_tx_os_vic_dma_start(void) { return (int)op3_lib_tisp_vic_dma_start(0, 0, 0, 0); }
int t32_tx_os_vic_core_s_stream(u32 sd, void *c) { return (int)sstream_rec(sd, T32O_PTR(c)); }
int t32_tx_os_vic_core_ops_init(u32 sd, void *c) { return (int)cinit_rec(sd, T32O_PTR(c)); }
void t32_tx_os_gpio_request(u32 g, const char *l) { greq_rec(g, l); }
u32 t32_tx_os_gpio_to_desc(u32 g) { return desc_rec(g); }
u32 t32_tx_os_gpiod_get_raw_value(u32 d) { return raw_rec(d); }
void t32_tx_os_gpio_direction_output(u32 g, u32 v) { op3_call("private_gpio_direction_output", 2, g, v, 0, 0); }
void t32_tx_os_gpio_free(u32 g) { op3_call("private_gpio_free", 1, g, 0, 0, 0); }

/* unused hooks of the included headers */
void t32_tx_isp_irq_handle(void) {}
void t32_tx_isp_irq_thread_handle(void) {}
u32 t32_tx_isp_dev, t32_tx_g_d9008;

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }
static u32 errp(void) { return (u32)-1 - op3_rnd() % 4095; }

static void build(u32 base, int f, u32 m)
{
	u32 k, i, j;

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	sw(T32_SD_VIC, (f == 2 || (m & 7) > 1) ? base + O_VIC : (m & 7) == 0 ? 0 : errp());
	sw(T32_SD_CLKS, base + O_CLK);
	sw(T32_SD_NCLK, (m >> 3) % 6);
	sw(O_VIC + T32_VIC_SNS_NUM, (m & 0x100) ? op3_rnd() : (m >> 9) % 7);
	sw(O_VIC + T32_VIC_SENSOR, (m & 0x7000) ? base + O_SNS : 0);
	sw(O_SNS + T32_SNS_MODE, (m & 0x8000) ? 2 : op3_rnd() % 4);
	sw(O_VIC + T32_VIC_RESET_BUSY, (m & 0x30000) ? 0 : op3_rnd());
	for (i = 0; i < T32_VIC_INPUTS; i++)
		sw(O_VIC + T32_VIC_STATE(i), op3_rnd() % 6);
	for (i = 0; i < 2; i++)
		for (j = 0; j < 2; j++) {
			tmpl[O_VIC + T32_VIC_GPIO_ON(i, j)] = (op3_rnd() & 1) ? (u8)op3_rnd() : 0;
			sw(O_VIC + T32_VIC_GPIO_LVL(i, j), (op3_rnd() & 1) ? T32_VIC_GPIO_HIGH : op3_rnd() % 8);
		}
	for (k = 0; k < 8; k++)
		raw_val[k] = (op3_rnd() & 3) == 3 ? op3_rnd() : op3_rnd() & 1;
	flags_v = op3_rnd();
	init_ret[0] = (op3_rnd() & 3) ? 0 : op3_rnd();
	init_ret[1] = (op3_rnd() & 3) ? 0 : op3_rnd();
	par_ret = (op3_rnd() & 3) ? 0 : op3_rnd();
	dma_ret = op3_rnd();
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_init = 0;
}

enum { F_START, F_INDEX, F_RESET, F_ACT, F_SLAKE, F_SUSP, F_RES, F_RAW, F_N };
static const char *const fname[F_N] = {
	"vic_start", "vic_index_cal", "vic_reset", "activate_subdev", "slake_subdev", "suspend_module",
	"resume_module", "save_raw"
};
typedef u64 (*vfn)(u32, u32, u32, u32);
static const vfn vend[F_N] = {
	tx_isp_vic_start, tx_isp_vic_index_cal, tx_isp_vic_reset_isra_5, tx_isp_vic_activate_subdev,
	tx_isp_vic_slake_subdev, tx_isp_vic_suspend_module, tx_isp_vic_resume_module, tx_isp_vic_save_raw
};
static int cur_f;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a4;
	switch (cur_f) {
	case F_START: return (u32)t32_tx_vic_start(a0, a1);
	case F_INDEX: return (u32)t32_tx_vic_index_cal(a0, a1);
	case F_RESET: return (u32)t32_tx_vic_reset(a0);
	case F_ACT: return (u32)t32_tx_vic_activate_subdev(a0);
	case F_SLAKE: return (u32)t32_tx_vic_slake_subdev(a0);
	case F_SUSP: return (u32)t32_tx_vic_suspend_module(a0);
	case F_RES: return (u32)t32_tx_vic_resume_module(a0);
	default: return (u32)t32_tx_vic_save_raw(a0, a1, a2, a3);
	}
}

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 ret;
};
static struct snap sv, so;

int main(void)
{
	unsigned int i;
	u32 base;

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		int f = (int)(i % F_N);
		u32 m = op3_rnd(), a0, a1 = op3_rnd(), a2 = op3_rnd(), a3 = op3_rnd();

		build(base, f, m);
		if (f == F_START || f == F_INDEX)
			a0 = base + O_VIC;
		else if (f == F_RAW || f == F_RESET)
			a0 = f == F_RAW ? op3_rnd() : base;
		else
			a0 = (m & 0xc0000) == 0 ? 0 : (m & 0xc0000) == 0x40000 ? errp() : base;
		if (f == F_START)
			a1 = (m & 0x100000) ? 1 : op3_rnd() & 3;
		if (f == F_INDEX)
			a1 = (m & 0x100000) ? op3_rnd() : (u32)((int)(op3_rnd() % 9) - 4);

		setup();
		sv.ret = OP3_VC(vend[f], a0, a1, a2, a3, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		setup();
		cur_f = f;
		so.ret = OP3_VC(ours, a0, a1, a2, a3, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);

		if (op3_log_cmp(fname[f], &sv.log, &so.log))
			return 1;
		if ((sv.ret != so.ret && !(f == F_RAW && par_ret)) || memcmp(sv.w, so.w, ARENA)) {
			fprintf(stderr, "%s: ret %x/%x or world differs\n", fname[f], sv.ret, so.ret);
			return 1;
		}
	}
	printf("t32_txvic_oracle: %u cases equal (%u functions)\n", NCASE, F_N);
	return 0;
}
