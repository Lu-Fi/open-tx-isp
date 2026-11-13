// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: lib_tisp_sync_sensor_attr and lib_tisp_is_file_exist (driver/t32/tx_isp_t32_libwrap.h,
 * group 6) against Matt Davis' mechanical MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: return value (low word), the channel copies t32_data_dd2d4 (all 1024 bytes), the attribute
 * block (unchanged), the AE trigger call and channel; for the file check the kern_path name and flags, whether
 * path_put runs and gets the same path object kern_path filled.  Inputs: channels 0 / 1 with high garbage
 * bytes, random attribute blocks, kern_path results 0 / -2 / random.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libwrap.h"

u64 lib_tisp_sync_sensor_attr(u32, u32, u32, u32);
u64 lib_tisp_is_file_exist(u32, u32, u32, u32);
extern u8 t32_data_dd2d4[0x400];

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

static u32 rs = 0x77665544u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 ret_val, kp_ret, v_n, v_ch, v_kp, v_name, v_flags, v_put, v_same;
static u32 o_n, o_ch, o_kp, o_flags, o_put, o_same;
static const char *o_name;
static u32 v_path;
static void *o_path;

u64 tisp_ae_par_sensor_trig(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	v_n++;
	v_ch = a0;
	return ret_val;
}

u64 kern_path(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	v_kp++;
	v_name = a0;
	v_flags = a1;
	v_path = a2;
	return kp_ret;
}

u64 path_put(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	v_put++;
	v_same = a0 == v_path;
	return 0;
}

static u32 o_trig(void *c, u32 ch) { (void)c; o_n++; o_ch = ch; return ret_val; }
static int o_kpath(void *c, const char *name, u32 flags, void *path)
{
	(void)c;
	o_kp++;
	o_name = name;
	o_flags = flags;
	o_path = path;
	return (int)kp_ret;
}
static void o_pput(void *c, void *path) { (void)c; o_put++; o_same = path == o_path; }

int main(void)
{
	unsigned int c, i, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	static u8 copy[sizeof(t32_data_dd2d4)];
	struct t32_lib_hooks h;

	memset(&h, 0, sizeof(h));
	h.ae_sensor_trig = o_trig;
	h.kern_path = o_kpath;
	h.path_put = o_pput;
	for (c = 0; c < n; c++) {
		u32 ch = (rnd() & 1) | (c % 3 ? rnd() & 0xffffff00u : 0), r, o;
		u8 attr[232], *va;
		char *vname;
		unsigned int bad;

		t32o_reset();
		for (i = 0; i < sizeof(copy); i++)
			t32_data_dd2d4[i] = copy[i] = (u8)rnd();
		for (i = 0; i < sizeof(attr); i++)
			attr[i] = (u8)rnd();
		va = t32o_alloc(sizeof(attr));
		memcpy(va, attr, sizeof(attr));
		ret_val = rnd();
		v_n = o_n = 0;
		r = (u32)t32o_call(F(lib_tisp_sync_sensor_attr), ch, T32O_PTR(va), 0, 0, 0);
		o = t32_lib_sync_sensor_attr(&h, copy, ch, attr);
		bad = r != o || v_n != o_n || v_ch != o_ch || memcmp(copy, t32_data_dd2d4, sizeof(copy)) ||
		      memcmp(va, attr, sizeof(attr));
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH sync_sensor_attr(%x): ret %x/%x\n", ch, r, o);

		vname = t32o_alloc(16);
		strcpy(vname, "/etc/sensor");
		kp_ret = c % 3 == 0 ? 0 : c % 3 == 1 ? (u32)-2 : rnd();
		v_kp = v_put = v_same = o_kp = o_put = o_same = 0;
		r = (u32)t32o_call(F(lib_tisp_is_file_exist), T32O_PTR(vname), 0, 0, 0, 0);
		o = t32_lib_is_file_exist(&h, vname);
		bad = r != o || v_kp != o_kp || v_put != o_put || v_same != o_same || v_flags != o_flags ||
		      v_name != T32O_PTR(vname) || o_name != vname;
		compared++;
		if (bad && failed++ < 10)
			printf("MISMATCH is_file_exist kp %x: ret %x/%x put %u/%u\n", kp_ret, r, o, v_put, o_put);
	}
	printf("t32_libsa_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
