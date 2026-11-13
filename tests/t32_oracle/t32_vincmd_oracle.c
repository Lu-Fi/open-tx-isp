// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: video_input_cmd_show of driver/t32/tx_isp_t32_vincmd.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc).
 * Compared: the seq_printf / sprintf calls in order (format, %s texts,
 * values), the 512 text bytes after the call, the return value.
 * Inputs: slots empty or with random text.  Exit 0 = all equal.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_vincmd.h"

u64 video_input_cmd_show(u32, u32, u32, u32);

enum { F_SEQ = 1, F_SPRINTF };
char t32_data_d9460[512];
char t32_data_c42b4[] = "------------>> sensor id: %d <<------------\n";	/* data.c strings */
char t32_data_c42e4[] = "null";
char t32_data_c42ec[] = "%s";
char t32_data_c42f0[] = "%s\n";
static struct t32o8_log *cur;

u64 private_seq_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 w[2] = { a2, a3 };

	t32o8_rec_fmt(cur, F_SEQ, a0, T32O_HOST(a1), w, 2);
	return 0;
}
static int do_sprintf(char *buf, const char *fmt, const char *s)
{
	u32 w[1] = { T32O_PTR(s) };

	t32o8_rec_fmt(cur, F_SPRINTF, (u32)(buf - t32_data_d9460), fmt, w, 1);
	return sprintf(buf, "%s", s);
}
u64 sprintf_ref(u32 a0, u32 a1, u32 a2, u32 a3) __asm__("t32o8_sprintf");
u64 sprintf_ref(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return (u32)do_sprintf(T32O_HOST(a0), T32O_HOST(a1), T32O_HOST(a2)); }

static int o_seq(void *seq, const char *fmt, ...)
{
	u32 w[4];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 4);
	va_end(ap);
	t32o8_rec_fmt(cur, F_SEQ, T32O_PTR(seq), fmt, w, n);
	return 0;
}
static int o_sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	const char *s;

	va_start(ap, fmt);
	s = va_arg(ap, const char *);
	va_end(ap);
	return do_sprintf(buf, fmt, s);
}
static const struct t32_vincmd_ops ops = { o_seq, o_sprintf };

static u32 rs = 0x51c3d0u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, k, failed = 0;
	static char init[512], vend[512];

	for (c = 0; c < 4000; c++) {
		u32 seq = rnd(), vr, orr;

		for (k = 0; k < 512; k++)
			t32_data_d9460[k] = (char)(' ' + rnd() % 90);
		for (k = 0; k < 4; k++)
			t32_data_d9460[128 * k + ((rnd() & 1) ? 0 : rnd() % 100)] = 0;
		memcpy(init, t32_data_d9460, 512);
		t32o_reset();
		t32o8_clear();
		cur = &t32o8_v;
		vr = (u32)t32o_call((t32o_fn)(void (*)(void))video_input_cmd_show, seq, 0, 0, 0, 0);
		memcpy(vend, t32_data_d9460, 512);
		memcpy(t32_data_d9460, init, 512);
		cur = &t32o8_o;
		orr = (u32)t32_video_input_cmd_show(&ops, T32O_HOST(seq), t32_data_d9460);
		if ((t32o8_cmp("vincmd", failed < 8) || vr != orr || memcmp(vend, t32_data_d9460, 512)) && failed++ < 8)
			printf("MISMATCH case %u\n", c);
	}
	printf("t32_vincmd_oracle: 4000 compared, %u mismatches\n", failed);
	return failed ? 1 : 0;
}
