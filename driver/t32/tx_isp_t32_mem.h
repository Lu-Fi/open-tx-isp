/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP working-memory size (GET_ISP_MEM_INFO answer).
 *
 * libimp asks the driver how much contiguous memory the ISP needs for a VI,
 * allocates it from rmem and hands it back with SET_ISP_MEM_INFO.  The size
 * is the MDNS reference/motion buffers plus two small statistics planes,
 * rounded to whole pages.  The arithmetic below reproduces the vendor answer
 * (S0: 3840x2160 linear -> 0x9f9000), derived from the vendor module's
 * behaviour, not copied.  memopt is the compression ratio parameter in
 * 1/1000 (vendor default for PRJ007 gives 1500); values <= 1000 select the
 * uncompressed layout.
 */
#ifndef TX_ISP_T32_MEM_H
#define TX_ISP_T32_MEM_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#endif

#define T32_MEMOPT_DEFAULT	1500U

static inline u32 t32_kb_round(u32 bytes)
{
	return ((bytes + 1023) >> 10) << 10;
}

/*
 * w, h: sensor picture; wdr_short: 1 when no second (short) plane is needed;
 * extra_mb: whole MiB added on top (0 for linear mode).
 * Returns 0 for an unsupported memopt (> 2000).
 */
static inline u32 t32_isp_mem_size(u32 w, u32 h, u32 memopt, int no_motion,
				   u32 extra_mb)
{
	u32 blocks32 = (w + 31) >> 5;
	u32 size, v;

	if (memopt <= 1000) {
		u32 a = ((w + 15) & ~15U) * h;
		u32 b = (blocks32 << 5) * h;

		size = ((((a >> 5) + a + 1023) >> 10) +
			(((b >> 7) + (b >> 1) + 1023) >> 10)) << 10;
	} else if (memopt <= 2000) {
		v = h * blocks32 * (384000U / memopt);
		size = (((((v + 63) >> 6) << 3) + 255) >> 8) + 2;
		size <<= 8;
	} else {
		return 0;
	}
	/* per-12-pixel-group statistics plane */
	size += t32_kb_round((((((w + 7) >> 3) + 11) / 12) << 3) * h >> 3);
	/* motion plane */
	if (!no_motion)
		size += t32_kb_round(((blocks32 << 4) * h) >> 1);
	/* to the next page: a page-aligned size gets a whole extra page, as
	 * the vendor answer does; then whole MiB extras */
	size += (4096 - (size & 0xfff)) & 0xffff;
	return size + (extra_mb << 20);
}

#endif /* TX_ISP_T32_MEM_H */
