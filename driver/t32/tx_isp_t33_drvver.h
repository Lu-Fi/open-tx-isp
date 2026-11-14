/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 DRIVER_VERSION answer (vendor tx_isp_driver_version.isra.23, binary oracle block "drvver"): a 96-byte block
 * (SoC name at 0, version at 32, 32 zero bytes) to the caller's buffer; -14 (EFAULT) and a message when the copy
 * fails.  put = private_copy_to_user (0 = copied).
 */
#ifndef TX_ISP_T33_DRVVER_H
#define TX_ISP_T33_DRVVER_H

#define T33_DRVVER_BYTES	96

static inline int t33_driver_version(void *arg, const char *soc, const char *ver,
				     int (*put)(void *dst, const void *src, u32 n))
{
	char b[T33_DRVVER_BYTES];
	unsigned int i;

	memset(b, 0, sizeof(b));
	for (i = 0; i < 31 && soc[i]; i++)
		b[i] = soc[i];
	for (i = 0; i < 31 && ver[i]; i++)
		b[32 + i] = ver[i];
	if (put(arg, b, T33_DRVVER_BYTES)) {
		isp_printf(2, "[ %s:%d ] copy to user error\n", "tx_isp_driver_version", 1401);
		return -14;
	}
	return 0;
}

#endif /* TX_ISP_T33_DRVVER_H */
