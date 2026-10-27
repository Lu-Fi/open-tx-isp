/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 IQ bin sections, as the vendor's tisp_bin_data_parse reads them.
 *
 * The bin carries a table of 28 entries of { u16 enable, u16 length } at
 * offset 0x14, the project name at 0x98 and from 0xa0 on the sections packed
 * back to back in table order.  The vendor copies each enabled section into
 * its parameter struct (tparamsP) at a fixed offset (0, 104, 3392, 10132,
 * ...), so the distance between a section in the bin and in tparamsP is not
 * constant: that is why the AE block is at tparamsP + 0x68 in the code but at
 * bin + 0x104, while the LSC block happens to sit at bin + 0x98 + 0x68c4.
 * Sections used here: 1 AE (3288 bytes), 2 AWB (6740), 7 LSC (120).
 */
#ifndef TX_ISP_T32_IQ_H
#define TX_ISP_T32_IQ_H

#ifndef __KERNEL__
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
typedef uint16_t u16;
#endif
#endif

#define T32_IQ_TABLE		0x14U
#define T32_IQ_SECTIONS		28
#define T32_IQ_DATA		0xa0U

#define T32_IQ_SEC_AE		1
#define T32_IQ_SEC_AWB		2
#define T32_IQ_SEC_LSC		7

/* tisp_bin_data_parse's walk: a section is present when its enable is 1, and
 * only present sections occupy the stream.  Offset of section idx in the bin
 * (len: its length), 0 when absent or outside the bin. */
static inline u32 t32_iq_section(const u8 *bin, u32 bytes, unsigned int idx, u32 *len)
{
	u32 off = T32_IQ_DATA, l = 0;
	unsigned int i, en = 0;

	if (!bin || bytes < T32_IQ_DATA || idx >= T32_IQ_SECTIONS)
		return 0;
	for (i = 0; i <= idx; i++) {
		const u8 *e = bin + T32_IQ_TABLE + 4 * i;

		en = (e[0] | e[1] << 8) == 1;
		l = e[2] | e[3] << 8;
		if (i < idx && en)
			off += l;
	}
	if (!en || off + l > bytes)
		return 0;
	if (len)
		*len = l;
	return off;
}

#endif /* TX_ISP_T32_IQ_H */
