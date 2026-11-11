// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_isp_printf of driver/t32/tx_isp_t32_dbgp.h against the vendor isp_printf
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc, fn_..10, with the host declarations of fix_dbg.sh):
 * random level (incl. >= 2^31, compared unsigned), random module print level, formats with 0..2
 * integer arguments.  Compared: the result, the text printk("%pV") gets (rendered by vsnprintf)
 * and the number of stack dumps.  Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "t32_oracle_env.h"

struct va_format { const char *fmt; va_list *va; };
int t32dbg_printk(const char *fmt, ...);
void t32dbg_dump_stack(void);
#define T32_DBG_PRINTK t32dbg_printk
#define T32_DBG_DUMP_STACK t32dbg_dump_stack
#include "tx_isp_t32_dbgp.h"

int t32_data_ce260;
int isp_printf(int level, const char *fmt, ...);	/* the reference */

static char text[2][256];
static int ncall[2], nstack[2], side;

int t32dbg_printk(const char *fmt, ...)
{
	va_list ap;
	int n = 0;

	va_start(ap, fmt);
	if (!strcmp(fmt, "%pV")) {
		struct va_format *v = va_arg(ap, struct va_format *);
		va_list c;

		va_copy(c, *v->va);
		n = vsnprintf(text[side], sizeof(text[side]), v->fmt, c);
		va_end(c);
	} else {
		snprintf(text[side], sizeof(text[side]), "RAW:%s", fmt);
	}
	va_end(ap);
	ncall[side]++;
	return n;
}
void t32dbg_dump_stack(void) { nstack[side]++; }

static u32 rs = 0x3c6ef372u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	static const char *const f[] = { "plain text\n", "v=%d\n", "a=%x b=%u;", "[ %d:%d ]" };
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 20000, c, shown = 0;

	for (c = 0; c < n; c++) {
		u32 lv = (c & 3) == 0 ? rnd() : rnd() % 5, a = rnd(), b = rnd();
		int pl = (c & 7) == 0 ? (int)rnd() : (int)(rnd() % 5), rv, ro;
		const char *fm = f[rnd() & 3];

		t32_data_ce260 = pl;
		text[0][0] = text[1][0] = 0;
		ncall[0] = ncall[1] = nstack[0] = nstack[1] = 0;
		side = 0;
		rv = (int)t32o_call(((t32o_fn)(void (*)(void))isp_printf), lv, T32O_PTR(fm), a, b, 0);
		side = 1;
		ro = t32_isp_printf(pl, (int)lv, fm, a, b);
		if (rv != ro || ncall[0] != ncall[1] || nstack[0] != nstack[1] || strcmp(text[0], text[1])) {
			printf("t32_dbgp_oracle: case %u lv %08x pl %d \"%s\": vendor %d \"%s\" %d dumps / ours %d \"%s\" %d dumps\n",
			       c, lv, pl, fm, rv, text[0], nstack[0], ro, text[1], nstack[1]);
			return 1;
		}
		shown += ncall[0];
	}
	printf("t32_dbgp_oracle: %u calls compared (%u printed), 0 mismatches\n", n, shown);
	return 0;
}
