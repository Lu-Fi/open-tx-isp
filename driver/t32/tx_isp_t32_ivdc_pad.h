/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 IVDC (direct VIC -> encoder path): pad event handler from the vendor
 * module (Matt Davis' recovered C, PR #15): ivdc_pad_event_handle (0xf11c).
 *
 * Pad: subdev +0, pad index byte +4, enabled byte +5, frame channel +32
 * (stride source +332, height +336, buffer words +496 / +500 / +504, its
 * register base +284).  Subdev: pad array +276 (36 bytes per pad), IVDC
 * private +280 (register base +284, format copy +328 (116 bytes), mutex
 * +456).  Module data: cf2dc / cf2e0 / cf2e4 one-shot flags, ce230 memory
 * stride, d900c / d9010 line counts, d9014 IVDC mode, d9898 stream count,
 * d98b4 / d98b8 IVDC memory.
 *
 * Events 0x3000001..0x3000008 (others and a disabled pad: 0):
 *  1  forward with the argument;
 *  2  set format: copy, per-pad frame geometry, path setup, mode >= 2 checks
 *     the stride (-22), forward (tail call);
 *  3  stream on: enable the IRQ on the first, in modes 1..4 count and arm
 *     the IVDC, forward without argument;
 *  4  stream off: count down, on the last disable the IRQ and re-arm the
 *     one-shot flags, forward, then with IVDC memory set the idle setup;
 *  5  forward with the argument's +40 set to 4096;
 *  6  0;
 *  7  reqbufs: IVDC buffer size and address (-12 without memory), forward;
 *  8  forward without argument.
 * A forward returns the remote result, events 1 / 3 / 4 map -515 to 0.
 *
 * Vendor quirks kept: the stream-off idle setup writes +96 / +92 twice in
 * modes >= 2; mode 0 / 1 / >= 2 size formulas as in the vendor (32-bit).
 * Vendor UB (crashes, not reachable from a registered pad): the pad's
 * NULL / error checks come after its enabled byte is read; a NULL subdev
 * or private pointer is read through.
 *
 * Checked bit-exact against the vendor flow by tests/t32_oracle
 * (t32_ivdcp_oracle).
 */
#ifndef TX_ISP_T32_IVDC_PAD_H
#define TX_ISP_T32_IVDC_PAD_H

#include "tx_isp_t32_glue.h"

int isp_printf(unsigned int level, const char *fmt, ...);

#define T32_IVP_EV_BASE		0x3000001U
#define T32_IVP_SD_PADS		276U
#define T32_IVP_SD_PRIV		280U
#define T32_IVP_PAD_BYTES	36U
#define T32_IVP_REGS		284U
#define T32_IVP_FMT		328U
#define T32_IVP_FMT_BYTES	116U
#define T32_IVP_MUTEX		456U
#define T32_IVP_MAX_LINE	3840U
#define T32_IVP_EBUSY_REMOTE	(-515)

struct t32_ivdcp_os {
	int (*send)(u8 *pad, u32 event, u8 *arg);	/* tx_isp_send_event_to_remote */
	void (*enable_irq)(u8 *sd);
	void (*disable_irq)(u8 *sd);
	void (*mutex_lock)(u8 *m);
	void (*mutex_unlock)(u8 *m);
	u32 *cf2dc, *cf2e0, *cf2e4;
	const u32 *ce230, *d900c;
	u32 *d9010;
	const u32 *d9014;
	u32 *d9898;
	const u32 *d98b4, *d98b8;
};

static inline u32 t32_ivp_stride64(u32 v)
{
	return (u32)((int)(v + 63) / 64);
}

static inline int t32_ivp_forward(const struct t32_ivdcp_os *os, u8 *pad, u32 event, u8 *arg)
{
	u8 *sd = T32_PTR(t32_g_rd32(pad, 0));

	return os->send(T32_PTR(t32_g_rd32(sd, T32_IVP_SD_PADS)) + pad[4] * T32_IVP_PAD_BYTES, event, arg);
}

/* event 2: the per-pad frame geometry (pad 0 has its own layout) */
static inline void t32_ivp_geometry(const struct t32_ivdcp_os *os, u8 *r, u32 n, u32 w, u32 h)
{
	u32 half = h >> 1, d = *os->d900c, base;

	if (n == 0) {
		t32_g_wr32(r, 16, (h & 0xfff) << 16 | (w & 0xfff));
		t32_g_wr32(r, 20, (half & 0xfff) << 16 | ((w + 7) & 0xff8));
		t32_g_wr32(r, 76, half | 0x1100000U);
		t32_g_wr32(r, 80, half);
		t32_g_wr32(r, 448, h << 16 | w);
		t32_g_wr32(r, 208, 512);
		t32_g_wr32(r, 164, d ? d : half);
		t32_g_wr32(r, 168, d ? h - d : half);
		return;
	}
	if (n > 3)
		return;
	base = 1536 + (n - 1) * 64;
	t32_g_wr32(r, base, (h & 0xfff) << 16 | (w & 0xfff));
	t32_g_wr32(r, base + 4, (half & 0xfff) << 16 | ((w + 7) & 0xff8));
	t32_g_wr32(r, base + 20, half);
	t32_g_wr32(r, base + 24, half);
	t32_g_wr32(r, base + 44, h << 16 | w);
	t32_g_wr32(r, base + 28, d ? d : half);
	t32_g_wr32(r, base + 32, d ? h - d : half);
}

static inline int t32_ivp_set_format(const struct t32_ivdcp_os *os, u8 *pad, u8 *arg)
{
	u8 *sd = T32_PTR(t32_g_rd32(pad, 0)), *priv = T32_PTR(t32_g_rd32(sd, T32_IVP_SD_PRIV)), *r;
	u32 w, h, mode = *os->d9014;

	memcpy(priv + T32_IVP_FMT, arg, T32_IVP_FMT_BYTES);
	w = t32_g_rd32(arg, 4);
	h = t32_g_rd32(arg, 8);
	isp_printf(0, "width = %d, height = %d\n", w, h);
	r = T32_PTR(t32_g_rd32(priv, T32_IVP_REGS));
	if (*os->cf2e4 == 1) {
		if (mode == 1)
			t32_g_wr32(r, 32, (w + 63) >> 6 << 6);
		else if ((int)mode >= 2)
			t32_g_wr32(r, 32, t32_ivp_stride64(*os->ce230) << 6);
		*os->cf2e4 = 0;
	}
	t32_ivp_geometry(os, r, pad[4], w, h);
	t32_g_wr32(r, 36, 0xe488);
	t32_g_wr32(r, 88, 0x41081);
	t32_g_wr32(r, 100, 7);
	t32_g_wr32(r, 132, 1);
	t32_g_wr32(r, 104, 0x10000);
	if (mode == 1) {
		t32_g_wr32(r, 96, 0x3010c703U);
	} else if ((int)mode >= 2) {
		t32_g_wr32(r, 96, 0x3010f703U);
		t32_g_wr32(r, 100, 3);
		t32_g_wr32(r, 104, 0);
		if (*os->ce230 < w) {
			isp_printf(1, "\x1b[31m ivdc_mem_stride=%d, width=%d, not support. \x1b[0m\n", *os->ce230, w);
			return -22;
		}
	}
	t32_g_wr32(r, 108, 0x3000000);
	t32_g_wr32(r, 60, 0x10000000);
	t32_g_wr32(r, 72, (u32)((int)*os->d9010 / 2) | 0x200000);
	return t32_ivp_forward(os, pad, T32_IVP_EV_BASE + 1, arg);
}

static inline int t32_ivp_stream_on(const struct t32_ivdcp_os *os, u8 *pad)
{
	u8 *sd = T32_PTR(t32_g_rd32(pad, 0)), *priv = T32_PTR(t32_g_rd32(sd, T32_IVP_SD_PRIV));
	int ret;

	if (!*os->d9898)
		os->enable_irq(sd);
	if (*os->d9014 - 1 < 4) {
		u8 *r = T32_PTR(t32_g_rd32(priv, T32_IVP_REGS));

		if (*os->cf2dc == 1) {
			t32_g_wr32(r, 4, 2);
			*os->cf2dc = 0;
		} else {
			t32_g_wr32(r, 4, 34);
		}
		os->mutex_lock(priv + T32_IVP_MUTEX);
		(*os->d9898)++;
		os->mutex_unlock(priv + T32_IVP_MUTEX);
		r = T32_PTR(t32_g_rd32(priv, T32_IVP_REGS));
		t32_g_wr32(r, 236, 0);
		t32_g_wr32(r, 8, 44);
	}
	ret = t32_ivp_forward(os, pad, T32_IVP_EV_BASE + 2, NULL);
	return ret == T32_IVP_EBUSY_REMOTE ? 0 : ret;
}

static inline int t32_ivp_stream_off(const struct t32_ivdcp_os *os, u8 *pad)
{
	u8 *sd = T32_PTR(t32_g_rd32(pad, 0)), *priv = T32_PTR(t32_g_rd32(sd, T32_IVP_SD_PRIV)), *r;
	u32 mode;
	int ret;

	os->mutex_lock(priv + T32_IVP_MUTEX);
	(*os->d9898)--;
	os->mutex_unlock(priv + T32_IVP_MUTEX);
	if (!*os->d9898) {
		os->disable_irq(sd);
		*os->cf2dc = 1;
		*os->cf2e0 = 1;
		*os->cf2e4 = 1;
	}
	ret = t32_ivp_forward(os, pad, T32_IVP_EV_BASE + 3, NULL);
	if (ret && ret != T32_IVP_EBUSY_REMOTE)
		return ret;
	if (!*os->d98b4)
		return 0;
	r = T32_PTR(t32_g_rd32(priv, T32_IVP_REGS));
	mode = *os->d9014;
	if ((int)mode >= 2) {
		t32_g_wr32(r, 96, 0x3000c703U);
		t32_g_wr32(r, 92, mode == 2 ? 0x3c00000U : mode == 3 ? 0x5c00000U : 0x7c00000U);
	}
	t32_g_wr32(r, 0, 3);
	mode = *os->d9014;
	if ((int)mode >= 2) {
		t32_g_wr32(r, 96, 0x3010c703U);
		t32_g_wr32(r, 92, (mode == 2 ? 0x3c40000U : mode == 3 ? 0x5c40000U : 0x7c40000U) + 0x1100);
	}
	return 0;
}

static inline int t32_ivp_reqbufs(const struct t32_ivdcp_os *os, u8 *pad, u8 *arg)
{
	static const char fn[] = "ivdc_frame_channel_reqbufs";
	u8 *fc = T32_PTR(t32_g_rd32(pad, 32)), *r;
	u32 mode = *os->d9014, stride = 0, lines, lo, mem;

	if (mode == 1) {
		stride = (t32_g_rd32(fc, 332) + 63) >> 6 << 6;
		if (!*os->d9010) {
			u32 h = t32_g_rd32(fc, 336);

			*os->d9010 = h >> 1;
			t32_g_wr32(fc, 504, h * 3 * stride >> 2);
		} else {
			t32_g_wr32(fc, 504, *os->d9010 * 3 * stride >> 1);
		}
	} else if ((int)mode >= 2) {
		if (!*os->d9010) {
			t32_g_wr32(fc, 504, (u32)((int)(t32_ivp_stride64(*os->ce230) * 192U * 2160U) / 4));
			*os->d9010 = 1080;
		} else {
			t32_g_wr32(fc, 504, (u32)((int)(t32_ivp_stride64(*os->ce230) * 192U * *os->d9010) / 2));
		}
	}
	if (mode == 1 || (int)mode >= 2) {
		mem = *os->d98b8;
		if (!mem) {
			isp_printf(2, "ivdc mem paddr is NULL\n");
			return -12;
		}
		t32_g_wr32(fc, 500, mem);
		t32_g_wr32(fc, 496, mem + 0x80000000U);
		if (mem == 0x80000000U) {
			isp_printf(2, "dma alloc frame buffer Failed\n");
			return -12;
		}
		r = T32_PTR(t32_g_rd32(fc, T32_IVP_REGS));
		if (mode == 1) {
			isp_printf(1, "[%s] s%d stride:%d, ivdc_mem_line=%d\n", fn, pad[4], stride, *os->d9010);
			isp_printf(0, "ivdc_paddr = 0x%08x\n", t32_g_rd32(fc, 500));
			lines = *os->d9010;
			lo = (u32)((int)lines / 2);
			if (lines > T32_IVP_MAX_LINE)
				lines = T32_IVP_MAX_LINE;
			if (lo > T32_IVP_MAX_LINE)
				lo = T32_IVP_MAX_LINE;
			t32_g_wr32(r, 48, lo << 16 | lines);
			t32_g_wr32(r, 52, t32_g_rd32(fc, 500));
			t32_g_wr32(r, 56, stride * lines + t32_g_rd32(fc, 500));
		} else {
			isp_printf(0, "ivdc_paddr = 0x%08x\n", mem);
			if (*os->cf2e0 == 1) {
				lines = *os->d9010;
				lo = (u32)((int)lines / 2);
				if (lines > T32_IVP_MAX_LINE)
					lines = T32_IVP_MAX_LINE;
				*os->cf2e0 = 0;
				if (lo > T32_IVP_MAX_LINE)
					lo = T32_IVP_MAX_LINE;
				t32_g_wr32(r, 48, lo << 16 | lines);
				t32_g_wr32(r, 52, t32_g_rd32(fc, 500));
				t32_g_wr32(r, 56, t32_ivp_stride64(*os->ce230) * (lines << 6) + t32_g_rd32(fc, 500));
			}
		}
	}
	if (*os->d9014) {
		r = T32_PTR(t32_g_rd32(fc, T32_IVP_REGS));
		t32_g_wr32(r, 40, 0);
		t32_g_wr32(r, 44, 0);
	}
	return t32_ivp_forward(os, pad, T32_IVP_EV_BASE + 6, arg);
}

static inline int t32_ivdc_pad_event_handle(const struct t32_ivdcp_os *os, u8 *pad, u32 event, u8 *arg)
{
	int ret;

	if (!pad[5] || event - T32_IVP_EV_BASE >= 8)
		return 0;
	switch (event - T32_IVP_EV_BASE) {
	case 0:
		ret = t32_ivp_forward(os, pad, event, arg);
		return ret == T32_IVP_EBUSY_REMOTE ? 0 : ret;
	case 1:
		return t32_ivp_set_format(os, pad, arg);
	case 2:
		return t32_ivp_stream_on(os, pad);
	case 3:
		return t32_ivp_stream_off(os, pad);
	case 4:
		t32_g_wr32(arg, 40, 4096);
		return t32_ivp_forward(os, pad, event, arg);
	case 6:
		return t32_ivp_reqbufs(os, pad, arg);
	case 7:
		return t32_ivp_forward(os, pad, event, NULL);
	}
	return 0;
}

#endif
