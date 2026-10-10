/*
 * Diagnostic: watch this module's own sysfs parameter attribute arrays.
 *
 * rmmod tx_isp_t41 sometimes oopses in module_param_sysfs_remove(): an entry
 * of mk->mp->grp.attrs holds the value 2.  Both that pointer array and the
 * param_attribute block are kmalloc'd at load time and never written again
 * by the kernel until the unload, so any change is a stray write.
 *
 * t41_heap_watch=1 snapshots both at init, compares them every jiffy and at
 * each unload step, prints the changed words with their addresses and the
 * surrounding memory, records the last driver step (t41_heapwatch_mark())
 * and restores the snapshot so the unload does not oops.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>

struct t41_hw_param_attribute {
	struct module_attribute mattr;
	const struct kernel_param *param;
};

struct t41_hw_mp {
	unsigned int num;
	struct attribute_group grp;
	struct t41_hw_param_attribute attrs[0];
};

static int t41_heap_watch;
module_param(t41_heap_watch, int, 0444);
static unsigned int t41_heap_watch_hits;
module_param_named(heap_watch_hits, t41_heap_watch_hits, uint, 0444);

static struct timer_list t41_hw_timer;
static bool t41_hw_armed;
static u32 *t41_hw_attrs_snap;
static u8 *t41_hw_mp_snap;
static size_t t41_hw_mp_bytes;
static unsigned int t41_hw_n;
static const char *t41_hw_last_step = "init";
static unsigned long t41_hw_last_step_j;

#define T41_HW_RING 32
static struct { const char *tag; unsigned long j; u32 n; } t41_hw_ring[T41_HW_RING];
static unsigned int t41_hw_ring_pos;
static DEFINE_SPINLOCK(t41_hw_ring_lock);

void t41_heapwatch_mark(const char *step)
{
	unsigned long flags;
	unsigned int p;

	if (t41_heap_watch <= 0)
		return;
	t41_hw_last_step = step;
	t41_hw_last_step_j = jiffies;
	spin_lock_irqsave(&t41_hw_ring_lock, flags);
	p = t41_hw_ring_pos % T41_HW_RING;
	if (t41_hw_ring[p].tag == step) {
		t41_hw_ring[p].n++;
		t41_hw_ring[p].j = jiffies;
	} else {
		p = ++t41_hw_ring_pos % T41_HW_RING;
		t41_hw_ring[p].tag = step;
		t41_hw_ring[p].j = jiffies;
		t41_hw_ring[p].n = 1;
	}
	spin_unlock_irqrestore(&t41_hw_ring_lock, flags);
}

static void t41_hw_print_ring(void)
{
	unsigned int i;

	for (i = 1; i <= T41_HW_RING; ++i) {
		unsigned int p = (t41_hw_ring_pos + i) % T41_HW_RING;

		if (t41_hw_ring[p].tag)
			printk(KERN_ERR "t41-heapwatch: trail %s x%u, %ld jiffies ago\n",
			       t41_hw_ring[p].tag, t41_hw_ring[p].n,
			       (long)(jiffies - t41_hw_ring[p].j));
	}
}

int32_t system_reg_read(int32_t arg1);

/* Which ISP register still points near the hit?  Only blocks the driver
 * itself programs are scanned. */
static void t41_hw_scan_regs(const void *hit)
{
	static const struct { u32 start, end; } win[] = {
		{ 0x00000, 0x01000 }, { 0x04000, 0x07000 }, { 0x0b000, 0x0c000 },
		{ 0x18000, 0x1f000 }, { 0x40000, 0x40200 },
		{ 0xf0000, 0xf1200 }, { 0xf8000, 0xf8200 },
	};
	u32 phys = (u32)(uintptr_t)hit & 0x1fffffffU;
	unsigned int w, found = 0;
	u32 r;

	for (w = 0; w < ARRAY_SIZE(win); ++w)
		for (r = win[w].start; r < win[w].end; r += 4) {
			u32 v;

			/* MSCA completion FIFO pops are destructive reads. */
			if (r >= 0xf0100 && r < 0xf0400 &&
			    ((r & 0xff) == 0x74 || (r & 0xff) == 0x8c))
				continue;
			v = (u32)system_reg_read((int32_t)r);
			u32 pv = v & 0x1fffffffU;

			if (pv + 0x10000U >= phys && pv <= phys + 0x400U && pv) {
				printk(KERN_ERR "t41-heapwatch: reg %05x = %08x (hit phys %08x, delta %d)\n",
				       r, v, phys, (int)(phys - pv));
				found++;
			}
		}
	printk(KERN_ERR "t41-heapwatch: register scan done, %u candidates\n", found);
}

/* Every register in the driver's pages that holds a kernel-RAM address:
 * DMA engines aimed at kmalloc memory. */
static void t41_hw_dma_census(const char *where)
{
	static const u32 pages[] = {
		0x0, 0x1000, 0x2000, 0x3000, 0x4000, 0x5000, 0x6000, 0x7000,
		0x8000, 0xa000, 0xb000, 0xd000, 0xe000, 0x11000, 0x13000,
		0x18000, 0x19000, 0x1a000, 0x1b000, 0x1e000, 0x40000, 0x50000,
		0x60000, 0xf0000, 0xf1000, 0xf8000,
	};
	unsigned int pg, n = 0;
	u32 r;

	for (pg = 0; pg < ARRAY_SIZE(pages); ++pg)
		for (r = pages[pg]; r < pages[pg] + 0x1000; r += 4) {
			u32 v, pv;

			if (r >= 0xf0100 && r < 0xf0400 &&
			    ((r & 0xff) == 0x74 || (r & 0xff) == 0x8c))
				continue;
			v = (u32)system_reg_read((int32_t)r);
			pv = v & 0x1fffffffU;
			if (pv >= 0x00400000U && pv < 0x02600000U &&
			    ((v & 0xe0000000U) == 0 || (v & 0xe0000000U) == 0x80000000U)) {
				printk(KERN_ERR "t41-heapwatch: dma %s reg %05x = %08x\n",
				       where, r, v);
				n++;
			}
		}
	printk(KERN_ERR "t41-heapwatch: dma %s census %u\n", where, n);
}

void t41_heapwatch_census(const char *where)
{
	if (t41_heap_watch > 0)
		t41_hw_dma_census(where);
}

static void t41_hw_dump(const char *what, const u32 *base, int from, int to)
{
	int i;

	for (i = from; i < to; i += 8)
		printk(KERN_ERR "t41-heapwatch: %s %p: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		       what, &base[i], base[i], base[i + 1], base[i + 2],
		       base[i + 3], base[i + 4], base[i + 5], base[i + 6],
		       base[i + 7]);
}

static void t41_hw_check(const char *where)
{
	struct t41_hw_mp *mp;
	u32 *attrs;
	unsigned int i;
	int bad = 0;

	if (!t41_hw_armed)
		return;
	mp = (struct t41_hw_mp *)THIS_MODULE->mkobj.mp;
	attrs = (u32 *)mp->grp.attrs;
	for (i = 0; i <= t41_hw_n; ++i) {
		if (attrs[i] == t41_hw_attrs_snap[i])
			continue;
		printk(KERN_ERR "t41-heapwatch: %s: grp.attrs[%u] @%p %08x -> %08x (last step %s %lu jiffies ago)\n",
		       where, i, &attrs[i], t41_hw_attrs_snap[i], attrs[i],
		       t41_hw_last_step, jiffies - t41_hw_last_step_j);
		bad++;
	}
	if (bad) {
		for (i = 0; i <= t41_hw_n; ++i)
			if (attrs[i] != t41_hw_attrs_snap[i]) {
				t41_hw_print_ring();
				t41_hw_scan_regs(&attrs[i]);
				break;
			}
		/* Neighbouring kmalloc objects: what overran into the array? */
		t41_hw_dump("attrs", attrs, -64, ((int)t41_hw_n + 1 + 64 + 7) & ~7);
		memcpy(attrs, t41_hw_attrs_snap, (t41_hw_n + 1) * sizeof(u32));
	}
	for (i = 0; i < t41_hw_mp_bytes; i += 4) {
		u32 now = *(u32 *)((u8 *)mp + i);
		u32 was = *(u32 *)(t41_hw_mp_snap + i);

		if (now == was)
			continue;
		printk(KERN_ERR "t41-heapwatch: %s: mp+%#x @%p %08x -> %08x (last step %s %lu jiffies ago)\n",
		       where, i, (u8 *)mp + i, was, now, t41_hw_last_step,
		       jiffies - t41_hw_last_step_j);
		bad++;
	}
	if (bad) {
		memcpy(mp, t41_hw_mp_snap, t41_hw_mp_bytes);
		t41_heap_watch_hits += bad;
	}
}

static void t41_hw_tick(unsigned long data)
{
	(void)data;
	t41_hw_check("timer");
	mod_timer(&t41_hw_timer, jiffies + 1);
}

void t41_heapwatch_start(void)
{
	struct t41_hw_mp *mp = (struct t41_hw_mp *)THIS_MODULE->mkobj.mp;

	if (t41_heap_watch <= 0 || !mp || !mp->grp.attrs)
		return;
	t41_hw_n = mp->num;
	t41_hw_mp_bytes = sizeof(*mp) + mp->num * sizeof(mp->attrs[0]);
	t41_hw_attrs_snap = kmalloc((t41_hw_n + 1) * sizeof(u32), GFP_KERNEL);
	t41_hw_mp_snap = kmalloc(t41_hw_mp_bytes, GFP_KERNEL);
	if (!t41_hw_attrs_snap || !t41_hw_mp_snap) {
		kfree(t41_hw_attrs_snap);
		kfree(t41_hw_mp_snap);
		return;
	}
	{
		/* Already corrupted before init finished?  The array is fully
		 * determined by the param_attribute block. */
		u32 *a = (u32 *)mp->grp.attrs;
		unsigned int i;
		int bad = 0;

		for (i = 0; i < t41_hw_n; ++i)
			if (a[i] != (u32)(uintptr_t)&mp->attrs[i].mattr.attr) {
				printk(KERN_ERR "t41-heapwatch: init: grp.attrs[%u] @%p = %08x, want %p\n",
				       i, &a[i], a[i], &mp->attrs[i].mattr.attr);
				a[i] = (u32)(uintptr_t)&mp->attrs[i].mattr.attr;
				bad++;
			}
		if (a[t41_hw_n]) {
			printk(KERN_ERR "t41-heapwatch: init: grp.attrs[%u] (end) @%p = %08x\n",
			       t41_hw_n, &a[t41_hw_n], a[t41_hw_n]);
			a[t41_hw_n] = 0;
			bad++;
		}
		if (bad) {
			t41_heap_watch_hits += bad;
			t41_hw_dump("init-attrs", a, -64, ((int)t41_hw_n + 1 + 64 + 7) & ~7);
		}
	}
	memcpy(t41_hw_attrs_snap, mp->grp.attrs, (t41_hw_n + 1) * sizeof(u32));
	memcpy(t41_hw_mp_snap, mp, t41_hw_mp_bytes);
	printk(KERN_WARNING "t41-heapwatch: armed n=%u attrs=%p (%zu B) mp=%p (%zu B)\n",
	       t41_hw_n, mp->grp.attrs, (t41_hw_n + 1) * sizeof(u32), mp,
	       t41_hw_mp_bytes);
	t41_hw_armed = true;
	setup_timer(&t41_hw_timer, t41_hw_tick, 0);
	mod_timer(&t41_hw_timer, jiffies + 1);
}

void t41_heapwatch_step(const char *where)
{
	t41_heapwatch_mark(where);
	t41_hw_check(where);
}

void t41_heapwatch_stop(void)
{
	if (!t41_hw_armed)
		return;
	del_timer_sync(&t41_hw_timer);
	t41_hw_check("stop");
	{
		struct t41_hw_mp *mp = (struct t41_hw_mp *)THIS_MODULE->mkobj.mp;
		u32 *a = (u32 *)mp->grp.attrs;
		unsigned int i;

		for (i = 0; i < t41_hw_n; ++i)
			if (a[i] != (u32)(uintptr_t)&mp->attrs[i].mattr.attr)
				printk(KERN_ERR "t41-heapwatch: stop: grp.attrs[%u] @%p = %08x, want %p\n",
				       i, &a[i], a[i], &mp->attrs[i].mattr.attr);
	}
	t41_hw_armed = false;
	kfree(t41_hw_attrs_snap);
	kfree(t41_hw_mp_snap);
	printk(KERN_WARNING "t41-heapwatch: stopped hits=%u attrs=%p\n", t41_heap_watch_hits,
	       ((struct t41_hw_mp *)THIS_MODULE->mkobj.mp)->grp.attrs);
}

static int t41_hw_census_get(char *buf, const struct kernel_param *kp)
{
	(void)kp;
	t41_hw_dma_census("read");
	return scnprintf(buf, PAGE_SIZE, "see dmesg\n");
}
static const struct kernel_param_ops t41_hw_census_ops = {
	.get = t41_hw_census_get,
};
module_param_cb(t41_heap_census, &t41_hw_census_ops, NULL, 0444);

