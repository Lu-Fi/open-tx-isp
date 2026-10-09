#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_ccm_manual.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static void put(unsigned char *p, int v) { unsigned int u = v; p[0]=u; p[1]=u>>8; p[2]=u>>16; p[3]=u>>24; }
int main(void)
{
	unsigned char w[40]; struct t41_ccm_manual m; short q[9]; int i;
	memset(w, 0, sizeof(w)); w[0] = 1; w[1] = 0;
	for (i = 0; i < 9; ++i) put(w + 4 + i*4, i % 4 == 0 ? 65536 : 0);
	CHECK(!t41_ccm_manual_parse(w, &m) && m.manual == 1 && m.sat == 0);
	t41_ccm_manual_matrix(&m, q);
	for (i = 0; i < 9; ++i) CHECK(q[i] == (i % 4 == 0 ? 1024 : 0));
	/* swap R/B: 1.0 at 2,4,6 ; negative and rounding */
	put(w + 4, -65536); put(w + 8, 32); put(w + 12, 31); put(w + 16, -32); put(w + 20, -33);
	put(w + 24, 1 << 30); put(w + 28, -(1 << 30));
	t41_ccm_manual_parse(w, &m); t41_ccm_manual_matrix(&m, q);
	CHECK(q[0] == -1024);
	CHECK(q[1] == 1);	/* 32/64 = 0.5 rounds up */
	CHECK(q[2] == 0);	/* 31/64 */
	CHECK(q[3] == 0);	/* -32/64 = -0.5 rounds toward +inf */
	CHECK(q[4] == -1);	/* -33/64 */
	CHECK(q[5] == 8191 && q[6] == -8192);	/* clipped */
	w[1] = 2; CHECK(t41_ccm_manual_parse(w, &m) == -1);
	printf(fails ? "ccm_manual FAILED\n" : "ccm_manual ok\n");
	return fails != 0;
}
