/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 (PRJ008) IQ bin append data (openimp #99): tisp_get_module_enum and tisp_bin_append_data_parse of the vendor
 * tx-isp-t33.ko, binary oracle block "append" (tests/t33_binoracle/append.conf).
 *
 * Behind the 28 sections and their look-up payloads (tx_isp_t32_iq.h) a PRJ008 bin may carry append data, which
 * the header CRC does not cover: 8 header bytes (the word at +4 must not be 0) and records
 *	+0x00  name, a C string (APPEND_LDC, APPEND_LSC, ...; tisp_get_module_enum gives its number)
 *	+0x10  u32 enable (1)
 *	+0x14  u32 offset of the next record from the start of the append data (0, or >= the append size: last)
 *	+0x18  u32 data bytes
 *	+0x1c  data
 * The first record is at +8; at most 32 records are read.  The vendor uses two kinds, every other name gets the
 * "append type is not supported" message (the cv2005 bins carry APPEND_AWB and APPEND_CLM: nothing is stored):
 *   0x0f APPEND_LDC  tisp_ldc_get_extra_par (tx_isp_t33_ldcpar.h): 80 parameter bytes for lib_tisp_ldc_get_par
 *   0x10 APPEND_LSC  further LSC mesh tables, 0x3606 bytes each: the table as in the section 7 payload (0x3602
 *                    bytes: u16 colour temperature, three planes), then the u16 for the section's list at 0x28
 *                    (tisp_look_up_ct_list's hysteresis) and the byte for its list at 0x5b.  The section's table
 *                    count (byte 0x94) grows by the number of tables; the tables go behind the payload
 *                    (stLutInfo size, 16 bit: a second APPEND_LSC record lands on the same place, as the vendor's).
 *
 * tx_isp_t33_binapp.h (oracle block binapp) is the same parse on the vendor's own globals (tparamsP, stLutInfo),
 * without bounds; this is the form the driver runs.
 *
 * Not the vendor's (it has no checks and reads / writes past its buffers): a record must lie inside the append
 * data, and an APPEND_LSC record that would give more than T33_APP_LSC_MAX tables is ignored (the section's lists
 * hold 8 entries; the vendor writes on into the next bytes of its parameter block).
 */
#ifndef TX_ISP_T33_APPEND_H
#define TX_ISP_T33_APPEND_H

#include "tx_isp_t33_ldcpar.h"

#define T33_APP_REC		0x1cU		/* record header */
#define T33_APP_RECS		32
#define T33_APP_LDC		0x0f
#define T33_APP_LSC		0x10
#define T33_APP_LSC_REC		0x3606U		/* table + u16 + u8 (+ pad) */
#define T33_APP_LSC_TABLE	0x3602U		/* = T33_LSC_TABLE_BYTES */
#define T33_APP_LSC_MAX		8U
#define T33_APP_LSC_CNT		0x94U		/* section 7: number of tables */
#define T33_APP_LSC_HY		0x28U		/* section 7: u16 per table */
#define T33_APP_LSC_FLAG	0x5bU		/* section 7: u8 per table */

/* messages of tisp_bin_append_data_parse (log callback) */
#define T33_APP_LOG_NULL	0	/* no data or size < 8 (returns -1) */
#define T33_APP_LOG_LSC0	1	/* LSC mesh lut size = 0 */
#define T33_APP_LOG_TYPE	2	/* append type is not supported */
#define T33_APP_LOG_LDCSZ	3	/* tisp_ldc_get_extra_par: LDC par size is invalid */

/* tisp_get_module_enum: the number of an append record's name, 0x18 for an unknown one */
static inline u32 t33_append_enum(const char *name)
{
	static const char * const n[] = {
		"APPEND_TOP", "APPEND_AE", "APPEND_AF", "APPEND_AWB", "APPEND_BCSH", "APPEND_CCM", "APPEND_CDNS",
		"APPEND_CLM", "APPEND_CSC", "APPEND_CSCCR", "APPEND_DEFOG", "APPEND_DMSC", "APPEND_DPC",
		"APPEND_GAMMA", "APPEND_LCE", "APPEND_LDC", "APPEND_LSC", "APPEND_MDNS", "APPEND_MSCA",
		"APPEND_RDNS", "APPEND_SDNS", "APPEND_TMO", "APPEND_YDNS", "APPEND_YSP",
	};
	u32 i, k;

	for (i = 0; i < sizeof(n) / sizeof(n[0]); i++) {
		for (k = 0; name[k] == n[i][k]; k++)
			if (!name[k])
				return i;
	}
	return 0x18;
}

/* the LSC state the append data changes (the vendor's tparamsP section 7, stLutInfo and lutMaxSize parts) */
struct t33_app_lsc {
	u8 *sec;	/* section 7, writable (0x98 bytes) */
	u8 *lut;	/* the tables: payload, then the appended ones (stLutInfo + 0x14) */
	u32 size16;	/* payload bytes (stLutInfo + 2, 16 bit) */
	u32 cap;	/* bytes at lut (lutMaxSize) */
};

struct t33_app_ops {
	/* make lut hold `need` bytes: a new buffer with the first size16 bytes of the old one; 0 or an error */
	int (*grow)(struct t33_app_lsc *l, u32 need);
	void (*log)(int what, u32 val);
};

/* one APPEND_LSC record (enable 1, bytes != 0): rec its header, room the append bytes from rec on */
static inline void t33_append_lsc(struct t33_app_lsc *l, const struct t33_app_ops *ops, const u8 *rec, u32 room)
{
	u32 n = t33_le32(rec + 0x18) / T33_APP_LSC_REC, cnt, need, k, idx;
	const u8 *src = rec + T33_APP_REC;

	if (!l || !l->sec)
		return;
	cnt = n + l->sec[T33_APP_LSC_CNT];
	if (cnt > T33_APP_LSC_MAX || room < T33_APP_REC || n > (room - T33_APP_REC) / T33_APP_LSC_REC)
		return;		/* not the vendor's: see above */
	l->sec[T33_APP_LSC_CNT] = (u8)cnt;
	need = cnt * T33_APP_LSC_TABLE;
	if (l->cap < need) {
		if (ops->grow(l, need)) {
			l->sec[T33_APP_LSC_CNT] = (u8)(cnt - n);
			return;
		}
		l->cap = need;
	}
	for (k = 0; k < n; k++, src += T33_APP_LSC_REC) {
		u32 at = l->size16 + k * T33_APP_LSC_TABLE, i;

		if (at + T33_APP_LSC_TABLE <= l->cap)	/* always, but for a 16-bit wrapped payload size */
			for (i = 0; i < T33_APP_LSC_TABLE; i++)
				l->lut[at + i] = src[i];
		idx = cnt - n + k;
		l->sec[T33_APP_LSC_FLAG + idx] = src[0x3604];
		l->sec[T33_APP_LSC_HY + 2 * idx] = src[0x3602];
		l->sec[T33_APP_LSC_HY + 2 * idx + 1] = src[0x3603];
	}
}

/*
 * tisp_bin_append_data_parse(ch, app, size): app the bytes behind the last section / payload, size their number.
 * exist / par: the LDC parameter state (ldcParExist, stLdcPar); l: the channel's LSC state.  Returns 0, or -1
 * without append data.
 */
static inline int t33_bin_append_parse(u32 ch, const u8 *app, u32 size, u8 *exist, u8 *par, struct t33_app_lsc *l,
				       const struct t33_app_ops *ops)
{
	u32 next, at = 8, i, e;

	if (size < 8 || !app) {
		ops->log(T33_APP_LOG_NULL, size);
		return -1;
	}
	next = t33_le32(app + 4);
	for (i = 0; i < T33_APP_RECS && next; i++) {
		const u8 *rec = app + at;

		if (at + T33_APP_REC > size)	/* not the vendor's: the record header lies inside the data */
			break;
		e = t33_append_enum((const char *)rec);
		next = t33_le32(rec + 0x14);
		if (e == T33_APP_LDC) {
			/* not the vendor's: the 80 bytes lie inside the data */
			if (t33_le32(rec + 0x10) != 1 || t33_le32(rec + 0x18) != T33_LDC_PAR_BYTES ||
			    size - at >= T33_APP_REC + T33_LDC_PAR_BYTES) {
				if (t33_ldc_extra_par(exist, par, ch, rec))
					ops->log(T33_APP_LOG_LDCSZ, t33_le32(rec + 0x18));
			}
		} else if (e == T33_APP_LSC) {
			if (t33_le32(rec + 0x10) == 1) {
				if (!t33_le32(rec + 0x18))
					ops->log(T33_APP_LOG_LSC0, 0);
				else
					t33_append_lsc(l, ops, rec, size - at);
			}
		} else {
			ops->log(T33_APP_LOG_TYPE, e);
		}
		at = next;
		if (size <= next)
			break;
	}
	return 0;
}

/*
 * End of the sections and payloads, as tisp_bin_data_parse walks them (it has no copy for table entries 3 and
 * 26): the offset of the append data in a bin of `bytes` bytes, 0 when the sections do not fit.
 */
static inline u32 t33_bin_append_off(const u8 *bin, u32 bytes)
{
	u32 off = T32_IQ_DATA, l;
	unsigned int i;

	if (!bin || bytes < T32_IQ_DATA)
		return 0;
	for (i = 0; i < T32_IQ_SECTIONS; i++) {
		const u8 *e = bin + T32_IQ_TABLE + 4 * i;

		if (i == 3 || i == 26 || (e[0] | e[1] << 8) != 1)
			continue;
		l = e[2] | e[3] << 8;
		if (off + l > bytes)
			return 0;
		off += l + t32_iq_payload(bin + off, bytes - off, i);
	}
	return off <= bytes ? off : 0;
}

#endif /* TX_ISP_T33_APPEND_H */
