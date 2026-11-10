// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the ISP core frame-drop / event helpers of
 * driver/t32/tx_isp_t32_corefd.h against Matt Davis' recovered vendor C
 * (ref/, PR #15 @ 2e2e06bc): __ispcore_get_frame_drop,
 * __ispcore_single_cam_check_frame_drop, ispcoer_debug_info_fs_trige,
 * pm_core_event_process_thread, tisp_process_deinit.
 *
 * Both sides run on the same 2 KiB core image (restored between the runs);
 * compared: every byte of it, the calls in order (isp_printf with format,
 * %s and all argument words incl. the stack ones, the frame-drop attribute
 * words handed to tisp_set_frame_drop, completions, sleeps), return values.
 * Inputs: random masks (full / one bit missing / random), positions around
 * the pattern length, enable 0 / 1 / other, channel numbers incl. negative,
 * debug counters with bit 31 set / clear around the period, scripted event
 * results (the thread stops after 0..5 rounds, the drain after 1..120 polls).
 * Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_corefd.h"

u64 __ispcore_get_frame_drop(u32, u32, u32, u32);
u64 __ispcore_single_cam_check_frame_drop(u32, u32, u32, u32);
u64 ispcoer_debug_info_fs_trige(u32, u32, u32, u32);
u64 pm_core_event_process_thread(u32, u32, u32, u32);
u64 tisp_process_deinit(u32, u32, u32, u32);

#define CALL(f, a, b) t32o_call((t32o_fn)(void (*)(void))(f), a, b, 0, 0, 0)
enum { F_PRINTF = 1, F_GETFD, F_SETFD, F_COMPLETE, F_STOP, F_PROC, F_EXIT, F_EMPTY, F_SLEEP };

char t32_data_c36f0[] = "__ispcore_get_frame_drop";	/* data.c strings */
char t32_data_cc808[] = "[%s %d] chn:%d fd_size:%d fd_mark:0x%x drop frame\n";
char t32_data_c368c[] = "pm_core_event_process_thread";
char t32_data_cc948[] = "[%s][%d] process error\n";
char t32_data_bf250[] = "tisp_process_deinit";
char t32_data_c7fd0[] = "[[ %s:%d ]] tisp_event_exit error %d\n";

static struct t32o8_log *cur;
static u32 fd_words[T32_FD_ATTR_WORDS], n_stop, n_calls, r_proc_mask, r_exit, n_drain, r_empty;

u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
	u32 w[5] = { a2, a3, a4, a5, a6 };

	t32o8_rec_fmt(cur, F_PRINTF, a0, T32O_HOST(a1), w, 5);
	return 0;
}
static void getfd(u32 a0, u32 a1, u32 *attr)
{
	t32o8_rec(cur, F_GETFD, a0, a1, 0, 0);
	memcpy(attr, fd_words, sizeof(fd_words));
}
static void setfd(u32 a0, u32 a1, const u32 *attr)
{
	struct t32o8_call *c = t32o8_rec(cur, F_SETFD, a0, a1, 0, 0);

	memcpy(c->data, attr, sizeof(fd_words));
}
u64 tisp_get_frame_drop(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; getfd(a0, a1, T32O_HOST(a2)); return 0; }
u64 tisp_set_frame_drop(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; setfd(a0, a1, T32O_HOST(a2)); return 0; }
static u8 *core;
static u32 do_stop(void) { t32o8_rec(cur, F_STOP, 0, 0, 0, 0); return n_calls++ >= n_stop; }
static u32 do_proc(u32 a) { t32o8_rec(cur, F_PROC, a, 0, 0, 0); return (r_proc_mask >> (n_calls & 31)) & 1 ? n_calls * 7 : 0; }
static u32 do_empty(u32 ch) { t32o8_rec(cur, F_EMPTY, ch, 0, 0, 0); return ++n_calls >= n_drain ? r_empty : r_empty & 0xff00; }
u64 private_kthread_should_stop(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; return do_stop(); }
u64 isp_core_event_process(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return do_proc(a0); }
u64 tisp_event_exit(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a0; (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_EXIT, 0, 0, 0, 0); return r_exit; }
u64 tisp_event_work_empty(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; return do_empty(a0); }
u64 private_msleep(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_SLEEP, a0, 0, 0, 0); return 0; }

/* ours */
static int o_printf(int level, const char *fmt, ...)
{
	u32 w[6];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 6);
	va_end(ap);
	t32o8_rec_fmt(cur, F_PRINTF, (u32)level, fmt, w, n);
	return 0;
}
static void o_getfd(u32 s, u32 c, u32 *attr) { getfd(s, c, attr); }
static void o_setfd(u32 s, u32 c, u32 *attr) { setfd(s, c, attr); }
static void o_complete(void *d) { t32o8_rec(cur, F_COMPLETE, (u32)((u8 *)d - core), 0, 0, 0); }
static int o_stop(void) { return (int)do_stop(); }
static int o_proc(u32 a) { return (int)do_proc(a); }
static int o_exit(void) { t32o8_rec(cur, F_EXIT, 0, 0, 0, 0); return (int)r_exit; }
static int o_empty(u32 ch) { return (int)do_empty(ch); }
static void o_sleep(u32 ms) { t32o8_rec(cur, F_SLEEP, ms, 0, 0, 0); }
static const struct t32_corefd_ops ops = { o_printf, o_getfd, o_setfd, o_complete, o_stop, o_proc, o_exit, o_empty,
					   o_sleep };

static u32 rs = 0xc0ef0d00u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, k, failed = 0, compared = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	static u8 init[2048], vend[2048];

	t32o_reset();
	core = t32o_alloc(2048);
	for (c = 0; c < n; c++) {
		u32 which = c % 5, vr = 0, orr = 0, ch = rnd() % 4, len;
		int bad;

		for (k = 0; k < 2048; k++)
			core[k] = (u8)rnd();
		len = (rnd() & 1) ? rnd() % 32 : rnd();
		*t32_core_w(core, 264) = len;
		*t32_core_w(core, 272) = (rnd() & 1) ? rnd() % 34 : rnd();
		switch (rnd() % 3) {
		case 0: *t32_core_w(core, 268) = 0xffffffffu; break;
		case 1: *t32_core_w(core, 268) = ~(1u << (*t32_core_w(core, 272) & 31)); break;
		default: break;
		}
		*t32_core_w(core, 256) = (rnd() % 3) ? rnd() & 1 : rnd();
		*t32_core_w(core, 260) = (rnd() & 1) ? 0 : rnd();
		*t32_core_w(core, 116) = (rnd() & 3) ? rnd() % 12 : rnd();
		*t32_core_w(core, 744) = (rnd() % 3) ? 2 : rnd() % 4;
		*t32_core_w(core, 752) = rnd() % 8;
		*t32_core_w(core, 1048 + 4 * ch) = (rnd() & 1 ? 0x80000000u : 0) | (rnd() & 0x7fff0000u) | (rnd() % 10);
		if (!(rnd() % 8))
			*t32_core_w(core, 1048 + 4 * ch) = 0;
		for (k = 0; k < T32_FD_ATTR_WORDS; k++)
			fd_words[k] = rnd();
		n_stop = rnd() % 6;
		r_proc_mask = rnd();
		r_exit = (rnd() & 1) ? 0 : rnd();
		n_drain = (rnd() & 1) ? 1 + rnd() % 120 : 98 + rnd() % 5;
		r_empty = (rnd() & 1) ? 1 + rnd() % 255 : rnd();
		memcpy(init, core, 2048);
		t32o8_clear();
		t32o_complete_n = 0;
		/* vendor */
		cur = &t32o8_v;
		n_calls = 0;
		switch (which) {
		case 0: vr = (u32)CALL(__ispcore_get_frame_drop, T32O_PTR(core), 0); break;
		case 1: CALL(__ispcore_single_cam_check_frame_drop, T32O_PTR(core), 0); break;
		case 2: CALL(ispcoer_debug_info_fs_trige, T32O_PTR(core), ch); break;
		case 3: vr = (u32)CALL(pm_core_event_process_thread, rnd(), 0); break;
		default: CALL(tisp_process_deinit, (rnd() & 0x300) | ch, 0); break;
		}
		if (t32o_complete_n)	/* env.c records the vendor's private_complete */
			t32o8_rec(cur, F_COMPLETE, t32o_complete_arg - T32O_PTR(core), 0, 0, 0);
		memcpy(vend, core, 2048);
		memcpy(core, init, 2048);
		/* ours */
		cur = &t32o8_o;
		n_calls = 0;
		switch (which) {
		case 0: orr = (u32)t32_ispcore_get_frame_drop(&ops, core); break;
		case 1: t32_ispcore_single_cam_check_frame_drop(&ops, core); break;
		case 2: t32_ispcore_debug_info_fs_trige(&ops, core, ch); break;
		case 3: orr = (u32)t32_pm_core_event_process_thread(&ops); break;
		default: t32_tisp_process_deinit(&ops, ch); break;
		}
		bad = t32o8_cmp("corefd", failed < 8) || vr != orr || memcmp(vend, core, 2048) || t32o_complete_n > 1;
		compared++;
		if (bad && failed++ < 8)
			printf("MISMATCH case %u: ret %08x / %08x core %d\n", which, vr, orr, memcmp(vend, core, 2048) != 0);
	}
	printf("t32_corefd_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
