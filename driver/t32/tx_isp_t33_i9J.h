/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 AE face and antiflicker entry points: tisp_ae_face_get / _set and
 * tisp_g_antiflick / tisp_s_antiflick.  Each forwards its four argument words
 * unchanged to one AE API function (reached through struct t33_i9j_ops) and
 * returns 0.
 */
#ifndef TX_ISP_T33_I9J_H
#define TX_ISP_T33_I9J_H

#include "tx_isp_t33_blk.h"

typedef u32 (*t33_i9j_fn)(u32 a0, u32 a1, u32 a2, u32 a3);

struct t33_i9j_ops {
	t33_i9j_fn face_get;		/* tisp_ae_api_get_faceae */
	t33_i9j_fn face_set;		/* tisp_ae_sepc_area */
	t33_i9j_fn af_get;		/* tisp_ae_api_get_antiflick */
	t33_i9j_fn af_set;		/* tisp_ae_api_set_antiflick */
};

/* tisp_ae_face_get */
static inline int t33_ae_face_get(const struct t33_i9j_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	o->face_get(a0, a1, a2, a3);
	return 0;
}

/* tisp_ae_face_set */
static inline int t33_ae_face_set(const struct t33_i9j_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	o->face_set(a0, a1, a2, a3);
	return 0;
}

/* tisp_g_antiflick */
static inline int t33_g_antiflick(const struct t33_i9j_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	o->af_get(a0, a1, a2, a3);
	return 0;
}

/* tisp_s_antiflick */
static inline int t33_s_antiflick(const struct t33_i9j_ops *o, u32 a0, u32 a1, u32 a2, u32 a3)
{
	o->af_set(a0, a1, a2, a3);
	return 0;
}

#endif /* TX_ISP_T33_I9J_H */
