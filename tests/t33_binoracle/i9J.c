/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "i9J": AE face / antiflicker forwarding entry points. */
#include <stdint.h>
#include <string.h>
#include "tx_isp_t33_i9J.h"

extern u32 tisp_ae_api_get_faceae(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 tisp_ae_sepc_area(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 tisp_ae_api_get_antiflick(u32 a0, u32 a1, u32 a2, u32 a3);
extern u32 tisp_ae_api_set_antiflick(u32 a0, u32 a1, u32 a2, u32 a3);

static void i9j_ops(struct t33_i9j_ops *o)
{
	o->face_get = tisp_ae_api_get_faceae;
	o->face_set = tisp_ae_sepc_area;
	o->af_get = tisp_ae_api_get_antiflick;
	o->af_set = tisp_ae_api_set_antiflick;
}

int bo_tisp_ae_face_get(u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct t33_i9j_ops o;

	i9j_ops(&o);
	return t33_ae_face_get(&o, a0, a1, a2, a3);
}

int bo_tisp_ae_face_set(u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct t33_i9j_ops o;

	i9j_ops(&o);
	return t33_ae_face_set(&o, a0, a1, a2, a3);
}

int bo_tisp_g_antiflick(u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct t33_i9j_ops o;

	i9j_ops(&o);
	return t33_g_antiflick(&o, a0, a1, a2, a3);
}

int bo_tisp_s_antiflick(u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct t33_i9j_ops o;

	i9j_ops(&o);
	return t33_s_antiflick(&o, a0, a1, a2, a3);
}
