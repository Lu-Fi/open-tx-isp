// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_tisp_params_print and t32_tisp_params_print_detect of driver/t32/tx_isp_t32_tprint.h
 * against the vendor tisp_params_print / tisp_params_print_detect with their array printers print_array_awb_8 /
 * 16 / 32 (Matt Davis' recovered C, PR #15 @ 2e2e06bc; printk renamed by fix_show.sh / fix_dbg.sh).
 * Random AWB parameter blocks of channel 0..3 (t32_data_e16b4: first / second block; t32_data_e16a4: the detect
 * block with one random array per pointer).  Compared: the print stream (printk / isp_printf, rendered text), the
 * result of tisp_params_print and that no block changed.  Exit 0 = all equal, 1 = first mismatch.
 */
#include "t32_show_rec.h"
#include "tx_isp_t32_tprint.h"

u64 tisp_params_print(u32, u32, u32, u32), tisp_params_print_detect(u32, u32, u32, u32);
extern u32 t32_data_e16a4[4], t32_data_e16b4[4];
int t32dbg_printk(const char *fmt, ...);
int t32dbg_printk(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = sr_vlog(SR_PRINTK, 0, (u32)(uintptr_t)fmt, ap);
	va_end(ap);
	return n;
}

/* the array printers' strings (data.c, see t32_dbg_str.inc) */
const char t32_data_c8740[] = " %d,", t32_data_c8748[] = "]\n";

#define PSZ	6800
#define QSZ	115840
#define DSZ	256

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 1000, c, prints = 0, i;
	static u8 p0[PSZ], q0[QSZ], d0[DSZ];

	for (c = 0; c < n; c++) {
		u32 r0, r1, ch = sr_rnd() % 4;
		u8 *p, *q, *d, *blk;

		/* ---- tisp_params_print ---- */
		t32o_reset();
		for (i = 0; i < 4; i++) {
			blk = t32o_alloc(8);
			t32_data_e16b4[i] = T32O_PTR(blk);
			p = t32o_alloc(PSZ); q = t32o_alloc(QSZ);
			((u32 *)blk)[0] = T32O_PTR(p); ((u32 *)blk)[1] = T32O_PTR(q);
			if (i == ch) {
				sr_fill(p, PSZ); sr_fill(q, QSZ);
				memcpy(p0, p, PSZ); memcpy(q0, q, QSZ);
				break;
			}
		}
		p = T32O_HOST(((u32 *)T32O_HOST(t32_data_e16b4[ch]))[0]);
		q = T32O_HOST(((u32 *)T32O_HOST(t32_data_e16b4[ch]))[1]);
		sr_begin();
		r0 = (u32)t32o_call(SR_R(tisp_params_print), ch, sr_rnd(), sr_rnd(), sr_rnd(), 0);
		sr_side = 1;
		r1 = (u32)t32_tisp_params_print(t32_data_e16b4, ch);
		if (sr_cmp("t32_tpr2_oracle", c, "tisp_params_print")) return 1;
		if (r0 != r1 || memcmp(p0, p, PSZ) || memcmp(q0, q, QSZ)) {
			printf("t32_tpr2_oracle: case %u params_print: ret %08x/%08x\n", c, r0, r1);
			return 1;
		}
		prints += sr_n[0];

		/* ---- tisp_params_print_detect ---- */
		t32o_reset();
		for (i = 0; i < 4; i++)
			t32_data_e16a4[i] = T32O_PTR(t32o_alloc(DSZ));
		d = T32O_HOST(t32_data_e16a4[ch]);
		{	/* the words no array uses point into one random block too (an offset slip reads other data) */
			u8 *big = t32o_alloc(16384 + DSZ);

			sr_fill(big, 16384 + DSZ);
			for (i = 0; i < DSZ / 4; i++)
				((u32 *)d)[i] = T32O_PTR(big) + 4 * i;
		}
		for (i = 0; i < sizeof(t32_awb_det_arr) / sizeof(t32_awb_det_arr[0]); i++) {
			u32 sz = t32_awb_det_arr[i].n * t32_awb_det_arr[i].bits / 8;
			u8 *a = t32o_alloc(sz);

			sr_fill(a, sz);
			memcpy(d + t32_awb_det_arr[i].off, &(u32){ T32O_PTR(a) }, 4);
		}
		memcpy(d0, d, DSZ);
		sr_begin();
		t32o_call(SR_R(tisp_params_print_detect), ch, sr_rnd(), sr_rnd(), sr_rnd(), 0);
		sr_side = 1;
		t32_tisp_params_print_detect(t32_data_e16a4, ch);
		if (sr_cmp("t32_tpr2_oracle", c, "tisp_params_print_detect")) return 1;
		if (memcmp(d0, d, DSZ)) { printf("t32_tpr2_oracle: case %u detect block changed\n", c); return 1; }
		prints += sr_n[0];
	}
	printf("t32_tpr2_oracle: %u cases x 2 functions compared (%u prints), 0 mismatches\n", n, prints);
	return 0;
}
