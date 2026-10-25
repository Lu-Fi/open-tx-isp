/* T41 night NR/sharpen host check: runs the driver's MDNS/SDNS/YSP interpolation and packing
 * (driver/t41/tx_isp_t41_{mdns,sdns,ysp}.h) on a tuning bin bank and compares with ISP core dumps.
 * usage: t <bin> <bank 0=day|1=night> <gain q16 log2> [dump.regs ...]
 *        t <bin> <bank> scan <dump.regs>   (per block: gain ranges, step 256, that reproduce the dump)
 * dump.regs: one 32-bit hex word per line, word n = ISP core offset 4n. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../../driver/t41/tx_isp_t41_mdns.h"
#include "../../../driver/t41/tx_isp_t41_sdns.h"
#include "../../../driver/t41/tx_isp_t41_ysp.h"
#include "../../../driver/t41/tx_isp_t41_dmsc.h"

/* bank offsets as tisp_params_copy places them (linear, non-WDR): bank -> tparamsP */
#define BANK_MDNS 0x17188u /* -> tparamsP+0x13f00, 0x61a */
#define BANK_SDNS 0x1e140u /* -> tparamsP+0x17844, 0x384 */
#define BANK_YSP  0x1cc74u /* -> tparamsP+0x16dcc, 0xa78 */
#define BANK_DMSC 0x14768u /* -> tparamsP+0x12788, 0x1076 */
#define WIDTH 2880u
#define HEIGHT 1620u

static unsigned char *bin; static long binsz;
static unsigned int *dumps[4]; static unsigned int ndump[4]; static int nd;

static void load_dump(const char *path)
{
	FILE *f = fopen(path, "r"); char line[64]; unsigned int cap = 0x20000 / 4;
	if (!f) { perror(path); exit(2); }
	dumps[nd] = calloc(cap, 4);
	while (fgets(line, sizeof(line), f) && ndump[nd] < cap)
		dumps[nd][ndump[nd]++] = (unsigned int)strtoul(line, NULL, 16);
	fclose(f); nd++;
}

static int mism, total, quiet;
static void show(const char *blk, const struct t41_dpc_word *w, int n, unsigned int lo, unsigned int hi)
{
	int i, d;
	if (quiet) {
		for (i = 0; i < n; ++i)
			if (w[i].address >= lo && w[i].address <= hi && w[i].address / 4 < ndump[0]) {
				total++;
				if (dumps[0][w[i].address / 4] != w[i].value) mism++;
			}
		return;
	}
	for (i = 0; i < n; ++i) {
		unsigned int a = w[i].address;
		if (a < lo || a > hi) continue;
		printf("%s %05x calc=%08x", blk, a, w[i].value);
		for (d = 0; d < nd; ++d) {
			unsigned int v = a / 4 < ndump[d] ? dumps[d][a / 4] : 0xdeadbeef;
			printf("  d%d=%08x%s", d, v, v == w[i].value ? "" : " *");
			if (d == 0) { total++; if (v != w[i].value) mism++; }
		}
		printf("\n");
	}
}

static int run(const unsigned char *bank, unsigned int gain, unsigned int blocks)
{
	unsigned char dm[T41_DMSC_STATE_BYTES], ms[T41_MDNS_STATE_BYTES], ss[T41_SDNS_STATE_BYTES], ys[T41_YSP_STATE_BYTES];
	struct t41_dpc_word w[256]; int n;
	memset(ms, 0, sizeof(ms)); memset(ss, 0, sizeof(ss)); memset(ys, 0, sizeof(ys)); memset(dm, 0, sizeof(dm));
	if (blocks & 1) {
		if (t41_mdns_interpolate(bank + BANK_MDNS, T41_MDNS_PARAM_BYTES, ms, sizeof(ms), gain)) return -1;
		n = t41_mdns_pack(bank + BANK_MDNS, T41_MDNS_PARAM_BYTES, ms, sizeof(ms), 0, WIDTH, HEIGHT, w, 256);
		if (n < 0) return -1;
		show("MDNS", w, n, 0xf100, 0xf2b0);
	}
	if (blocks & 2) {
		if (t41_sdns_interpolate(bank + BANK_SDNS, T41_SDNS_PARAM_BYTES, ss, sizeof(ss), gain, 128)) return -1;
		n = t41_sdns_pack_dynamic(bank + BANK_SDNS, T41_SDNS_PARAM_BYTES, ss, sizeof(ss), 0, w, 256);
		if (n < 0) return -1;
		show("SDNS", w, n, 0x14000, 0x143ff);
	}
	if (blocks & 4) {
		if (t41_ysp_interpolate(bank + BANK_YSP, T41_YSP_PARAM_BYTES, ys, sizeof(ys), gain)) return -1;
		n = t41_ysp_pack_dynamic(bank + BANK_YSP, T41_YSP_PARAM_BYTES, ys, sizeof(ys), w, 256);
		if (n < 0) return -1;
		show("YSP ", w, n, 0x13000, 0x131ff);
	}
	if (blocks & 8) {
		if (t41_dmsc_interpolate(bank + BANK_DMSC, T41_DMSC_PARAM_BYTES, dm, sizeof(dm), gain, 128)) return -1;
		n = t41_dmsc_pack_dynamic(bank + BANK_DMSC, T41_DMSC_PARAM_BYTES, dm, sizeof(dm), w, 256);
		if (n < 0) return -1;
		show("DMSC", w, n, 0xa000, 0xa2ff);
	}
	return 0;
}

static void scan(const unsigned char *bank)
{
	static const char *names[] = {"MDNS", "SDNS", "YSP", "DMSC"};
	unsigned int b, g, start = 0; int in;
	quiet = 1;
	for (b = 0; b < 4; ++b) {
		printf("%s:", names[b]); in = 0;
		for (g = 0; g <= (10U << 16) + 256; g += 256) {
			int ok = 0;
			if (g <= (10U << 16)) { mism = total = 0; ok = !run(bank, g, 1U << b) && total && !mism; }
			if (ok && !in) { start = g; in = 1; }
			if (!ok && in) { printf(" %u-%u", start, g - 256); in = 0; }
		}
		printf("\n");
	}
}

int main(int argc, char **argv)
{
	FILE *f; unsigned char *bank;
	unsigned int gain; int i, mode;
	if (argc < 4) { fprintf(stderr, "usage: %s bin bank gain [dumps]\n", argv[0]); return 2; }
	f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
	fseek(f, 0, SEEK_END); binsz = ftell(f); rewind(f); bin = malloc(binsz);
	if (fread(bin, 1, binsz, f) != (size_t)binsz) return 2;
	fclose(f);
	mode = atoi(argv[2]); gain = (unsigned int)strtoul(argv[3], NULL, 0);
	for (i = 4; i < argc && nd < 4; ++i) load_dump(argv[i]);
	bank = bin + 0x40 + (mode ? t41_tmo_le32(bin + 0x34) : 0);
	if (!strcmp(argv[3], "scan")) {
		if (nd != 1) { fprintf(stderr, "scan needs one dump\n"); return 2; }
		scan(bank);
		return 0;
	}
	printf("bank %s at bin+0x%lx gain %u (%u + %u/65536)\n", mode ? "night" : "day",
	       (long)(bank - bin), gain, gain >> 16, gain & 0xffff);
	if (run(bank, gain, 15)) return 3;
	if (nd) printf("vs d0: %d/%d words differ\n", mism, total);
	return 0;
}
