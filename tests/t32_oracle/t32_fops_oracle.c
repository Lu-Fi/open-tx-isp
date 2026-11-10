// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the file handlers of driver/t32/tx_isp_t32_fops.h against
 * Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc; the read handlers
 * built through fix_op8_uaccess.sh): camera_mode_fops_read, vpuBs_fops_read,
 * ivdcbuf_fops_read, ispbuf_fops_read, proc_read, camera_mode_fops_open,
 * tisp_show_debug_info_open, video_input_cmd_open, proc_ivdc_open.
 *
 * The kernel calls are recorded on both sides (vendor: the o32 stubs below,
 * ours: struct t32_fops_ops) and compared call by call: arguments (the
 * source object of a copy maps vendor .bss <-> our state field), the bytes
 * copied, return values, and every byte of the vendor .bss / our state.
 * Inputs: random sensor counts 0..5, access masks 0 / 0x80000000 /
 * 0xc0000000 with user addresses on both sides of 0x80000000, partial copies
 * (scripted bytes not copied), text buffers with random strlen, *ppos with
 * a carry into the high word.  Exit 0 = all equal, 1 = mismatch.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fops.h"

u64 camera_mode_fops_read(u32, u32, u32, u32);
u64 vpuBs_fops_read(u32, u32, u32, u32);
u64 ivdcbuf_fops_read(u32, u32, u32, u32);
u64 ispbuf_fops_read(u32, u32, u32, u32);
u64 proc_read(u32, u32, u32, u32);
u64 camera_mode_fops_open(u32, u32, u32, u32);
u64 tisp_show_debug_info_open(u32, u32, u32, u32);
u64 video_input_cmd_open(u32, u32, u32, u32);
u64 proc_ivdc_open(u32, u32, u32, u32);

#define CALL(f, a, b, c, d) t32o_call((t32o_fn)(void (*)(void))(f), a, b, c, d, 0)

/* ---- vendor .bss and kernel stubs ---- */
u8 t32o8_ti[64] __attribute__((aligned(8)));	/* fake thread_info: +24 = addr_limit mask */
u32 t32_data_f35c4, t32_data_f35c8, t32_data_f3760;
char t32_data_f35cc[128], t32_data_f364c[276];
char t32_data_c6cc4[] = "Number of sensors:%d\n";	/* data.c:10070 */

struct call { u32 fn, a[4]; u8 data[300]; };
static struct call vlog[8], olog[8];
static unsigned int vn, on;
static u32 cu_left, ret_open, pde_val;
enum { F_COPY = 1, F_SEQ, F_SOPEN, F_SOPENSZ, F_PDE };

static struct call *rec(struct call *l, unsigned int *n, u32 fn, u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct call *c = &l[*n < 7 ? (*n)++ : 7];

	memset(c, 0, sizeof(*c));
	c->fn = fn; c->a[0] = a0; c->a[1] = a1; c->a[2] = a2; c->a[3] = a3;
	return c;
}

static u32 left_of(u32 n)
{
	return cu_left > n ? n : cu_left;
}

u64 __copy_user(u32 to, u32 from, u32 n, u32 a3)
{
	struct call *c = rec(vlog, &vn, F_COPY, to, from, n, 0);
	u32 left = left_of(n);

	(void)a3;
	memcpy(c->data, T32O_HOST(from), n - left < sizeof(c->data) ? n - left : sizeof(c->data));
	memcpy(T32O_HOST(to), T32O_HOST(from), n - left);
	return left;
}

u64 seq_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct call *c = rec(vlog, &vn, F_SEQ, a0, 0, a2, 0);

	(void)a3;
	snprintf((char *)c->data, sizeof(c->data), "%s", (const char *)T32O_HOST(a1));
	return 0;
}

static u32 id_show(u32 fn);
u64 single_open(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	rec(vlog, &vn, F_SOPEN, a0, id_show(a1), a2, 0);
	return ret_open;
}

u64 private_single_open_size(u32 a0, u32 a1, u32 a2, u32 a3)
{
	rec(vlog, &vn, F_SOPENSZ, a0, id_show(a1), a2, a3);
	return ret_open;
}

u64 PDE_DATA(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	rec(vlog, &vn, F_PDE, a0, 0, 0, 0);
	return pde_val;
}

/* the show functions the vendor open handlers bind (only their identity matters) */
u64 tisp_show_debug_info(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 video_input_cmd_show(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }
u64 tx_isp_ivdc_show(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return 0; }

static int o_proc_read(void *s, void *v) { (void)s; (void)v; return 0; }
static int o_debug_show(void *s, void *v) { (void)s; (void)v; return 0; }
static int o_vic_show(void *s, void *v) { (void)s; (void)v; return 0; }
static int o_ivdc_show(void *s, void *v) { (void)s; (void)v; return 0; }

static u32 id_show(u32 fn)
{
	if (fn == T32O_PTR(proc_read)) return 1;
	if (fn == T32O_PTR(tisp_show_debug_info)) return 2;
	if (fn == T32O_PTR(video_input_cmd_show)) return 3;
	if (fn == T32O_PTR(tx_isp_ivdc_show)) return 4;
	return 0xbad;
}

static u32 id_oshow(t32_show_fn f)
{
	if (f == o_proc_read) return 1;
	if (f == o_debug_show) return 2;
	if (f == o_vic_show) return 3;
	if (f == o_ivdc_show) return 4;
	return 0xbad0;
}

/* ---- our side: struct t32_fops_ops recording the same calls ---- */
static struct t32_fops_state st;
static u8 *ubuf_v, *ubuf_o, *edge_page;
static u32 o_from(const void *p)	/* our source object as the vendor's address */
{
	const char *c = p;

	if (p == &st.camera_mode) return T32O_PTR(&t32_data_f35c4);
	if (p == &st.vpu_bs_size) return T32O_PTR(&t32_data_f35c8);
	if (c >= st.ivdc_buf && c < st.ivdc_buf + 128) return T32O_PTR(t32_data_f35cc) + (u32)(c - st.ivdc_buf);
	if (c >= st.isp_buf && c < st.isp_buf + 276) return T32O_PTR(t32_data_f364c) + (u32)(c - st.isp_buf);
	return 0xbad1;
}

static unsigned long o_copy(void *to, const void *from, unsigned long n)
{
	u32 mask = *(u32 *)(t32o8_ti + 24), a = T32O_PTR(to), left;
	struct call *c;

	if ((a | (a + (u32)n) | (u32)n) & mask)	/* access_ok */
		return n;
	c = rec(olog, &on, F_COPY, a - T32O_PTR(ubuf_o) < 512 ? a - T32O_PTR(ubuf_o) + T32O_PTR(ubuf_v) : a,
		o_from(from), (u32)n, 0);
	left = left_of((u32)n);
	memcpy(c->data, from, n - left < sizeof(c->data) ? n - left : sizeof(c->data));
	memcpy(to, from, n - left);
	return left;
}

static int o_seq(void *seq, const char *fmt, ...)
{
	struct call *c = rec(olog, &on, F_SEQ, T32O_PTR(seq), 0, 0, 0);
	va_list ap;

	va_start(ap, fmt);
	c->a[2] = va_arg(ap, u32);
	va_end(ap);
	snprintf((char *)c->data, sizeof(c->data), "%s", fmt);
	return 0;
}

static int o_sopen(void *file, t32_show_fn show, void *data)
{
	rec(olog, &on, F_SOPEN, T32O_PTR(file), id_oshow(show), T32O_PTR(data), 0);
	return (int)ret_open;
}

static int o_sopensz(void *file, t32_show_fn show, void *data, unsigned long size)
{
	rec(olog, &on, F_SOPENSZ, T32O_PTR(file), id_oshow(show), T32O_PTR(data), (u32)size);
	return (int)ret_open;
}

static void *o_pde(void *inode)
{
	rec(olog, &on, F_PDE, T32O_PTR(inode), 0, 0, 0);
	return T32O_HOST(pde_val);
}

static const struct t32_fops_ops ops = { o_copy, o_seq, o_sopen, o_sopensz, o_pde };

/* ---- compare ---- */
static u32 rs = 0x0f0b5eedu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static unsigned int compared, failed;

static void check(const char *what, u32 vret, u32 oret)
{
	unsigned int i;
	int bad = vret != oret || vn != on;

	for (i = 0; !bad && i < vn; i++)
		bad = memcmp(&vlog[i], &olog[i], sizeof(vlog[i])) != 0;
	bad |= st.camera_mode != t32_data_f35c4 || st.vpu_bs_size != t32_data_f35c8 ||
	       st.sensor_number != t32_data_f3760 || memcmp(st.ivdc_buf, t32_data_f35cc, 128) ||
	       memcmp(st.isp_buf, t32_data_f364c, 276) || memcmp(ubuf_v, ubuf_o, 512);
	compared++;
	if (bad && failed++ < 8) {
		printf("MISMATCH %s: ret vendor %08x ours %08x, calls %u / %u\n", what, vret, oret, vn, on);
		for (i = 0; i < vn || i < on; i++)
			printf("  #%u vendor %u %08x %08x %08x %08x ours %u %08x %08x %08x %08x\n", i,
			       vlog[i].fn, vlog[i].a[0], vlog[i].a[1], vlog[i].a[2], vlog[i].a[3],
			       olog[i].fn, olog[i].a[0], olog[i].a[1], olog[i].a[2], olog[i].a[3]);
	}
}

static void fill_text(char *v, char *o, u32 size)
{
	u32 len = rnd() % size, i;

	for (i = 0; i < size; i++)
		v[i] = o[i] = (char)(1 + rnd() % 255);
	v[len] = o[len] = 0;
	v[size - 1] = o[size - 1] = 0;	/* strlen from *ppos > len stays inside */
}

static void one(u32 c)
{
	static char ib[128], pb[276];
	static const u32 masks[3] = { 0, 0x80000000u, 0xc0000000u };
	u32 to_v, to_o, mask = masks[rnd() % 3], count, which = c % 9;
	u64 pv, po;
	u64 *ppv = t32o_alloc(8);

	vn = on = 0;
	memset(vlog, 0, sizeof(vlog)); memset(olog, 0, sizeof(olog));
	memset(ubuf_v, 0x5a, 512); memset(ubuf_o, 0x5a, 512);
	*(u32 *)(t32o8_ti + 24) = mask;
	cu_left = (rnd() & 3) ? 0 : rnd() % 8;
	ret_open = (rnd() & 1) ? 0 : rnd();
	pde_val = rnd();
	st.ivdc_buf = ib; st.isp_buf = pb;
	st.camera_mode = t32_data_f35c4 = (rnd() & 1) ? rnd() % 6 : rnd();
	st.vpu_bs_size = t32_data_f35c8 = rnd();
	st.sensor_number = t32_data_f3760 = (rnd() & 3) ? rnd() % 6 : rnd();
	fill_text(t32_data_f35cc, ib, 128);
	fill_text(t32_data_f364c, pb, 276);
	/* user buffer: the low pool (passes every mask) or the page below 0x80000000 */
	if (!mask || (rnd() & 1)) {
		to_v = T32O_PTR(ubuf_v); to_o = T32O_PTR(ubuf_o);
	} else {
		to_v = to_o = T32O_PTR(edge_page) + 0x1000 - (rnd() % 0x200);
	}
	count = (rnd() & 1) ? rnd() % 300 : rnd();
	pv = po = (rnd() & 3) ? rnd() % 140 : ((u64)rnd() << 32 | (0xffffffffu - rnd() % 8));
	if ((u32)pv > 120)
		pv = po = (pv & ~0xffffffffull) | (rnd() % 120);
	switch (which) {
	case 0:
		check("camera_mode_fops_read", (u32)CALL(camera_mode_fops_read, rnd(), to_v, count, 0),
		      (u32)t32_camera_mode_read(&ops, &st, T32O_HOST(to_o)));
		break;
	case 1:
		check("vpuBs_fops_read", (u32)CALL(vpuBs_fops_read, rnd(), to_v, count, 0),
		      (u32)t32_vpubs_read(&ops, &st, T32O_HOST(to_o)));
		break;
	case 2:
	case 3: {
		u32 r;

		*ppv = pv;
		r = (u32)CALL(which == 2 ? ivdcbuf_fops_read : ispbuf_fops_read, rnd(), to_v, count, T32O_PTR(ppv));
		check(which == 2 ? "ivdcbuf_fops_read" : "ispbuf_fops_read", r,
		      (u32)t32_textbuf_read(&ops, which == 2 ? st.ivdc_buf : st.isp_buf, T32O_HOST(to_o), count, &po));
		compared++;
		if (*ppv != po && failed++ < 8)
			printf("MISMATCH ppos vendor %llx ours %llx\n", (unsigned long long)*ppv, (unsigned long long)po);
		break;
	}
	case 4: {
		u32 m = rnd();

		check("proc_read", (u32)CALL(proc_read, m, rnd(), 0, 0), (u32)t32_proc_read(&ops, &st, T32O_HOST(m)));
		break;
	}
	case 5: {
		u32 f = rnd();

		check("camera_mode_fops_open", (u32)CALL(camera_mode_fops_open, rnd(), f, 0, 0),
		      (u32)t32_single_show_open(&ops, T32O_HOST(f), o_proc_read));
		break;
	}
	case 6: {
		u32 f = rnd();

		check("tisp_show_debug_info_open", (u32)CALL(tisp_show_debug_info_open, rnd(), f, 0, 0),
		      (u32)t32_single_show_open(&ops, T32O_HOST(f), o_debug_show));
		break;
	}
	case 7:
	case 8: {
		u32 ino = rnd(), f = rnd();

		check(which == 7 ? "video_input_cmd_open" : "proc_ivdc_open",
		      (u32)CALL(which == 7 ? video_input_cmd_open : proc_ivdc_open, ino, f, 0, 0),
		      (u32)t32_pde_show_open(&ops, T32O_HOST(ino), T32O_HOST(f), which == 7 ? o_vic_show : o_ivdc_show,
					     which == 7 ? T32_VIC_CMD_SEQ_SIZE : T32_IVDC_SEQ_SIZE));
		break;
	}
	}
}

int main(void)
{
	unsigned int c, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 45000;

	edge_page = mmap((void *)(uintptr_t)0x7ffff000u, 0x1000, PROT_READ | PROT_WRITE,
			 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	if (edge_page == MAP_FAILED || T32O_PTR(edge_page) != 0x7ffff000u) {
		printf("t32_fops_oracle: cannot map the page below 0x80000000\n");
		return 1;
	}
	for (c = 0; c < n; c++) {
		t32o_reset();
		ubuf_v = t32o_alloc(512);
		ubuf_o = t32o_alloc(512);
		one(c);
	}
	printf("t32_fops_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
