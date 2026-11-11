// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: module graph (driver/t32/tx_isp_t32_txgraph.h) against
 * the vendor tx_isp_create_graph_and_nodes (Matt Davis' recovered C, PR #15
 * @ 2e2e06bc, symbols renamed op3_* by op3_ren.sh).  Inputs: 0..6 platform
 * devices mapped to random subdevices (type 0..3, parent / own id 0..15,
 * misc / proc fops or 0), d9008, scripted misc_register results.  The
 * vendor's "can't find" print takes a stale stack word as module number
 * (not compared; ours prints the index) and its later walks do not test
 * the drvdata, so every device has one here.  Compared: the event log
 * (names hashed), every world byte, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txgraph.h"

#define NCASE	30000
#define ARENA	0x1400u
#define NSD	6u
#define O_ISP	0x000u
#define O_SD(i)	(0x300u + 0x180u * (i))

u32 op3_t32_data_d9008, t32_tx_isp_dev, t32_tx_g_d9008;
void t32_tx_isp_irq_handle(void) {}
void t32_tx_isp_irq_thread_handle(void) {}
char op3_t32_data_be6a4[] = "tx_isp_create_graph_and_nodes";				/* data.c:1484 */
char op3_t32_data_c74d4[] = "[ %s:%d ] Can't find the module(%d) driver!\n";		/* data.c:10586 */
char op3_t32_data_c7504[] = "[ %s:%d ] the module(%d.%d) doesn't have parent!\n";	/* data.c:10598 */
char op3_t32_data_c7538[] = "[ %s:%d ] Failed to register tx-isp miscdev(%d.%d)!\n";	/* data.c:10611 */
u64 tx_isp_create_graph_and_nodes(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 map[NSD], misc_ret[NSD], n_misc;
static u32 fnv(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}
static u32 drv_rec(u32 p) { op3_call("private_platform_get_drvdata", 1, p, 0, 0, 0); return A(O_SD(map[p % NSD])); }
static u32 misc_rec(u32 m) { op3_call("private_misc_register", 1, m, 0, 0, 0); return misc_ret[n_misc++ % NSD]; }
static u32 proc_rec(u32 n, u32 mode, u32 par, u32 fops, u32 data)
{
	u32 a[5] = { fnv(T32O_HOST(n)), mode, par, fops, data };

	op3_callv("private_proc_create_data", 5, a);
	return 0;
}
u64 op3_private_platform_get_drvdata(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return drv_rec(a0); }
u64 op3_private_misc_register(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return misc_rec(a0); }
OP3_STUB(private_misc_deregister, 1)
u64 op3_private_proc_create_data(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 op3_private_proc_create_data(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return proc_rec(a0, a1, a2, a3, a4); }
u32 t32_tx_os_get_drvdata(u32 p) { return drv_rec(p); }
int t32_tx_os_misc_register(u32 m) { return (int)misc_rec(m); }
void t32_tx_os_misc_deregister(u32 m) { op3_call("private_misc_deregister", 1, m, 0, 0, 0); }
u32 t32_tx_os_proc_create_data_u(u32 n, u32 mode, u32 par, u32 fops, u32 data) { return proc_rec(n, mode, par, fops, data); }

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

static void build(u32 base)
{
	u32 k, n = op3_rnd() % 7;

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	sw(O_ISP + T32_ISP_NPLAT, n);
	for (k = 0; k < 8; k++)
		sw(O_ISP + T32_ISP_PLAT(k), k * NSD + (op3_rnd() % NSD) + 100 * NSD);
	for (k = 0; k < 16; k++)
		sw(O_ISP + T32_SD_SLOT(k), (op3_rnd() & 1) ? 0 : base + O_SD(op3_rnd() % NSD));
	for (k = 0; k < NSD; k++) {
		u32 s = O_SD(k), r = op3_rnd();

		tmpl[s + 0] = (u8)(r % 4);
		tmpl[s + 2] = (u8)((r >> 4) % 16);
		tmpl[s + 3] = (u8)((r >> 8) % 16);
		sw(s + T32_SD_MISC_FOPS, (r & 0x10000) ? 0 : op3_rnd());
		sw(s + T32_SD_PROC_FOPS, (r & 0x20000) ? 0 : op3_rnd());
		snprintf((char *)tmpl + s + 0x140, 16, "isp-m%u", k);
		sw(s + T32_SD_NAME, base + s + 0x140);
		map[k] = op3_rnd() % NSD;
		misc_ret[k] = (op3_rnd() & 7) ? op3_rnd() & 0xff : (u32)-(int)(op3_rnd() % 20 + 1);
	}
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
	n_misc = 0;
}

static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a1; (void)a2; (void)a3; (void)a4; return (u32)t32_tx_create_graph_and_nodes(a0); }

int main(void)
{
	unsigned int i;
	u32 base, rv, ro, d9008;
	struct op3_log lv;
	static u8 wv[ARENA];

	op3_mask_fmt = op3_t32_data_c74d4;
	op3_mask_args = 1u << 2;
	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		build(base);
		d9008 = (op3_rnd() & 1) ? op3_rnd() : 0;
		setup();
		op3_t32_data_d9008 = d9008;
		rv = OP3_VC(tx_isp_create_graph_and_nodes, base + O_ISP, 0, 0, 0, 0);
		lv = op3_log;
		memcpy(wv, arena, ARENA);
		setup();
		t32_tx_g_d9008 = d9008;
		ro = OP3_VC(ours, base + O_ISP, 0, 0, 0, 0);
		if (op3_log_cmp("create_graph_and_nodes", &lv, &op3_log))
			return 1;
		if (rv != ro || memcmp(wv, arena, ARENA)) {
			fprintf(stderr, "create_graph_and_nodes: ret %x/%x or world differs\n", rv, ro);
			return 1;
		}
	}
	printf("t32_txgraph_oracle: %u cases equal\n", NCASE);
	return 0;
}
