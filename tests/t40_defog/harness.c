/* host side of tests/t40_defog_host.sh (#332): driver/t40/tx_isp_t40_defog.h, one frame of the soft step
 * (t40_dfg_soft = OEM Tiziano_defog_soft) per case.  stdin: cases of 812 tool bytes + 3948 soft bytes (OEM
 * defog_main_soft_para image; the two pointer words at 2448/2452 are ignored, the curve knots/levels are
 * read from the tool at +149/+154 like the driver); stdout: the 3948 soft bytes after the step.
 * Built with -DDEFOG_H="<header>". */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32; typedef uint64_t u64;
typedef unsigned int uint; typedef unsigned int gfp_t;
#define module_param_named(a, b, c, d)
#define module_param(a, b, c)
#define MODULE_PARM_DESC(a, b)
#define t40_bringup_param_named(...)
#define T40C2
#define KERN_INFO ""
#define KERN_ERR ""
#define printk(...) 0
#define EINVAL 22
#define ENOMEM 12
#define ENODEV 19
#define GFP_KERNEL 0
enum dma_data_direction { DMA_BIDIRECTIONAL = 0 };
static void dma_cache_sync(void *d, void *p, size_t n, enum dma_data_direction x) { (void)d; (void)p; (void)n; (void)x; }
static void *vzalloc(size_t n) { return calloc(1, n); }
static void *vmalloc(size_t n) { return malloc(n); }
static void vfree(const void *p) { free((void *)p); }
static void *kmalloc(size_t n, gfp_t f) { (void)f; return malloc(n); }
static void kfree(const void *p) { free((void *)p); }
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

int main(int argc, char **argv)
{
	static struct t40_dfg_ctx c;

	if (argc > 1)
		t40_defog_motion_var = atoi(argv[1]) != 0;
	while (fread(c.tool, 1, T40_DFG_TOOL_BYTES, stdin) == T40_DFG_TOOL_BYTES &&
	       fread(&c.soft, 1, sizeof(c.soft), stdin) == sizeof(c.soft)) {
		t40_dfg_soft(&c);
		memset(c.soft.pad2417, 0, sizeof(c.soft.pad2417));
		fwrite(&c.soft, 1, sizeof(c.soft), stdout);
	}
	return 0;
}
