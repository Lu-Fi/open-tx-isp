/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 video link destroy: vendor tx_isp_video_link_destroy.isra.18 (0x15c08), called by tx_isp_release
 * (dev - 12, input 0..3, for every input whose link count is set) and the DESTROY_SUBDEV_LINKS ioctl.
 *
 * (lk, input), lk = ISP device - 12: +304 + 4 i sensor of input i, +320 + 4 s link count of set s.
 * Set index: input, +8 when the input has a sensor; the multi-sensor direct modes take input + 4
 * (mode 1 input 0, mode 2 inputs < 2, mode 3 < 3, mode 4 < 4).  A set with count <= 0 returns 0.
 * Every link of the set (20 bytes: source pad descriptor +0, sink +8, flags +16) is looked up with
 * find_subdev_link_pad on both ends; when both pads exist, both pads' links (+8) are destroyed and
 * the second result returned unless 0 or -ENOIOCTLCMD.  After the last link the count is cleared.
 * Same flow as the T32 (tx_isp_t32_txlink.h); the T33 ISP device is 4 bytes shorter before the
 * link words and the mode word is the direct_mode module parameter.
 *
 * Link sets (vendor .rodata 0x1648 -> .data descriptor arrays): 4 x 3 single-sensor links isp-m0
 * output k -> isp-fs, 4 x 4 IVDC sets (m0 -> ivdc -> fs on the first of three), 4 x 6 sets over isp-m1.
 *
 * Our driver has no subdevice link graph (the link ioctls are accepted and ignored), so this port is
 * not wired; it is checked against the vendor instructions by tests/t33_binoracle (block linkd,
 * openimp #99).
 */
#ifndef TX_ISP_T33_TXLINK_H
#define TX_ISP_T33_TXLINK_H

#define T33_LINK_SETS		12U
#define T33_LK_SENSOR(i)	(304U + 4U * (i))
#define T33_LK_COUNT(s)		(320U + 4U * (s))
#define T33_PAD_LINK		8U
#define T33_LINK_ENOIOCTLCMD	515

struct t33_pad_desc {
	const char *name;
	unsigned char type, index;	/* type 1 input pad, 2 output pad */
	unsigned short pad;
};

struct t33_link_desc {
	struct t33_pad_desc src, snk;
	unsigned int flags;
};

struct t33_link_set {
	const struct t33_link_desc *links;
	int n;
};

#define T33_LX(a, ta, ia, b, tb, ib)	{ { a, ta, ia, 0 }, { b, tb, ib, 0 }, 0x50 }
#define T33_L(a, b)			T33_LX(a, b)	/* a, b: pad (name, type, index) */
#define T33_M0(i)			"isp-m0", 2, i
#define T33_M1I(i)			"isp-m1", 1, i
#define T33_M1O(i)			"isp-m1", 2, i
#define T33_IVI(i)			"isp-ivdc", 1, i
#define T33_IVO(i)			"isp-ivdc", 2, i
#define T33_FS(i)			"isp-fs", 2, i
#define T33_L3(k)	{ T33_L(T33_M0(k), T33_FS(k)), T33_L(T33_M0(k + 1), T33_FS(k + 1)), T33_L(T33_M0(k + 2), T33_FS(k + 2)) }
#define T33_L4(k, v)	{ T33_L(T33_M0(k), T33_IVI(v)), T33_L(T33_IVO(v), T33_FS(k)), \
			  T33_L(T33_M0(k + 1), T33_FS(k + 1)), T33_L(T33_M0(k + 2), T33_FS(k + 2)) }
#define T33_L6(k)	{ T33_L(T33_M0(k), T33_M1I(k)), T33_L(T33_M1O(k), T33_FS(k)), \
			  T33_L(T33_M0(k + 1), T33_M1I(k + 1)), T33_L(T33_M1O(k + 1), T33_FS(k + 1)), \
			  T33_L(T33_M0(k + 2), T33_M1I(k + 2)), T33_L(T33_M1O(k + 2), T33_FS(k + 2)) }

static const struct t33_link_desc t33_links3[4][3] = { T33_L3(0), T33_L3(3), T33_L3(6), T33_L3(9) };
static const struct t33_link_desc t33_links4[4][4] = { T33_L4(0, 0), T33_L4(3, 1), T33_L4(6, 2), T33_L4(9, 3) };
static const struct t33_link_desc t33_links6[4][6] = { T33_L6(0), T33_L6(3), T33_L6(6), T33_L6(9) };

static const struct t33_link_set t33_link_sets[T33_LINK_SETS] = {
	{ t33_links3[0], 3 }, { t33_links3[1], 3 }, { t33_links3[2], 3 }, { t33_links3[3], 3 },
	{ t33_links4[0], 4 }, { t33_links4[1], 4 }, { t33_links4[2], 4 }, { t33_links4[3], 4 },
	{ t33_links6[0], 6 }, { t33_links6[1], 6 }, { t33_links6[2], 6 }, { t33_links6[3], 6 },
};

struct t33_link_ops {
	unsigned char *(*find_pad)(unsigned char *lk, const struct t33_pad_desc *d);	/* find_subdev_link_pad */
	int (*destroy_link)(unsigned char *link);					/* subdev_video_destroy_link */
};

static inline int t33_video_link_destroy(const struct t33_link_ops *ops, unsigned char *lk, unsigned int input,
					 unsigned int mode)
{
	unsigned int set = *(unsigned int *)(lk + T33_LK_SENSOR(input)) ? input + 8 : input;
	const struct t33_link_desc *d;
	int k;

	if ((mode == 1 && input == 0) || (mode == 2 && input < 2) || (mode == 3 && input < 3) ||
	    (mode == 4 && input < 4))
		set = input + 4;
	if (*(int *)(lk + T33_LK_COUNT(set)) <= 0)
		return 0;
	d = t33_link_sets[set].links;
	for (k = 0; k < t33_link_sets[set].n; k++, d++) {
		unsigned char *src = ops->find_pad(lk, &d->src), *snk = ops->find_pad(lk, &d->snk);
		int r;

		if (!src || !snk)
			continue;
		ops->destroy_link(src + T33_PAD_LINK);
		r = ops->destroy_link(snk + T33_PAD_LINK);
		if (r && r != -T33_LINK_ENOIOCTLCMD)
			return r;
	}
	*(int *)(lk + T33_LK_COUNT(set)) = 0;
	return 0;
}

#endif /* TX_ISP_T33_TXLINK_H */
