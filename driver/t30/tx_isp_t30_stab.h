/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T30 IMAGE_TUNING_CID_SYSTEM_TAB (0x800002c): the 0x70-byte block the
 * libimp sends (isp_core_stab_attr, as on the T20) holds the 60-byte
 * Apical system table followed by one enable byte per table item; only
 * the items whose enable byte is set are copied into the live table.
 * The libimp uses it for MoveState (day/night motion limits), the
 * integration-time cap and manual sinter/temper.
 *
 * The lifted apical_isp_stab_s_attr read the enable bytes from an
 * uninitialised local and the values 0x10 bytes off, so a block either
 * did nothing or wrote garbage into the table.
 */
#ifndef TX_ISP_T30_STAB_H
#define TX_ISP_T30_STAB_H

#define T30_STAB_BYTES		0x3c	/* system table */
#define T30_STAB_BLOCK		0x70	/* table + enable bytes */
#define T30_STAB_ITEMS		52

/* byte offset and size of every system table item, in table order
 * (global_freeze_firmware .. global_dis_y); enable byte = 0x3c + item */
static const struct { uint8_t off, len; } t30_stab_items[T30_STAB_ITEMS] = {
	{ 0, 1 }, { 1, 1 }, { 2, 1 }, { 3, 1 }, { 4, 1 }, { 5, 1 }, { 6, 1 }, { 7, 1 },
	{ 8, 1 }, { 9, 1 }, { 10, 1 }, { 11, 1 }, { 12, 1 }, { 13, 1 }, { 14, 1 }, { 15, 1 },
	{ 16, 4 },		/* manual exposure time */
	{ 20, 1 }, { 21, 1 }, { 22, 1 }, { 23, 1 },
	{ 24, 2 }, { 26, 2 },	/* integration time, maximum */
	{ 28, 1 }, { 29, 1 }, { 30, 1 }, { 31, 1 }, { 32, 1 }, { 33, 1 }, { 34, 1 }, { 35, 1 },
	{ 36, 1 }, { 37, 1 }, { 38, 1 }, { 39, 1 }, { 40, 1 }, { 41, 1 }, { 42, 1 }, { 43, 1 },
	{ 44, 1 }, { 45, 1 }, { 46, 1 }, { 47, 1 }, { 48, 1 }, { 49, 1 }, { 50, 1 }, { 51, 1 },
	{ 52, 1 }, { 53, 1 }, { 54, 1 },
	{ 56, 2 }, { 58, 2 },	/* DIS x, y */
};

static inline void t30_stab_apply(uint8_t *stab, const uint8_t *blk)
{
	unsigned int i;

	for (i = 0; i < T30_STAB_ITEMS; i++)
		if (blk[T30_STAB_BYTES + i])
			memcpy(stab + t30_stab_items[i].off, blk + t30_stab_items[i].off,
			       t30_stab_items[i].len);
}

#endif /* TX_ISP_T30_STAB_H */
