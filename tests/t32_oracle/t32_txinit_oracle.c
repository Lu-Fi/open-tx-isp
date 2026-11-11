// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: module init (driver/t32/tx_isp_t32_txinit.h) against the
 * vendor tx_isp_init (Matt Davis' recovered C, PR #15 @ 2e2e06bc, symbols
 * renamed op3_* by op3_ren.sh).  Inputs: d9008 / d9004 / d9014 / f3760
 * values, hf_prepare hook or 0 with scripted result, scripted device /
 * driver register results.  Compared: the event log, the platform driver
 * object, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txinit.h"

#define NCASE	20000

u32 op3_t32_data_d9008, op3_t32_data_d9004, op3_t32_data_d9014, op3_t32_data_f3760, op3_t32_data_f3830;
u8 op3_t32_data_ce280[64], op3_t32_data_cf620[128], op3_t32_data_cf67c[64];
u32 op3_t32_data_ce22c[1];
u8 op3_t32_data_bec88[16], op3_t32_data_bec1c[16], op3_t32_data_bebb0[16], op3_t32_data_becf4[16];
char op3_t32_data_c6d3c[] = "tx_isp_riscv_hf_prepare failed\n";			/* data.c:10100 */
char op3_t32_data_c6d5c[] = "[ %s:%d ] Failed to insmod isp driver!!!\n";		/* data.c:10108 */
char op3_t32_data_be698[] = "tx_isp_init";						/* data.c:1481 */
char op3_t32_data_c6d88[] = "ispbuf_len";						/* data.c:10119 */
char op3_t32_data_c6d94[] = "ivdcbuf_len";						/* data.c:10122 */
char op3_t32_data_c6da0[] = "vpuBs";						/* data.c:10125 */
char op3_t32_data_c6da8[] = "camera_mode";						/* data.c:10127 */

u64 tx_isp_init(u32, u32, u32, u32);

static u32 hf_ret, dev_ret, drv_ret;
static u32 fnv(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}
static u32 hf_rec(void) { op3_call("hf_prepare", 0, 0, 0, 0, 0); return hf_ret; }
static u32 create_rec(const char *n, u32 mode, u32 p, u32 fops, u32 data)
{
	u32 a[5] = { fnv(n), mode, p, fops, data };

	op3_callv("proc_create_data", 5, a);
	return 0;
}
u64 op3_private_platform_device_register(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; op3_call("platform_device_register", 1, a0, 0, 0, 0); return dev_ret; }
u64 op3_private_platform_driver_register(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; op3_call("platform_driver_register", 1, a0, 0, 0, 0); return drv_ret; }
OP3_STUB(private_platform_device_unregister, 1)
u64 op3_proc_create_data(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 op3_proc_create_data(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return create_rec(T32O_HOST(a0), a1, a2, a3, a4); }
int t32_tx_os_platform_device_register(u32 p) { return (int)op3_private_platform_device_register(p, 0, 0, 0); }
int t32_tx_os_platform_driver_register(u32 p) { return (int)op3_private_platform_driver_register(p, 0, 0, 0); }
void t32_tx_os_platform_device_unregister(u32 p) { op3_call("private_platform_device_unregister", 1, p, 0, 0, 0); }
u32 t32_tx_os_proc_create_data(const char *n, u32 m, u32 p, u32 f, u32 d) { return create_rec(n, m, p, f, d); }

static struct t32_tx_init_env env;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; return (u32)t32_tx_isp_init(&env); }

int main(void)
{
	unsigned int i, k;
	u8 drv0[128];
	struct op3_log lv;
	u8 dv[128];
	u32 rv, ro;

	for (i = 0; i < NCASE; i++) {
		u32 m = op3_rnd();

		for (k = 0; k < sizeof(drv0); k++)
			drv0[k] = (u8)op3_rnd();
		op3_t32_data_ce22c[0] = op3_rnd();
		env.fast_start = (m & 1) ? 0 : op3_rnd();
		env.pm_on = (m & 2) ? 0 : op3_rnd();
		env.link_mode = (m & 4) ? (u32)((int)(op3_rnd() % 4) - 1) : op3_rnd();
		env.sensor_num = (m & 8) ? (u32)((int)(op3_rnd() % 5) - 1) : op3_rnd();
		env.hf_prepare = (m & 0x10) ? 0 : T32O_PTR(hf_rec);
		hf_ret = (m & 0x20) ? (u32)-(int)(op3_rnd() % 20 + 1) : (m & 0x400) ? 0 : op3_rnd() & 0x7fffffff;
		dev_ret = (m & 0xc0) ? 0 : op3_rnd();
		drv_ret = (m & 0x300) ? 0 : op3_rnd();
		env.pdev = T32O_PTR(op3_t32_data_ce280);
		env.pdrv = T32O_PTR(op3_t32_data_cf620);
		env.pm_ops = T32O_PTR(op3_t32_data_cf67c);
		env.cfg_src = T32O_PTR(op3_t32_data_ce22c);
		env.fops_ispbuf = T32O_PTR(op3_t32_data_bec88);
		env.fops_ivdcbuf = T32O_PTR(op3_t32_data_bec1c);
		env.fops_vpubs = T32O_PTR(op3_t32_data_bebb0);
		env.fops_mode = T32O_PTR(op3_t32_data_becf4);

		memset(&op3_log, 0, sizeof(op3_log));
		memcpy(op3_t32_data_cf620, drv0, sizeof(drv0));
		op3_t32_data_d9008 = env.fast_start; op3_t32_data_d9004 = env.pm_on;
		op3_t32_data_d9014 = env.link_mode; op3_t32_data_f3760 = env.sensor_num; op3_t32_data_f3830 = env.hf_prepare;
		rv = OP3_VC(tx_isp_init, 0, 0, 0, 0, 0);
		lv = op3_log;
		memcpy(dv, op3_t32_data_cf620, sizeof(dv));

		memset(&op3_log, 0, sizeof(op3_log));
		memcpy(op3_t32_data_cf620, drv0, sizeof(drv0));
		ro = OP3_VC(ours, 0, 0, 0, 0, 0);
		if (op3_log_cmp("tx_isp_init", &lv, &op3_log))
			return 1;
		if (rv != ro || memcmp(dv, op3_t32_data_cf620, sizeof(dv))) {
			fprintf(stderr, "tx_isp_init: ret %x/%x or driver object differs\n", rv, ro);
			return 1;
		}
	}
	printf("t32_txinit_oracle: %u cases equal\n", NCASE);
	return 0;
}
