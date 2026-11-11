/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tuning register access: readable C of the vendor's tisp_reg_map_get
 * and tisp_reg_map_set (the tuning ioctl's raw register get / set).
 *
 * The tuning message carries the register offset at byte +27 and the
 * value at +31 (unaligned).  With direct register access
 * (t32_data_d03a0 == 0) the offset is relative to the ISP base
 * 0xecd00000 and the system register read / write is used; in shadow mode
 * (d03a0 != 0) tisp_reg_read / tisp_reg_write of the channel get the plain
 * offset.  A direct write whose value differs from register 0x40 in bit 11
 * is wrapped in tisp_mdns_start(0) / tisp_mdns_reg_trig(0).
 *
 * Vendor behaviour kept: set compares bit 11 of register 0x40 whatever the
 * target register is.  get returns 8 as the reply length, both return 0.
 * The OS / block calls are hooks (struct t32_regmap_ops).  Checked against
 * Matt Davis' recovered C (PR #15 @ 2e2e06bc) by tests/t32_oracle
 * (t32_regmap_oracle).
 */
#ifndef TX_ISP_T32_REGMAP_H
#define TX_ISP_T32_REGMAP_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <string.h>
#ifndef T32_HOST_U32
#define T32_HOST_U32
typedef uint32_t u32;
#endif
#ifndef T32_HOST_U8
#define T32_HOST_U8
typedef uint8_t u8;
#endif
#endif

#define T32_REGMAP_BASE		0xecd00000u
#define T32_REGMAP_MSG_REG	27
#define T32_REGMAP_MSG_VAL	31
#define T32_REGMAP_REPLY_LEN	8
#define T32_REGMAP_MDNS_REG	0x40
#define T32_REGMAP_MDNS_BIT	0x800

struct t32_regmap_ops {
	u32 (*sys_read)(u32 addr);
	u32 (*sys_write)(u32 addr, u32 val);
	u32 (*reg_read)(u32 ch, u32 reg);		/* tisp_reg_read */
	u32 (*reg_write)(u32 ch, u32 reg, u32 val);	/* tisp_reg_write */
	void (*mdns_start)(u32 ch);			/* tisp_mdns_start */
	void (*mdns_reg_trig)(u32 ch);			/* tisp_mdns_reg_trig */
};

struct t32_regmap {
	const struct t32_regmap_ops *ops;
	const u8 *shadow;			/* t32_data_d03a0 */
};

/* tisp_reg_map_get */
static inline u32 t32_reg_map_get(struct t32_regmap *m, u32 ch, u32 reg, u8 *msg, u32 *len)
{
	u32 val;

	if (*m->shadow == 0)
		val = m->ops->sys_read(T32_REGMAP_BASE + reg);
	else
		val = m->ops->reg_read(ch & 0xff, reg);
	memcpy(msg + T32_REGMAP_MSG_REG, &reg, 4);
	memcpy(msg + T32_REGMAP_MSG_VAL, &val, 4);
	*len = T32_REGMAP_REPLY_LEN;
	return 0;
}

/* tisp_reg_map_set */
static inline u32 t32_reg_map_set(struct t32_regmap *m, u32 ch, const u8 *msg)
{
	const struct t32_regmap_ops *o = m->ops;
	u32 reg, val;

	memcpy(&reg, msg + T32_REGMAP_MSG_REG, 4);
	memcpy(&val, msg + T32_REGMAP_MSG_VAL, 4);
	ch &= 0xff;
	if (*m->shadow) {
		o->reg_write(ch, reg, val);
		return 0;
	}
	reg += T32_REGMAP_BASE;
	if ((o->sys_read(T32_REGMAP_MDNS_REG) ^ val) & T32_REGMAP_MDNS_BIT) {
		o->mdns_start(0);
		o->sys_write(reg, val);
		o->mdns_reg_trig(0);
	} else {
		o->sys_write(reg, val);
	}
	return 0;
}

#endif
