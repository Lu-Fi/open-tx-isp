/* Host test: T32 ISP memory size (vs the vendor answer seen in QEMU S0) and
 * the RCFG list builder/walker round trip. make -C tests tx_isp_t32_mem_rcfg_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../driver/t32/tx_isp_t32_mem.h"
#include "../driver/t32/tx_isp_t32_rcfg.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

static u32 seen_reg[64], seen_val[64], nseen, nport, nall;
static void rec(u32 r, u32 v, void *p) { (void)p; if (nseen < 64) { seen_reg[nseen] = r; seen_val[nseen++] = v; } }
static void cnt(u32 r, u32 v, void *p) { (void)v; (void)p; nall++; if (r == 0x32024) nport++; }

/* the vendor stream-on list's structure (r20): 0x5010, 13 at 0x5040, select,
 * 3456 into the port 0x32024, deselect, 0x2f4 */
static u32 vendor_list[3480];
static void build_vendor_like(void)
{
	u32 *w = vendor_list, n = 0, i;
	w[++n] = 0x80001404; w[++n] = 1;
	w[++n] = 0x80301410; for (i = 0; i < 13; i++) w[++n] = i;
	w[++n] = 0x8000c808; w[++n] = 0x101;
	w[++n] = 0x35fcc809; for (i = 0; i < 3456; i++) w[++n] = i;
	w[++n] = 0x8000c808; w[++n] = 0;
	w[++n] = 0x800000bd; w[++n] = 0;
	w[0] = n;
}

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
	CHECK(stop == 17 && used == 16, "stops at the truncated port command (stop %u used %u)", stop, used);

	build_vendor_like();
	CHECK(vendor_list[0] == 0xd97, "vendor-like length %#x (vendor 0xd97)", vendor_list[0]);
	nall = nport = 0;
	used = t32_rcfg_walk(vendor_list, 3480, cnt, NULL, &stop);
	CHECK(stop == 0 && used == 0xd97 && nall == 1 + 13 + 1 + 3456 + 1 + 1 && nport == 3456,
	      "whole vendor list: used %#x writes %u port %u stop %u", used, nall, nport, stop);

	/* the real dump, when given: T32_RCFG_DUMP=.../rcfg-list0-r20.raw.txt */
	if (getenv("T32_RCFG_DUMP")) {
		FILE *f = fopen(getenv("T32_RCFG_DUMP"), "r");
		static u32 d[4096];
		u32 nd = 0;
		while (f && nd < 4096 && fscanf(f, "%x", &d[nd]) == 1)
			nd++;
		if (f)
			fclose(f);
		nall = nport = 0;
		used = t32_rcfg_walk(d, nd, cnt, NULL, &stop);
		CHECK(nd > 1 && stop == 0 && used == d[0] && nport == 3456,
		      "real dump: %u words, used %#x of %#x, port %u, stop %u", nd, used, d[0], nport, stop);
		printf("real dump: %u writes, %u into the 0x32024 port\n", nall, nport);
	}

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
	/* port command round trip */
	{
		u32 vals[5] = { 9, 8, 7, 6, 5 };
		t32_rcfg_init(&l, buf, 64);
		t32_rcfg_add(&l, 0x32020, 0x101);
		t32_rcfg_add_port(&l, 0x32024, vals, 5);
		t32_rcfg_add(&l, 0x32028, 1);	/* must not extend the port command */
		nseen = 0;
		t32_rcfg_walk(buf, 64, rec, NULL, &stop);
		CHECK(!l.overflow && stop == 0 && nseen == 7 && buf[3] == ((4u << 18) | (0x32024 >> 2)) &&
		      seen_reg[1] == 0x32024 && seen_reg[5] == 0x32024 && seen_val[5] == 5 &&
		      seen_reg[6] == 0x32028, "port round trip n=%u", nseen);
	}
	t32_rcfg_init(&l, buf, 4);
	t32_rcfg_add(&l, 0x10, 1);
	t32_rcfg_add(&l, 0x20, 1);
	CHECK(l.overflow, "overflow detected");
	printf("%s (%d failures)\n", fails ? "FAILED" : "ok", fails);
	return !!fails;
}
