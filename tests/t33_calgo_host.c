/*
 * t33_calgo_host: the T33 custom AE / AWB library ioctls (driver/t32/tx_isp_t32_calgo.c, openimp #805,
 * beyond the vendor) on host shims.  Checks: request checks (cookie, num, busy, not open), the get blocks for
 * the next statistics frame and answers -EAGAIN after the timeout (no busy loop), the info and init blocks
 * land at +8, a result goes to the manual path only with change != 0 (save on the first one), the close
 * restores only after a result and wakes a waiting get (-EINVAL), a signal is passed on, other commands
 * answer -ENOIOCTLCMD, and no ioctl leaves a mutex held.
 */
#include <stdio.h>
#include <stdarg.h>
#include "t33_calgo_host_env.h"

#define T33_CALGO_HOST
#include "../driver/t32/tx_isp_t32_calgo.c"

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int locks;
void host_mutex_lock(struct mutex *m) { CHECK(!m->held, "mutex taken twice"); m->held = 1; locks++; }
void host_mutex_unlock(struct mutex *m) { CHECK(m->held, "unlock without lock"); m->held = 0; locks--; }

static int fileA, fileB;
static int hook_mode;	/* 0 nothing (timeout), 1 AE tick, 2 AWB tick, 3 close AE, 4 signal */
static int hook_calls;
long host_wait_hook(void)
{
	u32 req[2] = { T33_CALGO_COOKIE, 0 };

	hook_calls++;
	switch (hook_mode) {
	case 1: t33_calgo_ae_tick(); break;
	case 2: t33_calgo_awb_tick(); break;
	case 3: {
		/* the close runs while the get sleeps: the get dropped the lock before waiting */
		CHECK(!calgo_lock.held, "get waits with calgo_lock held");
		CHECK(t33_calgo_ioctl(&fileA, T33_IOC_AE_ALGO_CLOSE, (unsigned long)req) == 0, "close during wait");
		break;
	}
	case 4: return -512;	/* -ERESTARTSYS */
	}
	return 0;
}

static int fault;
unsigned long copy_from_user(void *d, const void *s, unsigned long n) { if (fault) return n; memcpy(d, s, n); return 0; }
unsigned long copy_to_user(void *d, const void *s, unsigned long n) { if (fault) return n; memcpy(d, s, n); return 0; }
int isp_printf(unsigned int level, const char *fmt, ...) { (void)level; (void)fmt; return 0; }
int seq_printf(struct seq_file *m, const char *fmt, ...) { (void)m; (void)fmt; return 0; }

static int ae_info_calls, awb_info_calls, ae_apply, ae_apply_first, ae_restore, awb_apply, awb_apply_first, awb_restore;
static u32 last_v[4], last_r, last_b;
int t33_tb_calgo_ae_info(u32 ch, u8 *info) { (void)ch; ae_info_calls++; memset(info, 0xa5, T33_CALGO_AE_INFO); return 0; }
void t33_tb_calgo_ae_init(u32 ch, u8 *init) { (void)ch; memset(init, 0, T33_CALGO_AE_INIT); init[100] = 25; }
int t33_tb_calgo_ae_apply(u32 ch, const u32 *v, u8 *save, bool first)
{
	(void)ch;
	ae_apply++;
	ae_apply_first += first;
	memcpy(last_v, v, sizeof(last_v));
	if (first)
		memset(save, 0x3c, 41);
	return 0;
}
void t33_tb_calgo_ae_restore(u32 ch, const u8 *save) { (void)ch; ae_restore++; CHECK(save[0] == 0x3c && save[40] == 0x3c, "AE restore gets the saved state"); }
int t33_tb_calgo_awb_info(u32 ch, u8 *info) { (void)ch; awb_info_calls++; memset(info, 0x5a, T33_CALGO_AWB_INFO); return 0; }
int t33_tb_calgo_awb_apply(u32 ch, u32 r, u32 b, u32 *save, bool first)
{
	(void)ch;
	awb_apply++;
	awb_apply_first += first;
	last_r = r;
	last_b = b;
	if (first) {
		save[0] = 0;
		save[1] = 300;
		save[2] = 400;
	}
	return 0;
}
void t33_tb_calgo_awb_restore(u32 ch, const u32 *save) { (void)ch; awb_restore++; CHECK(save[1] == 300 && save[2] == 400, "AWB restore"); }

static long io(unsigned int cmd, void *p) { long r = t33_calgo_ioctl(&fileA, cmd, (unsigned long)p); CHECK(locks == 0, "lock left after %#x", cmd); return r; }

int main(void)
{
	static u32 buf[(8 + T33_CALGO_AWB_INFO) / 4 + 4];
	u32 hdr[2] = { T33_CALGO_COOKIE, 0 }, bad[2] = { 0x336ac, 0 }, n1[2] = { T33_CALGO_COOKIE, 1 };
	u32 set[16];

	/* not open yet */
	CHECK(io(T33_IOC_AE_ALGO_GET, hdr) == -EINVAL, "get before open");
	CHECK(io(T33_IOC_AE_ALGO_CLOSE, hdr) == -EINVAL, "close before open");
	CHECK(io(0xc0045427, hdr) == -ENOIOCTLCMD, "other command");
	/* open: request checks */
	CHECK(io(T33_IOC_AE_ALGO_OPEN, bad) == -EINVAL, "T40 cookie refused");
	CHECK(io(T33_IOC_AE_ALGO_OPEN, n1) == -EINVAL, "sensor 1 refused");
	fault = 1;
	CHECK(io(T33_IOC_AE_ALGO_OPEN, buf) == -EFAULT, "copy fault");
	fault = 0;
	memset(buf, 0, sizeof(buf));
	buf[0] = T33_CALGO_COOKIE;
	CHECK(io(T33_IOC_AE_ALGO_OPEN, buf) == 0, "AE open");
	CHECK(((u8 *)buf)[8 + 100] == 25 && buf[0] == T33_CALGO_COOKIE, "init attr at +8, header kept");
	CHECK(io(T33_IOC_AE_ALGO_OPEN, hdr) == -EBUSY, "second open");
	/* get: no statistics frame -> one wait, -EAGAIN */
	hook_mode = 0;
	hook_calls = 0;
	CHECK(io(T33_IOC_AE_ALGO_GET, buf) == -EAGAIN, "timeout -> EAGAIN");
	CHECK(hook_calls == 1 && ca[0].timeouts == 1 && ae_info_calls == 0, "one wait, no info");
	/* a frame during the wait */
	hook_mode = 1;
	memset(buf, 0, sizeof(buf));
	buf[0] = T33_CALGO_COOKIE;
	CHECK(io(T33_IOC_AE_ALGO_GET, buf) == 0, "get with a frame");
	CHECK(((u8 *)buf)[8] == 0xa5 && ((u8 *)buf)[8 + T33_CALGO_AE_INFO - 1] == 0xa5 && ((u8 *)buf)[8 + T33_CALGO_AE_INFO] == 0,
	      "info exactly at +8 .. +8+1996");
	/* a frame that came before the get is handed out at once (no wait) */
	t33_calgo_ae_tick();
	hook_calls = 0;
	hook_mode = 0;
	CHECK(io(T33_IOC_AE_ALGO_GET, buf) == 0 && hook_calls == 0, "pending frame: no wait");
	CHECK(io(T33_IOC_AE_ALGO_GET, buf) == -EAGAIN, "same frame not twice");
	/* AWB ticks do not wake the AE */
	hook_mode = 2;
	CHECK(io(T33_IOC_AE_ALGO_GET, buf) == -EAGAIN, "AWB tick does not wake AE");
	/* signal */
	hook_mode = 4;
	CHECK(io(T33_IOC_AE_ALGO_GET, buf) == -512, "signal passed on");
	/* set: change 0 -> nothing, change 1 -> manual path, first only once */
	memset(set, 0, sizeof(set));
	set[0] = T33_CALGO_COOKIE;
	CHECK(io(T33_IOC_AE_ALGO_SET, set) == 0 && ae_apply == 0, "change 0");
	set[2] = 1;
	set[4] = 100;
	set[5] = 2048;
	set[6] = 1024;
	set[7] = 1024;
	CHECK(io(T33_IOC_AE_ALGO_SET, set) == 0 && ae_apply == 1 && ae_apply_first == 1, "first result");
	CHECK(last_v[0] == 100 && last_v[1] == 2048 && last_v[2] == 1024 && last_v[3] == 1024, "it / again / dgain / isp dgain");
	CHECK(io(T33_IOC_AE_ALGO_SET, set) == 0 && ae_apply == 2 && ae_apply_first == 1, "second result");
	set[0] = 0;
	CHECK(io(T33_IOC_AE_ALGO_SET, set) == -EINVAL, "set with a bad cookie");
	/* close during a waiting get: restore once, the get returns -EINVAL */
	hook_mode = 3;
	CHECK(io(T33_IOC_AE_ALGO_GET, buf) == -EINVAL, "get woken by the close");
	CHECK(ae_restore == 1 && !ca[0].open && ca[0].info == NULL, "restored, closed, freed");
	CHECK(io(T33_IOC_AE_ALGO_SET, set) == -EINVAL, "set after close");
	/* reopen, close without a result: no restore; the vendor close cookie is garbage */
	memset(buf, 0, sizeof(buf));
	buf[0] = T33_CALGO_COOKIE;
	CHECK(io(T33_IOC_AE_ALGO_OPEN, buf) == 0, "reopen");
	bad[0] = 0x77001430;
	CHECK(io(T33_IOC_AE_ALGO_CLOSE, bad) == 0 && ae_restore == 1, "close without result, garbage cookie");
	/* AWB */
	memset(buf, 0, sizeof(buf));
	buf[0] = T33_CALGO_COOKIE;
	CHECK(io(T33_IOC_AWB_ALGO_OPEN, buf) == 0, "AWB open");
	CHECK(((u8 *)buf)[8] == 1 && ((u8 *)buf)[8 + 12] == 32 && ((u8 *)buf)[8 + T33_CALGO_AWB_INIT] == 0, "AWB init attr");
	hook_mode = 2;
	CHECK(io(T33_IOC_AWB_ALGO_GET, buf) == 0 && ((u8 *)buf)[8 + T33_CALGO_AWB_INFO - 1] == 0x5a, "AWB get");
	memset(set, 0, sizeof(set));
	set[0] = T33_CALGO_COOKIE;
	set[2] = 1;
	set[3] = 0;
	set[4] = 400;
	CHECK(io(T33_IOC_AWB_ALGO_SET, set) == -EINVAL && awb_apply == 0, "zero gain refused");
	set[3] = 500;
	CHECK(io(T33_IOC_AWB_ALGO_SET, set) == 0 && awb_apply == 1 && awb_apply_first == 1 && last_r == 500 && last_b == 400, "AWB result");
	CHECK(io(T33_IOC_AWB_ALGO_CLOSE, hdr) == 0 && awb_restore == 1, "AWB close restores");
	/* a dead client: the release of another file leaves the library alone, the opener's release unregisters it */
	memset(buf, 0, sizeof(buf));
	buf[0] = T33_CALGO_COOKIE;
	CHECK(io(T33_IOC_AE_ALGO_OPEN, buf) == 0, "open for the release test");
	set[0] = T33_CALGO_COOKIE;
	set[1] = 0;
	set[2] = 1;
	CHECK(io(T33_IOC_AE_ALGO_SET, set) == 0, "result before the crash");
	t33_calgo_release(&fileB);
	CHECK(ca[0].open && ae_restore == 1, "other file's release");
	t33_calgo_release(&fileA);
	CHECK(!ca[0].open && ae_restore == 2 && ca[0].info == NULL && locks == 0, "opener's release unregisters and restores");
	CHECK(io(T33_IOC_AE_ALGO_OPEN, buf) == 0, "reopen after the release");
	t33_calgo_release(&fileA);
	if (fails)
		printf("t33_calgo_host: %d FAIL\n", fails);
	else
		printf("t33_calgo_host: ok\n");
	return fails != 0;
}
