/* host side of the day/night part of tests/t40_defog_host.sh (#332): driver/t40/tx_isp_t40_defog.h through
 * t40_defog_init (day bank) -> D frames -> t40_defog_dn_refresh (night bank) -> N frames.
 * argv: bin width height D N air_day [trans_day ev night_start].  stdin: (D + N) x 540 u32 cell sums (stat[0], stat[1],
 * stat[2]).  stdout: the four outputs (3 x 180 air light + 180 transmission) after each night frame.  ev != 0: EV update
 * (tisp_defog_ev_update, EV = ev) right after the switch.  night_start = 1: the driver order of this repo (night bank
 * already active at the defog init, no refresh at the switch).  Built with -DDEFOG_H="<header>". */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
typedef unsigned int gfp_t;
#define module_param_named(a, b, c, d)
#define module_param(a, b, c)
#define MODULE_PARM_DESC(a, b)
#define T40C2
#define KERN_INFO ""
#define KERN_ERR ""
#define printk(...) 0
#define EINVAL 22
#define ENOMEM 12
#define ENODEV 19
enum dma_data_direction { DMA_BIDIRECTIONAL = 0 };
static void dma_cache_sync(void *d, void *p, size_t n, enum dma_data_direction x) { (void)d; (void)p; (void)n; (void)x; }
static void *vzalloc(size_t n) { return calloc(1, n); }
static void *vmalloc(size_t n) { return malloc(n); }
static void vfree(const void *p) { free((void *)p); }
static uint32_t regs[0x40000 / 4];
static int system_reg_write(uint32_t r, uint32_t v) { if (r < sizeof(regs)) regs[r / 4] = v; return 0; }
static uint32_t system_reg_read(uint32_t r) { return r < sizeof(regs) ? regs[r / 4] : 0; }
static uintptr_t tparamsN[2], tparams_day;
static unsigned char tispinfo[128];
static uint32_t tisp_get_tuning(void) { return 0; }
static int32_t system_irq_func_set(int32_t irq, int32_t fn) { (void)irq; (void)fn; return 0; }
static int32_t tisp_event_set_cb(int32_t ch, int32_t ev, int32_t fn) { (void)ch; (void)ev; (void)fn; return 0; }
static int64_t tisp_event_push(uint32_t ch, uintptr_t a1) { (void)ch; (void)a1; return 0; }
#include DEFOG_H

#define BANK 0x18ff4
int main(int argc, char **argv)
{
	static uint8_t bin[204800];
	uint32_t w, h, nd, nn, f, ev;
	int ns;
	FILE *fp;

	if (argc < 7 || !(fp = fopen(argv[1], "rb")) || fread(bin, 1, sizeof(bin), fp) != sizeof(bin))
		return 2;
	fclose(fp);
	w = atoi(argv[2]); h = atoi(argv[3]); nd = atoi(argv[4]); nn = atoi(argv[5]);
	t40_defog_air_day = atoi(argv[6]) != 0;
	if (argc > 7)
		t40_defog_trans_day = atoi(argv[7]) != 0;
	ev = argc > 8 ? strtoul(argv[8], NULL, 0) : 0;
	ns = argc > 9 && atoi(argv[9]);
	tparams_day = (uintptr_t)(bin + 0x18);
	tparamsN[0] = ns ? (uintptr_t)(bin + 0x18 + BANK) : tparams_day;
	if (t40_defog_init(w, h))
		return 3;
	for (f = 0; f < nd + nn; f++) {
		if (f == nd) {
			if (!ns) {
				tparamsN[0] = (uintptr_t)(bin + 0x18 + BANK);
				t40_defog_dn_refresh(w, h);
			}
			if (ev)
				t40_defog_ev_update((uint64_t)ev << 10);
		}
		if (fread(t40_dfg->soft.stat, 4, 540, stdin) != 540)
			return 4;
		t40_dfg_process(0, 0, 0, 0, 0);
		if (f >= nd)
			fwrite(t40_dfg->soft.out, 1, 720, stdout);
	}
	return 0;
}
