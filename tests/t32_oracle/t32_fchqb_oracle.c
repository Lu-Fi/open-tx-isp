// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our frame channel buffer queue
 * (driver/t32/tx_isp_t32_fch_qbuf.h) against the unmodified vendor
 * frame_channel_vb2_qbuf (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Random channels (buffers, queued list, state, index), v4l2_buffers that
 * pass or fail each check, kernel / user callers, module flags and tables,
 * DMA ops with and without the sync callback, and scripted hook answers run
 * through ours, are restored and run through the vendor; compared: every
 * byte, the module tables, the return value and the ordered hook log (user
 * copies with the copied-out bytes, mutex, spin locks with flags, DMA
 * syncs with address / size / direction, enqueue, fill, isp_printf with
 * all arguments).  Exit 0 = all equal.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fch_qbuf.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
char t32_data_be52c[] = "frame_channel_vb2_qbuf";
char t32_data_c5fcc[] = "The parameter from user is invalid!\n";
char t32_data_c5f08[] = "Failed to copy from user\n";
char t32_data_c5f98[] = "Failed to copy to user\n";
char t32_data_c6104[] = "[%s,%d] failed: Please update the version to pass the uv_addr.\n";
char t32_data_c6144[] = "qbuf: invalid buffer type\n";
char t32_data_c6160[] = "qbuf: buffer index(%d) out of range(%d) \n";
char t32_data_c618c[] = "qbuf: buffer is NULL\n";
char t32_data_c61a4[] = "qbuf: invalid memory type\n";
char t32_data_c61c0[] = "qbuf: invalid memory size, length = %d sizeimage = %d\n";
char t32_data_c61f8[] = "qbuf: buffer already in use [chan index %d] [0x%lx]\n";
u32 t32_data_d9008, t32_data_d9014, mips_dma_map_ops;
u32 t32_data_f37d4[12], t32_data_f37a4[12], t32_data_f3774[12], t32_data_d9ba4[12], t32_data_d9d04[4], t32_data_d9d14[4];

u64 frame_channel_vb2_qbuf(u32, u32, u32, u32);

static u32 rng = 0x9e3779b9u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 16
static struct { unsigned int n; u32 e[MAXE][8]; } EV[2];
static int side;
static u32 r_cfu, r_ctu, ub[24], nl, lk_fl[4], dmaops[16];
static void ev(u32 a, u32 b, u32 c, u32 d)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	}
}
static u32 hash(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}
static u32 h_cfu(u8 *dst, u32 user, u32 n) { ev(1, user, n, 0); memcpy(dst, ub, n); return r_cfu; }
static u32 h_ctu(u32 user, const u8 *src, u32 n) { ev(2, user, n, hash(src, n)); return r_ctu; }
static void h_mlock(u8 *m) { ev(3, T32O_PTR(m), 0, 0); }
static void h_munlock(u8 *m) { ev(4, T32O_PTR(m), 0, 0); }
static void l_common(u32 l, u32 *fl) { ev(5, l, 0, 0); *fl = lk_fl[nl++ % 4]; }
static void h_lock(u8 *l, u32 *fl) { l_common(T32O_PTR(l), fl); }
static void h_unlock(u8 *l, u32 fl) { ev(6, T32O_PTR(l), fl, 0); }
static void h_enq(u8 *vb) { ev(7, T32O_PTR(vb), 0, 0); }
static void f_common(u32 vb, u8 *buf) { ev(8, vb, hash(buf, T32_QB_BUF_BYTES), 0); t32_g_wr32(buf, 4 * (vb % 24), vb); }
static void h_fill(u8 *vb, u8 *buf) { f_common(T32O_PTR(vb), buf); }
static void s_common(u32 dev, u32 a, u32 s, u32 dir) { ev(10, dev, a, s); EV[side].e[EV[side].n - 1][4] = dir; }
static void ho_sync(u32 dev, u32 a, u32 s, u32 dir) { s_common(dev, a, s, dir); }
static u64 hr_sync(u32 dev, u32 a, u32 s, u32 dir) { s_common(dev, a, s, dir); return 0; }
static const struct t32_fchqb_os os = { h_cfu, h_ctu, h_mlock, h_munlock, h_lock, h_unlock, h_enq, h_fill, &mips_dma_map_ops,
					&t32_data_d9008, &t32_data_d9014, t32_data_f37d4, t32_data_f37a4, t32_data_f3774,
					t32_data_d9ba4, t32_data_d9d04, t32_data_d9d14 };
u64 private_copy_from_user(u32 d, u32 u, u32 n, u32 a3) { (void)a3; return h_cfu(T32O_HOST(d), u, n); }
u64 private_copy_to_user(u32 u, u32 s, u32 n, u32 a3) { (void)a3; return h_ctu(u, T32O_HOST(s), n); }
u64 t32o_mutex_lock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(3, m, 0, 0); return 0; }
u64 t32o_mutex_unlock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(4, m, 0, 0); return 0; }
u64 __private_spin_lock_irqsave(u32 l, u32 fl, u32 a2, u32 a3)
{
	u32 v;

	(void)a2; (void)a3;
	l_common(l, &v);
	t32_g_wr32(T32O_HOST(fl), 0, v);
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 l, u32 fl, u32 a2, u32 a3) { (void)a2; (void)a3; ev(6, l, fl, 0); return 0; }
u64 __enqueue_in_driver(u32 vb, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(7, vb, 0, 0); return 0; }
u64 __fill_tisp_buffer(u32 vb, u32 buf, u32 a2, u32 a3) { (void)a2; (void)a3; f_common(vb, T32O_HOST(buf)); return 0; }

/* both sides: level, format text, then each conversion (%s by its text) */
int isp_printf(unsigned int level, const char *fmt, ...)
{
	unsigned int n = EV[side].n++, k = 3;
	const char *f;
	va_list ap;

	va_start(ap, fmt);
	if (n < MAXE) {
		EV[side].e[n][0] = 9; EV[side].e[n][1] = level;
		EV[side].e[n][2] = hash((const u8 *)fmt, (u32)strlen(fmt));
		for (f = fmt; *f && k < 8; f++) {
			if (*f != '%')
				continue;
			while (f[1] && !strchr("sdxuc", f[1]))
				f++;
			f++;
			if (*f == 's') {
				unsigned long p = va_arg(ap, unsigned long);
				const char *s = (const char *)T32O_HOST((u32)p);

				EV[side].e[n][k++] = hash((const u8 *)s, (u32)strlen(s));
			} else {
				EV[side].e[n][k++] = va_arg(ap, unsigned int);
			}
		}
	}
	va_end(ap);
	return 0;
}

#define O_CH	0x0000u
#define O_VB	0x0400u		/* 8 x 0x100 */
#define O_UBUF	0x0c00u
#define IMGSZ	0x0d00u
static u8 *img, save0[IMGSZ], save1[IMGSZ];
static u32 tab0[6][12], tab1[6][12];
static u32 *const tabs[6] = { t32_data_f37d4, t32_data_f37a4, t32_data_f3774, t32_data_d9ba4, t32_data_d9d04, t32_data_d9d14 };
static const u32 tlen[6] = { 12, 12, 12, 12, 4, 4 };

static int fail(const char *what, unsigned int c)
{
	printf("t32_fchqb_oracle: FAIL case %u: %s\n", c, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, j, n = 60000, hit[4] = { 0 }, sync_on;

	img = t32o_alloc(IMGSZ);
	mips_dma_map_ops = T32O_PTR(dmaops);
	for (c = 0; c < n; c++) {
		u8 *ch = img + O_CH;
		u32 chw = T32O_PTR(ch), arg = T32O_PTR(img + O_UBUF), ro = 0, rr = 0, nb = 1 + rnd() % 8, idx;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		t32_g_wr32(ch, T32_QB_QLIST, T32O_PTR(ch + T32_QB_QLIST));
		t32_g_wr32(ch, T32_QB_QLIST + 4, rnd() % 2 ? T32O_PTR(ch + T32_QB_QLIST) : T32O_PTR(img + O_VB + 0x800 - 0x100 + 0xf0));
		t32_g_wr32(ch, T32_QB_NBUFS, nb);
		for (i = 0; i < 8; i++) {
			t32_g_wr32(ch, T32_QB_VBUFS + i * 4, rnd() % 8 ? T32O_PTR(img + O_VB + i * 0x100) : 0);
			t32_g_wr32(img + O_VB + i * 0x100, T32_QB_VB_STATE, rnd() % 5 ? 0 : rnd());
		}
		t32_g_wr32(ch, T32_QB_INDEX, rnd() % 4 ? rnd() % 12 : rnd() % 10);
		t32_g_wr32(ch, T32_QB_STATE, rnd());
		t32_g_wr32(ch, T32_QB_KERNEL, rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd());
		for (i = 0; i < 24; i++)
			ub[i] = rnd();
		ub[0] = rnd() % 8 ? rnd() % nb : rnd() % 10;
		if (rnd() % 10)
			ub[1] = t32_g_rd32(ch, T32_QB_QUEUE);
		if (rnd() % 10)
			ub[12] = t32_g_rd32(ch, T32_QB_MEMORY);
		if (rnd() % 10)
			ub[14] = t32_g_rd32(ch, T32_QB_SIZEIMAGE);
		if (rnd() % 10)
			ub[18] = 1;
		if (rnd() % 2)
			ub[13] &= 0x7fffffffu;
		if (rnd() % 3 == 0)
			ub[23] = 0;
		if (rnd() % 3 == 0)
			ub[21] = 0;
		memcpy(img + O_UBUF, ub, 96);
		for (i = 0; i < 6; i++)
			for (j = 0; j < 12; j++)
				tab0[i][j] = i < 3 ? (rnd() % 4 ? rnd() % 2 : rnd() % 4) : rnd() % 4;
		t32_data_d9008 = rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd();
		t32_data_d9014 = rnd() % 6;
		r_cfu = rnd() % 8 ? 0 : 1 + rnd() % 96;
		r_ctu = rnd() % 8 ? 0 : 1 + rnd() % 96;
		for (i = 0; i < 4; i++)
			lk_fl[i] = rnd();
		sync_on = rnd() % 4 != 0;
		if (rnd() % 20 == 0)
			chw = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		if (rnd() % 20 == 0)
			arg = rnd() % 2 ? 0 : (u32)-1 - rnd() % 4095;
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			for (i = 0; i < 6; i++)
				memcpy(tabs[i], tab0[i], tlen[i] * 4);
			dmaops[T32_QB_DMA_SYNC / 4] = !sync_on ? 0 : side ? T32O_PTR(hr_sync) : T32O_PTR(ho_sync);
			EV[side].n = 0;
			nl = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				ro = (u32)t32_frame_channel_vb2_qbuf(&os, T32O_HOST(chw), arg);
				memcpy(save1, img, IMGSZ);
				for (i = 0; i < 6; i++)
					memcpy(tab1[i], tabs[i], tlen[i] * 4);
			} else {
				rr = (u32)t32o_call(R(frame_channel_vb2_qbuf), chw, arg, 0, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c);
		for (i = 0; i < 6; i++)
			if (memcmp(tabs[i], tab1[i], tlen[i] * 4))
				return fail("module tables", c);
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c);
		idx = EV[0].n;
		hit[ro == 0 ? (idx > 4 ? 0 : 3) : ro == (u32)-22 ? 1 : 2]++;
	}
	printf("t32_fchqb_oracle: %u cases, 0 mismatches; queued / -22 / -12 or other / skipped: %u %u %u %u\n",
	       n, hit[0], hit[1], hit[2], hit[3]);
	return 0;
}
