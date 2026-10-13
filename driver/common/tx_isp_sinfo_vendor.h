/*
 * Vendor-compatible sensor info lookup for the shared sensor registry.
 *
 * The vendor stack lets every sensor module publish its `struct sensor_info`
 * under /proc/jz/sensor (flat files plus a <sensor name>/ directory).  On the
 * open stack the ISP owns /proc/jz/sensor and the prebuilt sensor modules
 * hand it only an I2C driver and an opaque subdev.  The static sensor_info
 * object (name, chip id, version, fps range, native size, wiring) is still
 * part of every such module's data, so it is located by a strict structural
 * match inside the module image.  Nothing is cached and nothing is written.
 *
 * This header is freestanding (no kernel calls) so it can be host tested.
 */
#ifndef TX_ISP_SINFO_VENDOR_H
#define TX_ISP_SINFO_VENDOR_H

#define TX_ISP_SINFO_VSTR_MAX 48U

struct tx_isp_sinfo_vinfo {
	const char *name;
	const char *version;
	unsigned int chip_id;
	int min_fps;
	int max_fps;
	unsigned int i2c_addr;
	int width;
	int height;
	/* Set when the matched layout carries the board wiring words. */
	int have_wiring;
	int rst_gpio;
	int pwdn_gpio;
	int boot;
	int mclk;
	int video_interface;
	int i2c_adapter;
};

/* Return the length of a printable NUL terminated string inside the image. */
static inline unsigned int
tx_isp_sinfo_vendor_str_len(const unsigned char *base, unsigned long size,
			    unsigned long addr)
{
	unsigned long off;
	unsigned int n;

	if (addr < (unsigned long)base || addr >= (unsigned long)base + size)
		return 0;
	off = addr - (unsigned long)base;
	for (n = 0; off + n < size && n < TX_ISP_SINFO_VSTR_MAX; ++n) {
		unsigned char c = base[off + n];

		if (!c)
			return n;
		if (c < 0x20 || c > 0x7e)
			return 0;
	}
	return 0;
}

static inline int tx_isp_sinfo_vendor_streq(const char *a, const char *b)
{
	while (*a && *a == *b) {
		++a;
		++b;
	}
	return *a == *b;
}

static inline int tx_isp_sinfo_vendor_dim_ok(unsigned int v)
{
	return v >= 16U && v <= 16384U;
}

/*
 * Find the sensor_info object of the module image [base, base + size).
 * Vendor layout (words): name, chip_id, version, min_fps, max_fps,
 * i2c_addr, width, height, rst_gpio, pwdn_gpio, boot, mclk,
 * video_interface, i2c_adapter, priv.  An older layout has an extra
 * actual_fps word before i2c_addr and no wiring words.
 *
 * @drvname must be the I2C driver name (the sensor name matches it); a
 * non-zero @addr must equal the object's i2c_addr.  Returns 1 and fills
 * @out on a match, 0 otherwise.
 */
static inline int
tx_isp_sinfo_vendor_scan(const void *image, unsigned long size,
			 const char *drvname, unsigned int addr,
			 struct tx_isp_sinfo_vinfo *out)
{
	const unsigned char *base = image;
	const unsigned int *w = image;
	unsigned long nwords = size / 4U;
	unsigned long i;

	if (!base || !drvname || !drvname[0] || nwords < 9U)
		return 0;
	if ((unsigned long)base & 3UL)
		return 0;

	for (i = 0; i + 8U <= nwords; ++i) {
		const char *name = (const char *)(unsigned long)w[i];
		unsigned int off;
		unsigned int min_fps = w[i + 3U];
		unsigned int max_fps = w[i + 4U];

		if (!tx_isp_sinfo_vendor_str_len(base, size, w[i]) ||
		    !tx_isp_sinfo_vendor_str_len(base, size, w[i + 2U]))
			continue;
		if (!tx_isp_sinfo_vendor_streq(name, drvname))
			continue;
		if (!max_fps || max_fps > 1000U || min_fps > max_fps)
			continue;

		/* off: index of the i2c_addr word (5 current, 6 older). */
		if (tx_isp_sinfo_vendor_dim_ok(w[i + 6U]) &&
		    tx_isp_sinfo_vendor_dim_ok(w[i + 7U]) &&
		    w[i + 5U] && w[i + 5U] < 0x80U &&
		    (!addr || w[i + 5U] == addr))
			off = 5U;
		else if (i + 9U <= nwords &&
			 tx_isp_sinfo_vendor_dim_ok(w[i + 7U]) &&
			 tx_isp_sinfo_vendor_dim_ok(w[i + 8U]) &&
			 w[i + 6U] && w[i + 6U] < 0x80U &&
			 (!addr || w[i + 6U] == addr))
			off = 6U;
		else
			continue;

		out->name = name;
		out->version = (const char *)(unsigned long)w[i + 2U];
		out->chip_id = w[i + 1U];
		out->min_fps = (int)min_fps;
		out->max_fps = (int)max_fps;
		out->i2c_addr = w[i + off];
		out->width = (int)w[i + off + 1U];
		out->height = (int)w[i + off + 2U];
		out->have_wiring = 0;
		out->rst_gpio = out->pwdn_gpio = out->boot = 0;
		out->mclk = out->video_interface = out->i2c_adapter = 0;
		if (off == 5U && i + 14U <= nwords) {
			out->have_wiring = 1;
			out->rst_gpio = (int)w[i + 8U];
			out->pwdn_gpio = (int)w[i + 9U];
			out->boot = (int)w[i + 10U];
			out->mclk = (int)w[i + 11U];
			out->video_interface = (int)w[i + 12U];
			out->i2c_adapter = (int)w[i + 13U];
		}
		return 1;
	}
	return 0;
}

#endif /* TX_ISP_SINFO_VENDOR_H */
