#ifndef TX_ISP_T23_MSCA_SYNC_H
#define TX_ISP_T23_MSCA_SYNC_H

/*
 * MSCA output lifetime rules (driver/t23/README.md "Output/channel restart
 * hang"), as pure decisions shared with the host test
 * (tests/tx_isp_t23_msca_sync_test.c):
 *
 *  1. Frames never reach an enabled output whose FIFO may hold addresses of
 *     a freed pool.  With the input stopped nothing is written at close or
 *     REQBUFS (stock); before the input starts again every enabled output
 *     that is not streaming is released (bit cleared, completions dropped,
 *     address FIFO cleared), except the starting channel when its FIFO was
 *     just rearmed with its current buffers.  A new tx-isp session releases
 *     all of them.  While frames flow, close and REQBUFS release at a frame
 *     boundary.  An output is never enabled with an empty (cleared) FIFO:
 *     the start waits for the first QBUF.
 *  2. 0xd040 / 0xd050 (+ 0xd010 update request) only change while no frame
 *     flows (input stopped) or at a frame boundary: the core ISR applies the
 *     pending change at frame-done; the caller waits for that with a bounded
 *     timeout and applies it directly if no frame-done comes (no frame flows
 *     then either).
 *  3. The address FIFO of an enabled output is never cleared while frames
 *     flow (the MSCA would write the next frame to address 0).
 *  4. An output kept enabled across the input stop and restarted with the
 *     configuration it holds is not reloaded (no tisp_msca_chx_cfg_load(),
 *     no 0xd010, no register write).  Every other start is stock-exact:
 *     tisp_channel_start() at STREAMON, no frame wait.
 *  5. A mirror/flip write with unchanged bits issues no update request.
 */

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint32_t u32;
#endif

/* Register changes waiting for the next frame boundary. */
struct t23_msca_pend {
	u32 set;	/* 0xd040 bits to set */
	u32 clr;	/* 0xd040 bits to clear */
	int flip;	/* 0xd050 write + 0xd010 request pending */
	u32 flip_word;
};

static inline void t23_msca_pend_bits(struct t23_msca_pend *p, u32 set,
				      u32 clr)
{
	/* the newer request wins for a bit named twice */
	p->set = (p->set & ~clr) | set;
	p->clr = (p->clr & ~set) | clr;
}

static inline int t23_msca_pend_any(const struct t23_msca_pend *p)
{
	return p->set || p->clr || p->flip;
}

/* New 0xd040 value; the bit requests are consumed. */
static inline u32 t23_msca_pend_take_d040(struct t23_msca_pend *p, u32 d040)
{
	d040 = (d040 & ~p->clr) | p->set;
	p->set = 0;
	p->clr = 0;
	return d040;
}

/* The flip word currently in effect or about to be (pending wins). */
static inline u32 t23_msca_flip_effective(const struct t23_msca_pend *p,
					  u32 reg)
{
	return p->flip ? p->flip_word : reg;
}

/*
 * Queue a flip word.  Returns 0 when it is a no-op that @skip_noop drops
 * (no update request), 1 when it was queued.
 */
static inline int t23_msca_flip_queue(struct t23_msca_pend *p, u32 reg,
				      u32 word, int skip_noop)
{
	if (skip_noop && word == t23_msca_flip_effective(p, reg))
		return 0;
	p->flip = 1;
	p->flip_word = word;
	return 1;
}

/* STREAMOFF: does the output keep its 0xd040 bit? (msca_keep_enabled) */
static inline int t23_msca_stop_keeps_bit(int keep_param, int input_stops)
{
	return keep_param >= 2 || (keep_param == 1 && input_stops);
}

enum t23_msca_restart {
	T23_MSCA_RELOAD = 0,	/* tisp_channel_start(): full cfg load */
	T23_MSCA_REUSE,		/* registers hold this cfg: only set the bit */
};

static inline enum t23_msca_restart t23_msca_restart_mode(int skip_param,
							  int live_valid,
							  int cfg_equal)
{
	return skip_param && live_valid && cfg_equal ? T23_MSCA_REUSE :
						       T23_MSCA_RELOAD;
}

/* STREAMON FIFO rearm (clear + refill) allowed? */
static inline int t23_msca_fifo_clear_allowed(int bit_set, int frames_flowing)
{
	return !(bit_set && frames_flowing);
}

/* @n frame boundaries passed since @start (wrapping counter). */
static inline int t23_msca_frames_since(u32 now, u32 start, u32 n)
{
	return (u32)(now - start) >= n;
}

#endif /* TX_ISP_T23_MSCA_SYNC_H */
