/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 AE ioctl attribute glue: scene (hlc / blc), target, only-read, scene get.
 * Ports of tx_isp_ae_hlc_or_blc_g_attr, tx_isp_ae_only_read_g_attr,
 * tx_isp_ae_scence_g_attr, tx_isp_ae_target_g_attr and tx_isp_ae_target_s_attr
 * (the .isra clones).  The kernel services (user copies, the tisp_* calls and
 * isp_printf) are reached through the ops table; the scratch blocks are the
 * caller's (the vendor keeps them on its stack).
 */
#ifndef TX_ISP_T33_ATTRAE2_H
#define TX_ISP_T33_ATTRAE2_H

#ifndef __KERNEL__
#include <string.h>
#endif
#include "tx_isp_t33_blk.h"

#define T33_AE2_C_SCENE_G 1
#define T33_AE2_C_SCENE_S 2
#define T33_AE2_C_EXPR_G 3
#define T33_AE2_C_AT_G 4
#define T33_AE2_C_AT_S 5
#define T33_AE2_C_EV_G 6
#define T33_AE2_C_EV_S 7
#define T33_AE2_C_BV 8
#define T33_AE2_C_STABLE 9

#define T33_AE2_LOG_CALL 1
#define T33_AE2_LOG_COPY 2

#define T33_AE2_SCN_BYTES 40U
#define T33_AE2_HLC_BYTES 8U
#define T33_AE2_TGT_BYTES 264U
#define T33_AE2_LIST 128U
#define T33_AE2_TGT_AT 8U
#define T33_AE2_TGT_EV 136U
#define T33_AE2_OR_BYTES 48U

struct t33_ae2_ops {
	int (*copy_in)(void *priv, u8 *dst, u32 user, u32 n);
	int (*copy_out)(void *priv, u32 user, const u8 *src, u32 n);
	void (*print)(void *priv, u32 level);
	int (*call)(void *priv, u32 id, u32 ch, u8 *buf);
	void *priv;
};

static inline void t33_ae2_put(u8 *p, u32 v)
{
	p[0] = (u8)v;
	p[1] = (u8)(v >> 8);
	p[2] = (u8)(v >> 16);
	p[3] = (u8)(v >> 24);
}

/* tx_isp_ae_hlc_or_blc_g_attr: out (8 bytes) is the caller's persistent scratch */
static inline int t33_ae_hlc_or_blc_g(const struct t33_ae2_ops *o, u32 ch, u32 user, u8 *scn, u8 *out)
{
	u32 a, b, m, k;
	int r = o->call(o->priv, T33_AE2_C_SCENE_G, ch, scn);

	if (r) {
		o->print(o->priv, T33_AE2_LOG_CALL);
		return r;
	}
	a = t33_le32(scn);
	m = scn[4];
	b = t33_le32(scn + 8);
	k = scn[12];
	if (!m && !k) {
		t33_ae2_put(out, 0);
		out[4] = 0;
	} else if (a == 3 && !k) {
		t33_ae2_put(out, 1);
		out[4] = (u8)m;
	} else if (b == 3 && !m) {
		t33_ae2_put(out, 2);
		out[4] = (u8)k;
	} else {
		o->print(o->priv, T33_AE2_LOG_CALL);
	}
	r = o->copy_out(o->priv, user, out, T33_AE2_HLC_BYTES);
	if (r)
		o->print(o->priv, T33_AE2_LOG_COPY);
	return r;
}

/* tx_isp_ae_scence_g_attr */
static inline int t33_ae_scene_g(const struct t33_ae2_ops *o, u32 ch, u32 user, u8 *scn)
{
	int r = o->call(o->priv, T33_AE2_C_SCENE_G, ch, scn);

	if (r)
		o->print(o->priv, T33_AE2_LOG_CALL);
	r = o->copy_out(o->priv, user, scn, T33_AE2_SCN_BYTES);
	if (r)
		o->print(o->priv, T33_AE2_LOG_COPY);
	return r;
}

/* tx_isp_ae_target_s_attr: user is the user pointer itself */
static inline int t33_ae_target_s(const struct t33_ae2_ops *o, u32 ch, u32 user, u8 *in, u8 *scn,
				  u8 *at, u8 *ev)
{
	int r = o->copy_in(o->priv, in, user, T33_AE2_TGT_BYTES);

	if (r)
		o->print(o->priv, T33_AE2_LOG_COPY);
	r = o->call(o->priv, T33_AE2_C_SCENE_G, ch, scn);
	if (r)
		goto fail;
	t33_ae2_put(scn + 16, t33_le32(in));
	t33_ae2_put(scn + 20, t33_le32(in + 4));
	r = o->call(o->priv, T33_AE2_C_SCENE_S, ch, scn);
	if (r)
		goto fail;
	memcpy(at, in + T33_AE2_TGT_AT, T33_AE2_LIST);
	r = o->call(o->priv, T33_AE2_C_AT_S, ch, at);
	if (r)
		goto fail;
	memcpy(ev, in + T33_AE2_TGT_EV, T33_AE2_LIST);
	r = o->call(o->priv, T33_AE2_C_EV_S, ch, ev);
	if (!r)
		return 0;
fail:
	o->print(o->priv, T33_AE2_LOG_CALL);
	return r;
}

/* tx_isp_ae_target_g_attr */
static inline int t33_ae_target_g(const struct t33_ae2_ops *o, u32 ch, u32 user, u8 *scn, u8 *at,
				  u8 *ev, u8 *out)
{
	int r = o->call(o->priv, T33_AE2_C_SCENE_G, ch, scn);

	if (r)
		goto fail;
	t33_ae2_put(out, t33_le32(scn + 16));
	t33_ae2_put(out + 4, t33_le32(scn + 20));
	r = o->call(o->priv, T33_AE2_C_AT_G, ch, at);
	if (r)
		goto fail;
	memcpy(out + T33_AE2_TGT_AT, at, T33_AE2_LIST);
	r = o->call(o->priv, T33_AE2_C_EV_G, ch, ev);
	if (r)
		goto fail;
	memcpy(out + T33_AE2_TGT_EV, ev, T33_AE2_LIST);
	r = o->copy_out(o->priv, user, out, T33_AE2_TGT_BYTES);
	if (!r)
		return 0;
	o->print(o->priv, T33_AE2_LOG_COPY);
	return r;
fail:
	o->print(o->priv, T33_AE2_LOG_CALL);
	return r;
}

/* tx_isp_ae_only_read_g_attr: expo 220 bytes, scn 40, stable 12, bv 4, out 48 (zeroed) */
static inline int t33_ae_only_read_g(const struct t33_ae2_ops *o, u32 ch, u32 user, u8 *expo, u8 *scn,
				     u8 *stable, u8 *bv, u8 *out)
{
	int r;

	if (o->call(o->priv, T33_AE2_C_EXPR_G, ch, expo))
		o->print(o->priv, T33_AE2_LOG_CALL);
	t33_ae2_put(out, t33_le32(expo + 204));
	t33_ae2_put(out + 4, t33_le32(expo + 208));
	t33_ae2_put(out + 8, t33_le32(expo + 212));
	t33_ae2_put(out + 12, 0);
	t33_ae2_put(out + 16, t33_le32(expo + 216));
	if (o->call(o->priv, T33_AE2_C_SCENE_G, ch, scn))
		o->print(o->priv, T33_AE2_LOG_CALL);
	t33_ae2_put(out + 20, t33_le32(scn + 32));
	t33_ae2_put(out + 24, t33_le32(scn + 36));
	o->call(o->priv, T33_AE2_C_BV, ch & 0xff, bv);
	t33_ae2_put(out + 40, t33_le32(bv));
	o->call(o->priv, T33_AE2_C_STABLE, ch & 0xff, stable);
	t33_ae2_put(out + 28, t33_le32(stable));
	t33_ae2_put(out + 32, t33_le32(stable + 4));
	t33_ae2_put(out + 36, t33_le32(stable + 8));
	r = o->copy_out(o->priv, user, out, T33_AE2_OR_BYTES);
	if (r)
		o->print(o->priv, T33_AE2_LOG_COPY);
	return r;
}

#endif /* TX_ISP_T33_ATTRAE2_H */
