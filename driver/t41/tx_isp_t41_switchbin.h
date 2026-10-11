/* SPDX-License-Identifier: MIT */
/*
 * T41 IMP_ISP_Tuning_SwitchBin (tuning control 0x080000a5): the request and
 * the tuning-bin validation, kept free of kernel state for the host test.
 *
 * Vendor libimp 1.1.0 .. 1.2.6 send { num, 0 (set), 0x80000a5, ptr } and the
 * stock tx_isp_switch_bin (ELF 0x9308/0x9348) copies 132 bytes from ptr:
 *     struct { u32 enable; char bname[128]; }
 * enable 1 loads bname; enable 0 reloads the path the sensor was started
 * with (core + 740 + vinum * 132).  It only runs while the ISP channel is
 * streaming (core state 4).
 *
 * The stock loader rewrites the single bin buffer of the channel in place
 * (header checks first, then the file is read over the live banks), so a
 * half-written or rejected file leaves the running ISP with a mixed bank.
 * The open driver therefore validates a complete private copy first
 * (t41_bin_validate) and only then publishes it.
 */
#ifndef TX_ISP_T41_SWITCHBIN_H
#define TX_ISP_T41_SWITCHBIN_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#endif

#define T41_SWITCH_BIN_CID        0x080000a5U
#define T41_SWITCH_BIN_PATH_BYTES 128U
#define T41_SWITCH_BIN_REQ_BYTES  132U

struct t41_switch_bin_req {
	uint32_t enable;
	char path[T41_SWITCH_BIN_PATH_BYTES];
};

/*
 * Validate the user request and pick the path.  enable 1 uses the request
 * path, enable 0 the sensor's default path (dflt, dflt_len bytes at most).
 * Returns 0 and *path, or a negative errno:
 *   -EINVAL        enable above 1, empty request path
 *   -ENAMETOOLONG  request path not NUL terminated inside the 128 bytes
 *   -ENOENT        enable 0 but no default path is known
 * Stock only tests enable >= 2 in the library; the kernel trusts the string.
 */
static inline int t41_switch_bin_req_check(const struct t41_switch_bin_req *req,
					   const char *dflt, size_t dflt_len,
					   const char **path)
{
	if (!req || !path)
		return -EINVAL;
	if (req->enable > 1U)
		return -EINVAL;
	if (req->enable) {
		if (memchr(req->path, '\0', sizeof(req->path)) == NULL)
			return -ENAMETOOLONG;
		if (!req->path[0])
			return -EINVAL;
		*path = req->path;
		return 0;
	}
	if (!dflt || !dflt_len || !dflt[0] ||
	    memchr(dflt, '\0', dflt_len) == NULL)
		return -ENOENT;
	*path = dflt;
	return 0;
}

/* ---- tuning bin: 64-byte header, day bank, night bank ------------------ */

#define T41_BIN_HEADER_BYTES 64U
/* Offsets of the header words the loader uses. */
#define T41_BIN_OFF_VERSION  0U     /* char[8], e.g. "1.00" */
#define T41_BIN_OFF_SOC      8U     /* char[8], "t41" */
#define T41_BIN_OFF_DNW      16U    /* u32 day/night/wdr flag bytes */
#define T41_BIN_OFF_DECLARED 48U    /* bytes after the header */
#define T41_BIN_OFF_NIGHT    52U    /* night bank offset inside the payload */
#define T41_BIN_OFF_CRC      60U
/*
 * Smallest bank the consumers may read: the CVersion string at +0x1efc0
 * (68 bytes) is the highest fixed offset of the open driver.  Real bins
 * (gc5603, 2026-10) carry two 0x1f008 byte banks.
 */
#define T41_BIN_BANK_MIN     (0x1efc0U + 68U)

struct t41_bin_info {
	uint32_t declared;     /* payload bytes covered by the CRC */
	uint32_t night_off;    /* night bank offset from the end of the header */
	uint32_t dnw;          /* the 4 flag bytes at +16 */
};

static const uint32_t t41_bin_crc_table[8] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
	0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
};

static inline uint32_t t41_bin_rd32(const unsigned char *p)
{
	uint32_t v;

	memcpy(&v, p, sizeof(v));
	return v;
}

/* The stock CRC: XOR of the words with a 3-bit table step, as in the
 * loader (words from the end of the header, a partial tail is ignored). */
static inline uint32_t t41_bin_crc(const unsigned char *payload,
				   uint32_t declared)
{
	uint32_t crc = 0;
	uint32_t off;

	for (off = 0; off + 4U <= declared; off += 4U) {
		crc ^= t41_bin_rd32(payload + off);
		crc ^= t41_bin_crc_table[crc & 7U];
	}
	return crc;
}

/*
 * Complete validation of a bin image, no side effects.
 *   manager_ver  the driver's compiled-in bin version ("1.00"), 8 bytes
 *   live_ver     the version of the bin currently loaded (8 bytes) or NULL
 *   soc          the SoC tag the driver accepts ("t41"), 8 bytes
 * Same acceptance rules as the stock loader (version equal to the manager
 * version or to the live one, SoC tag, extents, CRC) plus the bank sizes the
 * stock loader never checked.  Returns 0 or -EINVAL / -EPROTO:
 *   -EINVAL  too short, banks outside the file or smaller than the minimum
 *   -EPROTO  version, SoC tag or CRC mismatch
 */
static inline int t41_bin_validate(const unsigned char *buf, size_t size,
				   const unsigned char *manager_ver,
				   const unsigned char *live_ver,
				   const unsigned char *soc,
				   struct t41_bin_info *info)
{
	uint32_t declared, night_off;

	if (!buf || !manager_ver || !soc || !info)
		return -EINVAL;
	if (size < T41_BIN_HEADER_BYTES || size > 0x7fffffffU)
		return -EINVAL;

	if (memcmp(manager_ver, buf + T41_BIN_OFF_VERSION, 8) != 0 &&
	    (!live_ver ||
	     memcmp(live_ver, buf + T41_BIN_OFF_VERSION, 8) != 0))
		return -EPROTO;
	if (memcmp(soc, buf + T41_BIN_OFF_SOC, 8) != 0)
		return -EPROTO;

	declared = t41_bin_rd32(buf + T41_BIN_OFF_DECLARED);
	night_off = t41_bin_rd32(buf + T41_BIN_OFF_NIGHT);
	if (declared > (uint32_t)size - T41_BIN_HEADER_BYTES ||
	    night_off > declared)
		return -EINVAL;
	/* Both banks must lie completely inside the file. */
	if (night_off < T41_BIN_BANK_MIN ||
	    declared - night_off < T41_BIN_BANK_MIN)
		return -EINVAL;

	if (t41_bin_crc(buf + T41_BIN_HEADER_BYTES, declared) !=
	    t41_bin_rd32(buf + T41_BIN_OFF_CRC))
		return -EPROTO;

	info->declared = declared;
	info->night_off = night_off;
	info->dnw = t41_bin_rd32(buf + T41_BIN_OFF_DNW);
	return 0;
}

#endif /* TX_ISP_T41_SWITCHBIN_H */
