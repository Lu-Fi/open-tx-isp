// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: VIN glue (driver/t32/tx_isp_t32_txvin.h) against the
 * vendor tx_isp_vin_init, _reset, _activate_subdev, _slake_subdev,
 * _prepare_module, _suspend_module, _resume_module, _complete_module
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc, symbols renamed op3_* by
 * op3_ren.sh; slake's call of tx_isp_vin_init goes to the vendor function).
 *
 * Per case one random world (VIN device, 0..5 sensors on the list with core
 * ops tables, input states, counters, notify recorder or 0, a control
 * argument) at the same address for both sides; both run on the low stack
 * of t32o_call.  Compared: the event log (mutexes, callbacks and hooks with
 * the words of their control / message blocks, prints), every world byte,
 * the return value (complete: none).
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txvin.h"

#define NCASE	30000
#define ARENA	0x2000u
#define NSNS	5u
#define O_SNS(i) (0x400u + 0x200u * (i))	/* sensor: +264 ops at +0x160, core table +0x180 */
#define O_CTL	0x1800u

char op3_t32_data_c40f4[] = "[ %s:%d ] Don't have active sensor!\n";	/* data.c:7266 */
char op3_t32_data_bda04[] = "tx_isp_vin_init";				/* data.c:676 */
char op3_t32_data_c41a8[] = "[%s %d] Enter the resume ... \n";		/* data.c:7311 */
char op3_t32_data_bda4c[] = "tx_isp_vin_resume_module";		/* data.c:694 */
char op3_t32_data_c41c8[] = "\n [%s %d] Vin is null \n";		/* data.c:7319 */
char op3_t32_data_c41e0[] = "[%s %d] Enter the resume not found\n";	/* data.c:7325 */
char op3_t32_data_bda30[] = "tx_isp_vin_suspend_module";		/* data.c:687 */
char op3_t32_data_c4204[] = "[%s %d] Enter the suspend not found\n";	/* data.c:7334 */
char op3_t32_data_bda68[] = "tx_isp_vin_complete_module";		/* data.c:701 */
char op3_t32_data_c4420[] = "\n [%s %d] Vin NULL \n";			/* data.c:7469 */
char op3_t32_data_bda14[] = "tx_isp_vin_prepare_module";		/* data.c:680 */
char op3_t32_data_c4438[] = "[%s %d] AOV push event error\n";		/* data.c:7475 */
char op3_t32_data_c4458[] = "[%s %d] Enter the prepare ... \n";	/* data.c:7483 */

u64 tx_isp_vin_init(u32, u32, u32, u32);
u64 tx_isp_vin_reset(u32, u32, u32, u32);
u64 tx_isp_vin_activate_subdev(u32, u32, u32, u32);
u64 tx_isp_vin_slake_subdev(u32, u32, u32, u32);
u64 tx_isp_vin_prepare_module(u32, u32, u32, u32);
u64 tx_isp_vin_suspend_module(u32, u32, u32, u32);
u64 tx_isp_vin_resume_module(u32, u32, u32, u32);
u64 tx_isp_vin_complete_module(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }

/* scripted results: per sensor for the core ops, per call number for the rest */
static u32 r_init[NSNS + 1], r_reset[NSNS + 1], r_seq[16], n_seq;

static u32 sns_index(u32 s)
{
	u32 i;

	for (i = 0; i < NSNS; i++)
		if (s == A(O_SNS(i)))
			return i;
	return NSNS;
}

static u32 seq(void) { return r_seq[n_seq++ & 15]; }
static u32 w(u32 p, u32 k) { return ((const u32 *)T32O_HOST(p))[k]; }

static u32 cb_init(u32 s, u32 c)
{
	op3_call("core_init", 3, s, w(c, 0), w(c, 1), 0);
	return r_init[sns_index(s)];
}
static u32 cb_reset(u32 s, u32 c)
{
	op3_call("core_reset", 3, s, w(c, 0), w(c, 1), 0);
	return r_reset[sns_index(s)];
}
static u32 cb_notify(u32 sd, u32 ev, u32 msg)
{
	u32 a[5] = { sd, ev, w(msg, 0), w(msg, 1), w(msg, 2) };

	op3_callv("vin_notify", 5, a);
	return seq();
}

static u32 push_rec(u32 a0, u32 msg)
{
	u32 a[8], k;

	a[0] = a0;
	for (k = 0; k < 7; k++)
		a[1 + k] = w(msg, k);
	op3_callv("isp_core_event_push", 8, a);
	return seq();
}
static u32 stream_rec(u32 vin, u32 c, u32 on) { return op3_call("vin_set_stream", 4, vin, w(c, 0), w(c, 1), on); }
static u32 input_rec(u32 vin, u32 c) { return op3_call("subdev_sensor_ops_set_input", 3, vin, w(c, 0), w(c, 1), 0); }

OP3_STUB(private_mutex_lock, 1)
OP3_STUB(private_mutex_unlock, 1)
OP3_STUB(subdev_sensor_ops_release_all_sensor, 1)
u64 op3_isp_core_event_push(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return push_rec(a0, a1); }
u64 op3_vin_set_stream(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return stream_rec(a0, a1, a2); }
u64 op3_subdev_sensor_ops_set_input(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return input_rec(a0, a1); }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_tx_isp_vin_init(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_vin_init(a0, a1, a2, a3); }

void t32_tx_os_mutex_lock(u32 m) { op3_call("private_mutex_lock", 1, m, 0, 0, 0); }
void t32_tx_os_mutex_unlock(u32 m) { op3_call("private_mutex_unlock", 1, m, 0, 0, 0); }
int t32_tx_os_core_event_push(u32 a0, void *msg) { return (int)push_rec(a0, T32O_PTR(msg)); }
int t32_tx_os_vin_set_stream(u32 vin, void *c, u32 on) { return (int)stream_rec(vin, T32O_PTR(c), on); }
int t32_tx_os_sensor_set_input(u32 vin, void *c) { return (int)input_rec(vin, T32O_PTR(c)); }
int t32_tx_os_sensor_release_all(u32 vin) { return (int)op3_call("subdev_sensor_ops_release_all_sensor", 1, vin, 0, 0, 0); }

/* ---- world ---- */
static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

static void build(u32 base)
{
	u32 k, i, ns = op3_rnd() % (NSNS + 1), order[NSNS], prev;

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	for (i = 0; i < NSNS; i++)
		order[i] = i;
	for (i = NSNS - 1; i > 0; i--) {	/* random list order */
		u32 j = op3_rnd() % (i + 1), t = order[i];

		order[i] = order[j];
		order[j] = t;
	}
	prev = base + T32_VIN_LIST;	/* circular list through +304 nodes */
	for (i = 0; i < ns; i++) {
		u32 node = base + O_SNS(order[i]) + T32_SNS_NODE;

		sw(prev - base, node);			/* prev->next */
		sw(node - base + 4, prev);		/* node->prev */
		prev = node;
	}
	sw(prev - base, base + T32_VIN_LIST);
	sw(T32_VIN_LIST + 4, prev);
	for (i = 0; i < NSNS; i++) {
		u32 s = O_SNS(i), m = op3_rnd();

		sw(s + T32_SD_OPS, base + s + 0x160);
		sw(s + 0x160 + T32_OPS_CORE, (m & 1) ? 0 : base + s + 0x180);
		sw(s + 0x180 + T32_CORE_INIT, (m & 2) ? 0 : T32O_PTR(cb_init));
		sw(s + 0x180 + T32_CORE_RESET, (m & 4) ? 0 : T32O_PTR(cb_reset));
		r_init[i] = (m & 0x18) == 0 ? (u32)-T32_ENOIOCTLCMD : (m & 0x18) == 8 ? op3_rnd() : 0;
		r_reset[i] = (m & 0x60) == 0 ? (u32)-T32_ENOIOCTLCMD : (m & 0x60) == 0x20 ? op3_rnd() : 0;
	}
	for (i = 0; i < T32_VIN_INPUTS; i++) {
		u32 m = op3_rnd();

		sw(T32_VIN_SENSOR(i), (m & 3) ? base + O_SNS(op3_rnd() % NSNS) : 0);
		sw(T32_VIN_STATE(i), (m >> 2) % 6);
	}
	{
		u32 m = op3_rnd();

		sw(T32_VIN_ACTIVE, (m & 3) == 3 ? op3_rnd() : m & 3);
		sw(T32_VIN_NOTIFY, (m & 4) ? 0 : T32O_PTR(cb_notify));
		sw(O_CTL, (m & 8) ? 0 : op3_rnd());
		sw(O_CTL + 4, (m >> 4) & 3);
	}
	for (k = 0; k < 16; k++) {
		u32 m = op3_rnd() & 7;

		r_seq[k] = m < 5 ? 0 : op3_rnd();
	}
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	n_seq = 0;
}

enum { F_INIT, F_RESET, F_ACT, F_SLAKE, F_PREP, F_SUSP, F_RES, F_COMP, F_N };
static const char *const fname[F_N] = {
	"vin_init", "vin_reset", "activate_subdev", "slake_subdev", "prepare_module", "suspend_module",
	"resume_module", "complete_module"
};
typedef u64 (*vfn)(u32, u32, u32, u32);
static const vfn vend[F_N] = {
	tx_isp_vin_init, tx_isp_vin_reset, tx_isp_vin_activate_subdev, tx_isp_vin_slake_subdev,
	tx_isp_vin_prepare_module, tx_isp_vin_suspend_module, tx_isp_vin_resume_module, tx_isp_vin_complete_module
};

/* ours, called through t32o_call so that its stack objects sit below 4 GiB as well */
static int cur_f;
static u64 ours(u32 vin, u32 ctl, u32 a2, u32 a3, u32 a4)
{
	(void)a2; (void)a3; (void)a4;
	switch (cur_f) {
	case F_INIT: return (u32)t32_tx_vin_init(vin, ctl);
	case F_RESET: return (u32)t32_tx_vin_reset(vin, ctl);
	case F_ACT: return (u32)t32_tx_vin_activate_subdev(vin);
	case F_SLAKE: return (u32)t32_tx_vin_slake_subdev(vin);
	case F_PREP: return (u32)t32_tx_vin_prepare_module(vin);
	case F_SUSP: return (u32)t32_tx_vin_suspend_module(vin);
	case F_RES: return (u32)t32_tx_vin_resume_module(vin);
	default: t32_tx_vin_complete_module(vin); return 0;
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
		u32 m = op3_rnd(), vin;

		build(base);
		vin = (f >= F_PREP && (m & 7) == 0) ? 0 : (f >= F_PREP && (m & 7) == 1) ? (u32)-1 - (op3_rnd() % 4095) : base;

		setup();
		memcpy(arena, tmpl, ARENA);
		sv.ret = OP3_VC(vend[f], vin, A(O_CTL), 0, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);

		setup();
		memcpy(arena, tmpl, ARENA);
		cur_f = f;
		so.ret = OP3_VC(ours, vin, A(O_CTL), 0, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);

		if (op3_log_cmp(fname[f], &sv.log, &so.log))
			return 1;
		if ((f != F_COMP && sv.ret != so.ret) || memcmp(sv.w, so.w, ARENA)) {
			u32 k;

			for (k = 0; k < ARENA && sv.w[k] == so.w[k]; k++)
				;
			fprintf(stderr, "%s: ret %x/%x, first world byte diff at 0x%x\n", fname[f], sv.ret, so.ret, k);
			return 1;
		}
	}
	printf("t32_txvin_oracle: %u cases equal (%u functions)\n", NCASE, F_N);
	return 0;
}
