// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: RISC-V glue (driver/t32/tx_isp_t32_txriscv.h) against
 * the vendor tx_isp_riscv_stop / _is_stop / _is_run / _stop_vic /
 * _switch_gpio / _run / _probe / _remove, tx_isp_set_tiziano_param,
 * tx_isp_get_tiziano_para and tx_isp_get_riscv_night_mode (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc, symbols renamed op3_* by op3_ren.sh).
 *
 * Per case both sides start from the same random mailbox window
 * (0xb2220000, mapped and traced, t32_op3_glue.h), the same InitPara block
 * (or none, with / without the magic), the same memory object (none, zero
 * or random vaddr / paddr) and the same scripted vmalloc result.  Compared:
 * the event log (hook calls, prints with arguments, every mailbox store in
 * order), the final window, the globals (da798 / da790), the memory object,
 * InitPara and the return value.
 * Exit 0 = all equal, 1 = mismatch, 2 = setup failure.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txriscv.h"

#define NCASE	20000
#define PARAB	0x10000u

/* vendor data (data.c@2e2e06bc line) */
u32 op3_t32_data_da798, op3_t32_data_da790;	/* .bss: riscv mem object, InitPara */
u32 op3_rmem_base, op3_rmem_size;
u8 op3_t32_data_cfb64[2108];			/* boot code, content irrelevant (address compared) */
u32 op3_t32_data_bede0[4] = { 0, 0x4000, 0x8000, 0xc000 };	/* data.c:1947 */
char op3_t32_data_c7874[] = "vinum is %d, tiziano para ae:%d %d %d luma:%d ev:%d num:%d\n";	/* data.c:10818 */
char op3_t32_data_c78b0[] = "tiziano para awb: MfPara :%d %d %d %d %d %d\n";	/* data.c:10833 */
char op3_t32_data_c7834[] = "[CORE][%s, %d]: tx_isp_get_tiziano_para: vinum %d is invalid\n";	/* data.c:10802 */
char op3_t32_data_bee4c[] = "tx_isp_get_tiziano_para";	/* data.c:1974 */
char op3_t32_data_c7808[] = "tx_isp_riscv_is_run error InitPara is null\n";	/* data.c:10791 */
char op3_t32_data_c78e0[] = "get_riscv_night_mode failed para is NULL\n";	/* data.c:10845 */
char op3_t32_data_c799c[] = "[%s %d] The RISC-V memory address cannot be empty.";	/* data.c:10892 */
char op3_t32_data_bedf0[] = "tx_isp_riscv_stop_vic";	/* data.c:1951 */
char op3_t32_data_bee08[] = "tx_isp_riscv_switch_gpio";	/* data.c:1957 */
char op3_t32_data_bee38[] = "tx_isp_riscv_run";		/* data.c:1969 */
char op3_t32_data_c790c[] = "[%s %d] RISCV address is empty.";	/* data.c:10856 */
char op3_t32_data_c792c[] = "[%s %d] The vaddr or paddr cannot be empty.";	/* data.c:10864 */
char op3_t32_data_bee24[] = "tx_isp_riscv_probe";	/* data.c:1964 */
char op3_t32_data_c7958[] = "[%s %d] RISC-V memory allocation failed.";	/* data.c:10875 */
char op3_t32_data_c7984[] = "%s[%d]: paddr = 0x%08x\n";	/* data.c:10886 */

u64 tx_isp_riscv_stop(u32, u32, u32, u32);
u64 tx_isp_riscv_is_stop(u32, u32, u32, u32);
u64 tx_isp_riscv_is_run(u32, u32, u32, u32);
u64 tx_isp_riscv_stop_vic(u32, u32, u32, u32);
u64 tx_isp_riscv_switch_gpio(u32, u32, u32, u32, u32);
u64 tx_isp_riscv_run(u32, u32, u32, u32);
u64 tx_isp_riscv_probe(u32, u32, u32, u32);
u64 tx_isp_riscv_remove(u32, u32, u32, u32);
u64 tx_isp_set_tiziano_param(u32, u32, u32, u32);
u64 tx_isp_get_tiziano_para(u32, u32, u32, u32);
u64 tx_isp_get_riscv_night_mode(u32, u32, u32, u32);

/* ---- hooks: vendor stubs and our recorders, same names ---- */
static int vm_fail;
static u32 vmalloc_rec(u32 n)
{
	u32 a = 0;

	op3_call("private_vmalloc", 1, n, 0, 0, 0);
	if (!vm_fail)
		a = T32O_PTR(t32o_alloc(n));
	return a;
}

/* memset / memcpy: performed on host objects; on a KSEG address (>= 0x80000000) only logged */
static void memset_rec(u32 a, u32 c, u32 n)
{
	if (a >= 0x80000000u)
		op3_call("memset", 3, a, c, n, 0);
	else
		memset(T32O_HOST(a), (int)c, n);
}

static void memcpy_rec(u32 d, u32 s, u32 n)
{
	if (d >= 0x80000000u)
		op3_call("memcpy", 3, d, s, n, 0);
	else
		memcpy(T32O_HOST(d), T32O_HOST(s), n);
}

u64 op3_private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return vmalloc_rec(a0); }
u64 op3_memset(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memset_rec(a0, a1, a2); return a0; }
u64 op3_memcpy(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; memcpy_rec(a0, a1, a2); return a0; }
OP3_STUB(private_vfree, 1)
OP3_STUB(private_msleep, 1)
OP3_STUB(tiziano_fastpara_init, 1)
OP3_STUB(tisp_g_riscv_info, 3)
u64 op3_tx_isp_riscv_stop(u32 a0, u32 a1, u32 a2, u32 a3) { return tx_isp_riscv_stop(a0, a1, a2, a3); }

u32 t32_tx_os_vmalloc(u32 n) { return vmalloc_rec(n); }
void t32_tx_os_vfree(u32 a) { op3_call("private_vfree", 1, a, 0, 0, 0); }
void t32_tx_os_msleep(u32 ms) { op3_call("private_msleep", 1, ms, 0, 0, 0); }
void t32_tx_os_memset(u32 a, u32 c, u32 n) { memset_rec(a, c, n); }
void t32_tx_os_memcpy(u32 d, u32 s, u32 n) { memcpy_rec(d, s, n); }
void t32_tx_os_fastpara_init(u32 ch) { op3_call("tiziano_fastpara_init", 1, ch, 0, 0, 0); }
void t32_tx_os_g_riscv_info(u32 ch, u32 w, u32 p) { op3_call("tisp_g_riscv_info", 3, ch, w, p, 0); }

/* ---- case state ---- */
#define WINB	0x100u
static u32 win0[WINB / 4];
static u8 para0[PARAB];
static u32 mem0[2];
static int has_para, has_mem;
static u32 rmb, rms, ch, args[5];

struct snap {
	struct op3_log log;
	u32 win[WINB / 4];
	u8 para[PARAB];
	u32 mem[2], g_mem_set, g_para, ret;
};
static struct snap sv, so;

enum { F_STOP, F_ISSTOP, F_ISRUN, F_NIGHT, F_STOPVIC, F_GPIO, F_RUN, F_PROBE, F_REMOVE, F_SETTIZ, F_GETTIZ, F_N };
static const char *const fname[F_N] = {
	"riscv_stop", "riscv_is_stop", "riscv_is_run", "get_riscv_night_mode", "riscv_stop_vic",
	"riscv_switch_gpio", "riscv_run", "riscv_probe", "riscv_remove", "set_tiziano_param", "get_tiziano_para"
};

static u32 *win(void) { return (u32 *)(uintptr_t)T32_RISCV_MBOX; }

static void setup(u32 *g_mem, u32 *g_para)
{
	u8 *p;
	u32 *m;

	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	op3_win_open(1);
	memcpy(win(), win0, WINB);
	op3_win_open(0);
	*g_para = 0;
	*g_mem = 0;
	if (has_para) {
		p = t32o_alloc(PARAB);
		memcpy(p, para0, PARAB);
		*g_para = T32O_PTR(p);
	}
	if (has_mem) {
		m = t32o_alloc(8);
		m[0] = mem0[0];
		m[1] = mem0[1];
		*g_mem = T32O_PTR(m);
	}
}

static void take(struct snap *s, u32 g_mem, u32 g_para, u32 ret, u32 mem_at_start)
{
	s->log = op3_log;
	op3_win_open(1);
	memcpy(s->win, win(), WINB);
	op3_win_open(0);
	memset(s->para, 0, PARAB);
	if (g_para)
		memcpy(s->para, T32O_HOST(g_para), PARAB);
	s->mem[0] = s->mem[1] = 0;
	/* the object the call started with (remove frees it but the bytes stay), else the new one */
	if (mem_at_start || g_mem)
		memcpy(s->mem, T32O_HOST(mem_at_start ? mem_at_start : g_mem), 8);
	s->g_mem_set = g_mem;
	s->g_para = g_para;
	s->ret = ret;
}

static int run_case(int f)
{
	struct t32_tx_riscv r;
	u32 ret = 0, start;

	/* vendor */
	setup(&op3_t32_data_da798, &op3_t32_data_da790);
	op3_rmem_base = rmb;
	op3_rmem_size = rms;
	start = op3_t32_data_da798;
	switch (f) {
	case F_STOP: ret = OP3_VC(tx_isp_riscv_stop, 0, 0, 0, 0, 0); break;
	case F_ISSTOP: ret = OP3_VC(tx_isp_riscv_is_stop, 0, 0, 0, 0, 0); break;
	case F_ISRUN: ret = OP3_VC(tx_isp_riscv_is_run, ch, 0, 0, 0, 0); break;
	case F_NIGHT: ret = OP3_VC(tx_isp_get_riscv_night_mode, 0, 0, 0, 0, 0); break;
	case F_STOPVIC: ret = OP3_VC(tx_isp_riscv_stop_vic, args[0], 0, 0, 0, 0); break;
	case F_GPIO: ret = OP3_VC(tx_isp_riscv_switch_gpio, args[0], args[1], args[2], args[3], args[4]); break;
	case F_RUN: ret = OP3_VC(tx_isp_riscv_run, 0, 0, 0, 0, 0); break;
	case F_PROBE: ret = OP3_VC(tx_isp_riscv_probe, 0, 0, 0, 0, 0); break;
	case F_REMOVE: ret = OP3_VC(tx_isp_riscv_remove, 0, 0, 0, 0, 0); break;
	case F_SETTIZ: ret = OP3_VC(tx_isp_set_tiziano_param, ch, 0, 0, 0, 0); break;
	case F_GETTIZ: ret = OP3_VC(tx_isp_get_tiziano_para, ch, 0, 0, ch, 0); break;	/* a3: see the header */
	}
	take(&sv, op3_t32_data_da798, op3_t32_data_da790, ret, start);

	/* ours */
	setup(&r.mem, &r.init_para);
	r.rmem_base = rmb;
	r.rmem_size = rms;
	r.fw = T32O_PTR(op3_t32_data_cfb64);
	start = r.mem;
	switch (f) {
	case F_STOP: ret = (u32)t32_tx_riscv_stop(); break;
	case F_ISSTOP: ret = (u32)t32_tx_riscv_is_stop(); break;
	case F_ISRUN: ret = (u32)t32_tx_riscv_is_run(&r, ch); break;
	case F_NIGHT: ret = (u32)t32_tx_get_riscv_night_mode(&r); break;
	case F_STOPVIC: ret = (u32)t32_tx_riscv_stop_vic(&r, args[0]); break;
	case F_GPIO: ret = (u32)t32_tx_riscv_switch_gpio(&r, args[0], args[1], args[2], args[3], args[4]); break;
	case F_RUN: ret = (u32)t32_tx_riscv_run(&r); break;
	case F_PROBE: ret = (u32)t32_tx_riscv_probe(&r); break;
	case F_REMOVE: ret = (u32)t32_tx_riscv_remove(&r); break;
	case F_SETTIZ: ret = (u32)t32_tx_set_tiziano_param(); break;
	case F_GETTIZ: ret = (u32)t32_tx_get_tiziano_para(&r, ch); break;
	}
	take(&so, r.mem, r.init_para, ret, start);

	if (op3_log_cmp(fname[f], &sv.log, &so.log))
		return 1;
	if (memcmp(sv.win, so.win, WINB)) {
		fprintf(stderr, "%s: mailbox window differs\n", fname[f]);
		return 1;
	}
	if (sv.ret != so.ret || sv.g_mem_set != so.g_mem_set || sv.g_para != so.g_para ||
	    memcmp(sv.mem, so.mem, 8) || memcmp(sv.para, so.para, PARAB)) {
		fprintf(stderr, "%s: ret %x/%x mem %x/%x para %x/%x\n", fname[f], sv.ret, so.ret,
			sv.g_mem_set, so.g_mem_set, sv.g_para, so.g_para);
		return 1;
	}
	return 0;
}

int main(void)
{
	unsigned int i, k, cnt[F_N] = { 0 };

	op3_win_map(T32_RISCV_MBOX, 0x1000);
	for (i = 0; i < NCASE; i++) {
		int f = (int)(i % F_N);
		u32 m = op3_rnd();

		for (k = 0; k < WINB / 4; k++)
			win0[k] = op3_rnd();
		if (m & 1)
			win0[3] = 1;			/* done flag set */
		else if (m & 2)
			win0[3] = (m & 4) ? 0 : 2;
		for (k = 0; k < PARAB; k += 4)
			*(u32 *)(para0 + k) = op3_rnd() & ((m & 8) ? 0xff : 0xffffffffu);
		if (m & 0x10)
			*(u32 *)(para0 + T32_TIZ_MAGIC_OFF) = T32_TIZ_MAGIC;
		else if (m & 0x20)
			*(u32 *)(para0 + T32_TIZ_MAGIC_OFF) = T32_TIZ_MAGIC ^ (1u << (op3_rnd() & 31));
		has_para = (m & 0xc0) != 0;
		has_mem = (m & 0x300) != 0;
		mem0[0] = (m & 0x400) ? 0 : op3_rnd();
		mem0[1] = (m & 0x800) ? 0 : op3_rnd();
		vm_fail = (m & 0x7000) == 0;
		rmb = op3_rnd();
		rms = (m & 0x8000) ? op3_rnd() & 0xffffff : op3_rnd();
		ch = (m & 0x80000) ? op3_rnd() : (m >> 16) & 7;
		for (k = 0; k < 5; k++)
			args[k] = op3_rnd();
		if (run_case(f))
			return 1;
		cnt[f]++;
	}
	printf("t32_txriscv_oracle: %u cases equal (", NCASE);
	for (i = 0; i < F_N; i++)
		printf("%s%s %u", i ? ", " : "", fname[i], cnt[i]);
	printf(")\n");
	return 0;
}
