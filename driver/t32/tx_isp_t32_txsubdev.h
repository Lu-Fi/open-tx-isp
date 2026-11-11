/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 tx-isp subdevice init (vendor tx_isp_subdev_init and its inlined
 * tx_isp_subdev_device_init): ops, module init, irqs, the "isp-device"
 * memory resources (+232 + 4 i region, +244 + 4 i mapping by resource
 * index), then by the platform data type (+0): 1 = clocks (+4 count, +8
 * table) and the pads of the descriptor list (+12 u8 count, +16 two bytes
 * each: direction 2 = out, else in; parent byte) into 36 byte pads at +272
 * (out, counted at u16 +268) / +276 (in, u16 +270); 2 = clocks only;
 * others: the header module.
 * Pad: +0 subdevice, +4 index, +5 / +6 descriptor bytes, +7 2, +20 0.
 * Platform device: +0 name, +24 / +60 region name (+60 wins when set),
 * +88 platform data, +224 resource count.
 *
 * Checked bit-exact against the vendor flow (Matt Davis' recovered C,
 * PR #15) by tests/t32_oracle (t32_txsubdev_oracle).
 */
#ifndef TX_ISP_T32_TXSUBDEV_H
#define TX_ISP_T32_TXSUBDEV_H

#include "tx_isp_t32_txos.h"
#include "tx_isp_t32_txirq.h"
#include "tx_isp_t32_txcore.h"

#define T32_IORESOURCE_MEM	512U
#define T32_PDEV_NAME_ALT	60U
#define T32_PDEV_NAME_RES	24U
#define T32_PDEV_NRES		224U
#define T32_SD_REGION(i)	(232U + 4U * (i))
#define T32_SD_IOMAP(i)		(244U + 4U * (i))
#define T32_SD_NCLKS		260U
#define T32_SD_NPAD_OUT		268U	/* u16 */
#define T32_SD_NPAD_IN		270U	/* u16 */
#define T32_SD_PADS_OUT		272U
#define T32_SD_PADS_IN		276U
#define T32_SD_PAD_BYTES	36U
#define T32_SUBDEV_GFP		0x24000c0U
#define T32_ENXIO		6U
#define T32_EBUSY		16U

/* provided by the driver glue (kernel) or by the oracle (host) */
u32 t32_tx_os_platform_get_resource(u32 pdev, u32 type, u32 n);
u32 t32_tx_os_request_mem_region_u(u32 start, u32 n, u32 name);
u32 t32_tx_os_ioremap(u32 start, u32 n);
void t32_tx_os_iounmap(u32 a);
void t32_tx_os_release_mem_region(u32 start, u32 n);
int t32_tx_os_init_clks(u32 sd, u32 clks);
void t32_tx_os_release_clks(u32 sd);
u32 t32_tx_os_kmalloc(u32 n, u32 flags);
void t32_tx_os_kfree(u32 p);
void t32_tx_os_memset(u32 a, u32 c, u32 n);

/* the pads of one direction (2 = out, 1 = in); the in pads are marked 2 as well */
static inline void t32_tx_subdev_fill_pads(u32 sd, u32 pd, u32 pads, u32 dir)
{
	u32 k, j = 0;

	for (k = 0; (int)k < (int)t32_tx_b(pd + 12); k++) {
		u32 desc = t32_tx_ld(pd, 16) + 2 * k, p;

		if (t32_tx_b(desc) != dir)
			continue;
		p = pads + j * T32_SD_PAD_BYTES;
		t32_tx_wb(p + 4, (u8)j);
		t32_tx_st(p, 0, sd);
		j++;
		t32_tx_wb(p + 5, t32_tx_b(desc));
		t32_tx_wb(p + 7, 2);
		t32_tx_st(p, 20, 0);
		t32_tx_wb(p + 6, t32_tx_b(desc + 1));
	}
}

/*
 * The error chain of the vendor: "Failed to init subdev", unmap (the
 * vendor passes the address of the +244 field), release the last region,
 * free the irqs, module deinit.
 */
static inline int t32_tx_subdev_unwind(u32 sd, u32 region, int r)
{
	t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to init subdev!\n", "tx_isp_subdev_init", 3134);
	t32_tx_os_iounmap(sd + T32_SD_IOMAP(0));
	t32_tx_os_release_mem_region(t32_tx_ld(region, 0), t32_tx_ld(region, 4) + 1 - t32_tx_ld(region, 0));
	t32_tx_isp_free_irq(sd + T32_SD_IRQ);
	t32_tx_isp_module_deinit(sd);
	return r;
}

/*
 * tx_isp_subdev_init(pdev, sd, ops).  Vendor bugs kept: the in-pad
 * allocation is tested through the out-pad pointer (no out pads: always
 * "Failed to malloc inpads"); the unwind releases the last resource
 * (a NULL / unmatched last resource faults).
 */
static inline int t32_tx_subdev_init(u32 pdev, u32 sd, u32 ops)
{
	u32 pd, i, res = 0, reg, map, nout, nin, out = 0, in = 0, k;
	int r;

	if (!pdev || !sd) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] the parameters are invalid!\n", "tx_isp_subdev_init", 3078);
		return -(int)T32_EINVAL;
	}
	t32_tx_st(sd, T32_SD_OPS, ops);
	if (t32_tx_isp_module_init(pdev, sd)) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to init isp module(%s)\n", "tx_isp_subdev_init", 3085,
				     (const char *)T32_TX_P(t32_tx_ld(pdev, 0)));
		return -12;
	}
	pd = t32_tx_ld(pdev, T32_PDEV_DATA);
	if (!pd)
		return 0;
	r = t32_tx_isp_request_irq(pdev, sd + T32_SD_IRQ);
	if (r) {
		t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to request irq!\n", "tx_isp_subdev_init", 3097);
		t32_tx_isp_module_deinit(sd);
		return r;
	}
	for (i = 0; i < t32_tx_ld(pdev, T32_PDEV_NRES); i++) {
		u32 name;

		res = t32_tx_os_platform_get_resource(pdev, T32_IORESOURCE_MEM, i);
		if (!res)
			continue;
		if (strcmp((const char *)T32_TX_P(t32_tx_ld(res, 8)), "isp-device")) {
			res = 0;
			continue;
		}
		name = t32_tx_ld(pdev, T32_PDEV_NAME_ALT) ? t32_tx_ld(pdev, T32_PDEV_NAME_ALT)
							   : t32_tx_ld(pdev, T32_PDEV_NAME_RES);
		reg = t32_tx_os_request_mem_region_u(t32_tx_ld(res, 0), t32_tx_ld(res, 4) + 1 - t32_tx_ld(res, 0), name);
		res = reg;
		if (!reg) {
			t32_tx_os_isp_printf(2, "[ %s:%d ] Not enough memory for resources\n", "tx_isp_subdev_init", 3108);
			t32_tx_isp_free_irq(sd + T32_SD_IRQ);
			t32_tx_isp_module_deinit(sd);
			return -(int)T32_EBUSY;
		}
		map = t32_tx_os_ioremap(t32_tx_ld(reg, 0), t32_tx_ld(reg, 4) + 1 - t32_tx_ld(reg, 0));
		t32_tx_st(sd, T32_SD_IOMAP(i), map);
		if (!map) {
			t32_tx_os_isp_printf(2, "[ %s:%d ] Unable to ioremap registers\n", "tx_isp_subdev_init", 3115);
			t32_tx_os_release_mem_region(t32_tx_ld(reg, 0), t32_tx_ld(reg, 4) + 1 - t32_tx_ld(reg, 0));
			t32_tx_isp_free_irq(sd + T32_SD_IRQ);
			t32_tx_isp_module_deinit(sd);
			return -(int)T32_ENXIO;
		}
		t32_tx_st(sd, T32_SD_REGION(i), reg);
	}
	switch (t32_tx_b(pd)) {
	case 1:
		t32_tx_st(sd, T32_SD_NCLKS, t32_tx_b(pd + 4));
		r = t32_tx_os_init_clks(sd, t32_tx_ld(pd, 8));
		if (r) {
			t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to init %s's clks!\n", "tx_isp_subdev_device_init", 2996,
					     (const char *)T32_TX_P(t32_tx_ld(sd, T32_SD_NAME)));
			return t32_tx_subdev_unwind(sd, res, r);
		}
		for (k = 0; (int)k < (int)t32_tx_b(pd + 12); k++) {
			if (t32_tx_b(t32_tx_ld(pd, 16) + 2 * k) == 2)
				t32_tx_wh(sd + T32_SD_NPAD_OUT, t32_tx_h(sd + T32_SD_NPAD_OUT) + 1);
			else
				t32_tx_wh(sd + T32_SD_NPAD_IN, t32_tx_h(sd + T32_SD_NPAD_IN) + 1);
		}
		nout = t32_tx_h(sd + T32_SD_NPAD_OUT);
		if (nout) {
			out = t32_tx_os_kmalloc(nout * T32_SD_PAD_BYTES, T32_SUBDEV_GFP);
			if (!out) {
				t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to malloc %s's outpads\n", "tx_isp_subdev_device_init",
						     3012, (const char *)T32_TX_P(t32_tx_ld(sd, T32_SD_NAME)));
				t32_tx_os_release_clks(sd);
				return t32_tx_subdev_unwind(sd, res, -12);
			}
			t32_tx_os_memset(out, 0, t32_tx_h(sd + T32_SD_NPAD_OUT) * T32_SD_PAD_BYTES);
			t32_tx_subdev_fill_pads(sd, pd, out, 2);
		}
		nin = t32_tx_h(sd + T32_SD_NPAD_IN);
		if (nin) {
			in = t32_tx_os_kmalloc(nin * T32_SD_PAD_BYTES, T32_SUBDEV_GFP);
			if (!out) {
				t32_tx_os_isp_printf(2, "[ %s:%d ] Failed to malloc %s's inpads\n", "tx_isp_subdev_device_init",
						     3033, (const char *)T32_TX_P(t32_tx_ld(sd, T32_SD_NAME)));
				t32_tx_os_kfree(0);
				t32_tx_os_release_clks(sd);
				return t32_tx_subdev_unwind(sd, res, -12);
			}
			t32_tx_os_memset(in, 0, t32_tx_h(sd + T32_SD_NPAD_IN) * T32_SD_PAD_BYTES);
			t32_tx_subdev_fill_pads(sd, pd, in, 1);
		}
		t32_tx_st(sd, T32_SD_PADS_OUT, out);
		t32_tx_st(sd, T32_SD_PADS_IN, in);
		return 0;
	case 2:
		t32_tx_st(sd, T32_SD_NCLKS, t32_tx_b(pd + 4));
		r = t32_tx_os_init_clks(sd, t32_tx_ld(pd, 8));
		return r ? t32_tx_subdev_unwind(sd, res, r) : 0;
	default:
		t32_tx_os_isp_printf(0, "[ %s:%d ] It's header!\n", "tx_isp_subdev_init", 3131);
		return 0;
	}
}

#endif
