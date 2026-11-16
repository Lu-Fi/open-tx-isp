/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T33 mdns day/night IQ-bin section switch: port of the vendor's tisp_mdns_switch_para,
 * and the gain refresh tisp_mdns_par_refresh (88 KB in the vendor module: its first-call
 * and gated paths each inline the same body as the switch; t33_mdns_par_body here).
 *
 * The function copies five 11-byte tables of the new IQ section into the module's
 * global thresholds, lets tisp_mdns_binpara_update / func_mdns_para_update derive the
 * block state words (mdnsInfo[ch], indexed by byte offset / 4) and writes every
 * register field of the mdns block through tisp_reg_write_t33 (reg = list slot id in
 * cfg mode, else the register address).  The callees are reached through an ops
 * struct so the same code runs in the kernel and under the binary oracle (blocks sw-mdns
 * and mdnspr: the init / refresh / switch chain against the vendor module in Unicorn).
 */
#ifndef TX_ISP_T33_SWMDNS_H
#define TX_ISP_T33_SWMDNS_H

#include "tx_isp_t33_blk.h"

struct t33_swmdns_ops {
	void (*regw)(u32 ch, u32 blk, u32 reg, u32 val);	/* tisp_reg_write_t33 */
	void (*binpara)(u32 ch);				/* tisp_mdns_binpara_update */
	void (*para_update)(u32 ch);				/* func_mdns_para_update */
	void (*trig)(u32 ch);					/* tisp_mdns_t33_reg_trig */
	u32 (*intp8)(u32 i, u32 f, const u8 *p);		/* tisp_simple_intp_uint8_t */
	u32 (*intp16)(u32 i, u32 f, const u8 *p);		/* tisp_simple_intp_uint16_t */
};

struct t33_swm_ctx {
	const struct t33_swmdns_ops *op;
	u32 ch;
	u32 cfg;		/* mdnsCfgMode */
	const u32 *w;		/* mdnsInfo[ch] words */
};

/* runs of interpolations: info word (dst byte offset) = intp(section + src) */
struct t33_swm_run {
	u16 dst;
	u16 src;
	u8 n;
	u8 bits;
	u8 sstep;
	u8 dstep;
};

#define W_(id, ad, v)	c->op->regw(c->ch, 15, c->cfg ? (id) : (ad), (v))
#define F_(o, m, s)	((c->w[(o) >> 2] & (u32)(m)) << (s))
#define T3_(a, b, d)	(F_(a, 0xff, 0) | F_(b, 0xff, 8) | F_(d, 0xff, 16))
#define P_(a, b)	(F_(a, 0x1ff, 0) | F_(b, 0x1ff, 12))

static inline void t33_swm_g59(const struct t33_swm_ctx *c)
{
	W_(59, 0xd200, F_(6276, 1, 0) | F_(6280, 0xff, 4) | F_(6284, 1, 12) | F_(6288, 1, 16) |
	   F_(6292, 1, 20) | F_(6296, 1, 24) | F_(6300, 1, 28));
	W_(60, 0xd204, F_(6304, 7, 0) | F_(6308, 1, 4) | F_(6344, 0xf, 8) | F_(6348, 0xf, 12) |
	   F_(6352, 0xf, 16) | F_(6356, 0xf, 20));
	W_(61, 0xd208, F_(6360, 0xff, 0) | F_(6364, 7, 8) | F_(6400, 1, 12) | F_(6404, 0xff, 16) |
	   F_(6408, 7, 24));
}

static inline void t33_swm_g66(const struct t33_swm_ctx *c)
{
	W_(66, 0xd224, F_(5572, 3, 0) | F_(5576, 3, 4) | F_(5580, 7, 8) | F_(5584, 1, 12) |
	   F_(5588, 1, 16) | F_(5592, 0x1f, 20));
}

static inline void t33_swm_g67(const struct t33_swm_ctx *c)
{
	W_(67, 0xd228, F_(5596, 0x1f, 0) | F_(5600, 0x1f, 8) | F_(5604, 0x1f, 16) | F_(5608, 0x1f, 24));
}

static inline void t33_swm_g68(const struct t33_swm_ctx *c)
{
	W_(68, 0xd22c, F_(5612, 0x1f, 0) | F_(5616, 3, 8) | F_(5620, 3, 12) | F_(5624, 3, 16) |
	   F_(5812, 0x1ff, 20));
	W_(69, 0xd230, F_(5816, 0xf, 0) | F_(5820, 0xff, 4) | F_(5824, 0xff, 12) | F_(5828, 0xff, 20));
	W_(70, 0xd234, F_(5832, 0xff, 0) | F_(5836, 0x1f, 8) | F_(5840, 7, 16) | F_(5844, 1, 20));
	W_(71, 0xd238, F_(5848, 0xff, 0) | F_(5852, 0xff, 8) | F_(5984, 1, 16) | F_(5988, 1, 20));
}

static inline void t33_swm_g73(const struct t33_swm_ctx *c)
{
	W_(73, 0xd244, F_(5992, 0xf, 0) | F_(5996, 0xf, 4) | F_(6000, 0x1f, 8) | F_(6004, 0x3f, 16) |
	   F_(6008, 0x3f, 24));
	W_(74, 0xd248, F_(6012, 0x3f, 0) | F_(6216, 0x1ff, 8) | F_(6220, 0x1ff, 20));
	W_(75, 0xd24c, F_(6224, 0x1ff, 0) | F_(6228, 0xf, 12) | F_(6232, 0xf, 16) | F_(6236, 0xf, 20) |
	   F_(6240, 0x1f, 24));
}

static inline void t33_swm_g77(const struct t33_swm_ctx *c)
{
	W_(77, 0xd258, F_(6640, 0xf, 0) | F_(6644, 0x3ff, 4) | F_(6648, 0xf, 16) | F_(6652, 0x3ff, 20));
	W_(78, 0xd25c, F_(6656, 0xf, 0) | F_(6660, 0x3ff, 4) | F_(6664, 0xf, 16) | F_(6668, 0xf, 20) |
	   F_(6672, 0x1f, 24));
	W_(79, 0xd260, F_(6676, 0x1f, 0) | F_(6680, 0x1f, 8) | F_(6684, 3, 16) | F_(6688, 1, 20) |
	   F_(6692, 1, 24) | F_(6696, 1, 28));
	W_(80, 0xd264, F_(6700, 1, 0) | F_(6704, 0x7f, 4) | F_(6776, 0x1ff, 12));
	W_(81, 0xd268, F_(6780, 0x1ff, 0) | F_(6784, 0x1ff, 12) | F_(6788, 0xf, 24));
	W_(82, 0xd26c, F_(6792, 0xf, 0) | F_(6796, 0xf, 4) | F_(6832, 0x1ff, 8) | F_(6836, 0x1ff, 20));
	W_(83, 0xd270, F_(6840, 0x1ff, 0) | F_(6844, 0xf, 12) | F_(6848, 0xf, 16) | F_(6852, 0xf, 20));
	W_(84, 0xd274, F_(6872, 0x1ff, 0) | F_(6876, 0xf, 12) | F_(6880, 0x1f, 16));
}

static inline void t33_swm_g86(const struct t33_swm_ctx *c)
{
	W_(86, 0xd284, F_(7944, 1, 0) | F_(7948, 0xff, 4) | F_(7952, 1, 12) | F_(7956, 1, 16) |
	   F_(7960, 1, 20) | F_(7964, 1, 24) | F_(7968, 1, 28));
	W_(87, 0xd288, F_(7972, 7, 0) | F_(7976, 1, 4) | F_(8012, 0xf, 8) | F_(8016, 0xf, 12) |
	   F_(8020, 0xf, 16) | F_(8024, 0xf, 20));
	W_(88, 0xd28c, F_(8028, 0xff, 0) | F_(8032, 7, 8) | F_(8068, 1, 12) | F_(8072, 0xff, 16) |
	   F_(8076, 7, 24));
}

static inline void t33_swm_g95(const struct t33_swm_ctx *c)
{
	W_(95, 0xd2b4, F_(7416, 0x1ff, 0) | F_(7420, 0xf, 12) | F_(7424, 0xff, 16));
}

static inline void t33_swm_g99(const struct t33_swm_ctx *c)
{
	W_(99, 0xd2c8, F_(7596, 0xf, 0) | F_(7600, 0xf, 4) | F_(7604, 0x1f, 8) | F_(7608, 0x3f, 16) |
	   F_(7612, 0x3f, 24));
	W_(100, 0xd2cc, F_(7616, 0x3f, 0) | F_(7884, 0x1ff, 8) | F_(7888, 0x1ff, 20));
	W_(101, 0xd2d0, F_(7892, 0x1ff, 0) | F_(7896, 0xf, 12) | F_(7900, 0xf, 16) | F_(7904, 0xf, 20) |
	   F_(7908, 0x1f, 24));
}

static inline void t33_swm_g103(const struct t33_swm_ctx *c)
{
	W_(103, 0xd2e0, F_(8456, 0xf, 0) | F_(8460, 0xf, 4) | F_(8464, 0xf, 8) | F_(8512, 0xf, 12) |
	   F_(8516, 0xf, 16) | F_(8520, 0xf, 20) | F_(8684, 0xf, 24));
}

static inline void t33_swm_g105(const struct t33_swm_ctx *c)
{
	W_(105, 0xd2e8, F_(8308, 0xf, 0) | F_(8312, 0x3ff, 4) | F_(8316, 0xf, 16) | F_(8320, 0x3ff, 20));
	W_(106, 0xd2ec, F_(8324, 0xf, 0) | F_(8328, 0x3ff, 4) | F_(8332, 0xf, 16) | F_(8336, 0xf, 20) |
	   F_(8340, 0x1f, 24));
	W_(107, 0xd2f0, F_(8344, 0x1f, 0) | F_(8348, 0x1f, 8) | F_(8352, 3, 16) | F_(8356, 1, 20) |
	   F_(8360, 1, 24) | F_(8364, 1, 28));
	W_(108, 0xd2f4, F_(8368, 1, 0) | F_(8372, 0x7f, 4) | F_(8716, 0x1f, 12));
}

/* first batch: the registers of the new section, then the combined control word 5 */
static inline void t33_swm_regs1(const struct t33_swm_ctx *c, u32 stbyte, u32 uvflag)
{
	u32 k, w5;

	W_(6, 0xd028, F_(5568, 1, 4) | F_(4076, 1, 0));
	W_(24, 0xd100, F_(3848, 0xff, 0));
	W_(25, 0xd104, F_(3852, 0xff, 0) | F_(3860, 0xff, 8));
	W_(26, 0xd108, F_(3856, 0xffff, 0) | F_(3864, 0xffff, 16));
	W_(27, 0xd10c, F_(3872, 0xffff, 0) | F_(3868, 0xffff, 16));
	W_(28, 0xd110, F_(3876, 0xff, 0) | F_(3880, 3, 8) | F_(3884, 3, 16));
	W_(29, 0xd114, F_(3888, 0xff, 0) | F_(3892, 0xff, 8) | F_(3900, 0xff, 24));
	W_(30, 0xd118, F_(3896, 0x1ff, 0));
	W_(112, 0xd180, T3_(3972, 3976, 3980));
	W_(113, 0xd180, T3_(3984, 3988, 3992));
	W_(114, 0xd180, T3_(3996, 4000, 4012));
	W_(115, 0xd180, T3_(4016, 4020, 4024));
	W_(116, 0xd180, T3_(4028, 4032, 4036));
	W_(117, 0xd180, T3_(4040, 4100, 4104));
	W_(118, 0xd180, T3_(4108, 4148, 4152));
	W_(119, 0xd180, T3_(4156, 4160, 4164));
	W_(120, 0xd180, T3_(4168, 4172, 4176));
	W_(121, 0xd180, T3_(4180, 4184, 4188));
	W_(122, 0xd180, T3_(4192, 4196, 4200));
	W_(123, 0xd180, T3_(4204, 4208, 4212));
	W_(124, 0xd180, T3_(4216, 4220, 4224));
	W_(125, 0xd180, T3_(4236, 4240, 4244));
	W_(126, 0xd180, T3_(4248, 4252, 4256));
	W_(127, 0xd180, T3_(4260, 4264, 4284));
	W_(128, 0xd180, T3_(4288, 4292, 4296));
	W_(129, 0xd180, T3_(4300, 4304, 4308));
	W_(130, 0xd180, T3_(4312, 4324, 4328));
	W_(131, 0xd180, T3_(4332, 4336, 4340));
	W_(132, 0xd180, T3_(4344, 4348, 4352));
	W_(133, 0xd180, T3_(4356, 4360, 4364));
	W_(134, 0xd180, T3_(4368, 4424, 4428));
	W_(135, 0xd180, T3_(4432, 4436, 4440));
	W_(136, 0xd180, T3_(4444, 4448, 4452));
	W_(137, 0xd180, T3_(4456, 4460, 4464));
	W_(138, 0xd180, T3_(4468, 4472, 4476));
	W_(139, 0xd180, F_(4480, 0xff, 0));
	W_(141, 0xd184, T3_(4492, 4496, 4500));
	W_(142, 0xd184, T3_(4504, 4508, 4512));
	W_(143, 0xd184, T3_(4516, 4520, 4556));
	W_(144, 0xd184, T3_(4560, 4564, 4568));
	W_(145, 0xd184, T3_(4572, 4576, 4580));
	W_(146, 0xd184, T3_(4584, 4628, 4632));
	W_(147, 0xd184, T3_(4636, 4640, 4644));
	W_(148, 0xd184, T3_(4648, 4652, 4656));
	W_(149, 0xd184, T3_(4668, 4672, 4676));
	W_(150, 0xd184, T3_(4680, 4684, 4688));
	W_(151, 0xd184, T3_(4692, 4696, 4716));
	W_(152, 0xd184, T3_(4720, 4724, 4728));
	W_(153, 0xd184, T3_(4732, 4736, 4740));
	W_(154, 0xd184, T3_(4744, 4780, 4784));
	W_(155, 0xd184, T3_(4788, 4792, 4796));
	W_(156, 0xd184, T3_(4800, 4804, 4808));
	W_(157, 0xd184, T3_(4844, 4848, 4852));
	W_(158, 0xd184, T3_(4856, 4860, 4864));
	W_(159, 0xd184, T3_(4868, 4872, 4876));
	W_(160, 0xd184, T3_(4880, 4884, 4888));
	W_(161, 0xd184, T3_(4524, 4528, 4532));
	W_(162, 0xd184, T3_(4536, 4540, 4544));
	W_(163, 0xd184, T3_(4548, 4552, 4588));
	W_(164, 0xd184, T3_(4592, 4596, 4600));
	W_(165, 0xd184, T3_(4604, 4608, 4612));
	W_(166, 0xd184, T3_(4616, 4748, 4752));
	W_(167, 0xd184, T3_(4756, 4760, 4764));
	W_(168, 0xd184, T3_(4768, 4772, 4776));
	W_(169, 0xd184, T3_(4812, 4816, 4820));
	W_(170, 0xd184, T3_(4824, 4828, 4832));
	W_(171, 0xd184, T3_(4836, 4840, 4892));
	W_(172, 0xd184, T3_(4896, 4900, 4904));
	W_(173, 0xd184, T3_(4908, 4912, 4916));
	W_(174, 0xd184, T3_(4920, 4940, 4944));
	W_(175, 0xd184, T3_(4948, 4952, 4956));
	W_(176, 0xd184, T3_(4960, 4964, 4968));
	W_(177, 0xd184, T3_(4972, 4976, 4980));
	W_(178, 0xd184, T3_(4984, 4988, 4992));
	W_(179, 0xd184, T3_(4996, 5000, 5004));
	W_(180, 0xd184, F_(5008, 0xff, 0));
	W_(32, 0xd188, F_(3904, 0xff, 0) | F_(3908, 0xff, 8) | F_(3912, 0xf, 16) | F_(3916, 0xf, 20));
	W_(33, 0xd18c, F_(3920, 0xff, 0) | F_(3924, 1, 8) | F_(3928, 0xff, 12) | F_(3932, 0xff, 20));
	W_(34, 0xd190, F_(3936, 0xf, 0) | F_(3940, 0xf, 4) | F_(3944, 0xff, 8) | F_(3948, 1, 16) |
	   F_(3952, 1, 20));
	W_(35, 0xd194, F_(3956, 0xff, 0) | F_(3960, 0xff, 8) | F_(3964, 3, 16) | F_(3968, 0xff, 20) |
	   F_(4004, 3, 28));
	W_(36, 0xd198, F_(4008, 0xff, 0) | F_(4044, 0xff, 8) | F_(4048, 7, 16) | F_(4052, 0xff, 20));
	W_(37, 0xd19c, F_(4056, 0xf, 0) | F_(4060, 0xf, 4) | F_(4064, 0xf, 8) | F_(4068, 0xf, 12) |
	   F_(4072, 0xf, 16) | F_(4080, 0xff, 20));
	W_(38, 0xd1a0, F_(4084, 0xff, 0) | F_(4088, 0xf, 8) | F_(4092, 0xff, 12) | F_(4096, 0xf, 20) |
	   F_(4112, 0xf, 24));
	W_(39, 0xd1a4, F_(4116, 0xf, 0) | F_(4120, 0xf, 4) | F_(4124, 0xf, 8) | F_(4128, 0xf, 12) |
	   F_(4132, 0xf, 16) | F_(4136, 0xf, 20));
	W_(40, 0xd1a8, F_(4140, 0xff, 0) | F_(4144, 0xff, 8) | F_(4228, 3, 16) | F_(4232, 0xff, 20) |
	   F_(4268, 7, 28));
	W_(41, 0xd1ac, F_(4272, 0xff, 0) | F_(4276, 3, 8) | F_(4280, 0xff, 12) | F_(4316, 3, 20));
	W_(42, 0xd1b0, F_(4320, 0xff, 0) | F_(4372, 0xff, 8) | F_(4376, 0xff, 16));
	W_(43, 0xd1b4, F_(4380, 0xff, 0) | F_(4384, 0xff, 8) | F_(4388, 0xff, 16) | F_(4392, 0xf, 24));
	W_(44, 0xd1b8, F_(4396, 0xf, 0) | F_(4400, 0xff, 4) | F_(4404, 1, 12) | F_(4408, 0x1f, 16) |
	   F_(4412, 0x1f, 24));
	W_(45, 0xd1bc, F_(4416, 0x1f, 0) | F_(4420, 0x1f, 8) | F_(4484, 3, 16) | F_(4488, 3, 20) |
	   F_(4620, 3, 24));
	W_(46, 0xd1c0, F_(4624, 0xff, 0) | F_(4660, 3, 8) | F_(4664, 0xff, 12) | F_(4700, 0xff, 20) |
	   F_(4704, 1, 28));
	W_(47, 0xd1c4, F_(4708, 0xff, 0) | F_(4712, 1, 8) | F_(4924, 3, 12) | F_(4928, 3, 16) |
	   F_(4932, 3, 20) | F_(4936, 3, 24));
	W_(182, 0xd1cc, T3_(5100, 5104, 5108));
	W_(183, 0xd1cc, T3_(5112, 5116, 5128));
	if (uvflag)
		W_(184, 0xd1cc, T3_(5132, 5136, 5140));
	W_(185, 0xd1cc, T3_(5144, 5148, 5152));
	W_(186, 0xd1cc, T3_(5156, 5160, 5164));
	W_(187, 0xd1cc, T3_(5168, 5172, 5176));
	W_(188, 0xd1cc, T3_(5180, 5184, 5188));
	W_(189, 0xd1cc, T3_(5192, 5196, 5200));
	W_(190, 0xd1cc, T3_(5204, 5208, 5220));
	W_(191, 0xd1cc, T3_(5224, 5228, 5232));
	W_(192, 0xd1cc, T3_(5236, 5240, 5244));
	W_(193, 0xd1cc, T3_(5248, 5252, 5256));
	W_(194, 0xd1cc, T3_(5260, 5264, 5268));
	W_(195, 0xd1cc, T3_(5272, 5276, 5280));
	W_(196, 0xd1cc, T3_(5284, 5300, 5304));
	W_(197, 0xd1cc, T3_(5308, 5312, 5316));
	W_(198, 0xd1cc, T3_(5320, 5324, 5328));
	W_(199, 0xd1cc, T3_(5332, 5336, 5340));
	W_(200, 0xd1cc, T3_(5344, 5348, 5352));
	W_(201, 0xd1cc, T3_(5356, 5372, 5376));
	W_(202, 0xd1cc, T3_(5380, 5384, 5388));
	W_(203, 0xd1cc, T3_(5392, 5396, 5400));
	W_(204, 0xd1cc, T3_(5404, 5408, 5412));
	W_(205, 0xd1cc, T3_(5416, 5420, 5424));
	W_(206, 0xd1cc, T3_(5428, 5432, 5436));
	W_(207, 0xd1cc, T3_(5440, 5444, 5448));
	W_(208, 0xd1cc, T3_(5452, 5536, 5540));
	W_(49, 0xd1d0, F_(5120, 7, 0) | F_(5124, 0xf, 4) | F_(5212, 0xf, 8) | F_(5216, 3, 12) |
	   F_(5288, 0xf, 16) | F_(5292, 0x1f, 20));
	W_(50, 0xd1d4, F_(5296, 0x1f, 0) | F_(5360, 0xf, 8) | F_(5364, 0x1f, 12) | F_(5368, 0x1f, 20));
	W_(51, 0xd1d8, F_(5456, 0x1ff, 0) | F_(5460, 0x1ff, 12));
	W_(52, 0xd1dc, F_(5464, 0x7ff, 0) | F_(5468, 0x7ff, 12));
	W_(53, 0xd1e0, F_(5472, 0x7ff, 0) | F_(5476, 0x7ff, 12) | F_(5480, 0xf, 24));
	W_(54, 0xd1e4, F_(5484, 0xf, 0) | F_(5488, 0xf, 4) | F_(5492, 0xf, 8) | F_(5496, 0xf, 12) |
	   F_(5500, 0xf, 16) | F_(5504, 0xf, 20) | F_(5508, 0xf, 24));
	W_(55, 0xd1e8, F_(5512, 0xf, 0) | F_(5516, 0xf, 4) | F_(5520, 1, 8) | F_(5524, 0x7ff, 12) |
	   F_(5528, 1, 24) | F_(5532, 1, 28));
	W_(56, 0xd1ec, F_(5544, 0x7f, 0) | F_(5548, 1, 8) | F_(5552, 0x1f, 12) | F_(5556, 0x1f, 20));
	W_(57, 0xd1f0, F_(5560, 0x1f, 0) | F_(5564, 0x1f, 8));
	W_(210, 0xd1f8, T3_(6244, 6248, 6252));
	W_(211, 0xd1f8, T3_(6256, 6260, 6264));
	W_(212, 0xd1f8, T3_(6268, 6272, 6312));
	W_(213, 0xd1f8, T3_(6316, 6320, 6324));
	W_(214, 0xd1f8, T3_(6328, 6332, 6336));
	W_(215, 0xd1f8, T3_(6340, 6368, 6372));
	W_(216, 0xd1f8, T3_(6376, 6380, 6384));
	W_(217, 0xd1f8, T3_(6388, 6392, 6396));
	W_(218, 0xd1f8, T3_(6412, 6416, 6420));
	W_(219, 0xd1f8, T3_(6424, 6428, 6432));
	W_(220, 0xd1f8, T3_(6436, 6440, 6452));
	W_(221, 0xd1f8, T3_(6456, 6460, 6464));
	W_(222, 0xd1f8, T3_(6468, 6472, 6476));
	W_(223, 0xd1f8, T3_(6480, 6492, 6496));
	W_(224, 0xd1f8, T3_(6500, 6504, 6508));
	W_(225, 0xd1f8, T3_(6512, 6516, 6520));
	W_(226, 0xd1f8, T3_(6532, 6536, 6540));
	W_(227, 0xd1f8, T3_(6544, 6548, 6552));
	W_(228, 0xd1f8, T3_(6556, 6560, 6568));
	W_(229, 0xd1f8, T3_(6576, 6572, 6580));
	W_(231, 0xd1fc, F_(6588, 0x1f, 0) | F_(6592, 0x1f, 8) | F_(6596, 0x1f, 16) | F_(6600, 0x1f, 24));
	W_(232, 0xd1fc, F_(6604, 0x1f, 0) | F_(6608, 0x1f, 8) | F_(6612, 0x1f, 16) | F_(6616, 0x1f, 24));
	W_(233, 0xd1fc, F_(6620, 0x1f, 0) | F_(6624, 0x1f, 8) | F_(6628, 0x1f, 16));
	t33_swm_g59(c);
	W_(62, 0xd20c, F_(6444, 0xff, 0) | F_(6448, 7, 8) | F_(6484, 0xff, 12) | F_(6488, 7, 20));
	W_(63, 0xd210, F_(6524, 0xff, 0) | F_(6528, 7, 8) | F_(6564, 3, 12) | F_(6584, 0xff, 16));
	for (k = 0; k < 9; k++)
		W_(235 + k, 0xd21c, T3_(5688 + 12 * k, 5692 + 12 * k, 5696 + 12 * k));
	W_(244, 0xd21c, T3_(5796, 5804, 5800));
	W_(245, 0xd21c, F_(5808, 0xff, 0));
	for (k = 0; k < 7; k++)
		W_(247 + k, 0xd220, P_(5628 + 8 * k, 5632 + 8 * k));
	W_(254, 0xd220, P_(5684, 5856));
	t33_swm_g66(c);
	t33_swm_g67(c);
	t33_swm_g68(c);
	for (k = 0; k < 16; k++)
		W_(272 + k, 0xd240, T3_(6016 + 12 * k, 6020 + 12 * k, 6024 + 12 * k));
	t33_swm_g73(c);
	for (k = 0; k < 5; k++)
		W_(290 + k, 0xd254, T3_(6708 + 12 * k, 6712 + 12 * k, 6716 + 12 * k));
	W_(295, 0xd254, T3_(6768, 6772, 6800));
	W_(296, 0xd254, T3_(6804, 6808, 6812));
	W_(298, 0xd254, T3_(6828, 6856, 6864));
	W_(299, 0xd254, F_(6860, 0xff, 0) | F_(6868, 0xff, 8));
	t33_swm_g77(c);
	W_(301, 0xd27c, T3_(7912, 7916, 7920));
	W_(302, 0xd27c, T3_(7924, 7928, 7932));
	W_(303, 0xd27c, T3_(7936, 7940, 7980));
	W_(304, 0xd27c, T3_(7984, 7988, 7992));
	W_(305, 0xd27c, T3_(7996, 8000, 8004));
	W_(306, 0xd27c, T3_(8008, 8036, 8040));
	W_(307, 0xd27c, T3_(8044, 8048, 8052));
	W_(308, 0xd27c, T3_(8056, 8060, 8064));
	W_(309, 0xd27c, T3_(8080, 8084, 8088));
	W_(310, 0xd27c, T3_(8092, 8096, 8100));
	W_(311, 0xd27c, T3_(8104, 8108, 8120));
	W_(312, 0xd27c, T3_(8124, 8128, 8132));
	W_(313, 0xd27c, T3_(8136, 8140, 8144));
	W_(314, 0xd27c, T3_(8148, 8160, 8164));
	W_(315, 0xd27c, T3_(8168, 8172, 8176));
	W_(316, 0xd27c, T3_(8180, 8184, 8188));
	W_(317, 0xd27c, T3_(8200, 8204, 8208));
	W_(318, 0xd27c, T3_(8212, 8216, 8220));
	W_(319, 0xd27c, T3_(8224, 8228, 8236));
	W_(320, 0xd27c, T3_(8244, 8240, 8248));
	W_(322, 0xd280, F_(8256, 0x1f, 0) | F_(8260, 0x1f, 8) | F_(8264, 0x1f, 16) | F_(8268, 0x1f, 24));
	W_(323, 0xd280, F_(8272, 0x1f, 0) | F_(8276, 0x1f, 8) | F_(8280, 0x1f, 16) | F_(8284, 0x1f, 24));
	W_(324, 0xd280, F_(8288, 0x1f, 0) | F_(8292, 0x1f, 8) | F_(8296, 0x1f, 16));
	t33_swm_g86(c);
	W_(89, 0xd290, F_(8112, 0xff, 0) | F_(8116, 7, 8) | F_(8152, 0xff, 12) | F_(8156, 7, 20));
	W_(90, 0xd294, F_(8192, 0xff, 0) | F_(8196, 7, 8) | F_(8232, 3, 12) | F_(8252, 0xff, 16));
	for (k = 0; k < 15; k++)
		W_(326 + k, 0xd2a0, T3_(7168 + 12 * k, 7172 + 12 * k, 7176 + 12 * k));
	W_(343, 0xd2a0, T3_(7400, 7408, 7404));
	W_(344, 0xd2a0, T3_(7412, 7452, 7456));
	for (k = 0; k < 7; k++)
		W_(346 + k, 0xd2a4, P_(7108 + 8 * k, 7112 + 8 * k));
	W_(353, 0xd2a4, P_(7164, 7460));
	for (k = 0; k < 12; k++)
		W_(371 + k, 0xd2a8, F_(6904 + 16 * k, 0x1f, 0) | F_(6908 + 16 * k, 0x1f, 8) |
		   F_(6912 + 16 * k, 0x1f, 16) | F_(6916 + 16 * k, 0x1f, 24));
	W_(94, 0xd2b0, F_(7372, 0xf, 0) | F_(7376, 0xf, 4) | F_(7380, 0xf, 8) | F_(7384, 0xf, 12) |
	   F_(7388, 0xf, 16) | F_(7392, 0xf, 20) | F_(7396, 0xf, 24));
	t33_swm_g95(c);
	W_(96, 0xd2b8, F_(7428, 0xff, 0) | F_(7432, 0xff, 8) | F_(7436, 0xff, 16) | F_(7440, 0x1f, 24));
	for (k = 0; k < 21; k++)
		W_(384 + k, 0xd2c4, T3_(7620 + 12 * k, 7624 + 12 * k, 7628 + 12 * k));
	t33_swm_g99(c);
	for (k = 0; k < 5; k++)
		W_(407 + k, 0xd2d8, T3_(8376 + 12 * k, 8380 + 12 * k, 8384 + 12 * k));
	W_(412, 0xd2d8, T3_(8436, 8440, 8468));
	W_(413, 0xd2d8, T3_(8472, 8476, 8480));
	W_(415, 0xd2d8, T3_(8496, 8524, 8556));
	W_(416, 0xd2d8, T3_(8588, 8620, 8528));
	W_(417, 0xd2d8, T3_(8560, 8592, 8624));
	W_(418, 0xd2d8, T3_(8532, 8564, 8596));
	W_(419, 0xd2d8, T3_(8628, 8536, 8568));
	W_(420, 0xd2d8, T3_(8600, 8632, 8540));
	W_(421, 0xd2d8, T3_(8572, 8604, 8636));
	W_(422, 0xd2d8, T3_(8544, 8576, 8608));
	W_(423, 0xd2d8, T3_(8640, 8548, 8580));
	W_(424, 0xd2d8, T3_(8612, 8644, 8552));
	W_(425, 0xd2d8, T3_(8584, 8616, 8648));
	t33_swm_g103(c);
	t33_swm_g105(c);
	W_(3, 0xd01c, F_(3812, 0xff, 0));
	W_(4, 0xd020, F_(3784, 1, 0) | F_(5072, 1, 4) | F_(3816, 1, 16) | F_(3820, 1, 17) |
	   F_(3824, 1, 18) | F_(5084, 1, 20) | F_(5088, 1, 21) | F_(5092, 1, 22) | F_(5096, 1, 23) |
	   F_(3828, 1, 24) | F_(3832, 1, 25) | F_(3836, 1, 26) | F_(3840, 1, 27));
	w5 = F_(3788, 1, 0) | F_(3844, 1, 31) | F_(3792, 1, 4) | F_(5076, 1, 8) | F_(5080, 1, 12) |
	     F_(3800, 1, 21) | F_(3804, 1, 22) | F_(3808, 1, 23) | F_(5020, 1, 29);
	if (c->w[2190] == 1)
		w5 |= 0x10000000;
	if (stbyte != 1)
		w5 |= F_(3796, 1, 20);
	W_(5, 0xd024, w5);
}

/* second batch: rewrite of the registers that depend on the section just copied */
static inline void t33_swm_regs2(const struct t33_swm_ctx *c)
{
	u32 k;

	t33_swm_g59(c);
	W_(64, 0xd214, F_(6632, 0x1ff, 0) | F_(6636, 0xf, 12));
	W_(254, 0xd220, P_(5684, 5856));
	for (k = 0; k < 15; k++)
		W_(255 + k, 0xd220, P_(5860 + 8 * k, 5864 + 8 * k));
	W_(270, 0xd220, F_(5980, 0x1ff, 0));
	t33_swm_g66(c);
	t33_swm_g68(c);
	W_(287, 0xd240, T3_(6196, 6200, 6204));
	W_(288, 0xd240, F_(6208, 0xff, 0) | F_(6212, 0xff, 8));
	t33_swm_g73(c);
	W_(294, 0xd254, T3_(6756, 6760, 6764));
	W_(295, 0xd254, T3_(6768, 6772, 6800));
	W_(297, 0xd254, T3_(6816, 6820, 6824));
	W_(298, 0xd254, T3_(6828, 6856, 6864));
	t33_swm_g77(c);
	t33_swm_g86(c);
	W_(91, 0xd298, F_(8300, 0x1ff, 0) | F_(8304, 0xf, 12));
	for (k = 0; k < 3; k++)
		W_(340 + k, 0xd2a0, T3_(7336 + 12 * k, 7340 + 12 * k, 7344 + 12 * k));
	W_(353, 0xd2a4, P_(7164, 7460));
	for (k = 0; k < 15; k++)
		W_(354 + k, 0xd2a4, P_(7464 + 8 * k, 7468 + 8 * k));
	W_(369, 0xd2a4, F_(7584, 0x1ff, 0));
	W_(93, 0xd2ac, F_(6884, 3, 0) | F_(6888, 3, 4) | F_(6892, 7, 8) | F_(6896, 1, 12) |
	   F_(6900, 1, 16) | F_(7096, 3, 20) | F_(7100, 3, 24) | F_(7104, 3, 28));
	t33_swm_g95(c);
	W_(97, 0xd2bc, F_(7444, 7, 0) | F_(7448, 1, 4) | F_(7588, 1, 8) | F_(7592, 1, 12));
	W_(404, 0xd2c4, T3_(7860, 7864, 7868));
	W_(405, 0xd2c4, T3_(7872, 7876, 7880));
	t33_swm_g99(c);
	W_(411, 0xd2d8, T3_(8424, 8428, 8432));
	W_(412, 0xd2d8, T3_(8436, 8440, 8468));
	W_(414, 0xd2d8, T3_(8484, 8488, 8492));
	W_(415, 0xd2d8, T3_(8496, 8524, 8556));
	W_(416, 0xd2d8, T3_(8588, 8620, 8528));
	W_(417, 0xd2d8, T3_(8560, 8592, 8624));
	W_(419, 0xd2d8, T3_(8628, 8536, 8568));
	W_(420, 0xd2d8, T3_(8600, 8632, 8540));
	W_(421, 0xd2d8, T3_(8572, 8604, 8636));
	W_(423, 0xd2d8, T3_(8640, 8548, 8580));
	W_(424, 0xd2d8, T3_(8612, 8644, 8552));
	W_(425, 0xd2d8, T3_(8584, 8616, 8648));
	W_(427, 0xd2dc, P_(8444, 8448));
	W_(428, 0xd2dc, P_(8452, 8500));
	W_(429, 0xd2dc, P_(8504, 8508));
	W_(430, 0xd2dc, P_(8652, 8656));
	W_(431, 0xd2dc, P_(8660, 8664));
	W_(432, 0xd2dc, P_(8668, 8672));
	W_(433, 0xd2dc, P_(8676, 8680));
	t33_swm_g103(c);
	W_(104, 0xd2e4, F_(8688, 0xf, 0) | F_(8692, 0xf, 4) | F_(8696, 0xf, 8) | F_(8700, 0xf, 12) |
	   F_(8704, 0xf, 16) | F_(8708, 0xf, 20) | F_(8712, 0xf, 24));
	t33_swm_g105(c);
}

/*
 * The part tisp_mdns_switch_para and tisp_mdns_par_refresh share (the vendor
 * inlines it in both): tisp_mdns_binpara_update, the gain interpolations at
 * the block's gain (word 2181) of the section word 2180 points at,
 * func_mdns_para_update, every register field of the block, then
 * tisp_mdns_t33_reg_trig.  cfg = mdnsCfgMode, uvflag = uvLimitFlag[ch],
 * stmem = stMemInfo (76 bytes per channel).
 */
static inline void t33_mdns_par_body(const struct t33_swmdns_ops *op, u32 ch, u32 *inf, u32 cfg, u32 uvflag,
				     const u8 *stmem)
{
	static const struct t33_swm_run run[] = {
		{ 20, 9, 6, 8, 11, 4 },
		{ 56, 78, 15, 16, 22, 4 },
		{ 116, 408, 36, 8, 11, 4 },
		{ 268, 806, 2, 8, 11, 4 },
		{ 292, 832, 63, 8, 11, 4 },
		{ 548, 1526, 1, 8, 11, 4 },
		{ 580, 1544, 22, 8, 11, 4 },
		{ 672, 1787, 57, 8, 11, 4 },
		{ 904, 2416, 3, 16, 24, 8 },
		{ 932, 2488, 3, 8, 11, 4 },
		{ 964, 2526, 31, 8, 11, 4 },
		{ 1100, 2870, 93, 8, 11, 4 },
		{ 1472, 3894, 6, 16, 22, 4 },
		{ 1496, 4026, 11, 8, 11, 4 },
		{ 1540, 4148, 1, 16, 22, 4 },
		{ 1544, 4170, 10, 8, 11, 4 },
		{ 1620, 4289, 3, 8, 11, 4 },
		{ 1652, 4327, 56, 8, 11, 4 },
		{ 1956, 4963, 65, 8, 11, 4 },
		{ 2248, 5686, 48, 8, 11, 4 },
		{ 2452, 6218, 15, 16, 22, 4 },
		{ 2512, 6548, 67, 8, 11, 4 },
		{ 2788, 7287, 2, 8, 11, 4 },
		{ 2812, 7313, 79, 8, 11, 4 },
		{ 3132, 8183, 1, 8, 11, 4 },
		{ 3164, 8201, 22, 8, 11, 4 },
		{ 3256, 8444, 57, 8, 11, 4 },
		{ 3488, 9072, 3, 16, 24, 8 },
		{ 3512, 9143, 4, 8, 11, 4 },
		{ 3548, 9192, 59, 8, 11, 4 },
	};
	struct t33_swm_ctx cx;
	const struct t33_swm_ctx *c = &cx;
	const u8 *sec;
	u32 gain, i, f, r, k;

	ch &= 0xff;
	op->binpara(ch);
	gain = inf[2181];
	sec = (const u8 *)(unsigned long)inf[2180];
	i = (u32)((int)gain >> 16);
	f = gain & 0xffff;
	for (r = 0; r < ARRAY_SIZE(run); r++)
		for (k = 0; k < run[r].n; k++) {
			const u8 *p = sec + run[r].src + k * run[r].sstep;
			u32 *d = inf + ((run[r].dst + k * run[r].dstep) >> 2);

			*d = run[r].bits == 8 ? op->intp8(i, f, p) : op->intp16(i, f, p);
		}
	op->para_update(ch);
	cx.op = op;
	cx.ch = ch;
	cx.cfg = cfg & 0xff;
	cx.w = inf;
	t33_swm_regs1(c, stmem[ch * 76 + 11], uvflag & 0xff);
	t33_swm_regs2(c);
	op->trig(ch);
}

/*
 * tisp_mdns_switch_para: inf = mdnsInfo[ch], tp = tparamsP[ch], g = the five global
 * threshold tables (global_ybsnthres, ypbv0stathres, ypbv1stathres, yrefweimin,
 * yrefweiminadjthres; 11 bytes per channel), cfg = mdnsCfgMode, uvflag =
 * uvLimitFlag[ch], stmem = stMemInfo (76 bytes per channel).
 */
static inline int t33_mdns_switch_para(const struct t33_swmdns_ops *op, u32 ch, u32 *inf, const u8 *tp,
				       u8 *const g[5], u32 cfg, u32 uvflag, const u8 *stmem)
{
	static const u16 cp[5] = { 29526, 29570, 29636, 30316, 30349 };
	u32 r, k;

	ch &= 0xff;
	for (r = 0; r < 5; r++)
		for (k = 0; k < 11; k++)
			g[r][ch * 11 + k] = tp[cp[r] + k];
	inf[2180] = (u32)(unsigned long)(tp + 25144);
	t33_mdns_par_body(op, ch, inf, cfg, uvflag, stmem);
	return 0;
}

/*
 * tisp_mdns_par_refresh(ch, gain, thr) (tisp_mdns_refresh passes thr = word
 * 2182, stIspInitPar + 1000): with the block's last gain (word 2181) still
 * -1 (tisp_mdns_init) the gain is taken and the start words go out first
 * (0xd018 = 0 with the RISC-V MDNS on, riscv = riscvEnable[ch] &&
 * riscvMdnsEn, else 0xd018 = 1 and 0xd014 = 1); later calls take the gain
 * only when it moved by thr or more (unsigned).  Then the shared body.
 * tispMdnsRefresh (refresh) is set on every call, also below thr.
 */
static inline void t33_mdns_par_refresh(const struct t33_swmdns_ops *op, u32 ch, u32 *inf, u32 gain, u32 thr,
					u32 cfg, u32 uvflag, const u8 *stmem, u32 riscv, u8 *refresh)
{
	u32 last = inf[2181];

	if (last == 0xffffffffU) {
		inf[2181] = gain;
		if (riscv) {
			op->regw(ch, 15, (cfg & 0xff) ? 2 : 0xd018, 0);
		} else {
			op->regw(ch, 15, (cfg & 0xff) ? 2 : 0xd018, 1);
			op->regw(ch, 15, (cfg & 0xff) ? 1 : 0xd014, 1);
		}
	} else {
		if ((gain >= last ? gain - last : last - gain) < thr)
			goto out;
		inf[2181] = gain;	/* moved by thr or more */
	}
	t33_mdns_par_body(op, ch, inf, cfg, uvflag, stmem);
out:
	*refresh = 1;
}

#undef W_
#undef F_
#undef T3_
#undef P_

#endif /* TX_ISP_T33_SWMDNS_H */
