/* SPDX-License-Identifier: GPL-2.0 */
/* T33 binary oracle, block "drvver": tx_isp_driver_version.isra.23 (tx_isp_t33_drvver.h), SoC "PRJ008" and the
 * libimp version "20251107a" as the T33 build's module parameter defaults. */
#include <stdint.h>
#include <string.h>

typedef uint8_t u8;
typedef uint32_t u32;

extern int isp_printf(unsigned int level, const char *fmt, ...);
extern int private_copy_to_user(void *dst, const void *src, u32 n);

#include "tx_isp_t33_drvver.h"

int bo_driver_version(u32 arg)
{
	return t33_driver_version((void *)arg, "PRJ008", "20251107a", private_copy_to_user);
}
