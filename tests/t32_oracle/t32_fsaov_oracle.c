// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our frame-source AOV prepare / resume
 * (driver/t32/tx_isp_t32_fs_aov.h) against the unmodified vendor
 * fs_prepare_module and fs_resume_module (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc).  A random image (fs device, 5 channels, source buffers, save
 * areas) and the module tables d9954 / d9c04 run through ours, are restored
 * and run through the vendor; compared: every byte, both tables, the return
 * value and the ordered log of the hook calls (stream-off / -on with the type,
 * reqbufs and set_fmt with their bytes, qbuf address, completion polls,
 * isp_printf level / format text / arguments).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fs_aov.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
/* vendor rodata (data.c, decoded) */
char t32_data_be4f0[] = "fs_resume_module", t32_data_be504[] = "fs_prepare_module";
char t32_data_c6318[] = "[%s %d] AOV fs is NULL\n", t32_data_c6330[] = "[%s %d] fs state is error\n";
char t32_data_c634c[] = "[%s %d] qbufs is error\n", t32_data_c6364[] = "isp frame channel, complete failed\n";
char t32_data_c6914[] = "[%s %d] Enter the FS prepare ... \n";
char t32_data_c6938[] = "framesource channel memory space does not exist!\n";
char t32_data_c696c[] = "[%s %d] stream off is error\n", t32_data_c698c[] = "[%s %d] reqbufs is error\n";
u32 t32_data_d9954[8], t32_data_d9c04[8];

u64 fs_prepare_module(u32, u32, u32, u32);
u64 fs_resume_module(u32, u32, u32, u32);

static u32 rng = 0x9e3779b9u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 160
static struct { unsigned int n; u32 e[MAXE][6]; } EV[2];
static int side;
static u32 sc[8];
static unsigned int tw_left, ncall;
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
/* hook results: mostly 0, sometimes the scripted value of the call kind */
static int res(unsigned int k) { ncall++; return (sc[k] >> (ncall % 8) & 1) && (sc[7] & 1 << k) ? (int)(sc[k] | 1) : 0; }
static int h_soff(u8 *ch, u8 *t) { ev(1, T32O_PTR(ch), t32_g_rd32(t, 0), 0); return res(0); }
static int h_son(u8 *ch, u8 *t) { ev(2, T32O_PTR(ch), t32_g_rd32(t, 0), 0); return res(1); }
static int h_req(u8 *ch, u8 *q) { ev(3, T32O_PTR(ch), hash(q, T32_REQBUFS_BYTES), 0); return res(2); }
static int h_fmt(u8 *ch, u8 *f) { ev(4, T32O_PTR(ch), hash(f, T32_FCH_FMT_BYTES), 0); return res(3); }
static int h_qbuf(u8 *ch, u32 b) { ev(5, T32O_PTR(ch), b, 0); return res(4); }
static int h_tw(u8 *c) { ev(6, T32O_PTR(c), 0, 0); if (tw_left) { tw_left--; return 1 + (int)(sc[5] % 7); } return 0; }
static const struct t32_fs_aov_os os = { h_soff, h_son, h_req, h_fmt, h_qbuf, h_tw };
static const struct t32_fs_aov_tabs tabs = { t32_data_d9954, t32_data_d9c04 };

u64 frame_channel_vb2_streamoff(u32 c, u32 t, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)h_soff(T32O_HOST(c), T32O_HOST(t)); }
u64 frame_channel_vb2_streamon(u32 c, u32 t, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)h_son(T32O_HOST(c), T32O_HOST(t)); }
u64 frame_channel_reqbufs(u32 c, u32 q, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)h_req(T32O_HOST(c), T32O_HOST(q)); }
u64 frame_channel_vidioc_set_fmt(u32 c, u32 f, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)h_fmt(T32O_HOST(c), T32O_HOST(f)); }
u64 frame_channel_vb2_qbuf(u32 c, u32 b, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)h_qbuf(T32O_HOST(c), b); }
u64 try_wait_for_completion(u32 c, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return (u32)h_tw(T32O_HOST(c)); }

int isp_printf(unsigned int level, const char *fmt, ...)
{
	unsigned int n = EV[side].n++, k = 3;
	const char *f;
	va_list ap;

	va_start(ap, fmt);
	if (n < MAXE) {
		EV[side].e[n][0] = 20; EV[side].e[n][1] = level; EV[side].e[n][2] = hash((const u8 *)fmt, (u32)strlen(fmt));
		for (f = fmt; *f && k < 6; f++) {
			if (*f != '%')
				continue;
			f++;
			if (*f == 's') {
				unsigned long p = va_arg(ap, unsigned long);
				const char *s = side ? (const char *)T32O_HOST((u32)p) : (const char *)p;

				EV[side].e[n][k++] = hash((const u8 *)s, (u32)strlen(s));
			} else {
				EV[side].e[n][k++] = va_arg(ap, unsigned int);
			}
		}
	}
	va_end(ap);
	return 0;
}

#define O_FS	0x0000u
#define O_CH	0x0200u		/* 5 x 816 */
#define O_SRC	0x1200u		/* 8 x 140 */
#define O_SAVE	0x1700u		/* 5 x 8 x 140 */
#define IMGSZ	0x2d00u
static u8 *img, save0[IMGSZ], save1[IMGSZ];
static u32 t0[16], t1[16];

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_fsaov_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

int main(void)
{
	unsigned int c, i, k, n = 30000, eff[2] = { 0 }, tw0 = 0;

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % 2;
		u8 *fs = img + O_FS;
		u32 fsw = T32O_PTR(fs), ro = 0, rr = 0;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		for (i = 0; i < 8; i++)
			sc[i] = rnd();
		if (rnd() % 2)
			sc[7] = 0;		/* every hook succeeds */
		tw0 = rnd() % 4;
		t32_g_wr32(fs, T32_FS_CHANS, T32O_PTR(img + O_CH));
		t32_g_wr32(fs, T32_FS_NCHAN, rnd() % 8 == 0 ? 0x80000000u + rnd() % 4 : rnd() % 6);
		t32_g_wr32(fs, T32_FS_STATE, rnd() % 5 ? 2 + rnd() % 2 : rnd() % 2 ? 1 : rnd());
		for (i = 0; i < 8; i++) {
			t32_data_d9954[i] = rnd() % 6 == 0 ? 0 : T32O_PTR(img + O_SAVE + (i % 5) * 1120);
			t32_data_d9c04[i] = rnd() % 8 == 0 ? (u32)-1 - rnd() % 2 : rnd() % 9;
		}
		for (i = 0; i < 5; i++) {
			u8 *ch = img + O_CH + i * 816;
			u32 nb = rnd() % 9;

			t32_g_wr32(ch, T32_FCH_STATE, rnd() % 4 ? (op ? 5 : 4) : 3 + rnd() % 3);
			t32_g_wr32(ch, T32_FCH_NBUFS, nb);
			for (k = 0; k < 8; k++)
				t32_g_wr32(ch, T32_FCH_VBUFS + 4 * k, T32O_PTR(img + O_SRC + (rnd() % 8) * 140));
		}
		if (rnd() % 10 == 0)
			fsw = rnd() % 2 ? 0 : 0xfffff001u + rnd() % 0xfff;
		memcpy(save0, img, IMGSZ);
		memcpy(t0, t32_data_d9954, 32);
		memcpy(t0 + 8, t32_data_d9c04, 32);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			memcpy(t32_data_d9954, t0, 32);
			memcpy(t32_data_d9c04, t0 + 8, 32);
			EV[side].n = 0;
			ncall = 0;
			tw_left = tw0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				if (op == 0)
					ro = (u32)t32_fs_prepare_module(&os, &tabs, T32O_HOST(fsw));
				else
					ro = (u32)t32_fs_resume_module(&os, &tabs, T32O_HOST(fsw));
				memcpy(save1, img, IMGSZ);
				memcpy(t1, t32_data_d9954, 32);
				memcpy(t1 + 8, t32_data_d9c04, 32);
			} else {
				if (op == 0)
					rr = (u32)t32o_call(R(fs_prepare_module), fsw, 0, 0, 0, 0);
				else
					rr = (u32)t32o_call(R(fs_resume_module), fsw, 0, 0, 0, 0);
			}
		}
		if (ro != rr)
			return fail("return value", c, op);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c, op);
		if (memcmp(t1, t32_data_d9954, 32) || memcmp(t1 + 8, t32_data_d9c04, 32))
			return fail("module tables", c, op);
		if (EV[0].n > MAXE || EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c, op);
		if (memcmp(img, save0, IMGSZ))
			eff[op]++;
	}
	printf("t32_fsaov_oracle: %u cases, 0 mismatches; changed memory per op: %u %u\n", n, eff[0], eff[1]);
	return 0;
}
