// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our frame channel file operations (driver/t32/tx_isp_t32_fch.h)
 * against the unmodified vendor frame_channel_vb2_streamoff,
 * frame_channel_vidioc_get_fmt, frame_channel_open_fast and
 * frame_channel_release, frame_channel_vidioc_set_fmt (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc).  A random image (2 channels of 816 bytes,
 * a kernel argument word) runs through ours, is restored and runs through the
 * vendor; compared: every byte, the return values and the ordered log of the
 * hook calls (user copies with their data, remote event, stream-off, mutex,
 * completion, queue free, isp_printf level / format / channel).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_fch.h"

#define WEAK __attribute__((weak))
#define R(f)	((t32o_fn)(void (*)(void))(f))
WEAK char t32_data_c5fcc[4], t32_data_c5f08[4], t32_data_c5f74[4], t32_data_c5f98[4], t32_data_c63c4[4];
WEAK char t32_data_c5f24[4], t32_data_c5f40[4], t32_data_c5f58[4];
u32 t32_data_f3370[8], t32_data_d9008, t32_data_d9004;
u32 t32_data_f37d4[8], t32_data_f37a4[8], t32_data_f3774[8], t32_data_d9954[8], t32_data_d9b74[8];

u64 frame_channel_vb2_streamoff(u32, u32, u32, u32);
u64 frame_channel_vidioc_get_fmt(u32, u32, u32, u32);
u64 frame_channel_open_fast(u32, u32, u32, u32);
u64 frame_channel_release(u32, u32, u32, u32);
u64 frame_channel_vidioc_set_fmt(u32, u32, u32, u32);

static u32 rng = 0x6b8b4567u;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 16
static struct { unsigned int n; u32 e[MAXE][4]; } EV[2];
static int side;
static u32 r_cfu, r_ctu, r_ev, r_soff, cfu_val, ev_fill, ev_seed;
static u8 fmt_in[116];
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
static u32 hash(const u8 *p, u32 n);
static u32 h_cfu(u8 *dst, u32 user, u32 n)
{
	ev(1, user, n, 0);
	if (n == 116)
		memcpy(dst, fmt_in, 116);
	else
		memcpy(dst, &cfu_val, 4);
	return r_cfu;
}
static u32 h_ctu(u32 user, const u8 *src, u32 n) { ev(2, user, n, hash(src, n)); return r_ctu; }
static int h_ev(u32 sd, u32 cmd, u8 *data)
{
	u32 i, s = ev_seed;

	ev(3, sd, cmd, hash(data, 116));
	for (i = 0; i < ev_fill; i++) {
		s = s * 1103515245u + 12345u;
		data[i] = (u8)(s >> 16);
	}
	return (int)r_ev;
}
static int h_soff(u8 *ch, u32 type) { ev(4, T32O_PTR(ch), type, 0); return (int)r_soff; }
static void h_lock(u8 *m) { ev(5, T32O_PTR(m), 0, 0); }
static void h_unlock(u8 *m) { ev(6, T32O_PTR(m), 0, 0); }
static void h_initc(u8 *c) { ev(7, T32O_PTR(c), 0, 0); }
static void h_qfree(u8 *q, u32 m) { ev(8, T32O_PTR(q), m, 0); }
static void h_kfree(u32 p) { ev(10, p, 0, 0); }
static const struct t32_fch_os os = { h_cfu, h_ctu, h_ev, h_soff, h_lock, h_unlock, h_initc, h_qfree,
				      &t32_data_d9008, &t32_data_d9004, h_kfree };
static const struct t32_fch_tabs tabs = { t32_data_f37d4, t32_data_f37a4, t32_data_f3774, t32_data_d9954 };

u64 private_copy_from_user(u32 d, u32 u, u32 n, u32 a3) { (void)a3; return h_cfu(T32O_HOST(d), u, n); }
u64 private_copy_to_user(u32 u, u32 s, u32 n, u32 a3) { (void)a3; return h_ctu(u, T32O_HOST(s), n); }
u64 tx_isp_send_event_to_remote(u32 sd, u32 cmd, u32 d, u32 a3) { (void)a3; return (u32)h_ev(sd, cmd, T32O_HOST(d)); }
u64 __frame_channel_vb2_streamoff(u32 ch, u32 t, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)h_soff(T32O_HOST(ch), t); }
u64 t32o_mutex_lock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(5, m, 0, 0); return 0; }
u64 t32o_mutex_unlock(u32 m, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(6, m, 0, 0); return 0; }
u64 private_init_completion(u32 c, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; ev(7, c, 0, 0); return 0; }
u64 __vb2_queue_free(u32 q, u32 m, u32 a2, u32 a3) { (void)a2; (void)a3; ev(8, q, m, 0); return 0; }
u64 private_kfree(u32 p, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; h_kfree(p); return 0; }

static const char *const fmts[] = { "The parameter from user is invalid!\n", "Failed to copy from user\n",
	"Failed to set fmt to frame chan%d!\n", "Failed to copy to user\n",
	"Frame channel%d is slake now, Please activate it firstly!\n", "The frame type is invalid!\n",
	"The field is invalid!\n", "The colorspace is invalid!\n" };
int isp_printf(unsigned int level, const char *fmt, ...)
{
	const char *vend[] = { t32_data_c5fcc, t32_data_c5f08, t32_data_c5f74, t32_data_c5f98, t32_data_c63c4,
			       t32_data_c5f24, t32_data_c5f40, t32_data_c5f58 };
	va_list ap;
	u32 id = 99, a2;

	for (a2 = 0; a2 < 8; a2++)
		if (side ? fmt == vend[a2] : !strcmp(fmt, fmts[a2]))
			id = a2;
	va_start(ap, fmt);
	a2 = (u32)va_arg(ap, unsigned long);
	va_end(ap);
	ev(9, level, id, id == 2 || id == 4 ? a2 : 0);
	return 0;
}

#define O_CH	0x0000u		/* 2 x 816 */
#define O_KARG	0x0680u
#define O_FILE	0x0700u
#define IMGSZ	0x0780u
static u8 *img, save0[IMGSZ], save1[IMGSZ];
static u32 ours_tab[8], ours_cnt[8];

static int fail(const char *what, unsigned int c, unsigned int op)
{
	printf("t32_fch_oracle: FAIL case %u op %u: %s\n", c, op, what);
	return 1;
}

static u32 pick_ret(void)
{
	switch (rnd() % 4) {
	case 0: return 0;
	case 1: return (u32)T32_ENOIOCTLCMD;
	case 2: return rnd() % 2 ? 1 : (u32)-1;
	default: return rnd();
	}
}

int main(void)
{
	unsigned int c, i, n = 40000, eff[5] = { 0 }, freed = 0;
	u32 tsave[8], csave[8];

	img = t32o_alloc(IMGSZ);
	for (c = 0; c < n; c++) {
		unsigned int op = c % 5;
		u32 chw, arg, ro = 0, rr = 0, minor = rnd() % 8;

		for (i = 0; i < IMGSZ; i++)
			img[i] = (u8)rnd();
		r_cfu = rnd() % 3 ? 0 : rnd() % 3 == 0 ? 1 : rnd() % 2 ? 4 : rnd();
		r_ctu = rnd() % 3 ? 0 : rnd() % 2 ? 116 : rnd();
		r_ev = pick_ret();
		r_soff = rnd();
		cfu_val = rnd();
		ev_fill = rnd() % 3 ? rnd() % 117 : 116;
		ev_seed = rnd();
		t32_data_d9008 = rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd();
		t32_data_d9004 = rnd() % 2 ? 0 : rnd() % 2 ? 1 : rnd();
		for (i = 0; i < 8; i++) {
			u32 k = rnd() % 8;

			t32_data_f3370[i] = k == 0 ? 0 : k == 1 ? 0xfffff001u + rnd() % 0xfff : T32O_PTR(img + O_CH + (k % 2) * 816);
		}
		chw = T32O_PTR(img + O_CH + (rnd() % 2) * 816);
		switch (rnd() % 8) {
		case 0: chw = 0; break;
		case 1: chw = rnd() % 2 ? 0xfffff001u : 0xfffff001u + rnd() % 0xfff; break;
		default: break;
		}
		switch (rnd() % 6) {
		case 0: arg = 0; break;
		case 1: arg = rnd() % 2 ? 0xfffff001u : 0xfffff001u + rnd() % 0xfff; break;
		case 2: arg = 0xfffff000u; break;
		default: arg = T32O_PTR(img + O_KARG); break;
		}
		for (i = 0; i < 2; i++) {
			u8 *ch = img + O_CH + i * 816;
			u32 k = rnd() % 8;

			t32_g_wr32(ch, T32_FCH_STATE, k < 5 ? k : k == 5 ? 0x80000000u : rnd());
			if (rnd() % 2 || arg == 0xfffff000u)	/* the boundary word is no host address */
				t32_g_wr32(ch, T32_FCH_KARG, 0);
		}
		for (i = 0; i < 8; i++) {
			t32_data_f37d4[i] = rnd() % 4 ? rnd() : 0;
			t32_data_f37a4[i] = rnd() % 4 ? rnd() : 0;
			t32_data_f3774[i] = rnd() % 4 ? rnd() : 0;
			t32_data_d9954[i] = rnd() % 3 ? rnd() : 0;
		}
		for (i = 0; i < 8; i++)
			t32_data_d9b74[i] = rnd() % 3;
		for (i = 0; i < 116; i++)
			fmt_in[i] = (u8)rnd();
		t32_g_wr32(fmt_in, 0, rnd() % 6 ? 1 : rnd() % 2 ? 0 : rnd());
		t32_g_wr32(fmt_in, 16, rnd() % 3 == 0 ? 0 : rnd() % 6 ? 4 : rnd() % 3 == 0 ? 1 : rnd() % 2 ? 5 : rnd());
		t32_g_wr32(fmt_in, 28, rnd() % 6 ? 8 : rnd() % 2 ? 9 : rnd());
		memcpy(img + O_KARG, fmt_in, 116);
		if (op == 4) {		/* a kernel argument must be a real address (the vendor copies from it) */
			arg = T32O_PTR(img + O_KARG);
			if (rnd() % 3 == 0) {
				arg = rnd();
				t32_g_wr32(img + O_CH, T32_FCH_KARG, 0);
				t32_g_wr32(img + O_CH + 816, T32_FCH_KARG, 0);
			}
		}
		if (op >= 3) {		/* the vendor indexes its tables unchecked: keep the index in range */
			for (i = 0; i < 2; i++)
				t32_g_wr32(img + O_CH + i * 816, T32_FCH_INDEX, rnd() % 8);
			t32_g_wr32(img + O_FILE, 120, chw);
		}
		memcpy(tsave, t32_data_d9954, sizeof(tsave));
		memcpy(csave, t32_data_d9b74, sizeof(csave));
		memcpy(save0, img, IMGSZ);
		for (side = 0; side < 2; side++) {
			memcpy(img, save0, IMGSZ);
			memcpy(t32_data_d9954, tsave, sizeof(tsave));
			memcpy(t32_data_d9b74, csave, sizeof(csave));
			EV[side].n = 0;
			memset(EV[side].e, 0, sizeof(EV[side].e));
			if (!side) {
				switch (op) {
				case 0: ro = (u32)t32_frame_channel_vb2_streamoff(&os, T32O_HOST(chw), arg); break;
				case 1: ro = (u32)t32_frame_channel_vidioc_get_fmt(&os, T32O_HOST(chw), arg); break;
				case 2: ro = (u32)t32_frame_channel_open_fast(&os, t32_data_f3370, minor); break;
				case 3: ro = (u32)t32_frame_channel_release(&os, &tabs, img + O_FILE); break;
				default: ro = (u32)t32_frame_channel_vidioc_set_fmt(&os, t32_data_f3774, t32_data_d9b74, T32O_HOST(chw), arg); break;
				}
				memcpy(save1, img, IMGSZ);
				memcpy(ours_tab, t32_data_d9954, sizeof(ours_tab));
				memcpy(ours_cnt, t32_data_d9b74, sizeof(ours_cnt));
			} else {
				switch (op) {
				case 0: rr = (u32)t32o_call(R(frame_channel_vb2_streamoff), chw, arg, 0, 0, 0); break;
				case 1: rr = (u32)t32o_call(R(frame_channel_vidioc_get_fmt), chw, arg, 0, 0, 0); break;
				case 2: rr = (u32)t32o_call(R(frame_channel_open_fast), minor, 0, 0, 0, 0); break;
				case 3: rr = (u32)t32o_call(R(frame_channel_release), 0, T32O_PTR(img + O_FILE), 0, 0, 0); break;
				default: rr = (u32)t32o_call(R(frame_channel_vidioc_set_fmt), chw, arg, 0, 0, 0); break;
				}
			}
		}
		if (ro != rr)
			return fail("return value", c, op);
		if (memcmp(img, save1, IMGSZ))
			return fail("memory image", c, op);
		if (memcmp(ours_tab, t32_data_d9954, sizeof(ours_tab)))
			return fail("d9954 table", c, op);
		if (memcmp(ours_cnt, t32_data_d9b74, sizeof(ours_cnt)))
			return fail("d9b74 table", c, op);
		if (memcmp(tsave, t32_data_d9954, sizeof(tsave)))
			freed++;
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e)))
			return fail("hook call log", c, op);
		if (memcmp(img, save0, IMGSZ))
			eff[op]++;
	}
	printf("t32_fch_oracle: %u cases, 0 mismatches; cases that change memory per op: %u %u %u %u %u; d9954 freed %u\n", n,
	       eff[0], eff[1], eff[2], eff[3], eff[4], freed);
	return 0;
}
