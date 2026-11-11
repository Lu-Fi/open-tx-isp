// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle, phase 2 of the sensor subdev ioctl: register / release
 * and all commands end to end (see t32_ssio_oracle.c, same oracle with
 * SSIO_PHASE 2).
 */
#define SSIO_PHASE 2
#define SSIO_NAME "t32_ssior_oracle"
#include "t32_ssio_oracle.c"
