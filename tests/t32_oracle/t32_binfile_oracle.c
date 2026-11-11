// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the IQ bin switch of driver/t32/tx_isp_t32_binfile.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc):
 * tisp_read_bin_file, tisp_core_switch_bin.
 *
 * Compared: the file / fs / allocator calls in order (path text, flags,
 * sizes, the f_pos pointer, set_fs values), the isp_printf calls (format,
 * %s, all words incl. the stack ones and the 64-bit KB count), return
 * values, every byte of the channel state dc284 and common block da850.
 * Inputs: open failures (ERR_PTR values), sizes incl. high words and the
 * sign bit, vmalloc failures, mode 0 / other, loaded flags 0 / 1 / other,
 * bin index 0..5.  Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_binfile.h"

u64 tisp_read_bin_file(u32, u32, u32, u32);
u64 tisp_core_switch_bin(u32, u32, u32, u32);

#define CALL(f, a, b, c) t32o_call((t32o_fn)(void (*)(void))(f), a, b, c, 0, 0)
enum { F_OPEN = 1, F_CLOSE, F_GETFS, F_SETFS, F_VMALLOC, F_VFREE, F_READ, F_PRINTF };

char t32_data_bf228[] = "tisp_read_bin_file";	/* data.c strings */
char t32_data_c81c0[] = "[ISP][%s, %d]: ISP: open %s file for isp calibrate read failed \n";
char t32_data_c8204[] = "[[ %s:%d ]]: Failed to alloc %lld KB buffer!\n";
char t32_data_bf210[] = "tisp_core_switch_bin";
char t32_data_c8234[] = "[[ %s:%d ]] no bin file on the system!!!\n";

static struct t32o8_log *cur;
static u8 *file, *inode, *vbuf;
static u32 r_open_err, r_fs, r_vm_fail, r_read;

static u32 fid(u32 p) { return p == T32O_PTR(file) ? 1 : p == T32O_PTR(vbuf) ? 2 : p == T32O_PTR(file + 48) ? 3 : p; }

static u32 do_open(const char *path, u32 fl, u32 mode)
{
	struct t32o8_call *c = t32o8_rec(cur, F_OPEN, 0, fl, mode, 0);

	snprintf(c->data, sizeof(c->data), "%s", path);
	return r_open_err ? r_open_err : T32O_PTR(file);
}
u64 private_filp_open(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return do_open(T32O_HOST(a0), a1, a2); }
u64 private_filp_close(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; t32o8_rec(cur, F_CLOSE, fid(a0), a1, 0, 0); return 0; }
u64 private_get_fs(u32 a0, u32 a1, u32 a2, u32 a3)	/* mm_segment_t returned through a hidden pointer */
{
	(void)a1; (void)a2; (void)a3;
	t32o8_rec(cur, F_GETFS, 0, 0, 0, 0);
	*(u32 *)T32O_HOST(a0) = r_fs;
	return a0;
}
u64 private_set_fs(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_SETFS, a0, 0, 0, 0); return 0; }
u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	t32o8_rec(cur, F_VMALLOC, a0, 0, 0, 0);
	return r_vm_fail ? 0 : T32O_PTR(vbuf);
}
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_VFREE, fid(a0), 0, 0, 0); return 0; }
u64 private_vfs_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	t32o8_rec(cur, F_READ, fid(a0), fid(a1), a2, fid(a3));
	return r_read;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
	u32 w[5] = { a2, a3, a4, a5, a6 };

	t32o8_rec_fmt(cur, F_PRINTF, a0, T32O_HOST(a1), w, 5);
	return 0;
}

/* ours */
static void *o_open(const char *path, int fl, int mode) { return T32O_HOST(do_open(path, (u32)fl, (u32)mode)); }
static void o_close(void *f, void *id) { private_filp_close(T32O_PTR(f), T32O_PTR(id), 0, 0); }
static u64 o_size(void *f) { u8 *ino = T32O_HOST(*(u32 *)((u8 *)f + 16)); return *(u64 *)(ino + 48); }
static void *o_pos(void *f) { return (u8 *)f + 48; }
static u32 o_getfs(void) { t32o8_rec(cur, F_GETFS, 0, 0, 0, 0); return r_fs; }
static void o_setfs(u32 s) { private_set_fs(s, 0, 0, 0); }
static void *o_vmalloc(u32 n) { return T32O_HOST(private_vmalloc(n, 0, 0, 0)); }
static void o_vfree(void *p) { private_vfree(T32O_PTR(p), 0, 0, 0); }
static int o_read(void *f, void *b, u32 n, void *pos) { return (int)private_vfs_read(T32O_PTR(f), T32O_PTR(b), n, T32O_PTR(pos)); }
static int o_printf(int level, const char *fmt, ...)
{
	u32 w[6];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 6);
	va_end(ap);
	t32o8_rec_fmt(cur, F_PRINTF, (u32)level, fmt, w, n);
	return 0;
}
static const struct t32_binfile_ops ops = { o_open, o_close, o_size, o_pos, o_getfs, o_setfs, o_vmalloc, o_vfree,
					    o_read, o_printf };

static u32 rs = 0xb1bf11e5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, k, failed = 0, compared = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	static u8 dc_o[1808], da_o[704];

	t32o_reset();
	file = t32o_alloc(128); inode = t32o_alloc(128); vbuf = t32o_alloc(64);
	for (c = 0; c < n; c++) {
		u32 which = c & 1, ch = rnd() & 1, vr, orr;
		u8 *attr = t32o_alloc(160);
		int bad;

		t32o_reset();
		file = t32o_alloc(128); inode = t32o_alloc(128); vbuf = t32o_alloc(64); attr = t32o_alloc(160);
		*(u32 *)(file + 16) = T32O_PTR(inode);
		*(u32 *)(inode + 48) = rnd();
		*(u32 *)(inode + 52) = (rnd() & 1) ? 0 : rnd();
		r_open_err = (rnd() % 5) ? 0 : 0xfffff001u + rnd() % 0xfff;
		r_fs = rnd();
		r_vm_fail = !(rnd() % 4);
		r_read = rnd();
		for (k = 0; k < 160; k++)
			attr[k] = (u8)(' ' + rnd() % 90);
		attr[5 + rnd() % 140] = 0;
		attr[159] = 0;
		*(u32 *)attr = (rnd() & 1) ? 0 : rnd();
		attr[4] = (u8)(rnd() % 6);
		for (k = 0; k < 1808; k++)
			t32_data_dc284[k] = dc_o[k] = (rnd() % 3) ? (u8)(rnd() & 1) : (u8)rnd();
		for (k = 0; k < 704; k++)
			t32_data_da850[k] = da_o[k] = (u8)rnd();
		t32o8_clear();
		cur = &t32o8_v;
		if (which)
			vr = (u32)CALL(tisp_core_switch_bin, ch, T32O_PTR(attr), rnd());
		else
			vr = (u32)CALL(tisp_read_bin_file, rnd(), T32O_PTR(attr) + 5, rnd());
		cur = &t32o8_o;
		if (which)
			orr = (u32)t32_core_switch_bin(&ops, dc_o + ch * 904, da_o + ch * 336, attr);
		else
			orr = (u32)t32_read_bin_file(&ops, (const char *)attr + 5);
		bad = t32o8_cmp("binfile", failed < 8) || vr != orr || memcmp(dc_o, t32_data_dc284, 1808) ||
		      memcmp(da_o, t32_data_da850, 704);
		compared++;
		if (bad && failed++ < 8)
		{
			for (k = 0; k < 1808 && dc_o[k] == t32_data_dc284[k]; k++)
				;
			printf("MISMATCH case %u ch %u: ret %08x / %08x dc@%u (%02x/%02x) mode %08x idx %u\n", which, ch, vr, orr, k,
			       k < 1808 ? t32_data_dc284[k] : 0, k < 1808 ? dc_o[k] : 0, *(u32 *)attr, attr[4]);
		}
	}
	printf("t32_binfile_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
