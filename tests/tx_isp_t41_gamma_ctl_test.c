#include <stdio.h>
#include <string.h>
#include "../driver/t41/tx_isp_t41_gamma_ctl.h"
static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
int main(void)
{
	unsigned char info[T41_GAMMA_INFO_BYTES], params[0x240], attr[264], out[264];
	unsigned char t0[258], t1[258], t2[258];
	const unsigned char *tab[3] = { t0, t1, t2 };
	unsigned int i;
	memset(t0, 0x11, 258); memset(t1, 0x22, 258); memset(t2, 0x33, 258);
	memset(info, 0xaa, sizeof(info)); memset(params, 0xbb, sizeof(params));
	memset(attr, 0, sizeof(attr));
	/* type 2 (Rec709) */
	attr[0] = 2;
	CHECK(!t41_gamma_ctl_check(attr));
	CHECK(t41_gamma_ctl_store(info, params, attr, tab) == 1);
	CHECK(info[0x420] == 1 && info[0x21c] == 0x22 && info[0x21c + 257] == 0x22 && info[0x21c + 258] == 0xaa);
	CHECK(params[0x12c] == 0x22 && params[0x12c + 257] == 0x22 && params[0x12c + 258] == 255);
	for (i = 0; i < 10; ++i) CHECK(params[0x22e + i] == 255);
	CHECK(params[0x22e + 10] == 0xbb);
	CHECK(info[0x218] == 255 && info[0x219] == 0);
	/* user curve */
	for (i = 0; i < 129; ++i) { attr[4 + 2*i] = i; attr[5 + 2*i] = 1; }
	attr[0] = 4;
	CHECK(!t41_gamma_ctl_check(attr));
	CHECK(t41_gamma_ctl_store(info, params, attr, tab) == 1);
	CHECK(info[0x21c] == 0 && info[0x21d] == 1 && info[0x21e] == 1);
	attr[5] = 0x20; CHECK(t41_gamma_ctl_check(attr) == -1);
	attr[0] = 5; CHECK(t41_gamma_ctl_check(attr) == -1);
	/* default releases the flag */
	attr[0] = 0; attr[5] = 1;
	CHECK(t41_gamma_ctl_store(info, params, attr, tab) == 0 && info[0x420] == 0);
	/* get returns stored type + live curve */
	info[0x21c] = 0x77;
	t41_gamma_ctl_load(info, out);
	CHECK(out[0] == 0 && out[4] == 0x77 && out[261] == info[0x21c + 257]);
	printf(fails ? "gamma_ctl FAILED\n" : "gamma_ctl ok\n");
	return fails != 0;
}
