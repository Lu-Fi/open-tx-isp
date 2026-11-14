/* tx_isp_t33_ldc_test: online LDC values of driver/t32/tx_isp_t33_ldc.h against a vendor
 * tx-isp-t33.ko 2.0.2.1 QEMU run (t33l cv2005 1920x1080, LDC 16x16, LUT pa 0x025a0000 + 0x80,
 * IPC base 0x03398000; audit issue491 vendor_online_ldc_ref.txt, openimp #491). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../driver/t32/tx_isp_t33_ldc.h"

static const struct t32_front_write ipc_ref[] = {
	{ 0x01094, 0x03398000 }, { 0x01098, 0x00200000 }, { 0x010c4, 0 }, { 0x010c8, 0 },
	{ 0x2704c, 0x03398000 }, { 0x27050, 0x780 }, { 0x27054, 0x03513b00 }, { 0x27058, 0x780 },
	{ 0x2705c, 0x03495200 }, { 0x27098, 0x010e021c }, { 0x2709c, 0x00400080 }, { 0x27060, 0x780 },
	{ 0x27064, 0x0354fb00 }, { 0x27068, 0x780 }, { 0x20080, 1 }, { 0x20088, 0x010e021c },
	{ 0x20090, 0x03495200 }, { 0x20054, 0x00040034 }, { 0x20060, 0x07800780 }, { 0x2008c, 0x03398000 },
	{ 0x21070, 1 }, { 0x21064, 0x100 }, { 0x21078, 0x00400080 }, { 0x21060, 0x35 }, { 0x21048, 4 },
	{ 0x21054, 0x07800780 }, { 0x2107c, 0x03513b00 }, { 0x21080, 0x0354fb00 },
};
static const struct t32_front_write list_ref[] = {
	{ 0x27044, 0x04380780 }, { 0x27048, 0x04380780 }, { 0x27040, 0 }, { 0x2706c, 0x025a0080 },
	{ 0x27070, 0x1e8 }, { 0x27074, 1 }, { 0x21084, 0x04380780 }, { 0x27020, 0xffffffff }, { 0x27024, 1 },
};

static int cmp(const char *what, const struct t32_front_write *a, unsigned int n,
	       const struct t32_front_write *r, unsigned int nr)
{
	unsigned int i, bad = 0;

	if (n != nr) {
		printf("%s: %u writes, vendor %u\n", what, n, nr);
		return 1;
	}
	for (i = 0; i < n; i++)
		if (a[i].reg != r[i].reg || a[i].val != r[i].val) {
			printf("%s %u: 0x%05x <- 0x%08x, vendor 0x%05x <- 0x%08x\n", what, i, a[i].reg, a[i].val,
			       r[i].reg, r[i].val);
			bad++;
		}
	return bad != 0;
}

int main(void)
{
	const uint32_t blk[12] = { 0, 4, 0x10, 1, 0, 0x04380780, 2, 0x025a0000, 0x80 };
	struct t33_ldc_attr a;
	struct t32_front_write w[T33_LDC_IPC_WRITES];
	uint32_t area, total;
	unsigned int n;
	int fail = 0;

	memset(&a, 0, sizeof(a));
	if (t33_ldc_attr_from_block(blk, &a)) {
		printf("FAIL attr\n");
		return 1;
	}
	n = t33_ldc_list(&a, w);
	fail |= cmp("list25", w, n, list_ref, sizeof(list_ref) / sizeof(list_ref[0]));
	total = t33_ldc_mem_size(&a, 1920, 1080, &area);
	if (total != 1923840 || area != 0x17bb00) {
		printf("mem size %u / area 0x%x, vendor 1923840 / 0x17bb00\n", total, area);
		fail = 1;
	}
	n = t33_ldc_ipc_writes(&a, 0x03398000, 1920, 1080, w);
	fail |= cmp("ipc", w, n, ipc_ref, sizeof(ipc_ref) / sizeof(ipc_ref[0]));
	printf("tx_isp_t33_ldc_test: %s\n", fail ? "FAIL" : "ok (list 25, IPC size, 28 IPC/DMA writes = vendor)");
	return fail;
}
