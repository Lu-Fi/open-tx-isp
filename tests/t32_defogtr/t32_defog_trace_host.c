// SPDX-License-Identifier: GPL-2.0
/*
 * T32 defog init against the vendor (openimp #854): the register writes of
 * tisp_defog_init as the driver composes them (tx_isp_t32_defogd.c: the
 * defog.h port with defog_count_weight35abc of defogcw.h on a copy of IQ
 * section 14) for the cv8001 PRJ007 day bin at 3840x2160, compared write by
 * write with the vendor tx-isp-t32.ko in QEMU (trace t32plan/s0/tr10-isp.txt,
 * phase ISP_AddSensor, 309 writes to 0xa0xx / 0xa2xx / 0x320a0 / 0x320a4).
 * The DMA addresses 0xa210..0xa21c are compared relative to 0xa210.
 *   usage: t32_defog_trace_host <cv8001-PRJ007-day.bin> <vendor writes>
 *          t32_defog_trace_host <bin> - <width> <height>   (print the init writes, device read-back)
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "tx_isp_t32_defogcw.h"
#include "tx_isp_t32_defogs.h"
#include "tx_isp_t32_iq.h"

static struct t32_defog_chan c;

static void *h_vm(u32 n) { return calloc(1, n); }
static void *h_km(u32 n, u32 g) { (void)g; return calloc(1, n); }
static void h_free(void *p) { free(p); }
static const struct t32_defog_cw_ops cw_ops = { h_vm, h_free };

static void h_cw(u32 ch, u32 h, u32 w, u32 gain, u8 *p80, u8 *p128, u8 *p430, u8 *p462, u8 *p494,
		 u8 *p526, u8 *p558)
{
	u8 *const out[5] = { p430, p462, p494, p526, p558 };

	(void)ch;
	if (t32_defog_count_weight35abc(0, h, w, gain, (u16 *)p80, (u16 *)p128, out, &c.vbuf, &c.vsize, &cw_ops))
		exit(3);
}
static void h_soft(struct t32_defog_par *p) { t32_defog_soft(p); }
static u32 h_rd(u32 r) { (void)r; return 0; }
static void h_sync(const u8 *b, u32 l) { (void)b; (void)l; }
static void h_push(u32 ch, const u32 *e) { (void)ch; (void)e; }
static void h_irq(u32 ch, u32 id) { (void)ch; (void)id; }
static void h_cb(u32 ch, u32 e) { (void)ch; (void)e; }
static const struct t32_defog_ops ops = { h_cw, h_soft, h_rd, h_sync, h_push, h_vm, h_km, h_free, h_free, h_irq, h_cb };

int main(int argc, char **argv)
{
	static struct t32_front_write w[512];
	static u32 vr[512], vv[512];
	u32 size[2] = { 3840, 2160 }, off, len = 0, base = 0, vbase = 0;
	unsigned int n = 0, vn = 0, i, bad = 0;
	uint8_t *bin, *sec;
	long bytes;
	FILE *f;

	if (argc != 3 && argc != 5)
		return 2;
	if (argc == 5) {
		size[0] = (u32)strtoul(argv[3], NULL, 0);
		size[1] = (u32)strtoul(argv[4], NULL, 0);
	}
	f = fopen(argv[1], "rb");
	if (!f)
		return 2;
	fseek(f, 0, SEEK_END);
	bytes = ftell(f);
	rewind(f);
	bin = malloc(bytes);
	if (fread(bin, 1, bytes, f) != (size_t)bytes)
		return 2;
	fclose(f);
	f = argc == 5 ? NULL : fopen(argv[2], "r");
	if (!f && argc == 3)
		return 2;
	while (f && vn < 512 && fscanf(f, "%x %x", &vr[vn], &vv[vn]) == 2)
		vn++;
	if (f)
		fclose(f);
	off = t32_iq_section(bin, (u32)bytes, 14, &len);
	if (!off || len < T32_DEFOG_SEC_BYTES || t32_front_le32(bin + off) != T32_DEFOG_ID) {
		printf("t32_defog_trace_host: no defog section\n");
		return 1;
	}
	sec = malloc(T32_DEFOG_SEC_OFF + T32_DEFOG_SEC_BYTES);
	memcpy(sec + T32_DEFOG_SEC_OFF, bin + off, T32_DEFOG_SEC_BYTES);
	c.ops = &ops;
	if (t32_defog_init(&c, 0, size, sec, w, &n))
		return 3;
	if (argc == 5) {
		for (i = 0; i < n; i++)
			printf("%05x %08x\n", w[i].reg, w[i].val);
		return 0;
	}
	for (i = 0; i < vn; i++)
		if (vr[i] == 0xa210)
			vbase = vv[i];
	for (i = 0; i < n; i++)
		if (w[i].reg == 0xa210)
			base = w[i].val;
	if (n != vn) {
		printf("t32_defog_trace_host: %u writes, vendor %u\n", n, vn);
		bad++;
	}
	for (i = 0; i < n && i < vn; i++) {
		int dma = w[i].reg >= 0xa210 && w[i].reg <= 0xa21c;
		u32 a = dma ? w[i].val - base : w[i].val, b = dma ? vv[i] - vbase : vv[i];

		if (w[i].reg != vr[i] || a != b) {
			if (bad++ < 10)
				printf("t32_defog_trace_host: write %u: %05x = %08x, vendor %05x = %08x\n", i, w[i].reg,
				       w[i].val, vr[i], vv[i]);
		}
	}
	if (bad) {
		printf("t32_defog_trace_host: FAIL (%u differences)\n", bad);
		return 1;
	}
	printf("t32_defog_trace_host: OK (%u writes equal to the vendor tisp_defog_init, cv8001 3840x2160)\n", n);
	return 0;
}
