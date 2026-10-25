/* Host test: T32 ISP memory size (vs the vendor answer seen in QEMU S0) and
 * the RCFG list builder/walker round trip. make -C tests tx_isp_t32_mem_rcfg_test */
#include <stdio.h>
#include <string.h>
#include "../driver/t32/tx_isp_t32_mem.h"
#include "../driver/t32/tx_isp_t32_rcfg.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

static u32 seen_reg[64], seen_val[64], nseen;
static void rec(u32 r, u32 v, void *p) { (void)p; if (nseen < 64) { seen_reg[nseen] = r; seen_val[nseen++] = v; } }

int main(void)
{
	u32 s = t32_isp_mem_size(3840, 2160, T32_MEMOPT_DEFAULT, 0, 0);
	CHECK(s == 0x9f9000, "mem 3840x2160 = %#x, vendor 0x9f9000", s);
	CHECK(t32_isp_mem_size(1920, 1080, 3000, 0, 0) == 0, "memopt > 2000 refused");

	/* first commands of the vendor stream-on list (r20) */
	u32 vend[] = { 0x11, 0x80001404, 0x00000001, 0x80301410, 1,2,3,4,5,6,7,8,9,10,11,12,13, 0x35fcc809 };
	u32 stop, used;
	nseen = 0;
	used = t32_rcfg_walk(vend, sizeof vend / 4, rec, NULL, &stop);
	CHECK(nseen == 14 && seen_reg[0] == 0x5010 && seen_val[0] == 1 &&
	      seen_reg[1] == 0x5040 && seen_reg[13] == 0x5070 && seen_val[13] == 13,
	      "vendor head decode n=%u r0=%#x r1=%#x", nseen, seen_reg[0], seen_reg[1]);
	CHECK(stop == 17 && used == 16, "stops at the non-command word (stop %u used %u)", stop, used);

	u32 buf[64];
	struct t32_rcfg_list l;
	t32_rcfg_init(&l, buf, 64);
	t32_rcfg_add(&l, 0x5010, 1);
	for (u32 i = 0; i < 13; i++)
		t32_rcfg_add(&l, 0x5040 + 4 * i, i + 1);
	t32_rcfg_add(&l, 0xf0040, 7);
	CHECK(!l.overflow && buf[0] == 18 && buf[1] == 0x80001404 && buf[3] == 0x80301410 &&
	      buf[17] == (0x80000000u | (0xf0040 >> 2)), "builder words %#x %#x %#x", buf[0], buf[1], buf[3]);
	nseen = 0;
	t32_rcfg_walk(buf, 64, rec, NULL, &stop);
	CHECK(stop == 0 && nseen == 15 && seen_reg[14] == 0xf0040 && seen_val[14] == 7, "round trip");
	t32_rcfg_init(&l, buf, 4);
	t32_rcfg_add(&l, 0x10, 1);
	t32_rcfg_add(&l, 0x20, 1);
	CHECK(l.overflow, "overflow detected");
	printf("%s (%d failures)\n", fails ? "FAILED" : "ok", fails);
	return !!fails;
}
