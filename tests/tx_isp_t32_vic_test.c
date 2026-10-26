/* Host test: T32 VIC input configuration and CSI/D-PHY values derived from
 * the sensor attributes, against the vendor stack's QEMU trace for the
 * cv8001 (S0 r10, 2-lane RAW10 MIPI 3840x2160 at 1000 Mbps).
 * make -C tests tx_isp_t32_vic_test */
#include <stdio.h>
#include <string.h>
#include "../driver/t32/tx_isp_t32_vic.h"
#include "../driver/t32/tx_isp_t32_msca.h"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL " __VA_ARGS__); printf("\n"); } } while (0)

static void put32(unsigned char *a, u32 off, u32 v) { memcpy(a + off, &v, 4); }

/* VIC0 writes of the vendor at AddSensor after the reset (r10), in order */
static const struct t32_reg_write vendor_vic[] = {
	{ 0x1a4, 0x000a000a }, { 0x100, 0x000004b0 }, { 0x01c, 2 }, { 0x024, 1 },
	{ 0x014, 0x0f000870 }, { 0x10c, 0x0002b000 }, { 0x110, 0x0f000000 },
	{ 0x114, 0 }, { 0x118, 0 }, { 0x11c, 0 }, { 0x200, 0x80000011 }, { 0x204, 0 },
	{ 0x1ac, 0x00020000 }, { 0x1a8, 0x00004440 }, { 0x1b0, 0x10 }, { 0x1a0, 0 },
	{ 0x104, 0 }, { 0x108, 0 },
};

int main(void)
{
	unsigned char attr[T32_ATTR_SIZE];
	struct t32_reg_seq s;
	struct t32_csi_cfg c;
	unsigned int i;

	memset(attr, 0, sizeof attr);
	put32(attr, T32_ATTR_DBUS_TYPE, T32_DBUS_MIPI);
	put32(attr, T32_ATTR_MIPI_CLK, 1000);
	attr[T32_ATTR_MIPI_LANES] = 2;
	put32(attr, T32_ATTR_MIPI_TWIDTH, 3840);
	put32(attr, T32_ATTR_MIPI_THEIGHT, 2160);
	put32(attr, T32_ATTR_SC_DT_VALUE, 0x2b);
	put32(attr, T32_ATTR_SC_CSI_FMT, 1);

	CHECK(!t32_vic_input_seq(attr, 3840, 2160, &s), "cv8001 accepted");
	CHECK(s.n == sizeof vendor_vic / sizeof vendor_vic[0], "count %u", s.n);
	for (i = 0; i < s.n && i < sizeof vendor_vic / sizeof vendor_vic[0]; i++)
		CHECK(s.w[i].off == vendor_vic[i].off && s.w[i].val == vendor_vic[i].val,
		      "write %u: %#x=%#x, vendor %#x=%#x", i, s.w[i].off, s.w[i].val,
		      vendor_vic[i].off, vendor_vic[i].val);

	CHECK(!t32_csi_cfg_from_attr(attr, &c), "csi cfg");
	CHECK(c.lanes_m1 == 1 && c.host_100 == 3 && c.phy_80 == 0x1f && c.phy_lane == 0x3f &&
	      c.settle == 0x19, "csi %u %#x %#x %#x settle %#x (vendor 1 3 1f 3f 19)",
	      c.lanes_m1, c.host_100, c.phy_80, c.phy_lane, c.settle);

	/* other attributes: 4 lanes RAW12 1920 wide, crops, WDR 2 frames */
	attr[T32_ATTR_MIPI_LANES] = 4;
	put32(attr, T32_ATTR_MIPI_CLK, 528);
	put32(attr, T32_ATTR_SC_CSI_FMT, 2);
	put32(attr, T32_ATTR_MIPI_TWIDTH, 1920);
	put32(attr, T32_ATTR_SC_FRAME_MODE, 1);
	attr[T32_ATTR_SC_CROP0X] = 8;
	CHECK(!t32_vic_input_seq(attr, 1920, 1080, &s), "raw12 accepted");
	CHECK(s.w[1].val == (12 * 1920 + 31) / 32 && s.w[3].val == 2, "raw12 line words %u fmt %u",
	      s.w[1].val, s.w[3].val);
	CHECK(s.w[5].val == (0x2b << 12 | 1 << 4) && s.w[6].val == (1920u << 16 | 8),
	      "ctrl %#x / 0x110 %#x", s.w[5].val, s.w[6].val);
	CHECK(s.w[10].off == 0x1ac && s.w[10].val == 0x4410, "wdr2 0x1ac %#x", s.w[10].val);
	CHECK(!t32_csi_cfg_from_attr(attr, &c) && c.host_100 == 0xf && c.lanes_m1 == 3 &&
	      c.settle == 115 * 528 / 4000 - 3, "4 lanes: %#x %u settle %u", c.host_100, c.lanes_m1, c.settle);
	CHECK(t32_csi_settle(80) == 255, "low clk (negative) settle %u", t32_csi_settle(80));
	CHECK(t32_csi_settle(10000) == 255, "settle clamps high");
	put32(attr, T32_ATTR_SC_FRAME_MODE, 3);
	CHECK(t32_vic_input_seq(attr, 1920, 1080, &s) == -EINVAL, "frame mode 3 refused");
	put32(attr, T32_ATTR_DBUS_TYPE, T32_DBUS_DVP);
	CHECK(t32_vic_input_seq(attr, 1920, 1080, &s) == -EOPNOTSUPP, "DVP not supported");
	put32(attr, T32_ATTR_DBUS_TYPE, T32_DBUS_MIPI);
	attr[T32_ATTR_MIPI_INDEX] = 1;
	CHECK(t32_csi_cfg_from_attr(attr, &c) == -EOPNOTSUPP, "CSI1 not supported");

	/* MSCA scaler steps the vendor wrote (S3 runs v1/v2) */
	CHECK(t32_msca_step(3840, 1920) == 0x8000 && t32_msca_step(2160, 1080) == 0x8000, "step /2");
	CHECK(t32_msca_step(3840, 1280) == 0xc000, "step /3 %#x", t32_msca_step(3840, 1280));
	CHECK(t32_msca_step(3840, 640) == 0x18000 && t32_msca_step(2160, 360) == 0x18000, "step /6");
	CHECK(t32_msca_step(2160, 480) == 0x12000, "step V 2160->480 %#x", t32_msca_step(2160, 480));
	CHECK(t32_msca_step(3840, 3840) == 0x4000 && t32_msca_step(100, 0) == 0, "unity / zero");

	printf("%s (%d failures)\n", fails ? "FAIL" : "PASS", fails);
	return fails != 0;
}
