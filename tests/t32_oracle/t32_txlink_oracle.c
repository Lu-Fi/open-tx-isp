// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: video links / release (driver/t32/tx_isp_t32_txlink.h)
 * against the vendor tx_isp_video_link_destroy_isra_4 and tx_isp_release
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc, symbols renamed op3_* by
 * op3_ren.sh; release's link destroy calls run the vendor function).
 *
 * Per case one random ISP device (open count, 16 subdevice slots with PM
 * release callbacks or 0, per input sensor / linked flags, link counts of
 * the 12 sets, 16 buffers), link mode 0..5, scripted pad lookups and link
 * destroy results.  The 12 link sets have the vendor's counts (bed7c,
 * data.c:1922) over oracle descriptor arrays (only their addresses are
 * used).  Compared: the event log, every world byte, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txlink.h"

#define NCASE	30000
#define ARENA	0x3000u
#define NSD	5u
#define O_DEV	0x100u	/* the isra function gets O_DEV - 12 */
#define O_SD(i)	(0x800u + 0x200u * (i))
#define O_FILE	0x1e00u
#define O_PAD	0x2000u

u32 op3_t32_data_d9014, t32_tx_link_mode;
u32 t32_tx_isp_dev, t32_tx_g_d9008;
static u8 descs[T32_LINK_SETS][6 * T32_LINK_BYTES] __attribute__((aligned(4)));
static const u32 nlink[T32_LINK_SETS] = { 3, 3, 3, 3, 4, 4, 4, 4, 6, 6, 6, 6 };	/* data.c:1922.. */
u32 op3_t32_data_bed7c[2 * T32_LINK_SETS];
struct t32_tx_link_set t32_tx_link_sets[T32_LINK_SETS];

u64 tx_isp_video_link_destroy_isra_4(u32, u32, u32, u32);
u64 tx_isp_release(u32, u32, u32, u32);

static u8 *arena;
static u32 A(u32 off) { return T32O_PTR(arena) + off; }
static u32 pad_tab[T32_LINK_SETS * 12], dl_ret[64], rel_ret[NSD + 1];

static u32 find_rec(u32 isp, u32 d)
{
	u32 k = (d - T32O_PTR(descs)) / 4;

	op3_call("find_subdev_link_pad", 2, isp, d, 0, 0);
	return pad_tab[k % (T32_LINK_SETS * 12)];
}
static u32 dl_rec(u32 l) { op3_call("subdev_video_destroy_link", 1, l, 0, 0, 0); return dl_ret[(l / 4) & 63]; }
static u32 sd_index(u32 sd)
{
	u32 i;

	for (i = 0; i < NSD; i++)
		if (sd == A(O_SD(i)))
			return i;
	return NSD;
}
static u32 cb_rel(u32 sd, u32 file) { op3_call("pm_release", 2, sd, file, 0, 0); return rel_ret[sd_index(sd)]; }

OP3_STUB(private_kfree, 1)
u64 op3_find_subdev_link_pad(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return find_rec(a0, a1); }
u64 op3_subdev_video_destroy_link(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return dl_rec(a0); }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset(T32O_HOST(a0), (int)a1, a2); return a0; }
u64 op3_tx_isp_video_link_destroy_isra_4(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_video_link_destroy_isra_4(a0, a1, a2, a3); }
u32 t32_tx_os_find_subdev_link_pad(u32 isp, u32 d) { return find_rec(isp, d); }
int t32_tx_os_subdev_video_destroy_link(u32 l) { return (int)dl_rec(l); }
void t32_tx_os_kfree(u32 p) { op3_call("private_kfree", 1, p, 0, 0, 0); }
void t32_tx_os_memset(u32 a, u32 c, u32 n) { memset(T32O_HOST(a), (int)c, n); }

static u8 tmpl[ARENA];
static void sw(u32 off, u32 v) { *(u32 *)(tmpl + off) = v; }

static void build(u32 base)
{
	u32 k, i;

	for (k = 0; k < ARENA; k += 4)
		sw(k, op3_rnd());
	sw(O_DEV + T32_DEV_OPENS, (op3_rnd() & 1) ? 0 : op3_rnd() % 4);
	for (k = 0; k < T32_DEV_SD_N; k++)
		sw(O_DEV + T32_DEV_OPEN_SD + 4 * k, (op3_rnd() & 3) == 0 ? base + O_SD(op3_rnd() % NSD) : 0);
	for (k = 0; k < 4; k++) {
		sw(O_DEV + T32_DEV_SENSOR_OF(k), (op3_rnd() & 1) ? op3_rnd() : 0);
		sw(O_DEV + T32_DEV_LINKED(k), (op3_rnd() & 1) ? op3_rnd() % 3 : 0);
	}
	for (k = 4; k < T32_LINK_SETS; k++)	/* link counts of the sets 4.. (dev +312 + 4 s) */
		sw(O_DEV + T32_DEV_LINKED(k), (op3_rnd() & 1) ? (u32)((int)(op3_rnd() % 4) - 1) : 0);
	for (k = T32_DEV_BUFS; k < T32_DEV_BUFS_END; k += T32_DEV_BUF_BYTES)
		sw(O_DEV + k, (op3_rnd() & 1) ? op3_rnd() : 0);
	for (i = 0; i < NSD; i++) {
		u32 s = O_SD(i);

		sw(s + T32_SD_OPS, base + s + 0x120);
		sw(s + 0x120 + T32_OPS_INTERNAL, (op3_rnd() & 3) ? base + s + 0x140 : 0);
		sw(s + 0x140 + T32_PM_RELEASE, (op3_rnd() & 3) ? T32O_PTR(cb_rel) : 0);
	}
	for (i = 0; i <= NSD; i++) {
		u32 r = op3_rnd() & 7;

		rel_ret[i] = r < 4 ? 0 : r < 6 ? (u32)-T32_ENOIOCTLCMD : op3_rnd();
	}
	sw(O_FILE + 120, base + O_DEV);
	for (k = 0; k < T32_LINK_SETS * 12; k++)
		pad_tab[k] = (op3_rnd() & 3) ? base + O_PAD + 16 * (op3_rnd() % 32) : 0;
	for (k = 0; k < 64; k++) {
		u32 r = op3_rnd() & 15;

		dl_ret[k] = r < 10 ? 0 : r < 13 ? (u32)-T32_ENOIOCTLCMD : op3_rnd();
	}
}

static void setup(void)
{
	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	arena = t32o_alloc(ARENA);
	memcpy(arena, tmpl, ARENA);
}

static int cur_f;
static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)a2; (void)a3; (void)a4;
	return cur_f ? (u32)t32_tx_isp_release(a0, a1) : (u32)t32_tx_video_link_destroy(a0, a1);
}

struct snap {
	struct op3_log log;
	u8 w[ARENA];
	u32 ret;
};
static struct snap sv, so;

int main(void)
{
	unsigned int i, k;
	u32 base;

	for (k = 0; k < T32_LINK_SETS; k++) {
		op3_t32_data_bed7c[2 * k] = T32O_PTR(descs[k]);
		op3_t32_data_bed7c[2 * k + 1] = nlink[k];
		t32_tx_link_sets[k].links = T32O_PTR(descs[k]);
		t32_tx_link_sets[k].n = nlink[k];
	}
	setup();
	base = A(0);
	for (i = 0; i < NCASE; i++) {
		int f = (int)(i & 1);
		u32 mode = op3_rnd() % 6, a0, a1;

		build(base);
		a0 = f ? op3_rnd() : base + O_DEV - 12;
		a1 = f ? base + O_FILE : op3_rnd() % 4;

		setup();
		op3_t32_data_d9014 = mode;
		sv.ret = OP3_VC(f ? tx_isp_release : tx_isp_video_link_destroy_isra_4, a0, a1, 0, 0, 0);
		sv.log = op3_log;
		memcpy(sv.w, arena, ARENA);
		setup();
		t32_tx_link_mode = mode;
		cur_f = f;
		so.ret = OP3_VC(ours, a0, a1, 0, 0, 0);
		so.log = op3_log;
		memcpy(so.w, arena, ARENA);
		if (op3_log_cmp(f ? "release" : "link_destroy", &sv.log, &so.log))
			return 1;
		if (sv.ret != so.ret || memcmp(sv.w, so.w, ARENA)) {
			fprintf(stderr, "%s: ret %x/%x or world differs\n", f ? "release" : "link_destroy", sv.ret, so.ret);
			return 1;
		}
	}
	printf("t32_txlink_oracle: %u cases equal (2 functions)\n", NCASE);
	return 0;
}
