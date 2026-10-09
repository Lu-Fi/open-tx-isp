#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_csc_ctl.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
static void put(unsigned char *p, int v) { unsigned int u = v; p[0]=u; p[1]=u>>8; p[2]=u>>16; p[3]=u>>24; }
int main(void)
{
	unsigned char w[92]; int v;
	memset(w, 0, sizeof(w));
	for (v = 0; v < 6; ++v) { put(w, v); CHECK(t41_csc_ctl_check(w) == v); }
	put(w, 7); CHECK(t41_csc_ctl_check(w) == -1);
	put(w, -1); CHECK(t41_csc_ctl_check(w) == -1);
	/* user: BT709-like */
	put(w, 6); put(w + 4, 13933); put(w + 8, 46871); put(w + 12, 4732);
	w[42] = 0; w[43] = 255; w[44] = 0; w[45] = 255;
	CHECK(t41_csc_ctl_check(w) == 6);
	put(w + 4, 262145); CHECK(t41_csc_ctl_check(w) == -1);
	put(w + 4, 13933); put(w + 48, -262145); CHECK(t41_csc_ctl_check(w) == -1);
	put(w + 48, 65536); w[42] = 200; w[43] = 100; CHECK(t41_csc_ctl_check(w) == -1);
	w[42] = 16; w[43] = 235; w[44] = 240; w[45] = 16; CHECK(t41_csc_ctl_check(w) == -1);
	printf(fails ? "csc_ctl FAILED\n" : "csc_ctl ok\n");
	return fails != 0;
}
