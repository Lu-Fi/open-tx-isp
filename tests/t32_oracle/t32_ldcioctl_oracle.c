// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our LDC ioctls (driver/t32/tx_isp_t32_ldcioctl.h) against Matt Davis' mechanical MIPS-to-C
 * recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value, every callee call in order (copy_from_user size / source, logs: level,
 * message, line; attribute contents handed to the set calls, LDC id, wait object / timeout, the core ioctl op:
 * sub-device and argument), the private block and the caller's buffer.  Inputs: the five commands, commands
 * +-1 and random, copy failure, set / op results 0 / -515 / random, wait 0 / nonzero, missing ops, NULL
 * sub-device for 0x1000000.  Not compared (vendor NULL dereference): 0x5000005 / 0x5000007 with a NULL or error
 * sub-device, 0x1000000 with an error sub-device.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ldcioctl.h"

u64 ldc_offline_unlocked_ioctl(u32, u32, u32, u32);
u64 ldc_core_ops_ioctl(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

u32 t32_data_d9948[1];
char t32_data_c5d54[4] = "f", t32_data_c5d70[4] = "f", t32_data_c5e68[4] = "f", t32_data_c5e88[4] = "f",
     t32_data_be484[4] = "f", t32_data_be470[4] = "f", t32_data_be494[4] = "f";

static u32 rs = 0x3c6ef372u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

enum { E_COPY = 1, E_LOG, E_OFFSET, E_WAIT, E_GET, E_SET, E_CORE };
struct ev { u32 id, a[3]; u8 d[28]; };
#define NE 8
static struct ev ve[NE], oe[NE];
static unsigned int vn, on;
static u32 copy_fail, set_ret, wait_ret, core_ret;
static u8 getpat[16];
static u8 *dev_v, *dev_o, *argv_, *argo, *sd_v;
static struct t32_ldc_sdio *sd_o;

#define TAG_ARG		0xa0000000u
#define TAG_DEV		0xd0000000u
#define TAG_SD		0x50000000u

static u32 v_norm(u32 p)
{
	if (p >= T32O_PTR(argv_) && p < T32O_PTR(argv_) + 32)
		return TAG_ARG + p - T32O_PTR(argv_);
	if (p >= T32O_PTR(dev_v) && p < T32O_PTR(dev_v) + 64)
		return TAG_DEV + p - T32O_PTR(dev_v);
	if (sd_v && p == T32O_PTR(sd_v))
		return TAG_SD;
	return p;
}
static u32 o_norm(const void *q)
{
	const u8 *p = q;

	if (p >= argo && p < argo + 32)
		return TAG_ARG + (u32)(p - argo);
	if (p >= dev_o && p < dev_o + 64)
		return TAG_DEV + (u32)(p - dev_o);
	if (sd_o && q == (const void *)sd_o)
		return TAG_SD;
	return (u32)(uintptr_t)q;
}

static struct ev *vadd(u32 id, u32 a0, u32 a1, u32 a2)
{
	static struct ev dummy;
	struct ev *e = vn < NE ? &ve[vn] : &dummy;

	vn++;
	e->id = id; e->a[0] = a0; e->a[1] = a1; e->a[2] = a2;
	return e;
}
static struct ev *oadd(u32 id, u32 a0, u32 a1, u32 a2)
{
	static struct ev dummy;
	struct ev *e = on < NE ? &oe[on] : &dummy;

	on++;
	e->id = id; e->a[0] = a0; e->a[1] = a1; e->a[2] = a2;
	return e;
}

/* vendor callees */
u64 private_copy_from_user(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	vadd(E_COPY, a2, v_norm(a1), 0);
	if (copy_fail)
		return copy_fail;
	memcpy((void *)(uintptr_t)a0, (const void *)(uintptr_t)a1, a2);
	return 0;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 m = a1 == T32O_PTR(t32_data_c5d54) ? 1 : a1 == T32O_PTR(t32_data_c5d70) ? 2 :
		a1 == T32O_PTR(t32_data_c5e68) ? 3 : a1 == T32O_PTR(t32_data_c5e88) ? 4 : 99;

	(void)a2;
	vadd(E_LOG, a0, m, m >= 3 ? a3 : 0);
	return 0;
}
u64 tisp_offline_ldc_set_attr(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	memcpy(vadd(E_OFFSET, 0, 0, 0)->d, (const void *)(uintptr_t)a0, 28);
	return rnd();
}
u64 private_wait_for_completion_timeout(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	vadd(E_WAIT, v_norm(a0), a1, 0);
	return wait_ret;
}
u64 tisp_online_ldc_get_attr(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	vadd(E_GET, a0, 0, 0);
	memcpy((void *)(uintptr_t)a1, getpat, 16);
	return rnd();
}
u64 tisp_online_ldc_set_attr(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	memcpy(vadd(E_SET, a0, 0, 0)->d, (const void *)(uintptr_t)a1, 16);
	return set_ret;
}
static u64 v_core(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	vadd(E_CORE, v_norm(a0), v_norm(a1), 0);
	return core_ret;
}

/* our hooks */
static u32 o_copy(void *c, void *dst, const void *src, u32 n)
{
	(void)c;
	oadd(E_COPY, n, o_norm(src), 0);
	if (copy_fail)
		return copy_fail;
	memcpy(dst, src, n);
	return 0;
}
static void o_log(void *c, u32 level, u32 msg, u32 line) { (void)c; oadd(E_LOG, level, msg, line); }
static u32 o_offset(void *c, void *attr) { (void)c; memcpy(oadd(E_OFFSET, 0, 0, 0)->d, attr, 28); return 0; }
static u32 o_wait(void *c, void *done, u32 ms) { (void)c; oadd(E_WAIT, o_norm(done), ms, 0); return wait_ret; }
static u32 o_get(void *c, u32 id, void *attr) { (void)c; oadd(E_GET, id, 0, 0); memcpy(attr, getpat, 16); return 0; }
static u32 o_set(void *c, u32 id, void *attr) { (void)c; memcpy(oadd(E_SET, id, 0, 0)->d, attr, 16); return set_ret; }
static u32 o_core(void *sd, void *arg) { oadd(E_CORE, o_norm(sd), o_norm(arg), 0); return core_ret; }

static u32 pick_ret(void)
{
	switch (rnd() % 4) {
	case 0: return 0;
	case 1: return T32_LDC_ENOIOCTLCMD;
	case 2: return rnd() % 2 ? 1 : T32_LDC_ENOIOCTLCMD + 1;
	default: return rnd();
	}
}

int main(void)
{
	static const u32 cmds[] = { 2, 1, 3, T32_LDC_IOC_ONLINE_EN, T32_LDC_IOC_ONLINE_ATTR, T32_LDC_IOC_DONE_GET,
				    T32_LDC_IOC_CORE, 0x5000004, 0x5000008, 0xffffff, 0x1000001, 0 };
	unsigned int c, i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_ldcio_hooks h = { NULL, NULL, o_copy, o_log, o_offset, o_wait, o_get, o_set };
	static struct t32_ldc_sdio osd;
	static struct t32_sd_ops oops;
	static struct t32_sd_core_ops ocore;
	static u8 priv_o[512];

	for (c = 0; c < n; c++) {
		u32 cmd = c % 13 == 12 ? rnd() : cmds[c % 12], r, o, bad, w, *ops_v, *core_v;
		u8 *priv_v;
		int off = c / 13 % 3 == 0, nullsd = 0;

		t32o_reset();
		vn = on = 0;
		memset(ve, 0, sizeof(ve));
		memset(oe, 0, sizeof(oe));
		dev_v = t32o_alloc(64);
		dev_o = t32o_alloc(64);
		memset(dev_v, 0x5a, 64);
		memset(dev_o, 0x5a, 64);
		argv_ = t32o_alloc(32);
		argo = t32o_alloc(32);
		sd_v = t32o_alloc(288);
		ops_v = t32o_alloc(4);
		core_v = t32o_alloc(8);
		priv_v = t32o_alloc(512);
		for (i = 0; i < 32; i++)
			argv_[i] = argo[i] = (u8)rnd();
		if (c % 4 == 1)
			memset(argv_, 0, 4), memset(argo, 0, 4);
		for (i = 0; i < 512; i++)
			priv_v[i] = priv_o[i] = (u8)rnd();
		for (i = 0; i < 16; i++)
			getpat[i] = (u8)rnd();
		t32_data_d9948[0] = T32O_PTR(dev_v);
		h.dev = dev_o;
		copy_fail = rnd() % 6 == 0 ? (rnd() | 1) : 0;
		set_ret = pick_ret();
		core_ret = pick_ret();
		wait_ret = rnd() % 4 == 0 ? 0 : rnd() | 1;
		/* sub-device: ops -> core -> ioctl, each possibly missing */
		w = c % 7 == 3 ? 0 : T32O_PTR(core_v);
		ops_v[0] = w;
		core_v[0] = rnd();
		core_v[1] = c % 7 == 5 ? 0 : (u32)(uintptr_t)v_core;
		memcpy(sd_v + 264, (u32[]){ T32O_PTR(ops_v) }, 4);
		memcpy(sd_v + 280, (u32[]){ T32O_PTR(priv_v) }, 4);
		ocore.ioctl = c % 7 == 5 ? NULL : o_core;
		oops.core = w ? &ocore : NULL;
		osd.ops = &oops;
		osd.priv = priv_o;
		sd_o = &osd;
		if (!off && cmd == T32_LDC_IOC_CORE && c / 13 % 4 == 1)
			nullsd = 1;
		if (off) {
			r = (u32)t32o_call(F(ldc_offline_unlocked_ioctl), rnd(), cmd, T32O_PTR(argv_), rnd(), 0);
			o = t32_ldc_offline_ioctl(&h, cmd, argo);
		} else {
			u32 vs = nullsd ? 0 : T32O_PTR(sd_v);

			if (nullsd)
				sd_o = NULL;
			r = (u32)t32o_call(F(ldc_core_ops_ioctl), vs, cmd, T32O_PTR(argv_), rnd(), 0);
			o = t32_ldc_core_ioctl(&h, sd_o, cmd, argo);
		}
		bad = r != o || vn != on || memcmp(ve, oe, sizeof(ve)) || memcmp(priv_v, priv_o, 512) ||
		      memcmp(argv_, argo, 32) || memcmp(dev_v, dev_o, 64) || vn > NE;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH %s cmd %x case %u: ret %x/%x calls %u/%u\n", off ? "offline" : "core", cmd, c, r, o,
			       vn, on);
	}
	printf("t32_ldcioctl_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
