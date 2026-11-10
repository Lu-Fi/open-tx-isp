// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, second half of the private_* wrappers
 * (driver/t32/tx_isp_t32_private.h):
 *
 * 1. Against Matt Davis' mechanical C (ref/, PR #15 @ 2e2e06bc, o32 shape,
 *    run on the low stack): private_i2c_add_driver, private_platform_driver_register,
 *    private_get_isp_priv_mem, private_proc_create, private_get_init_net and
 *    private_reset_tx_isp_module.  The reset polls CPM SRBC at KSEG1
 *    0xb00000c4; the oracle maps a page there, sets the "stopped" bit after a
 *    random number of sleeps (or never: 500 rounds, -1) and logs the
 *    register after every hook call, so the order of the register updates
 *    against isp_printf / msleep / __udelay is compared too.
 *
 * 2. Against the vendor instructions of tx-isp-t32.o, where the recovered C
 *    is marked diff_mismatch and does something else (oem_* below, each a
 *    transcription of the listing at the given address):
 *    vmalloc 0x838, vfree 0x864, copy_from_user 0x8b8, copy_to_user 0x910,
 *    kthread_run 0xa30, seq_printf 0xaf0.  seq_printf's result is the v0 of
 *    the void kernel seq_printf (undefined) and is not compared.
 *
 * Compared per case: hook trace (names, arguments, order), return value
 * (int results in the low 32 bits, as in v0), pool memory, thread_info and
 * the SRBC word.  Exit 0 = all equal, 1 = mismatch.
 */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#pragma GCC diagnostic ignored "-Wpointer-to-int-cast"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include "t32o_kern.h"
#include "t32o_kern_rec.h"
#include "tx_isp_t32_private.h"

typedef u64 (*t32o_fn)(u32, u32, u32, u32, u32);
u64 t32o_call(t32o_fn fn, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4);
u64 private_i2c_add_driver(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_platform_driver_register(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_get_isp_priv_mem(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_proc_create(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_get_init_net(u32 a0, u32 a1, u32 a2, u32 a3);
u64 private_reset_tx_isp_module(u32 a0, u32 a1, u32 a2, u32 a3);

/* ---- data of the mechanical references ---- */
u32 t32_data_d9008;			/* vendor zeratul_open (.bss + 8): fast start */
u8 t32_data_c6e00[] = "optimize startup time\n";	/* vendor .rodata.str1.4 + 0x36f0 */

/* ---- o32 kernel symbols the mechanical references call (same hooks as t32o_kern.h) ---- */
static u64 rec2(const char *nm, int n, u64 a, u64 b, u64 c, u64 d, u64 e)
{
	static const int k_[5] = { 0 };
	u64 v[5] = { a, b, c, d, e };

	return t32k_rec(nm, n, k_, v);
}
u64 o32_i2c_register_driver(u32 a0, u32 a1, u32 a2, u32 a3) __asm__("i2c_register_driver");
u64 o32_i2c_register_driver(u32 a0, u32 a1, u32 a2, u32 a3) { return rec2("i2c_register_driver", 2, a0, a1, 0, 0, 0); }
u64 o32_platform_driver_register(u32 a0, u32 a1, u32 a2, u32 a3) __asm__("__platform_driver_register");
u64 o32_platform_driver_register(u32 a0, u32 a1, u32 a2, u32 a3) { return rec2("__platform_driver_register", 2, a0, a1, 0, 0, 0); }
u64 o32_proc_create_data(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) __asm__("proc_create_data");
u64 o32_proc_create_data(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { return rec2("proc_create_data", 5, a0, a1, a2, a3, a4); }
u64 o32_isp_printf(u32 a0, u32 a1, u32 a2, u32 a3) __asm__("isp_printf");
u64 o32_isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	static const int k_[2] = { T32K_V, T32K_S };
	u64 v[2] = { a0, a1 };

	return t32k_rec("isp_printf", 2, k_, v);
}
u64 o32_udelay(u32 a0, u32 a1, u32 a2, u32 a3) __asm__("__udelay");
u64 o32_udelay(u32 a0, u32 a1, u32 a2, u32 a3) { return rec2("__udelay", 1, a0, 0, 0, 0, 0); }
/* private_msleep is a tail call of msleep (vendor 0x1e0, t32_priv_oracle) */
u64 o32_private_msleep(u32 a0, u32 a1, u32 a2, u32 a3) __asm__("private_msleep");
u64 o32_private_msleep(u32 a0, u32 a1, u32 a2, u32 a3) { return rec2("msleep", 1, a0, 0, 0, 0, 0); }

/* ---- vendor instruction transcriptions (tx-isp-t32.o) ---- */
/* 0x838: sltiu v0,a0,4096; below -> __kmalloc(size, 0x24000c0), else vmalloc */
static void *oem_vmalloc(unsigned long size)
{
	if ((u32)size < 4096)
		return t32k___kmalloc(size, 0x24000c0u);
	return t32k_vmalloc(size);
}
/* 0x864: (a0 + 0x40000000) < 0x3dffe000 -> vfree, else kfree */
static void oem_vfree(void *p)
{
	if ((u32)((u32)(uintptr_t)p + 0x40000000u) < 0x3dffe000u)
		t32k_vfree(p);
	else
		t32k_kfree(p);
}
/* 0x8b8 / 0x910: ((p + n) | p | n) & thread_info->addr_limit == 0 -> __copy_user (a2 = rest),
 * else memcpy and 0 */
static long oem_copy(void *to, const void *from, long size, u32 user)
{
	u32 m = (((user + (u32)size) | user | (u32)size) & t32k_ti_word(24));

	if (m == 0)
		return (long)t32k___copy_user(to, from, size);
	t32k_memcpy(to, from, size);
	return 0;
}
static long oem_copy_from_user(void *to, const void *from, long size) { return oem_copy(to, from, size, (u32)(uintptr_t)from); }
static long oem_copy_to_user(void *to, const void *from, long size) { return oem_copy(to, from, size, (u32)(uintptr_t)to); }
/* 0xa30: kthread_create_on_node(fn, data, -1, name); sltiu v0,v0,-4095 -> wake_up_process */
static struct task_struct *oem_kthread_run(void *fn, void *data, const char *name)
{
	struct task_struct *k = t32k_kthread_create_on_node(fn, data, -1, name);

	if ((u32)(uintptr_t)k < (u32)-4095)
		t32k_wake_up_process(k);
	return k;
}
/* 0xaf0: vaf = { a1, &varargs }; seq_printf(a0, "%pV", &vaf) */
static void oem_seq_printf(struct seq_file *m, const char *f, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, f);
	vaf.fmt = f;
	vaf.va = &args;
	t32k_rec_vaf("seq_printf", T32K_U(m), "%pV", &vaf);
	va_end(args);
}

/* ---- the SRBC page ---- */
#define SRBC	(*(volatile u32 *)(uintptr_t)T32_CPM_SRBC_KSEG1)
static int sleeps, ack_at;
static void on_hook(const char *name)
{
	struct t32k_ent *e = t32k_push("srbc", 1);

	if (e)
		e->a[0] = SRBC;
	if ((!strcmp(name, "msleep") || !strcmp(name, "__udelay")) && ++sleeps == ack_at)
		SRBC |= T32_SRBC_ISP_STOPPED;
}

#define NCASE	4000
#define P(i)	((void *)(uintptr_t)r[i])
#define Q(i)	((void *)(t32k_pool[r[i] & 3] + ((r[i] >> 8) & 15) * 8))
#define LO(x)	((u64)(s64)(s32)(x))
typedef u64 (*t32p_call)(u64 *r);
static int fast;

static u64 o_vmalloc(u64 *r) { return T32K_U(t32_private_vmalloc((u32)r[0])); }
static u64 f_vmalloc(u64 *r) { return T32K_U(oem_vmalloc((u32)r[0])); }
static u64 o_vfree(u64 *r) { t32_private_vfree((void *)(uintptr_t)(u32)r[0]); return 0; }
static u64 f_vfree(u64 *r) { oem_vfree((void *)(uintptr_t)(u32)r[0]); return 0; }
static u64 o_copy_from_user(u64 *r) { return (u64)t32_private_copy_from_user(P(0), (void *)(uintptr_t)(u32)r[1], (long)(s32)r[2]); }
static u64 f_copy_from_user(u64 *r) { return (u64)oem_copy_from_user(P(0), (void *)(uintptr_t)(u32)r[1], (long)(s32)r[2]); }
static u64 o_copy_to_user(u64 *r) { return (u64)t32_private_copy_to_user((void *)(uintptr_t)(u32)r[0], P(1), (long)(s32)r[2]); }
static u64 f_copy_to_user(u64 *r) { return (u64)oem_copy_to_user((void *)(uintptr_t)(u32)r[0], P(1), (long)(s32)r[2]); }
static int thr(void *d) { return 0; }
static u64 o_kthread_run(u64 *r) { return T32K_U(t32_private_kthread_run(thr, P(1), (const char *)P(2))); }
static u64 f_kthread_run(u64 *r) { return T32K_U(oem_kthread_run((void *)thr, P(1), (const char *)P(2))); }
static u64 o_seq_printf(u64 *r) { t32_private_seq_printf((struct seq_file *)P(0), (const char *)P(1), (int)r[2], (int)r[3], (int)r[4], (int)r[5]); return 0; }
static u64 f_seq_printf(u64 *r) { oem_seq_printf((struct seq_file *)P(0), (const char *)P(1), (int)r[2], (int)r[3], (int)r[4], (int)r[5]); return 0; }

#define CALL(fn, a0, a1, a2, a3)	t32o_call((t32o_fn)(void (*)(void))fn, (u32)(a0), (u32)(a1), (u32)(a2), (u32)(a3), 0)
static u64 o_i2c_add_driver(u64 *r) { return LO(t32_private_i2c_add_driver((struct i2c_driver *)(uintptr_t)(u32)r[0])); }
static u64 f_i2c_add_driver(u64 *r) { return LO(CALL(private_i2c_add_driver, r[0], r[1], r[2], r[3])); }
static u64 o_platform_driver_register(u64 *r) { return LO(t32_private_platform_driver_register((struct platform_driver *)(uintptr_t)(u32)r[0])); }
static u64 f_platform_driver_register(u64 *r) { return LO(CALL(private_platform_driver_register, r[0], r[1], r[2], r[3])); }
static u64 o_get_isp_priv_mem(u64 *r) { t32_private_get_isp_priv_mem(Q(0), Q(1)); return 0; }
static u64 f_get_isp_priv_mem(u64 *r) { CALL(private_get_isp_priv_mem, (uintptr_t)Q(0), (uintptr_t)Q(1), r[2], r[3]); return 0; }
/* mode is a umode_t: the o32 caller hands it over zero-extended */
static u64 o_proc_create(u64 *r)
{
	return LO(T32K_U(t32_private_proc_create((const char *)(uintptr_t)(u32)r[0], (umode_t)r[1],
						 (struct proc_dir_entry *)(uintptr_t)(u32)r[2],
						 (const struct file_operations *)(uintptr_t)(u32)r[3])));
}
static u64 f_proc_create(u64 *r) { return LO(CALL(private_proc_create, r[0], (u16)r[1], r[2], r[3])); }
static u64 o_get_init_net(u64 *r) { return LO(T32K_U(t32_private_get_init_net())); }
static u64 f_get_init_net(u64 *r) { return LO(CALL(private_get_init_net, r[0], r[1], r[2], r[3])); }
static u64 o_reset_tx_isp_module(u64 *r) { return LO(t32_private_reset_tx_isp_module((int)(r[0] & 3 ? 0 : r[1]), &t32_data_d9008)); }
static u64 f_reset_tx_isp_module(u64 *r) { return LO(CALL(private_reset_tx_isp_module, r[0] & 3 ? 0 : r[1], 0, 0, 0)); }

#define E(fn, rs)	{ #fn, o_##fn, f_##fn, rs }
static const struct { const char *name; t32p_call o, f; int cmp_ret; } tab[] = {
	E(vmalloc, 1), E(vfree, 1), E(copy_from_user, 1), E(copy_to_user, 1), E(kthread_run, 1), E(seq_printf, 0),
	E(i2c_add_driver, 1), E(platform_driver_register, 1), E(get_isp_priv_mem, 1), E(proc_create, 1),
	E(get_init_net, 1), E(reset_tx_isp_module, 1),
};

static u8 pool0[sizeof(t32k_pool)], ti0[sizeof(t32k_ti)];
static u8 pool_o[sizeof(t32k_pool)], ti_o[sizeof(t32k_ti)];
static struct t32k_log log_o;
static u32 srbc0, srbc_o, base0, size0;

/* one argument: a pool pointer, a boundary (allocation, vmalloc range, access mask) or a random value */
static u64 arg(u64 *s)
{
	static const u32 edge[] = { 4095, 4096, 4097, 0xbfffffffu, 0xc0000000u, 0xfdffdfffu, 0xfdffe000u,
				    0x7fffffffu, 0x80000000u, 0xfffff000u, 0xfffff001u };
	u64 x = t32k_next(s);

	switch ((x >> 60) & 7) {
	case 0: return (u64)(uintptr_t)(t32k_pool[x & 3] + ((x >> 8) & 15) * 8);
	case 1: return 0;
	case 2: return (u64)-1;
	case 3: return edge[(x >> 8) % (sizeof(edge) / sizeof(edge[0]))] + (s32)((x >> 16) % 3) - 1;
	case 4: return (u32)x;
	case 5: return (u64)(s64)(s32)x;
	case 6: return x & 0xfff;
	default: return x;
	}
}

static void setup(u64 seed)
{
	static const u32 lim[4] = { 0, 0x80000000u, 0xffffffffu, 0x80000000u };
	u64 s = seed ^ 0x9e3779b97f4a7c15ull;
	size_t i;

	for (i = 0; i < sizeof(pool0); i++)
		pool0[i] = (u8)t32k_next(&s);
	for (i = 0; i < sizeof(ti0); i++)
		ti0[i] = (u8)t32k_next(&s);
	*(u32 *)ti0 = (u32)(uintptr_t)t32k_pool[3];
	*(u32 *)(ti0 + 24) = (s & 4) ? lim[s & 3] : (u32)t32k_next(&s);	/* KERNEL_DS, USER_DS, other */
	srbc0 = (u32)t32k_next(&s) & ~T32_SRBC_ISP_STOPPED;
	if ((s & 0x70) == 0)
		srbc0 |= T32_SRBC_ISP_STOPPED;			/* stopped already */
	base0 = (u32)t32k_next(&s);
	size0 = (u32)t32k_next(&s);
	fast = (t32k_next(&s) & 1);
}

static void arm(u64 seed)
{
	memmove(t32k_pool, pool0, sizeof(pool0));
	memmove(t32k_ti, ti0, sizeof(ti0));
	memset(&t32k_cur, 0, sizeof(t32k_cur));
	t32k_rs = seed | 1;
	SRBC = srbc0;
	ispmem_base = base0;
	ispmem_size = size0;
	t32_data_d9008 = fast;
	sleeps = 0;
	ack_at = (int)(seed >> 40) % 12 == 0 ? 0 : 1 + (int)((seed >> 20) % 520);	/* 0: never */
}

int main(void)
{
	u64 s = 0x7432b0bba5eedull, r[8], ro, rf;
	unsigned int t, c, bad = 0, n = 0, fails = 0;
	int i;

	if (mmap((void *)(T32_CPM_SRBC_KSEG1 & ~0xfffUL), 4096, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == MAP_FAILED) {
		printf("t32_privx_oracle: cannot map the SRBC page\n");
		return 1;
	}
	t32k_on = on_hook;
	for (t = 0; t < sizeof(tab) / sizeof(tab[0]); t++) {
		for (c = 0; c < NCASE; c++) {
			u64 seed = t32k_next(&s);

			for (i = 0; i < 8; i++)
				r[i] = arg(&s);
			setup(seed);
			arm(seed);
			ro = tab[t].o(r);
			log_o = t32k_cur;
			memmove(pool_o, t32k_pool, sizeof(pool_o));
			memmove(ti_o, t32k_ti, sizeof(ti_o));
			srbc_o = SRBC;
			arm(seed);
			rf = tab[t].f(r);
			n++;
			if (!strcmp(tab[t].name, "reset_tx_isp_module") && (u32)rf == (u32)-1)
				fails++;
			if (t32k_logcmp(tab[t].name, c, &log_o, &t32k_cur)) {
				bad++;
			} else if (tab[t].cmp_ret && ro != rf) {
				printf("%s case %u: return 0x%llx vs 0x%llx\n", tab[t].name, c,
				       (unsigned long long)ro, (unsigned long long)rf);
				bad++;
			} else if (memcmp(pool_o, t32k_pool, sizeof(pool_o)) || memcmp(ti_o, t32k_ti, sizeof(ti_o)) ||
				   srbc_o != SRBC) {
				printf("%s case %u: memory differs\n", tab[t].name, c);
				bad++;
			}
			if (bad > 10)
				goto out;
		}
	}
out:
	printf("t32_privx_oracle: %u wrappers, %u cases (%u reset timeouts), %u mismatches\n",
	       (unsigned int)(sizeof(tab) / sizeof(tab[0])), n, fails, bad);
	return bad ? 1 : 0;
}
