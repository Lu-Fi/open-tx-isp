/*
 * DEBUG ONLY: hang_debug heartbeat, stall detector, panic notifier and the
 * persistent record (see tx_isp_t23_hangdbg.inc).
 */

#include <linux/timer.h>
#include <linux/notifier.h>
#include <asm/io.h>

static struct timer_list regtrace_t23_hang_timer;
static void __iomem *regtrace_t23_hang_iomem;
static bool regtrace_t23_hang_timer_live;
static bool regtrace_t23_hang_notifier_live;
static u32 regtrace_t23_hang_prev_enter[T23H_PATHS];
static u32 regtrace_t23_hang_prev_exit[T23H_PATHS];
static unsigned long regtrace_t23_hang_since[T23H_PATHS];
static u32 regtrace_t23_hang_prev_frames;
static unsigned long regtrace_t23_hang_frames_since;
static bool regtrace_t23_hang_stall_reported;
static u32 regtrace_t23_hang_stuck_reported;

static u32 regtrace_t23_hang_frames(void)
{
    u32 frames = 0;
    int i;

    for (i = 0; i < REGTRACE_FRAMECHAN_COUNT; i++)
        frames += regtrace_framechan_done_total[i];
    return frames;
}

static void regtrace_t23_hang_print_counts(const char *tag,
                                           volatile struct regtrace_t23_hang_rec *h)
{
    char line[400];
    int len = 0;
    unsigned int p;

    for (p = 0; p < T23H_PATHS && len < (int)sizeof(line) - 40; p++) {
        if (!h->enter[p] && !h->exit[p])
            continue;
        len += scnprintf(line + len, sizeof(line) - len, " %s=%u/%u@%u",
                         regtrace_t23_hang_names[p], h->enter[p],
                         h->exit[p], h->stage[p]);
        if (p >= T23H_TXISP_IOCTL && p <= T23H_FC_IOCTL)
            len += scnprintf(line + len, sizeof(line) - len, ":%x",
                             h->arg[p]);
    }
    printk(KERN_WARNING "t23hang %s hb=%u%s\n", tag, h->hb, line);
}

static void regtrace_t23_hang_print_events(volatile struct regtrace_t23_hang_rec *h)
{
    u32 head = h->ev_head;
    u32 n = head < REGTRACE_T23_HANG_EVENTS ? head : REGTRACE_T23_HANG_EVENTS;
    u32 i;

    for (i = head - n; i != head; i++) {
        u32 ev = h->ev[i % REGTRACE_T23_HANG_EVENTS];
        u32 path = ev >> 27;

        printk(KERN_WARNING "t23hang  ev[%u] %s stage=%u j=0x%05x\n",
               i, path < T23H_PATHS ? regtrace_t23_hang_names[path] : "?",
               (ev >> 19) & 0xffU, ev & 0x7ffffU);
    }
}

static void regtrace_t23_hang_print_rec(const char *tag,
                                        volatile struct regtrace_t23_hang_rec *h)
{
    printk(KERN_WARNING "t23hang %s boot=0x%x hb=%u hb_j=0x%x panic=%u core_st=0x%x core_empty=%u vic_st=0x%x/0x%x vic_empty=%u ivdc_empty=%u msca_pop=%u msca_st=0x%x\n",
           tag, h->boot, h->hb, h->hb_jiffies, h->panic, h->core_status,
           h->core_empty, h->vic_status0, h->vic_status1, h->vic_empty,
           h->ivdc_empty, h->msca_popped, h->msca_status);
    regtrace_t23_hang_print_counts(tag, h);
    regtrace_t23_hang_print_events(h);
}

/* Register and queue state; never pops a FIFO. */
static void regtrace_t23_hang_dump_state(const char *why)
{
    void __iomem *core = NULL;
    void __iomem *vic = NULL;
    unsigned long flags;
    u32 queued = 0;
    u32 done_count;
    u32 unmatched;
    int i;

    if (regtrace_t23_core_sd)
        core = (void __iomem *)(uintptr_t)*(u32 *)(regtrace_t23_core_sd + 0xb8);
    if (regtrace_t23_vic_sd)
        vic = (void __iomem *)(uintptr_t)*(u32 *)(regtrace_t23_vic_sd + 0xb8);

    spin_lock_irqsave(&regtrace_framechan_done_lock, flags);
    for (i = 0; i < REGTRACE_FRAMECHAN_QBUF_SLOTS; i++)
        if (regtrace_framechan_qbuf_queued[0][i])
            queued |= 1U << i;
    done_count = regtrace_framechan_done_count[0];
    unmatched = regtrace_framechan_done_unmatched[0];
    spin_unlock_irqrestore(&regtrace_framechan_done_lock, flags);

    printk(KERN_WARNING "t23hang DUMP %s: stream_mask=0x%x ch0 streaming=%d queued=0x%x qbufs=%u done_count=%u done_total=%u unmatched=%u msca_ch_en=0x%x irq_en=%d/%d/%d clks=%d/%d\n",
           why, regtrace_framechan_stream_mask,
           regtrace_framechan_streaming[0], queued,
           regtrace_framechan_qbuf_count[0], done_count,
           regtrace_framechan_done_total[0], unmatched,
           regtrace_t23_msca_ch_en, regtrace_t23_core_irq_enabled,
           regtrace_t23_vic_irq_enabled, regtrace_t23_ivdc_irq_enabled,
           regtrace_t23_core_clks_enabled, regtrace_t23_vic_clks_enabled);
    if (core && regtrace_t23_core_clks_enabled)
        printk(KERN_WARNING "t23hang DUMP core mask=0x%x status=0x%x run=0x%x r1060=0x%x d040=0x%x msca_st=0x%x/0x%x/0x%x\n",
               readl(core + 0xb0), readl(core + 0xb4), readl(core + 0x800),
               readl(core + 0x1060), readl(core + 0xd040),
               readl(core + 0xd13c), readl(core + 0xd23c),
               readl(core + 0xd33c));
    if (vic && regtrace_t23_vic_clks_enabled)
        printk(KERN_WARNING "t23hang DUMP vic ctrl=0x%x st=0x%x/0x%x mask=0x%x/0x%x size=0x%x mdma=0x%x\n",
               readl(vic + 0x0), readl(vic + 0x1e0), readl(vic + 0x1e4),
               readl(vic + 0x1e8), readl(vic + 0x1ec), readl(vic + 0x04),
               readl(vic + 0x300));
}

static void regtrace_t23_hang_tick(unsigned long data)
{
    volatile struct regtrace_t23_hang_rec *h = regtrace_t23_hang;
    unsigned long now = jiffies;
    unsigned long stall = msecs_to_jiffies(regtrace_t23_hang_stall_ms);
    u32 frames;
    u32 d_core;
    u32 d_vic;
    unsigned int p;

    (void)data;
    if (!h)
        return;

    if (regtrace_t23_hang_iomem &&
        (h->magic != REGTRACE_T23_HANG_MAGIC ||
         h->canary != REGTRACE_T23_HANG_MAGIC)) {
        /* Someone else owns the page: stop writing there. */
        printk(KERN_ERR "t23hang persistent page 0x%x clobbered (magic=0x%x canary=0x%x); falling back to the in-module record\n",
               regtrace_t23_hang_paddr, h->magic, h->canary);
        regtrace_t23_hang_static = *(struct regtrace_t23_hang_rec *)h;
        regtrace_t23_hang_static.magic = REGTRACE_T23_HANG_MAGIC;
        regtrace_t23_hang_static.canary = REGTRACE_T23_HANG_MAGIC;
        regtrace_t23_hang = &regtrace_t23_hang_static;
        h = regtrace_t23_hang;
    }

    T23H_ENTER(T23H_HEARTBEAT, 0);
    h->hb++;
    h->hb_jiffies = (u32)now;

    frames = regtrace_t23_hang_frames();
    d_core = h->enter[T23H_CORE_IRQ] - regtrace_t23_hang_prev_enter[T23H_CORE_IRQ];
    d_vic = h->enter[T23H_VIC_IRQ] - regtrace_t23_hang_prev_enter[T23H_VIC_IRQ];

    printk(KERN_WARNING "t23hang hb=%u frames=%u(+%u) irq/s core=%u vic=%u ivdc=+%u empty=%u/%u/%u mask=0x%x dq=%u/%u@%u wait=%u/%u qbuf=%u/%u son=%u/%u@%u soff=%u/%u@%u gate=%u/%u@%u:%x txio=%u/%u:%x m0io=%u/%u:%x fcio=%u/%u:%x ae=%u/%u awb=%u/%u cirq@%u st=0x%x msca=0x%x\n",
           h->hb, frames, frames - regtrace_t23_hang_prev_frames,
           d_core, d_vic,
           h->enter[T23H_IVDC_IRQ] - regtrace_t23_hang_prev_enter[T23H_IVDC_IRQ],
           h->core_empty, h->vic_empty, h->ivdc_empty,
           regtrace_framechan_stream_mask,
           h->enter[T23H_DQBUF], h->exit[T23H_DQBUF], h->stage[T23H_DQBUF],
           h->enter[T23H_WAIT], h->exit[T23H_WAIT],
           h->enter[T23H_QBUF], h->exit[T23H_QBUF],
           h->enter[T23H_STREAMON], h->exit[T23H_STREAMON], h->stage[T23H_STREAMON],
           h->enter[T23H_STREAMOFF], h->exit[T23H_STREAMOFF], h->stage[T23H_STREAMOFF],
           h->enter[T23H_IRQGATE], h->exit[T23H_IRQGATE], h->stage[T23H_IRQGATE],
           h->arg[T23H_IRQGATE],
           h->enter[T23H_TXISP_IOCTL], h->exit[T23H_TXISP_IOCTL], h->arg[T23H_TXISP_IOCTL],
           h->enter[T23H_M0_IOCTL], h->exit[T23H_M0_IOCTL], h->arg[T23H_M0_IOCTL],
           h->enter[T23H_FC_IOCTL], h->exit[T23H_FC_IOCTL], h->arg[T23H_FC_IOCTL],
           h->enter[T23H_AE_WORK], h->exit[T23H_AE_WORK],
           h->enter[T23H_AWB_WORK], h->exit[T23H_AWB_WORK],
           h->stage[T23H_CORE_IRQ], h->core_status, h->msca_status);

    /* Interrupt rates far above the ~100/s a streaming sensor produces. */
    if (d_core > 2000U || d_vic > 2000U)
        printk(KERN_ERR "t23hang IRQ STORM? core=+%u vic=+%u in %u ms (empty %u/%u)\n",
               d_core, d_vic, regtrace_t23_hang_period_ms,
               h->core_empty, h->vic_empty);

    /* Frames stop while a channel streams. */
    if (frames != regtrace_t23_hang_prev_frames ||
        !regtrace_framechan_stream_mask) {
        regtrace_t23_hang_frames_since = now;
        regtrace_t23_hang_stall_reported = false;
    } else if (!regtrace_t23_hang_stall_reported &&
               time_after(now, regtrace_t23_hang_frames_since + stall)) {
        regtrace_t23_hang_stall_reported = true;
        printk(KERN_ERR "t23hang FRAME STALL: no MSCA completion for %u ms while streaming\n",
               jiffies_to_msecs(now - regtrace_t23_hang_frames_since));
        regtrace_t23_hang_dump_state("frame-stall");
        regtrace_t23_hang_print_events(h);
    }

    /* A call in flight with no progress (DQBUF/WAIT may block by design). */
    for (p = T23H_QBUF; p <= T23H_AWB_WORK; p++) {
        bool in_flight = h->enter[p] != h->exit[p];

        if (!in_flight || h->enter[p] != regtrace_t23_hang_prev_enter[p] ||
            h->exit[p] != regtrace_t23_hang_prev_exit[p]) {
            regtrace_t23_hang_since[p] = now;
            regtrace_t23_hang_stuck_reported &= ~(1U << p);
            continue;
        }
        if (p == T23H_DQBUF || p == T23H_WAIT)
            continue;
        if (!(regtrace_t23_hang_stuck_reported & (1U << p)) &&
            time_after(now, regtrace_t23_hang_since[p] + stall)) {
            regtrace_t23_hang_stuck_reported |= 1U << p;
            printk(KERN_ERR "t23hang STUCK %s stage=%u arg=0x%x for %u ms\n",
                   regtrace_t23_hang_names[p], h->stage[p], h->arg[p],
                   jiffies_to_msecs(now - regtrace_t23_hang_since[p]));
            regtrace_t23_hang_dump_state("stuck");
            regtrace_t23_hang_print_events(h);
        }
    }

    for (p = 0; p < T23H_PATHS; p++) {
        regtrace_t23_hang_prev_enter[p] = h->enter[p];
        regtrace_t23_hang_prev_exit[p] = h->exit[p];
    }
    regtrace_t23_hang_prev_frames = frames;
    T23H_EXIT(T23H_HEARTBEAT);

    mod_timer(&regtrace_t23_hang_timer,
              jiffies + msecs_to_jiffies(max(regtrace_t23_hang_period_ms, 100U)));
}

static int regtrace_t23_hang_panic(struct notifier_block *nb,
                                   unsigned long event, void *unused)
{
    volatile struct regtrace_t23_hang_rec *h = regtrace_t23_hang;

    (void)nb;
    (void)event;
    (void)unused;
    if (h) {
        h->panic++;
        /* No register access: the bus may be what hung. */
        regtrace_t23_hang_print_rec("PANIC", h);
    }
    return NOTIFY_DONE;
}

static struct notifier_block regtrace_t23_hang_panic_nb = {
    .notifier_call = regtrace_t23_hang_panic,
};

static void regtrace_t23_hang_init(void)
{
    volatile struct regtrace_t23_hang_rec *h = &regtrace_t23_hang_static;

    BUILD_BUG_ON(sizeof(struct regtrace_t23_hang_rec) > PAGE_SIZE);
    BUILD_BUG_ON(T23H_PATHS > 16);
    if (regtrace_t23_hang_debug <= 0)
        return;

    if (regtrace_t23_hang_paddr) {
        if (!IS_ALIGNED(regtrace_t23_hang_paddr, PAGE_SIZE)) {
            printk(KERN_ERR "t23hang hang_debug_paddr=0x%x is not page aligned; persistent record off\n",
                   regtrace_t23_hang_paddr);
        } else {
            regtrace_t23_hang_iomem =
                ioremap_nocache(regtrace_t23_hang_paddr, PAGE_SIZE);
            if (!regtrace_t23_hang_iomem)
                printk(KERN_ERR "t23hang cannot map 0x%x; persistent record off\n",
                       regtrace_t23_hang_paddr);
        }
    }

    if (regtrace_t23_hang_iomem) {
        volatile struct regtrace_t23_hang_rec *old =
            (volatile struct regtrace_t23_hang_rec *)regtrace_t23_hang_iomem;

        if (old->magic == REGTRACE_T23_HANG_MAGIC &&
            old->version == REGTRACE_T23_HANG_VERSION &&
            old->size == sizeof(struct regtrace_t23_hang_rec)) {
            printk(KERN_WARNING "t23hang previous boot record at 0x%x (last heartbeat %u, %s):\n",
                   regtrace_t23_hang_paddr, old->hb,
                   old->panic ? "PANICKED" : "no panic seen");
            regtrace_t23_hang_print_rec("PREV", old);
        } else {
            printk(KERN_WARNING "t23hang no previous record at 0x%x (words 0x%x 0x%x 0x%x 0x%x)\n",
                   regtrace_t23_hang_paddr, old->magic, old->version,
                   old->size, old->boot);
        }
        h = old;
    }

    memset_io((void __iomem *)h, 0, sizeof(struct regtrace_t23_hang_rec));
    h->version = REGTRACE_T23_HANG_VERSION;
    h->size = sizeof(struct regtrace_t23_hang_rec);
    h->boot = (u32)jiffies;
    h->canary = REGTRACE_T23_HANG_MAGIC;
    wmb();
    h->magic = REGTRACE_T23_HANG_MAGIC;
    wmb();
    regtrace_t23_hang = h;

    setup_timer(&regtrace_t23_hang_timer, regtrace_t23_hang_tick, 0);
    mod_timer(&regtrace_t23_hang_timer,
              jiffies + msecs_to_jiffies(max(regtrace_t23_hang_period_ms, 100U)));
    regtrace_t23_hang_timer_live = true;
    atomic_notifier_chain_register(&panic_notifier_list,
                                   &regtrace_t23_hang_panic_nb);
    regtrace_t23_hang_notifier_live = true;
    if (regtrace_t23_hang_iomem)
        printk(KERN_WARNING "t23hang DEBUG enabled: period=%u ms stall=%u ms persistent record at phys 0x%x (%u bytes)\n",
               regtrace_t23_hang_period_ms, regtrace_t23_hang_stall_ms,
               regtrace_t23_hang_paddr,
               (unsigned int)sizeof(struct regtrace_t23_hang_rec));
    else
        printk(KERN_WARNING "t23hang DEBUG enabled: period=%u ms stall=%u ms in-module record (%u bytes)\n",
               regtrace_t23_hang_period_ms, regtrace_t23_hang_stall_ms,
               (unsigned int)sizeof(struct regtrace_t23_hang_rec));
}

static void regtrace_t23_hang_exit_module(void)
{
    if (regtrace_t23_hang_timer_live) {
        del_timer_sync(&regtrace_t23_hang_timer);
        regtrace_t23_hang_timer_live = false;
    }
    if (regtrace_t23_hang_notifier_live) {
        atomic_notifier_chain_unregister(&panic_notifier_list,
                                         &regtrace_t23_hang_panic_nb);
        regtrace_t23_hang_notifier_live = false;
    }
    if (regtrace_t23_hang) {
        regtrace_t23_hang_print_rec("EXIT", regtrace_t23_hang);
        /* A clean unload is not a hang: keep no stale record behind. */
        regtrace_t23_hang->magic = 0;
        wmb();
    }
    regtrace_t23_hang = NULL;
    if (regtrace_t23_hang_iomem) {
        iounmap(regtrace_t23_hang_iomem);
        regtrace_t23_hang_iomem = NULL;
    }
}
