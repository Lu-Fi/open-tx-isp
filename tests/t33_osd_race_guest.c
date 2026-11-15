// SPDX-License-Identifier: GPL-2.0
/*
 * Host race test of the T33 MSCA attribute block (driver/t33/tx_isp_t33_osd.c,
 * openimp #578): one thread is the stream-on (tx_isp_t32_hw.c t33_tisp_init_tone:
 * t33_osd_lock, t33_msca_attr_stream_on with a new input size, t33_osd_list_rebuilt,
 * t33_osd_unlock), the other the tuning ids (OSD block / mask set + get, hv flip)
 * in a loop.  Built with -fsanitize=address: before the fix the stream-on kfree()s
 * stMscaAttr[0] under the tuning call's pointer (heap-use-after-free); with the
 * osd mutex the run is clean.  T33_OSD_RACE_OLD builds against the old osd.c
 * (no t33_osd_lock): the before run.
 */
#define __KERNEL__
#include <pthread.h>
#include <stdlib.h>
#include "../driver/t33/tx_isp_t33_osd.c"

#ifndef T33_OSD_RACE_LOOPS
#define T33_OSD_RACE_LOOPS 4000
#endif

u32 t33_st_isp_init_par_mode[1008];
static u32 list23[0x800];
static volatile u32 in_w = 1920, in_h = 1080;
static volatile int stop;

int isp_printf(unsigned int level, const char *fmt, ...) { (void)level; (void)fmt; return 0; }
int printk(const char *fmt, ...) { (void)fmt; return 0; }
int system_reg_write(u32 a, u32 v) { (void)a; (void)v; return 0; }
int tisp_reg_write_t33(u32 ch, u32 mod, u32 reg, u32 val)
{
	(void)ch; (void)mod;
	list23[reg & 0x7ff] = val;
	return 0;
}
u32 tisp_reg_read_t33(u32 ch, u32 mod, u32 reg)
{
	(void)ch; (void)mod;
	return list23[reg & 0x7ff];
}
void t33_msca_list_refresh(void) { }
void t32_core_in_size(u32 *w, u32 *h) { *w = in_w; *h = in_h; }
int t32_core_clk_active(void) { return 1; }
int t32_core_iq_bin(const u8 **bin, u32 *bytes) { (void)bin; (void)bytes; return -ENOENT; }

static void *stream_on_loop(void *arg)
{
	int i;

	(void)arg;
	for (i = 0; i < T33_OSD_RACE_LOOPS; i++) {
		u32 w = (i & 1) ? 1280 : 1920, h = (i & 1) ? 720 : 1080;

		in_w = w;
		in_h = h;
#ifndef T33_OSD_RACE_OLD
		t33_osd_lock();
#endif
		t33_msca_attr_stream_on(w, h);
		t33_osd_list_rebuilt();
#ifndef T33_OSD_RACE_OLD
		t33_osd_unlock();
#endif
	}
	stop = 1;
	return NULL;
}

static void *tuning_loop(void *arg)
{
	u8 b[36], g[36], f;
	unsigned long n = 0;

	(void)arg;
	while (!stop) {
		memset(b, 0, sizeof(b));
		b[0] = n & 3;	/* OSD window */
		b[1] = 1;	/* enable */
		t33_osd_p16(b + 2, 100); t33_osd_p16(b + 4, 50); t33_osd_p16(b + 6, 200); t33_osd_p16(b + 8, 100);
		t32_tx_os_tisp_s_osd_block_attr(0, b);
		t32_tx_os_tisp_g_osd_block_attr(0, g);
		memset(b, 0, sizeof(b));
		b[0] = 0; b[1] = n & 3; b[2] = 1;	/* mask channel 0, window, enable */
		t33_osd_p16(b + 4, 64); t33_osd_p16(b + 6, 64); t33_osd_p16(b + 8, 128); t33_osd_p16(b + 10, 128);
		t32_tx_os_tisp_s_mscaler_mask_block_attr(0, b);
		f = (u8)(n & 3);
		t33_msca_hv_flip_set(0, &f);
		t33_msca_hv_flip_get(0, &f);
		n++;
	}
	printf("tuning calls %lu\n", n);
	return NULL;
}

int main(void)
{
	pthread_t a, b;

	t33_st_isp_init_par_mode[226] = 0;
	pthread_create(&b, NULL, tuning_loop, NULL);
	pthread_create(&a, NULL, stream_on_loop, NULL);
	pthread_join(a, NULL);
	pthread_join(b, NULL);
	t33_msca_attr_exit();
	if (shim_live) {
		printf("t33_osd_race: FAIL, %ld attribute blocks leaked (double init)\n", shim_live);
		return 1;
	}
	printf("t33_osd_race: %d stream-ons with a size change, PASS\n", T33_OSD_RACE_LOOPS);
	return 0;
}
