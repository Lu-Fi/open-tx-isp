/* SPDX-License-Identifier: GPL-2.0 */
/* t10_binoracle: firmware globals the vendor module defines and our firmware object only references (the driver
 * defines them elsewhere), linked into our object so blocks can compare them with GLOBAL (openimp #539). */
#include <stdint.h>
uint32_t stab[0x3c / 4];	/* system_tab (vendor .bss 0x3c bytes) */
