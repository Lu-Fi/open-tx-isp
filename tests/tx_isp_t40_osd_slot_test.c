/* Host test of driver/t40/tx_isp_t40_osd_slot.h (T40 OSD setters, #158 #160 #141). */
#include <assert.h>
#include <stdio.h>

#include "../driver/t40/tx_isp_t40_osd_slot.h"

int main(void)
{
	u32 tbl[300] = { 0 };
	char *base = (char *)tbl;

	/* #158: the counter is at table + 4 * vinum + offset in BYTES.  The lifted "uint32_t *s0; s0 + (vinum << 2)"
	 * form landed 16 bytes per sensor too far (656 + 16 for vinum 1). */
	assert((char *)t40_osd_counter(tbl, 0, 656) == base + 656);
	assert((char *)t40_osd_counter(tbl, 1, 656) == base + 660);
	assert((char *)t40_osd_counter(tbl, 1, 668) == base + 672);
	assert((char *)t40_osd_counter(tbl, 1, 680) == base + 684);
	assert((char *)(tbl + (1 << 2)) - base == 16);
	/* no slot table yet (OSD set before the sensor exists) and a sensor index out of range: no bump */
	assert(t40_osd_counter(NULL, 0, 656) == NULL);
	assert(t40_osd_counter(tbl, 2, 656) == NULL);
	assert(t40_osd_counter(tbl, 0xffffffffu, 656) == NULL);

	/* #141: KSEG0 / KSEG1 pictures (what GetOSDAttr / GetSingleOSDAttr return) become physical */
	assert(t40_osd_phys(0x80123400u) == 0x00123400u);
	assert(t40_osd_phys(0xa0123400u) == 0x00123400u);
	assert(t40_osd_phys(0x00123400u) == 0x00123400u);
	assert(t40_osd_phys(0x07ffff00u) == 0x07ffff00u);
	assert(t40_osd_phys(0xc0000000u) == 0xc0000000u);

	/* #160: origin outside the channel */
	assert(t40_osd_origin_ok(0, 0, 1920, 1080));
	assert(t40_osd_origin_ok(1919, 1079, 1920, 1080));
	assert(!t40_osd_origin_ok(1920, 0, 1920, 1080));
	assert(!t40_osd_origin_ok(0, 1080, 1920, 1080));
	assert(!t40_osd_origin_ok(0, 0, 0, 0));
	puts("tx_isp_t40_osd_slot_test: ok");
	return 0;
}
