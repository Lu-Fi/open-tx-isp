// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our lib_tisp_save_file (driver/t32/tx_isp_t32_libfile.h) against Matt Davis' mechanical
 * MIPS-to-C recovery (ref/, PR #15 @ 2e2e06bc).
 *
 * Compared bit-exact: every OS call in order with its arguments (path, flags, mode, file, buffer, length,
 * position pointer, address limits, close id) and the return value.  Inputs: random buffers / lengths /
 * paths, file pointers (also an error pointer), old address limits and close results.
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libfile.h"

u64 lib_tisp_save_file(u32, u32, u32, u32);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

static u32 rs = 0x082efa98u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

struct ev { u32 id, a[4]; };
#define NE 8
static struct ev ve[NE], oe[NE];
static unsigned int vn, on;
static u32 file, oldfs, close_ret;

static void vadd(u32 id, u32 a0, u32 a1, u32 a2, u32 a3) { if (vn < NE) ve[vn] = (struct ev){ id, { a0, a1, a2, a3 } }; vn++; }
static void oadd(u32 id, u32 a0, u32 a1, u32 a2, u32 a3) { if (on < NE) oe[on] = (struct ev){ id, { a0, a1, a2, a3 } }; on++; }

u64 private_filp_open(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; vadd(1, a0, a1, a2, 0); return file; }
u64 private_get_fs(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vadd(2, 0, 0, 0, 0); *(u32 *)(uintptr_t)a0 = oldfs; return rnd(); }
u64 private_set_fs(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; vadd(3, a0, 0, 0, 0); return rnd(); }
u64 private_vfs_write(u32 a0, u32 a1, u32 a2, u32 a3) { vadd(4, a0, a1, a2, a3); return rnd(); }
u64 private_filp_close(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; vadd(5, a0, a1, 0, 0); return close_ret; }

static void *o_open(void *c, const char *p, u32 fl, u32 m) { (void)c; oadd(1, (u32)(uintptr_t)p, fl, m, 0); return (void *)(uintptr_t)file; }
static void o_getfs(void *c, u32 *old) { (void)c; oadd(2, 0, 0, 0, 0); *old = oldfs; }
static void o_setfs(void *c, u32 fs) { (void)c; oadd(3, fs, 0, 0, 0); }
static u32 o_write(void *c, void *f, const void *b, u32 l, void *pos)
{
	(void)c;
	oadd(4, (u32)(uintptr_t)f, (u32)(uintptr_t)b, l, (u32)(uintptr_t)pos);
	return 0;
}
static u32 o_close(void *c, void *f, void *id) { (void)c; oadd(5, (u32)(uintptr_t)f, (u32)(uintptr_t)id, 0, 0); return close_ret; }

int main(void)
{
	unsigned int c, compared = 0, failed = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_file_hooks h = { NULL, o_open, o_getfs, o_setfs, o_write, o_close };

	for (c = 0; c < n; c++) {
		u32 buf = rnd() & 0x7fffffffu, len = rnd() % 3 ? rnd() % 4096 : rnd(), path = rnd() & 0x7fffffffu, r, o;

		vn = on = 0;
		memset(ve, 0, sizeof(ve));
		memset(oe, 0, sizeof(oe));
		file = c % 17 == 0 ? (u32)-(1 + rnd() % 4095) : rnd() & 0x7ffffff0u;
		oldfs = rnd();
		close_ret = rnd();
		r = (u32)t32o_call(F(lib_tisp_save_file), buf, len, path, rnd(), 0);
		o = t32_lib_save_file(&h, (const void *)(uintptr_t)buf, len, (const char *)(uintptr_t)path);
		compared++;
		if ((r != o || vn != on || memcmp(ve, oe, sizeof(ve))) && failed++ < 10)
			printf("MISMATCH case %u: ret %x/%x calls %u/%u\n", c, r, o, vn, on);
	}
	printf("t32_libfile_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
