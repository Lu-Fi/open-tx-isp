/* Host test of driver/t23/tx_isp_t23_ivdc_direct.h (openimp #767): the IVDC
 * register values for a 1280x720 channel 0 must equal the first writes of
 * the vendor stack in tests/t23/vendor-1.3.0-ivdc-regs.txt (QEMU trace,
 * direct_mode=1 ivdc_mem_line=300, ring at 0x01eb1400), plus the vendor
 * corner cases (wide picture, zero mem line, wrong mode). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t23/tx_isp_t23_ivdc_direct.h"

static unsigned int ref_val(const char *path, unsigned int off, int *found)
{
	char line[256];
	FILE *f = fopen(path, "r");
	unsigned int a, v;

	*found = 0;
	assert(f);
	while (fgets(line, sizeof(line), f))
		if (line[0] == '0' && sscanf(line, "%x %x", &a, &v) == 2 &&
		    a == 0x13200000U + off) {
			*found = 1;
			break;
		}
	fclose(f);
	return v;
}

int main(int argc, char **argv)
{
	const char *ref = argc > 1 ? argv[1] : "t23/vendor-1.3.0-ivdc-regs.txt";
	struct t23_ivdc_reg r[T23_IVDC_MAX_REGS];
	unsigned int thr = 0, line = 300, v;
	int n, i, found, dm = 1, checked = 0;

	/* QEMU reset value of 0x24 has bits 10:8 set: no 0x24 write in the trace */
	n = t23_ivdc_geometry(1280, 720, 0x700, &thr, r);
	assert(thr == 360);
	for (i = 0; i < n; i++) {
		v = ref_val(ref, r[i].off, &found);
		assert(found);
		if (v != r[i].val) {
			printf("geometry 0x%x: 0x%x, vendor 0x%x\n", r[i].off, r[i].val, v);
			return 1;
		}
		checked++;
	}
	n = t23_ivdc_ring(1280, line, 0x01eb1400U, &dm, r);
	assert(n == 5);
	for (i = 0; i < n; i++) {
		v = ref_val(ref, r[i].off, &found);
		assert(found && v == r[i].val);
		checked++;
	}
	assert(ref_val(ref, 0x40, &found) == T23_IVDC_IRQ_MASK && found);
	assert(ref_val(ref, 0x04, &found) == 2 && found);
	assert(ref_val(ref, 0x08, &found) == 0xc && found);
	assert(t23_ivdc_ring_bytes(1280, 720, &line) == 300u * 3u * 1280u / 2u);
	/* vendor corner cases */
	thr = 0;
	n = t23_ivdc_geometry(1280, 720, 0, &thr, r);
	for (i = 0; i < n && r[i].off != 0x24; i++)
		;
	assert(i < n && r[i].val == 0x388);
	line = 0;
	assert(t23_ivdc_ring_bytes(1920, 1080, &line) == 1080u * 3u * 2048u / 8u && line == 270);
	dm = 0;
	assert(t23_ivdc_ring(1280, 300, 0x1000000, &dm, r) == -1);
	dm = 1;
	assert(t23_ivdc_ring(3840, 5000, 0x1000000, &dm, r) == 5 && dm == 2);
	assert(r[0].val == 0x48000u / 3840u && r[2].val == (2500u << 16 | 0xf00u));
	/* openimp #800: re-init after a core start cleared 0x1c (vendor init(1) per enable) */
	assert(!t23_ivdc_needs_init(0, 0, 0));
	assert(!t23_ivdc_needs_init(1, 3, 0xa0000000U));
	assert(!t23_ivdc_needs_init(1, 3, 0xa0000008U));
	assert(t23_ivdc_needs_init(1, 3, 0x00000000U));   /* core start wrote 0 */
	assert(t23_ivdc_needs_init(1, 3, 0x00000008U));   /* core start wrote 8 */
	assert(t23_ivdc_needs_init(1, 2, 0xa0000000U));   /* after stream off */
	assert(t23_ivdc_needs_init(1, 0, 0));
	assert(t23_ivdc_tag_unsafe(1, 0) && t23_ivdc_tag_unsafe(1, 0x20000000U));
	assert(!t23_ivdc_tag_unsafe(1, 0xa0000000U) && !t23_ivdc_tag_unsafe(0, 0));
	/* the alias the tags hit on a 32 MB part (mem=24M + rmem=8M) */
	assert((T23_IVDC_Y_ADDR & ((32u << 20) - 1u)) == 0u && (T23_IVDC_UV_ADDR & ((32u << 20) - 1u)) == 16u << 20);
	printf("t23_ivdc_direct_host: ok (%d registers equal to the vendor trace)\n", checked);
	return 0;
}
