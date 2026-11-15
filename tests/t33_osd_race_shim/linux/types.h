/* host shim for t33_osd_race_host_test.c (openimp #578) */
#ifndef SHIM_LINUX_TYPES_H
struct module;
struct i2c_driver;
struct platform_device;
#define SHIM_LINUX_TYPES_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <errno.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;
#endif
