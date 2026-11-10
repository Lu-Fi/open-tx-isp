// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the tuning glue of driver/t32/tx_isp_t32_tuning.h against
 * Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc, printk pointed at
 * the recorder by fix_op8_glue.sh): tiziano_fastpara_init / _exit,
 * tisp_enable_tuning / tisp_disable_tuning, tisp_core_tuning_init / _deinit,
 * tisp_switch_bin.
 *
 * Every kernel / driver call is recorded on both sides (t32o8_rec.h) and
 * compared call by call (arguments, format text, %s strings), plus return
 * values and every vendor object vs our state: the init flag, the mapping,
 * the 4532 tisp_tattr bytes, the proc entry, the fops owner, isSwitchbin.
 * Inputs: random sequences of the seven calls with random sensor indexes,
 * physical base, mapped words, and allocator / proc_create / tuning-node
 * results (failures included).  Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_tuning.h"

u64 tiziano_fastpara_init(u32, u32, u32, u32);
u64 tiziano_fastpara_exit(u32, u32, u32, u32);
u64 tisp_enable_tuning(u32, u32, u32, u32);
u64 tisp_disable_tuning(u32, u32, u32, u32);
u64 tisp_core_tuning_init(u32, u32, u32, u32);
u64 tisp_core_tuning_deinit(u32, u32, u32, u32);
u64 tisp_switch_bin(u32, u32, u32, u32);

#define CALL(f, a) t32o_call((t32o_fn)(void (*)(void))(f), a, 0, 0, 0, 0)

enum { F_IOREMAP = 1, F_IOUNMAP, F_PRINTK, F_ISPPRINTF, F_VMALLOC, F_VFREE, F_IRQ, F_PROC, F_RMPROC,
       F_MKNODE, F_RMNODE, F_SWITCH };

/* ---- vendor objects (data.c lines) ---- */
u32 t32_data_cfb60 = 0x03fbc000u;	/* g_ae_para_addr, data.c:18565 */
u32 t32_data_da790, t32_data_da794;	/* g_pstTizianoInitPara, init flag (.bss) */
u32 t32_data_f0904, t32_data_dada0;	/* tisp_tattr, proc entry (.bss) */
u8 t32_data_e401c[4];			/* isSwitchbin (.bss) */
u32 t32_data_ce22c[1];			/* data.c:16952: &__this_module (set per run) */
u32 t32_data_d0810[4];			/* proc file operations, +0 owner */
char t32_data_c77c4[] = T32_FASTPARA_FMT;		/* data.c */
char t32_data_c77ec[] = T32_FASTPARA_FAIL_FMT;
char t32_data_c7cfc[] = T32_TISP_PROC_NAME;
char t32_data_c7d04[] = T32_TISP_PROC_FAIL_FMT;
char t32_data_bf0b8[] = "tisp_core_tuning_init";
char t32_data_cb6b0[] = T32_TATTR_FAIL_FMT;
char t32_data_c3000[] = "tisp_enable_tuning";
u64 tisp_top_frame_count(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

static u32 *map_v, *map_o;		/* the "mapped" parameter window (12608 B each) */
static u8 *tattr_v, *tattr_o;
static u32 r_vmalloc_fail, r_proc, r_mknode, r_rmnode, r_switch, r_printk;

u64 __ioremap(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	t32o8_rec(&t32o8_v, F_IOREMAP, a0, a1, a2, 0);
	return T32O_PTR(map_v);
}

u64 __iounmap(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_IOUNMAP, a0 == T32O_PTR(map_v) ? 1 : a0, 0, 0, 0);
	return 0;
}

u64 t32o8_printk(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 w[3] = { a1, a2, a3 };

	t32o8_rec_fmt(&t32o8_v, F_PRINTK, 0, T32O_HOST(a0), w, 3);
	return r_printk;
}

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 w[2] = { a2, a3 };

	t32o8_rec_fmt(&t32o8_v, F_ISPPRINTF, a0, T32O_HOST(a1), w, 2);
	return 0;
}

u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_VMALLOC, a0, 0, 0, 0);
	if (r_vmalloc_fail)
		return 0;
	memset(tattr_v, 0xa5, T32_TATTR_BYTES + 64);	/* vmalloc memory is not zeroed */
	return T32O_PTR(tattr_v);
}

u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_VFREE, a0 == T32O_PTR(tattr_v) ? 1 : a0, 0, 0, 0);
	return 0;
}

u64 system_irq_func_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	t32o8_rec(&t32o8_v, F_IRQ, a0, a1, a2 == T32O_PTR(tisp_top_frame_count) ? 1 : a2, 0);
	return 0;
}

u64 private_proc_create(u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct t32o8_call *c = t32o8_rec(&t32o8_v, F_PROC, 0, a1, a2, a3 == T32O_PTR(t32_data_d0810) ? 1 : a3);

	snprintf(c->data, sizeof(c->data), "%s", (const char *)T32O_HOST(a0));
	return r_proc;
}

u64 private_remove_proc_entry(u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct t32o8_call *c = t32o8_rec(&t32o8_v, F_RMPROC, 0, a1, 0, 0);

	(void)a2; (void)a3;
	snprintf(c->data, sizeof(c->data), "%s", (const char *)T32O_HOST(a0));
	return 0;
}

u64 tisp_code_create_tuning_node(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_MKNODE, 0, 0, 0, 0);
	return r_mknode;
}

u64 tisp_code_destroy_tuning_node(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_RMNODE, 0, 0, 0, 0);
	return r_rmnode;
}

u64 tisp_core_switch_bin(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a0; (void)a1; (void)a2; (void)a3;
	t32o8_rec(&t32o8_v, F_SWITCH, 0, 0, 0, 0);
	return r_switch;
}

/* ---- ours ---- */
static u32 fops_o[4];
static void *o_ioremap(u32 phys, u32 size, u32 flags)
{
	t32o8_rec(&t32o8_o, F_IOREMAP, phys, size, flags, 0);
	return map_o;
}
static void o_iounmap(void *p) { t32o8_rec(&t32o8_o, F_IOUNMAP, p == map_o ? 1 : T32O_PTR(p), 0, 0, 0); }
static int o_printk(const char *fmt, ...)
{
	u32 w[6];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 6);
	va_end(ap);
	t32o8_rec_fmt(&t32o8_o, F_PRINTK, 0, fmt, w, n);
	return (int)r_printk;
}
static int o_isp_printf(int level, const char *fmt, ...)
{
	u32 w[6];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 6);
	va_end(ap);
	t32o8_rec_fmt(&t32o8_o, F_ISPPRINTF, (u32)level, fmt, w, n);
	return 0;
}
static void *o_vmalloc(u32 size)
{
	t32o8_rec(&t32o8_o, F_VMALLOC, size, 0, 0, 0);
	if (r_vmalloc_fail)
		return NULL;
	memset(tattr_o, 0xa5, T32_TATTR_BYTES + 64);
	return tattr_o;
}
static void o_vfree(void *p) { t32o8_rec(&t32o8_o, F_VFREE, p == tattr_o ? 1 : T32O_PTR(p), 0, 0, 0); }
static int o_irq(int chn, int irq, void *h)
{
	t32o8_rec(&t32o8_o, F_IRQ, (u32)chn, (u32)irq, h == (void *)tisp_top_frame_count ? 1 : T32O_PTR(h), 0);
	return 0;
}
static void *o_proc(const char *name, u32 mode, void *parent, void *fops)
{
	struct t32o8_call *c = t32o8_rec(&t32o8_o, F_PROC, 0, mode, T32O_PTR(parent), fops == fops_o ? 1 : T32O_PTR(fops));

	snprintf(c->data, sizeof(c->data), "%s", name);
	return T32O_HOST(r_proc);
}
static void o_rmproc(const char *name, void *parent)
{
	struct t32o8_call *c = t32o8_rec(&t32o8_o, F_RMPROC, 0, T32O_PTR(parent), 0, 0);

	snprintf(c->data, sizeof(c->data), "%s", name);
}
static int o_mknode(void) { t32o8_rec(&t32o8_o, F_MKNODE, 0, 0, 0, 0); return (int)r_mknode; }
static int o_rmnode(void) { t32o8_rec(&t32o8_o, F_RMNODE, 0, 0, 0, 0); return (int)r_rmnode; }
static int o_switch(void) { t32o8_rec(&t32o8_o, F_SWITCH, 0, 0, 0, 0); return (int)r_switch; }

static const struct t32_tuning_ops ops = {
	o_ioremap, o_iounmap, o_printk, o_isp_printf, o_vmalloc, o_vfree, o_irq, o_proc, o_rmproc,
	o_mknode, o_rmnode, o_switch, (void *)tisp_top_frame_count
};

static u32 rs = 0x7c0ffee5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;
static struct t32_tuning_state st;
static void *module_o;

/* vendor objects vs our state (pointers by identity) */
static int state_diff(void)
{
	return t32_data_da794 != st.fastpara_inited ||
	       (t32_data_da790 == T32O_PTR(map_v)) != (st.init_para == map_o) ||
	       (!t32_data_da790) != (!st.init_para) ||
	       (t32_data_f0904 == T32O_PTR(tattr_v)) != (st.tattr == tattr_o) || (!t32_data_f0904) != (!st.tattr) ||
	       memcmp(tattr_v, tattr_o, T32_TATTR_BYTES + 64) ||
	       t32_data_dada0 != T32O_PTR(st.proc) ||
	       (t32_data_d0810[0] == t32_data_ce22c[0]) != (fops_o[0] == T32O_PTR(module_o)) ||
	       t32_data_e401c[0] != st.is_switch_bin;
}

int main(void)
{
	unsigned int c, s, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 4000;

	for (c = 0; c < n; c++) {
		t32o_reset();
		map_v = t32o_alloc(12608); map_o = t32o_alloc(12608);
		tattr_v = t32o_alloc(T32_TATTR_BYTES + 64); tattr_o = t32o_alloc(T32_TATTR_BYTES + 64);
		module_o = t32o_alloc(64);
		memset(&st, 0, sizeof(st));
		t32_data_da790 = t32_data_da794 = t32_data_f0904 = t32_data_dada0 = 0;
		t32_data_e401c[0] = 0;
		memset(t32_data_d0810, 0, sizeof(t32_data_d0810));
		memset(fops_o, 0, sizeof(fops_o));
		t32_data_ce22c[0] = T32O_PTR(module_o);
		st.this_module = module_o;
		st.fops_owner = (void **)fops_o;
		t32_data_cfb60 = st.ae_para_addr = (rnd() & 1) ? 0x03fbc000u : rnd();
		for (s = 0; s < 12; s++) {
			u32 which = rnd() % 7, arg = (rnd() & 1) ? rnd() % 4 : rnd(), vr = 0, orr = 0, k;

			for (k = 0; k < 12608 / 4; k++)
				map_v[k] = map_o[k] = rnd();
			r_vmalloc_fail = !(rnd() % 5);
			r_proc = (rnd() % 4) ? T32O_PTR(module_o) + 8 : 0;
			r_mknode = (rnd() & 1) ? 0 : rnd();
			r_rmnode = (rnd() & 1) ? 0 : rnd();
			r_switch = (rnd() & 1) ? 0 : rnd();
			r_printk = rnd() % 64;
			t32o8_clear();
			switch (which) {
			case 0:
				CALL(tiziano_fastpara_init, arg);
				t32_fastpara_init(&ops, &st, arg);
				break;
			case 1:
				CALL(tiziano_fastpara_exit, 0);
				t32_fastpara_exit(&ops, &st);
				break;
			case 2:
				vr = (u32)CALL(tisp_enable_tuning, 0);
				orr = (u32)t32_enable_tuning(&ops, &st);
				break;
			case 3:
				CALL(tisp_disable_tuning, 0);
				t32_disable_tuning(&ops, &st);
				break;
			case 4:
				vr = (u32)CALL(tisp_core_tuning_init, 0);
				orr = (u32)t32_core_tuning_init(&ops, &st, fops_o);
				break;
			case 5:
				vr = (u32)CALL(tisp_core_tuning_deinit, 0);
				orr = (u32)t32_core_tuning_deinit(&ops, &st);
				break;
			default:
				vr = (u32)CALL(tisp_switch_bin, 0);
				orr = (u32)t32_switch_bin(&ops, &st);
				break;
			}
			compared++;
			if ((t32o8_cmp("tuning", failed < 8) | (vr != orr) | state_diff()) && failed++ < 8)
				printf("MISMATCH call %u: ret vendor %08x ours %08x, state %d\n", which, vr, orr, state_diff());
		}
	}
	printf("t32_tuning_oracle: %u calls compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
