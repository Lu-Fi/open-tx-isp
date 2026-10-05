#ifndef TX_ISP_T23_CRUMBS_H
#define TX_ISP_T23_CRUMBS_H

/*
 * Persistent step markers ("crumbs") for silent hard hangs.
 *
 * Debug only, off by default (module parameter crumbs).  crumbs=1 keeps one
 * uncached 4 KiB page at the end of the MDNS buffer that libimp allocates
 * in rmem (GET_BUF is padded by one page for it); crumbs=2 uses the page at
 * crumb_addr.  The record survives a timps restart or a module reload and
 * the next SET_BUF prints it to kmsg (or read it with devmem, address in
 * the kmsg line "crumbs at 0x...").  It does NOT survive a reboot in rmem:
 * the T23 U-Boot zeroes its malloc area, which covers all of rmem.  Only a
 * page in a mem= hole (crumbs=2) survives a watchdog reset.
 *
 * Pure layout and helpers on a word array, shared with the host test.
 */

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
typedef uint32_t u32;
#endif

#define T23_CRUMB_MAGIC		0x54323343U	/* "T23C" */
#define T23_CRUMB_PAGE_BYTES	4096U

/* word offsets */
#define T23_CRUMB_W_MAGIC	0	/* T23_CRUMB_MAGIC while valid */
#define T23_CRUMB_W_SESSION	1	/* jiffies at SET_BUF of this record */
#define T23_CRUMB_W_SEQ		2	/* number of steps recorded */
#define T23_CRUMB_W_STEP	3	/* last step | arg << 16 */
#define T23_CRUMB_W_JIFFIES	4	/* jiffies of the last step */
#define T23_CRUMB_W_IRQ_IN	5	/* irq number + 1 while in hard ISR, else 0 */
#define T23_CRUMB_W_IRQ_COUNT	6	/* hard ISR entries */
#define T23_CRUMB_W_IRQ_JIFFIES	7	/* jiffies at the last ISR entry */
#define T23_CRUMB_W_QBUF	8	/* QBUF count */
#define T23_CRUMB_W_D040	9	/* MSCA enable mask at the last step */
#define T23_CRUMB_W_D050	10	/* MSCA mirror/flip word at the last step */
#define T23_CRUMB_W_MASK	11	/* framechan stream mask at the last step */
#define T23_CRUMB_RING		16	/* first ring word */
#define T23_CRUMB_RING_LEN	64	/* entries of two words */

/* step codes (low 16 bits of a ring entry's first word) */
enum t23_crumb_step {
	T23C_SETBUF = 1,
	T23C_TXISP_ON,
	T23C_TXISP_OFF,
	T23C_CHAN_ON,		/* framechan STREAMON enter, arg channel */
	T23C_CHAN_REARM,	/* FIFO rearm done, arg pushed */
	T23C_CHAN_INPUT_ON,	/* input (CSI/sensor/VIC) start done */
	T23C_CHAN_TISP_ON,	/* TISP stream regs written */
	T23C_CHAN_MSCA_ON,	/* MSCA output start done, arg channel */
	T23C_CHAN_IRQ_ON,	/* irq gate done = STREAMON complete */
	T23C_CHAN_OFF,		/* framechan STREAMOFF enter, arg channel */
	T23C_CHAN_MSCA_OFF,	/* MSCA output stop done, arg 1 = bit cleared */
	T23C_CHAN_INPUT_OFF,	/* input stopped (last channel) */
	T23C_CHAN_OFF_DONE,
	T23C_CFG_LOAD,		/* tisp_msca_chx_cfg_load enter, arg channel */
	T23C_CFG_COMMIT,	/* 0xd010 = 1 of the cfg load */
	T23C_CFG_ENABLE,	/* 0xd040 written by the cfg load */
	T23C_CFG_SKIP,		/* restart with unchanged geometry, no reload */
	T23C_FLIP_WRITE,	/* 0xd050 + 0xd010 update request */
	T23C_FLIP_SKIP,		/* flip bits unchanged, no update request */
	T23C_SENSOR_FLIP,	/* sensor flip ioctl, arg mode */
	T23C_SENSOR_FLIP_DONE,
	T23C_FIFO_CLEAR,	/* MSCA address FIFO clear, arg channel */
	T23C_LAST_CLOSE,	/* tx-isp last close */
	T23C_RELEASE,		/* output released (close/REQBUFS/session), arg channel */
	T23C_SYNC,		/* 0xd040 change queued for a frame boundary, arg set | clr << 4 */
	T23C_SYNC_TIMEOUT,	/* no frame boundary in time, applied directly */
	T23C_INPUT_DRAIN,	/* last frame drained before the core stop, arg 1 = timed out */
	T23C_REARM_SKIP,	/* FIFO of a live enabled output left alone, arg channel */
	T23C_CFG_REUSE,		/* restart with the loaded cfg: bit only, arg channel */
};

static inline void t23_crumb_reset(volatile u32 *w, u32 session)
{
	unsigned int i;

	for (i = 0; i < T23_CRUMB_PAGE_BYTES / 4U; i++)
		w[i] = 0;
	w[T23_CRUMB_W_SESSION] = session;
	w[T23_CRUMB_W_MAGIC] = T23_CRUMB_MAGIC;
}

static inline int t23_crumb_valid(const volatile u32 *w)
{
	return w[T23_CRUMB_W_MAGIC] == T23_CRUMB_MAGIC;
}

static inline void t23_crumb_mark(volatile u32 *w, u32 step, u32 arg,
				  u32 now)
{
	u32 seq = w[T23_CRUMB_W_SEQ];
	u32 slot = T23_CRUMB_RING + (seq % T23_CRUMB_RING_LEN) * 2U;
	u32 word = (step & 0xffffU) | (arg << 16);

	w[slot] = word;
	w[slot + 1U] = now;
	w[T23_CRUMB_W_STEP] = word;
	w[T23_CRUMB_W_JIFFIES] = now;
	w[T23_CRUMB_W_SEQ] = seq + 1U;
}

/*
 * Ring entry @back steps before the newest one (0 = newest).  Returns 0 and
 * fills @word/@when, or -1 when fewer steps were recorded.
 */
static inline int t23_crumb_entry(const volatile u32 *w, u32 back,
				  u32 *word, u32 *when)
{
	u32 seq = w[T23_CRUMB_W_SEQ];
	u32 slot;

	if (back >= seq || back >= T23_CRUMB_RING_LEN)
		return -1;
	slot = T23_CRUMB_RING +
	       ((seq - 1U - back) % T23_CRUMB_RING_LEN) * 2U;
	*word = w[slot];
	*when = w[slot + 1U];
	return 0;
}

#endif /* TX_ISP_T23_CRUMBS_H */
