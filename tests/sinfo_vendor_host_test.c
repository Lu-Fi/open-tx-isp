/* Host test: locating the sensor module's sensor_info (tx_isp_sinfo_vendor.h).
 * Builds module-like images (strings + the vendor sensor_info layouts, with
 * 32-bit pointer words) and checks the match, the values and the rejections.
 * Needs a 64-bit host that can map below 4 GiB (MAP_32BIT). */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include "../driver/common/tx_isp_sinfo_vendor.h"

#define IMG 4096
static uint32_t *img;

static uint32_t addr_of(void *p) { return (uint32_t)(uintptr_t)p; }

static int check(int ok, const char *what)
{
	if (!ok)
		printf("FAIL: %s\n", what);
	return !ok;
}

int main(void)
{
	struct tx_isp_sinfo_vinfo v;
	char *str;
	int bad = 0;
	uint32_t *o;

	img = mmap(NULL, IMG, PROT_READ | PROT_WRITE,
		   MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
	if (img == MAP_FAILED) {
		puts("SKIP: no 32-bit mapping");
		return 0;
	}
	memset(img, 0, IMG);
	str = (char *)img + 2048;
	strcpy(str, "jxf23");
	strcpy(str + 16, "H20181031a");

	/* Current vendor layout at word 40 (15 words, wiring included). */
	o = img + 40;
	o[0] = addr_of(str);
	o[1] = 0xf23;
	o[2] = addr_of(str + 16);
	o[3] = 5;
	o[4] = 30;
	o[5] = 0x40;
	o[6] = 1920;
	o[7] = 1080;
	o[8] = 7; o[9] = 8; o[10] = 1; o[11] = 2; o[12] = 1; o[13] = 3;

	bad += check(tx_isp_sinfo_vendor_scan(img, IMG, "jxf23", 0x40, &v), "match");
	bad += check(!strcmp(v.name, "jxf23") && !strcmp(v.version, "H20181031a"), "strings");
	bad += check(v.chip_id == 0xf23 && v.min_fps == 5 && v.max_fps == 30, "chip/fps");
	bad += check(v.i2c_addr == 0x40 && v.width == 1920 && v.height == 1080, "addr/size");
	bad += check(v.have_wiring && v.rst_gpio == 7 && v.pwdn_gpio == 8 &&
		     v.boot == 1 && v.mclk == 2 && v.video_interface == 1 &&
		     v.i2c_adapter == 3, "wiring");
	bad += check(tx_isp_sinfo_vendor_scan(img, IMG, "jxf23", 0, &v) &&
		     v.max_fps == 30, "addr unknown matches");
	bad += check(!tx_isp_sinfo_vendor_scan(img, IMG, "jxf23", 0x30, &v), "addr mismatch rejected");
	bad += check(!tx_isp_sinfo_vendor_scan(img, IMG, "sc2336", 0x40, &v), "name mismatch rejected");
	bad += check(!tx_isp_sinfo_vendor_scan(img, IMG, "", 0x40, &v), "empty name rejected");

	/* Older layout: actual_fps word before i2c_addr, no wiring words. */
	memset(o, 0, 64);
	o[0] = addr_of(str);
	o[1] = 0x9b3a;
	o[2] = addr_of(str + 16);
	o[3] = 5;
	o[4] = 25;
	o[5] = 25;		/* actual_fps */
	o[6] = 0x30;
	o[7] = 1280;
	o[8] = 720;
	bad += check(tx_isp_sinfo_vendor_scan(img, IMG, "jxf23", 0x30, &v), "old layout match");
	bad += check(v.max_fps == 25 && v.i2c_addr == 0x30 && v.width == 1280 &&
		     v.height == 720 && !v.have_wiring, "old layout values");

	/* Rejections: bad fps range, pointer outside the image, bad size. */
	o[5] = 25; o[3] = 40;	/* min > max */
	bad += check(!tx_isp_sinfo_vendor_scan(img, IMG, "jxf23", 0x30, &v), "min>max rejected");
	o[3] = 5; o[0] = addr_of(str) + 4096;
	bad += check(!tx_isp_sinfo_vendor_scan(img, IMG, "jxf23", 0x30, &v), "outside pointer rejected");
	o[0] = addr_of(str); o[7] = 4;
	bad += check(!tx_isp_sinfo_vendor_scan(img, IMG, "jxf23", 0x30, &v), "tiny width rejected");
	bad += check(!tx_isp_sinfo_vendor_scan(NULL, 0, "jxf23", 0, &v), "null image");

	/* Non printable string must not match. */
	o[7] = 1280; str[1] = 1;
	bad += check(!tx_isp_sinfo_vendor_scan(img, IMG, "jxf23", 0x30, &v), "non printable rejected");

	puts(bad ? "FAILED" : "OK sinfo_vendor_host_test");
	return bad ? 1 : 0;
}
