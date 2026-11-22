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
	printf("t23_ivdc_direct_host: ok (%d registers equal to the vendor trace)\n", checked);
	return 0;
}
