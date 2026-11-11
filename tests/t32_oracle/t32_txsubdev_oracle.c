// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: subdevice init (driver/t32/tx_isp_t32_txsubdev.h)
 * against the vendor tx_isp_subdev_init (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc, symbols renamed op3_* by op3_ren.sh; its tx_isp_module_init /
 * tx_isp_request_irq / tx_isp_free_irq / tx_isp_module_deinit calls run
 * the vendor functions, ours our ports).
 *
 * Per case a random platform device (name, platform data type 0..3 with
 * clocks / pad descriptors, 0..3 memory resources with matching or other
 * names; the last one always "isp-device", as the vendor unwind needs it),
 * 0..3 platform irqs, scripted request / ioremap / clock / kmalloc / irq
 * results.  Function pointers stored by the module / irq init are compared
 * through a vendor <-> ours map.  Compared: the event log, every world
 * byte, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txsubdev.h"

#define NCASE	30000
#define ARENA	0x2000u
#define O_PDEV	0x000u
#define O_PD	0x100u
#define O_DESC	0x140u
#define O_RES	0x180u		/* 3 x 16 */
#define O_REG	0x200u		/* regions 3 x 16 */
#define O_SD	0x400u		/* 512 */
#define O_PADS	0x800u
#define O_STR	0xc00u

u32 t32_tx_isp_dev, t32_tx_g_d9008;
char op3_t32_data_be6f8[] = "tx_isp_subdev_init";				/* data.c:1505 */
char op3_t32_data_c7344[] = "[ %s:%d ] the parameters are invalid!\n";	/* data.c:10486 */
char op3_t32_data_c736c[] = "[ %s:%d ] Failed to init isp module(%s)\n";	/* data.c:10496 */
char op3_t32_data_c7398[] = "[ %s:%d ] Failed to request irq!\n";		/* data.c:10507 */
char op3_t32_data_c73bc[] = "isp-device";					/* data.c:10516 */
char op3_t32_data_c73c8[] = "[ %s:%d ] Not enough memory for resources\n";	/* data.c:10519 */
char op3_t32_data_c73f4[] = "[ %s:%d ] Unable to ioremap registers\n";	/* data.c:10530 */
char op3_t32_data_be6dc[] = "tx_isp_subdev_device_init";			/* data.c:1498 */
char op3_t32_data_c741c[] = "[ %s:%d ] Failed to init %s's clks!\n";		/* data.c:10540 */
char op3_t32_data_c7444[] = "[ %s:%d ] Failed to malloc %s's outpads\n";	/* data.c:10550 */
char op3_t32_data_c7470[] = "[ %s:%d ] Failed to malloc %s's inpads\n";	/* data.c:10561 */
char op3_t32_data_c7498[] = "[ %s:%d ] It's header!\n";			/* data.c:10571 */
char op3_t32_data_c74b0[] = "[ %s:%d ] Failed to init subdev!\n";		/* data.c:10577 */
char op3_t32_data_be350[] = "tx_isp_request_irq";				/* data.c:1271 */
char op3_t32_data_c5290[] = "%s[%d] the parameters are invalid!\n";		/* data.c:8393 */
char op3_t32_data_c52b4[] = "%s[%d] Failed to request irq(%d).\n";		/* data.c:8402 */
char op3_t32_data_be70c[] = "tx_isp_module_init";				/* data.c:1510 */

u64 tx_isp_subdev_init(u32, u32, u32, u32);
u64 tx_isp_module_init(u32, u32, u32, u32);
u64 tx_isp_request_irq(u32, u32, u32, u32);
u64 tx_isp_free_irq(u32, u32, u32, u32);
u64 tx_isp_module_deinit(u32, u32, u32, u32);
u64 tx_isp_enable_irq_d0d0(u32, u32, u32, u32);
u64 tx_isp_disable_irq_d0e8(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 fnv(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}

static u32 nres_ok[4], req_ok[4], map_v[4], clk_ret, km_fail, n_km, nirq, req_irq_ret;
static u32 res_rec(u32 p, u32 t, u32 n) { op3_call("private_platform_get_resource", 3, p, t, n, 0); return nres_ok[n & 3] ? A(O_RES + 16 * (n & 3)) : 0; }
static u32 region_rec(u32 s, u32 n, u32 name)
{
	u32 k = (s >> 4) & 3;

	op3_call("private_request_mem_region", 3, s, n, fnv(T32O_HOST(name)), 0);
	return req_ok[k] ? A(O_REG + 16 * k) : 0;
}
static u32 map_rec(u32 s, u32 n) { op3_call("private_ioremap", 2, s, n, 0, 0); return map_v[(s >> 4) & 3]; }
static u32 km_rec(u32 n, u32 f)
{
	u32 k = n_km++;

	op3_call("private_kmalloc", 2, n, f, 0, 0);
	return (km_fail & (1u << k)) ? 0 : A(O_PADS + 0x200 * k);
}
static int irq_get(u32 p, u32 i) { op3_call("private_platform_get_irq", 2, p, i, 0, 0); return i < nirq ? (int)(40 + i) : -6; }
static u32 irq_req(u32 irq, u32 h, u32 t, u32 f, u32 name, u32 dev)
{
	u32 a[6] = { irq, h, t, f, name, dev };

	op3_callv("private_request_threaded_irq", 6, a);
	return irq == 40 + nirq - 1 ? req_irq_ret : 0;
}

void op3_isp_irq_handle(void) {}
void op3_isp_irq_thread_handle(void) {}
void t32_tx_isp_irq_handle(void) __attribute__((alias("op3_isp_irq_handle")));
void t32_tx_isp_irq_thread_handle(void) __attribute__((alias("op3_isp_irq_thread_handle")));
u64 op3_tx_isp_notify(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 op3_tx_isp_enable_irq_d0d0(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_enable_irq_d0d0(a0, a1, a2, a3); }
u64 op3_tx_isp_disable_irq_d0e8(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_disable_irq_d0e8(a0, a1, a2, a3); }
u64 op3_tx_isp_module_init(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_module_init(a0, a1, a2, a3); }
u64 op3_tx_isp_request_irq(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_request_irq(a0, a1, a2, a3); }
u64 op3_tx_isp_free_irq(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_free_irq(a0, a1, a2, a3); }
u64 op3_tx_isp_module_deinit(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_module_deinit(a0, a1, a2, a3); }
u64 op3_memcpy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memcpy(T32O_HOST(a0), T32O_HOST(a1), a2); return a0; }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_private_platform_get_resource(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return res_rec(a0, a1, a2); }
u64 op3_private_request_mem_region(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return region_rec(a0, a1, a2); }
u64 op3_private_ioremap(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return map_rec(a0, a1); }
u64 op3_isp_subdev_init_clks(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; op3_call("isp_subdev_init_clks", 2, a0, a1, 0, 0); return clk_ret; }
u64 op3_private_kmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return km_rec(a0, a1); }
u64 op3_private_platform_get_irq(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)irq_get(a0, a1); }
u64 op3_private_request_threaded_irq(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5);
u64 op3_private_request_threaded_irq(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5) { return irq_req(a0, a1, a2, a3, a4, a5); }
OP3_STUB(private_iounmap, 1)
OP3_STUB(private_release_mem_region, 2)
OP3_STUB(isp_subdev_release_clks, 1)
OP3_STUB(private_kfree, 1)
OP3_STUB(private_spin_lock_init, 1)
OP3_STUB(private_disable_irq, 1)
OP3_STUB(private_enable_irq, 1)
OP3_STUB(private_free_irq, 2)

u32 t32_tx_os_platform_get_resource(u32 p, u32 t, u32 n) { return res_rec(p, t, n); }
u32 t32_tx_os_request_mem_region_u(u32 s, u32 n, u32 name) { return region_rec(s, n, name); }
u32 t32_tx_os_ioremap(u32 s, u32 n) { return map_rec(s, n); }
void t32_tx_os_iounmap(u32 a) { op3_call("private_iounmap", 1, a, 0, 0, 0); }
void t32_tx_os_release_mem_region(u32 s, u32 n) { op3_call("private_release_mem_region", 2, s, n, 0, 0); }
int t32_tx_os_init_clks(u32 sd, u32 c) { op3_call("isp_subdev_init_clks", 2, sd, c, 0, 0); return (int)clk_ret; }
void t32_tx_os_release_clks(u32 sd) { op3_call("isp_subdev_release_clks", 1, sd, 0, 0, 0); }
u32 t32_tx_os_kmalloc(u32 n, u32 f) { return km_rec(n, f); }
void t32_tx_os_kfree(u32 p) { op3_call("private_kfree", 1, p, 0, 0, 0); }
void t32_tx_os_memset(u32 a, u32 c, u32 n) { memset(T32O_HOST(a), (int)c, n); }
void t32_tx_os_enable_irq(u32 irq) { op3_call("private_enable_irq", 1, irq, 0, 0, 0); }
void t32_tx_os_disable_irq(u32 irq) { op3_call("private_disable_irq", 1, irq, 0, 0, 0); }
void t32_tx_os_free_irq(u32 irq, u32 dev) { op3_call("private_free_irq", 2, irq, dev, 0, 0); }
int t32_tx_os_platform_get_irq(u32 p, u32 i) { return irq_get(p, i); }
int t32_tx_os_request_threaded_irq(u32 irq, u32 h, u32 t, u32 f, u32 name, u32 dev) { return (int)irq_req(irq, h, t, f, name, dev); }
void t32_tx_os_spin_lock_init(u32 l) { op3_call("private_spin_lock_init", 1, l, 0, 0, 0); }
u32 t32_tx_os_spin_lock_irqsave(u32 l) { return l; }
void t32_tx_os_spin_unlock_irqrestore(u32 l, u32 f) { (void)l; (void)f; }
void t32_tx_os_vic_intp_enable(u32 i) { (void)i; }
void t32_tx_os_vic_intp_disable(u32 i) { (void)i; }
void t32_tx_os_complete(u32 d) { (void)d; }
u32 t32_tx_os_get_drvdata(u32 d) { return d; }
int t32_tx_os_unlocked_ioctl(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
void t32_tx_os_platform_driver_unregister(u32 d) { (void)d; }
void t32_tx_os_platform_device_unregister(u32 d) { (void)d; }
void t32_tx_os_clk_prepare_enable(u32 c) { (void)c; }
void t32_tx_os_clk_disable_unprepare(u32 c) { (void)c; }
int t32_tx_os_riscv_run(void) { return 0; }
int t32_tx_os_riscv_stop(void) { return 0; }

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

static void build(u32 base)
{
	u32 k, m = op3_rnd(), nres = 1 + op3_rnd() % 3;

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	strcpy((char *)tmpl + O_STR, "isp-device");
	strcpy((char *)tmpl + O_STR + 16, "other-dev");
	strcpy((char *)tmpl + O_STR + 32, "tx-isp-mod");
	strcpy((char *)tmpl + O_STR + 48, "tx-isp-alt");
	sw(O_PDEV + 0, base + O_STR + 32);
	strcpy((char *)tmpl + O_STR + 80, "tx-isp-res");
	sw(O_PDEV + T32_PDEV_NAME_RES, base + O_STR + 80);
	sw(O_PDEV + T32_PDEV_NAME_ALT, (m & 1) ? 0 : base + O_STR + 48);
	sw(O_PDEV + T32_PDEV_DATA, (m & 0xe) == 0 ? 0 : base + O_PD);
	sw(O_PDEV + T32_PDEV_NRES, nres);
	for (k = 0; k < 3; k++) {
		u32 r = O_RES + 16 * k, s = op3_rnd() & ~0xffu;

		sw(r + 0, s | (k << 4));
		sw(r + 4, (s | (k << 4)) + op3_rnd() % 0x1000);
		sw(r + 8, base + O_STR + ((k == nres - 1 || (op3_rnd() & 1)) ? 0 : 16));
		sw(O_REG + 16 * k, s | (k << 4));
		sw(O_REG + 16 * k + 4, (s | (k << 4)) + op3_rnd() % 0x1000);
		nres_ok[k] = (k == nres - 1) || (op3_rnd() & 3);
		req_ok[k] = (op3_rnd() & 7) != 0;
		map_v[k] = (op3_rnd() & 7) ? op3_rnd() | 1 : 0;
	}
	tmpl[O_PD + 0] = (u8)(op3_rnd() % 4);
	tmpl[O_PD + 4] = (u8)op3_rnd();
	sw(O_PD + 8, op3_rnd());
	tmpl[O_PD + 12] = (u8)(op3_rnd() % 6);
	sw(O_PD + 16, base + O_DESC);
	for (k = 0; k < 6; k++) {
		tmpl[O_DESC + 2 * k] = (u8)(op3_rnd() % 4);
		tmpl[O_DESC + 2 * k + 1] = (u8)op3_rnd();
	}
	/* subdevice: zero pad counters (the vendor counts on top), irq info clear */
	*(u16 *)(tmpl + O_SD + T32_SD_NPAD_OUT) = 0;
	*(u16 *)(tmpl + O_SD + T32_SD_NPAD_IN) = 0;
	snprintf((char *)tmpl + O_STR + 64, 16, "sd-name");
	clk_ret = (op3_rnd() & 7) ? 0 : op3_rnd();
	km_fail = (op3_rnd() & 7) ? 0 : 1;
	nirq = op3_rnd() % 4;
	req_irq_ret = (op3_rnd() & 7) ? 0 : op3_rnd() | 1;
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_km = 0;
}

static u32 ops_v;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a3; (void)a4; return (u32)t32_tx_subdev_init(a0, a1, a2); }

static void canon(u8 *w)
{
	u32 k;

	for (k = 0; k < ARENA; k += 4) {
		u32 *p = (u32 *)(w + k);

		if (*p == T32_TX_A(t32_tx_isp_notify))
			*p = T32O_PTR(op3_tx_isp_notify);
		else if (*p == T32_TX_A(t32_tx_isp_irq_enable))
			*p = T32O_PTR(op3_tx_isp_enable_irq_d0d0);
		else if (*p == T32_TX_A(t32_tx_isp_irq_disable))
			*p = T32O_PTR(op3_tx_isp_disable_irq_d0e8);
	}
}

int main(void)
{
	unsigned int i;
	u32 base, rv, ro, pdev, sd;
	struct op3_log lv;
	static u8 wv[ARENA];

	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		u32 m = op3_rnd();

		build(base);
		/* the sd name the prints use */
		*(u32 *)(tmpl + O_SD + T32_SD_NAME) = base + O_STR + 64;
		pdev = (m & 0x1f) == 0 ? 0 : base + O_PDEV;
		sd = (m & 0x1f) == 1 ? 0 : base + O_SD;
		ops_v = op3_rnd();
		setup();
		rv = OP3_VC(tx_isp_subdev_init, pdev, sd, ops_v, 0, 0);
		lv = op3_log;
		memcpy(wv, arena, ARENA);
		setup();
		ro = OP3_VC(ours, pdev, sd, ops_v, 0, 0);
		canon(arena);
		/* module_init copies pdev->name into the subdevice at +8: the prints read it afterwards */
		if (op3_log_cmp("subdev_init", &lv, &op3_log))
			return 1;
		if (rv != ro || memcmp(wv, arena, ARENA)) {
			u32 j;

			for (j = 0; j < ARENA && wv[j] == arena[j]; j++)
				;
			fprintf(stderr, "subdev_init: ret %x/%x, first world diff 0x%x\n", rv, ro, j);
			return 1;
		}
	}
	printf("t32_txsubdev_oracle: %u cases equal\n", NCASE);
	return 0;
}
