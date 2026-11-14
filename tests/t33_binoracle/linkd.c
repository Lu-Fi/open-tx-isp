/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 binary oracle, block "linkd": the video link destroy (driver/t32/tx_isp_t33_txlink.h) with the vendor
 * calling convention and callees, against tx_isp_video_link_destroy.isra.18 (spec: linkd.conf).
 */
#include "tx_isp_t33_txlink.h"

extern unsigned char *find_subdev_link_pad(unsigned char *lk, const struct t33_pad_desc *d);
extern int subdev_video_destroy_link(unsigned char *link);
unsigned int direct_mode;

static const struct t33_link_ops bo_ops = { .find_pad = find_subdev_link_pad, .destroy_link = subdev_video_destroy_link };

int bo_linkd(unsigned char *lk, unsigned int input) { return t33_video_link_destroy(&bo_ops, lk, input, direct_mode); }
