// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: sensor-info registry (driver/t32/tx_isp_t32_txsinfo.h)
 * against the vendor tx_isp_sinfo_init, _exit, _driver_add, _driver_del,
 * _sensor_bind, _sensor_unbind (Matt Davis' recovered C, PR #15 @ 2e2e06bc,
 * symbols renamed op3_* by op3_ren.sh).
 *
 * Per case random slots (used flags 0 / 1 / other, driver ids from a small
 * set so that matches happen, bound sensors 0 or set, proc entries), proc
 * directory 0 or set, scripted proc_mkdir result.  Compared: the event log
 * (mutex, proc and publish hooks with their arguments, names as hashes,
 * prints), the 640 slot bytes, the directory global, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txsinfo.h"

#define NCASE	30000

u8 op3_t32_data_d9024[640] __attribute__((aligned(16)));	/* .bss: slots */
u8 op3_t32_data_ce270[32];					/* mutex */
u32 op3_t32_data_d9020;						/* .bss: /proc/jz/sensor */
u8 op3_t32_data_bd58c[64];					/* fops of "count" (address compared) */
char op3_t32_data_c37a4[] = "jz/sensor";					/* data.c:6670 */
char op3_t32_data_c37b0[] = "\0014tx-isp-sinfo: cannot create /proc/jz/sensor\n";	/* data.c:6673 */
char op3_t32_data_c37e0[] = "count";						/* data.c:6685 */
char op3_t32_data_c374c[] = "\0014tx-isp-sinfo: no free sensor slot for %s\n";	/* data.c:6648 */

u64 tx_isp_sinfo_init(u32, u32, u32, u32);
u64 tx_isp_sinfo_exit(u32, u32, u32, u32);
u64 tx_isp_sinfo_driver_add(u32, u32, u32, u32);
u64 tx_isp_sinfo_driver_del(u32, u32, u32, u32);
u64 tx_isp_sinfo_sensor_bind(u32, u32, u32, u32);
u64 tx_isp_sinfo_sensor_unbind(u32, u32, u32, u32);

static u32 mkdir_ret;
static u8 slots0[640], name0[64];
static u32 dir0;

static u32 fnv(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}

static u32 mkdir_rec(const char *n, u32 p) { op3_call("proc_mkdir", 2, fnv(n), p, 0, 0); return mkdir_ret; }
static u32 create_rec(const char *n, u32 mode, u32 p, u32 fops, u32 data)
{
	u32 a[5] = { fnv(n), mode, p, fops, data };

	op3_callv("proc_create_data", 5, a);
	return 0x1234;
}
static void rmtree_rec(const char *n, u32 p) { op3_call("remove_proc_subtree", 2, fnv(n), p, 0, 0); }

OP3_STUB(mutex_lock, 1)
OP3_STUB(mutex_unlock, 1)
OP3_STUB(sinfo_slot_publish, 2)
u64 op3_proc_mkdir(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return mkdir_rec(T32O_HOST(a0), a1); }
u64 op3_proc_create_data(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 op3_proc_create_data(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return create_rec(T32O_HOST(a0), a1, a2, a3, a4); }
u64 op3_remove_proc_subtree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; rmtree_rec(T32O_HOST(a0), a1); return 0; }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }

void t32_tx_os_mutex_lock(u32 m) { op3_call("mutex_lock", 1, m, 0, 0, 0); }
void t32_tx_os_mutex_unlock(u32 m) { op3_call("mutex_unlock", 1, m, 0, 0, 0); }
void t32_tx_os_sinfo_slot_publish(u32 s, u32 i) { op3_call("sinfo_slot_publish", 2, s, i, 0, 0); }
void t32_tx_os_remove_proc_subtree(const char *n, u32 p) { rmtree_rec(n, p); }
u32 t32_tx_os_proc_mkdir(const char *n, u32 p) { return mkdir_rec(n, p); }
u32 t32_tx_os_proc_create_data(const char *n, u32 m, u32 p, u32 f, u32 d) { return create_rec(n, m, p, f, d); }

enum { F_INIT, F_EXIT, F_ADD, F_DEL, F_BIND, F_UNBIND, F_N };
static const char *const fname[F_N] = { "init", "exit", "driver_add", "driver_del", "sensor_bind", "sensor_unbind" };
typedef u64 (*vfn)(u32, u32, u32, u32);
static const vfn vend[F_N] = {
	tx_isp_sinfo_init, tx_isp_sinfo_exit, tx_isp_sinfo_driver_add, tx_isp_sinfo_driver_del,
	tx_isp_sinfo_sensor_bind, tx_isp_sinfo_sensor_unbind
};

static struct t32_tx_sinfo si;
static int cur_f;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a3; (void)a4;
	switch (cur_f) {
	case F_INIT: return (u32)t32_tx_sinfo_init(&si);
	case F_EXIT: t32_tx_sinfo_exit(&si); return 0;
	case F_ADD: return (u32)t32_tx_sinfo_driver_add(&si, a0, a1, a2);
	case F_DEL: t32_tx_sinfo_driver_del(&si, a0); return 0;
	case F_BIND: return (u32)t32_tx_sinfo_sensor_bind(&si, a0, a1);
	default: t32_tx_sinfo_sensor_unbind(&si, a0); return 0;
	}
}

struct snap {
	struct op3_log log;
	u8 slots[640];
	u32 dir, ret;
};
static struct snap sv, so;
static const u32 ids[4] = { 0x11, 0x22, 0x33, 0x44 };

int main(void)
{
	unsigned int i, k;
	static const int has_ret[F_N] = { 1, 0, 1, 0, 1, 0 };
	u8 *drv;

	for (i = 0; i < NCASE; i++) {
		int f = (int)(i % F_N);
		u32 m = op3_rnd(), a0, a1, a2;

		for (k = 0; k < 640; k++)
			slots0[k] = (u8)op3_rnd();
		for (k = 0; k < T32_SINFO_SLOTS; k++) {
			u8 *s = slots0 + k * T32_SINFO_SLOT_BYTES;
			u32 r = op3_rnd();

			s[0] = (r & 3) == 0 ? 0 : (r & 3) == 1 ? (u8)op3_rnd() : 1;
			*(u32 *)(s + T32_SINFO_ID) = ids[(r >> 2) & 3];
			*(u32 *)(s + T32_SINFO_SENSOR) = (r & 0x30) ? 0 : 0x5000 + (r >> 8) % 4;
			*(u32 *)(s + T32_SINFO_DRV) = 0x7000 + (r >> 12) % 3;
			*(u32 *)(s + T32_SINFO_PROC) = (r & 0x10000) ? 0 : op3_rnd();
			snprintf((char *)s + T32_SINFO_NAME, 16, "s%u", k);
		}
		if (m & 0x100)	/* all slots used */
			for (k = 0; k < T32_SINFO_SLOTS; k++)
				slots0[k * T32_SINFO_SLOT_BYTES] = 1;
		dir0 = (m & 3) ? op3_rnd() : 0;
		mkdir_ret = (m & 4) ? 0 : op3_rnd();
		a0 = 0; a1 = op3_rnd(); a2 = ids[(m >> 4) & 3];
		t32o_reset();
		drv = t32o_alloc(64);
		for (k = 0; k < 64; k++)
			drv[k] = (u8)op3_rnd();
		snprintf((char *)name0, sizeof(name0), "drv%u", m & 7);
		*(u32 *)(drv + T32_SINFO_DRV_NAME) = T32O_PTR(name0);
		switch (f) {
		case F_ADD: a0 = (m & 0x30) ? T32O_PTR(drv) : 0; break;
		case F_DEL: a0 = 0x7000 + (m >> 8) % 4; break;
		case F_BIND: a0 = (m & 0x200) ? 0 : 0x5000 + (m >> 10) % 5; a1 = ids[(m >> 13) & 3] + ((m & 0x8000) ? 1 : 0); break;
		case F_UNBIND: a0 = (m & 0x200) ? 0 : 0x5000 + (m >> 10) % 5; break;
		}

		memset(&op3_log, 0, sizeof(op3_log));
		memcpy(op3_t32_data_d9024, slots0, 640);
		op3_t32_data_d9020 = dir0;
		sv.ret = OP3_VC(vend[f], a0, a1, a2, 0, 0);
		sv.log = op3_log;
		memcpy(sv.slots, op3_t32_data_d9024, 640);
		sv.dir = op3_t32_data_d9020;

		memset(&op3_log, 0, sizeof(op3_log));
		memcpy(op3_t32_data_d9024, slots0, 640);	/* ours works on the same slot array */
		si.slots = T32O_PTR(op3_t32_data_d9024);
		si.lock = T32O_PTR(op3_t32_data_ce270);
		si.dir = dir0;
		si.fops = T32O_PTR(op3_t32_data_bd58c);
		cur_f = f;
		so.ret = OP3_VC(ours, a0, a1, a2, 0, 0);
		so.log = op3_log;
		memcpy(so.slots, op3_t32_data_d9024, 640);
		so.dir = si.dir;

		if (op3_log_cmp(fname[f], &sv.log, &so.log))
			return 1;
		if ((has_ret[f] && sv.ret != so.ret) || memcmp(sv.slots, so.slots, 640) || sv.dir != so.dir) {
			fprintf(stderr, "%s: ret %x/%x, slots or dir differ\n", fname[f], sv.ret, so.ret);
			return 1;
		}
	}
	printf("t32_txsinfo_oracle: %u cases equal (%u functions)\n", NCASE, F_N);
	return 0;
}
