/*
 * DEBUG ONLY: hang_debug=1 stage tracking for the T23 frame path.
 *
 * Every tracked path (interrupt handlers, frame channel QBUF/DQBUF/WAIT,
 * STREAMON/STREAMOFF, the stream IRQ gate, the tx-isp / isp-m0 / framechan
 * ioctls and the AE/AWB work) counts its entries and exits and records the
 * last stage it reached. A 1 s heartbeat prints the counters, flags a frame
 * stall or a path stuck in flight, and dumps the MSCA/VIC/core state once
 * per episode. With hang_debug_paddr= the record is also kept, uncached, in
 * a physical page that survives a watchdog reset: the next load with the
 * same address prints the previous boot's record before reusing the page.
 *
 * hang_debug=0 (default): the pointer below stays NULL and every hook is a
 * single not-taken branch.
 */

#define REGTRACE_T23_HANG_MAGIC 0x54323348U /* "T23H" */
#define REGTRACE_T23_HANG_VERSION 1U
#define REGTRACE_T23_HANG_EVENTS 64U

enum regtrace_t23_hang_path {
    T23H_CORE_IRQ,
    T23H_VIC_IRQ,
    T23H_IVDC_IRQ,
    T23H_QBUF,
    T23H_DQBUF,
    T23H_WAIT,
    T23H_STREAMON,
    T23H_STREAMOFF,
    T23H_IRQGATE,
    T23H_TXISP_IOCTL,
    T23H_M0_IOCTL,
    T23H_FC_IOCTL,
    T23H_AE_WORK,
    T23H_AWB_WORK,
    T23H_HEARTBEAT,
    T23H_PATHS
};

static const char *const regtrace_t23_hang_names[T23H_PATHS] = {
    "cirq", "virq", "iirq", "qbuf", "dqbuf", "wait", "son", "soff",
    "gate", "txio", "m0io", "fcio", "aew", "awbw", "hb",
};

/* All u32, so a raw dump (devmem) reads the same as the printk. */
struct regtrace_t23_hang_rec {
    u32 magic;
    u32 version;
    u32 size;
    u32 boot;               /* jiffies at module load */
    u32 hb;                 /* heartbeat count */
    u32 hb_jiffies;         /* jiffies of the last heartbeat */
    u32 panic;              /* panics seen by the notifier */
    u32 flags;
    u32 enter[16];
    u32 exit[16];
    u32 stage[16];          /* last stage reached */
    u32 arg[16];            /* cmd / status / irq of the last entry */
    u32 core_status;        /* last core 0xb4 */
    u32 core_empty;         /* core IRQs with no status and nothing drained */
    u32 vic_status0;
    u32 vic_status1;
    u32 vic_empty;          /* VIC IRQs with nothing pending */
    u32 ivdc_empty;
    u32 msca_popped;        /* MSCA done FIFO entries popped by the ISR */
    u32 msca_status;        /* last 0xd13c seen by the ISR */
    u32 ev_head;
    u32 ev[REGTRACE_T23_HANG_EVENTS]; /* path<<27 | stage<<19 | jiffies */
    u32 canary;
};

static int regtrace_t23_hang_debug;
module_param_named(hang_debug, regtrace_t23_hang_debug, int, 0444);
MODULE_PARM_DESC(hang_debug,
                 "DEBUG: 1 = frame path stage tracking, heartbeat and stall dumps (default 0)");
static uint regtrace_t23_hang_period_ms = 1000;
module_param_named(hang_debug_period_ms, regtrace_t23_hang_period_ms, uint, 0644);
MODULE_PARM_DESC(hang_debug_period_ms, "DEBUG: heartbeat period in ms (default 1000)");
static uint regtrace_t23_hang_stall_ms = 2000;
module_param_named(hang_debug_stall_ms, regtrace_t23_hang_stall_ms, uint, 0644);
MODULE_PARM_DESC(hang_debug_stall_ms,
                 "DEBUG: report a frame stall / stuck call after this many ms (default 2000)");
static uint regtrace_t23_hang_paddr;
module_param_named(hang_debug_paddr, regtrace_t23_hang_paddr, uint, 0444);
MODULE_PARM_DESC(hang_debug_paddr,
                 "DEBUG: physical address of a free 4 KiB RAM page that keeps the record across a watchdog reset (default 0 = off)");

static struct regtrace_t23_hang_rec regtrace_t23_hang_static;
static volatile struct regtrace_t23_hang_rec *regtrace_t23_hang;

static inline void regtrace_t23_hang_enter(unsigned int path, u32 arg)
{
    volatile struct regtrace_t23_hang_rec *h = regtrace_t23_hang;

    if (unlikely(h)) {
        h->enter[path]++;
        h->stage[path] = 1;
        h->arg[path] = arg;
    }
}

static inline void regtrace_t23_hang_stage(unsigned int path, u32 stage)
{
    volatile struct regtrace_t23_hang_rec *h = regtrace_t23_hang;

    if (unlikely(h))
        h->stage[path] = stage;
}

static inline void regtrace_t23_hang_exit(unsigned int path)
{
    volatile struct regtrace_t23_hang_rec *h = regtrace_t23_hang;

    if (unlikely(h))
        h->exit[path]++;
}

/* Process/softirq context only; not called from the interrupt handlers. */
static void regtrace_t23_hang_event(unsigned int path, u32 stage)
{
    volatile struct regtrace_t23_hang_rec *h = regtrace_t23_hang;
    unsigned long flags;
    u32 idx;

    if (likely(!h))
        return;
    local_irq_save(flags);
    idx = h->ev_head++ % REGTRACE_T23_HANG_EVENTS;
    h->ev[idx] = (path << 27) | ((stage & 0xffU) << 19) |
                 ((u32)jiffies & 0x7ffffU);
    local_irq_restore(flags);
}

/* Stage plus event, for the rare control paths. */
static inline void regtrace_t23_hang_mark(unsigned int path, u32 stage)
{
    if (unlikely(regtrace_t23_hang)) {
        regtrace_t23_hang_stage(path, stage);
        regtrace_t23_hang_event(path, stage);
    }
}

#define T23H_ENTER(p, a) regtrace_t23_hang_enter((p), (u32)(a))
#define T23H_STAGE(p, s) regtrace_t23_hang_stage((p), (s))
#define T23H_EXIT(p) regtrace_t23_hang_exit(p)
#define T23H_MARK(p, s) regtrace_t23_hang_mark((p), (s))
#define T23H_ON() unlikely(regtrace_t23_hang != NULL)
#define T23H_FIELD(f) (regtrace_t23_hang->f)
