/*
 * Report-only watcher for the /proc/jz/isp entries.
 *
 * A soak run oopsed in proc_reg_unlocked_ioctl() (uClibc fopen() issues
 * TCGETS on every file it opens): pde->proc_fops->unlocked_ioctl was
 * 0x30886b47.  None of the driver's proc fops has an unlocked_ioctl, so
 * either a proc_dir_entry of /proc/jz/isp or one of the fops structs it
 * points to had been overwritten.  The same pass also printed the audio AIC
 * register dump, which only /proc/jz/audio/audio_aic_info produces, so the
 * directory's rbtree probably reached a foreign entry as well.
 *
 * This file snapshots the fields of every entry the driver creates under
 * /proc/jz/isp that never change after creation (inode number, mode, iops,
 * fops, parent, data, name) plus the fops struct contents. A timer compares
 * them every T41_PW_INTERVAL_MS, reports a change with the surrounding memory
 * (first T41_PW_REPORTS reports in full, afterwards only the counters) and
 * checks that the directory's rbtree links only point at entries the driver
 * created.  Nothing is ever written back to the kernel's structures: a
 * detected change is only accepted into the private snapshot, so one
 * corruption is counted once.
 *
 * Linked last in the module: its static data must not move the recovered
 * core's .bss/.data, which the core addresses past its named objects.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/rbtree.h>
#include <linux/completion.h>

static int t41_proc_watch = 1;
module_param(t41_proc_watch, int, 0444);
MODULE_PARM_DESC(t41_proc_watch,
		 "Watch (report only, no restore) the /proc/jz/isp entries every 50 ms (default 1)");
static unsigned int t41_proc_watch_hits;
module_param_named(proc_watch_hits, t41_proc_watch_hits, uint, 0444);
static unsigned int t41_proc_watch_rb_hits;
module_param_named(proc_watch_rb_hits, t41_proc_watch_rb_hits, uint, 0444);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0) && \
	LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0)

/* fs/proc/internal.h of Linux 4.4; offsets checked below. */
struct t41_pde {
	unsigned int low_ino;
	umode_t mode;
	nlink_t nlink;
	kuid_t uid;
	kgid_t gid;
	loff_t size;
	const struct inode_operations *proc_iops;
	const struct file_operations *proc_fops;
	struct t41_pde *parent;
	struct rb_root subdir;
	struct rb_node subdir_node;
	void *data;
	atomic_t count;
	atomic_t in_use;
	struct completion *pde_unload_completion;
	struct list_head pde_openers;
	spinlock_t pde_unload_lock;
	u8 namelen;
	char name[];
};

#define T41_PW_MAX	12
#define T41_PW_NAME	16
#define T41_PW_REPORTS	10
#define T41_PW_INTERVAL_MS	50

struct t41_pw_entry {
	struct t41_pde *pde;
	unsigned int low_ino;
	umode_t mode;
	const struct inode_operations *proc_iops;
	const struct file_operations *proc_fops;
	struct t41_pde *parent;
	void *data;
	u8 namelen;
	char name[T41_PW_NAME];
	bool rb_bad;
	struct file_operations fops;
};

static struct t41_pw_entry t41_pw[T41_PW_MAX];
static unsigned int t41_pw_n;
static unsigned int t41_pw_busy;
static unsigned int t41_pw_reports;
static bool t41_pw_armed;
static struct timer_list t41_pw_timer;
static DEFINE_SPINLOCK(t41_pw_lock);

static void t41_pw_dump(const char *what, const void *p, size_t len)
{
	const u32 *w = (const u32 *)((uintptr_t)p & ~(uintptr_t)3);
	size_t i;

	printk(KERN_ERR "t41-procwatch: %s @%p:\n", what, p);
	for (i = 0; i < (len + 3) / 4; i += 4)
		printk(KERN_ERR "t41-procwatch:  %p: %08x %08x %08x %08x\n",
		       &w[i], w[i], w[i + 1], w[i + 2], w[i + 3]);
}

static bool t41_pw_known_node(const struct rb_node *node)
{
	unsigned int i;

	if (!node)
		return true;
	for (i = 0; i < t41_pw_n; ++i)
		if (&t41_pw[i].pde->subdir_node == node)
			return true;
	return false;
}

static void t41_pw_report(const struct t41_pw_entry *e, const char *field,
			  const void *was, const void *now)
{
	t41_proc_watch_hits++;
	if (t41_pw_reports >= T41_PW_REPORTS)
		return;
	t41_pw_reports++;
	printk(KERN_ERR "t41-procwatch: /proc/jz/isp entry '%s' (pde %p): %s changed %p -> %p (not restored)\n",
	       e->name, e->pde, field, was, now);
	t41_pw_dump("pde", e->pde, sizeof(*e->pde) + T41_PW_NAME);
	if (e->proc_fops)
		t41_pw_dump("fops", e->proc_fops, sizeof(struct file_operations));
}

static void t41_pw_check_entry(struct t41_pw_entry *e)
{
	struct t41_pde *d = e->pde;

#define T41_PW_FIELD(f) do {						\
		if (d->f != e->f) {					\
			t41_pw_report(e, #f, (const void *)(uintptr_t)e->f, \
				      (const void *)(uintptr_t)d->f);	\
			e->f = d->f;					\
		}							\
	} while (0)
	T41_PW_FIELD(low_ino);
	T41_PW_FIELD(mode);
	T41_PW_FIELD(proc_iops);
	T41_PW_FIELD(proc_fops);
	T41_PW_FIELD(parent);
	T41_PW_FIELD(data);
	T41_PW_FIELD(namelen);
#undef T41_PW_FIELD
	if (memcmp(d->name, e->name, min_t(unsigned int, e->namelen, T41_PW_NAME - 1))) {
		t41_pw_report(e, "name", NULL, NULL);
		memcpy(e->name, d->name, min_t(unsigned int, e->namelen, T41_PW_NAME - 1));
	}
	if (e->proc_fops && memcmp(e->proc_fops, &e->fops, sizeof(e->fops))) {
		const u32 *now = (const u32 *)e->proc_fops;
		const u32 *was = (const u32 *)&e->fops;
		unsigned int i;

		for (i = 0; i < sizeof(e->fops) / 4; ++i)
			if (now[i] != was[i]) {
				t41_pw_report(e, "fops word",
					      (const void *)(uintptr_t)was[i],
					      (const void *)(uintptr_t)now[i]);
				if (t41_pw_reports <= T41_PW_REPORTS)
					printk(KERN_ERR "t41-procwatch: fops %p word +0x%02x\n",
					       e->proc_fops, i * 4);
			}
		/* accept as the new snapshot (private copy only) */
		memcpy(&e->fops, e->proc_fops, sizeof(e->fops));
	}
}

/* Children of the isp directory may only link to each other. */
static void t41_pw_check_tree(void)
{
	unsigned int i;

	for (i = 0; i < t41_pw_n; ++i) {
		struct t41_pde *d = t41_pw[i].pde;
		const struct rb_node *n = &d->subdir_node;
		bool bad;

		if (S_ISDIR(t41_pw[i].mode)) {
			bad = !t41_pw_known_node(d->subdir.rb_node);
		} else {
			bad = !t41_pw_known_node(n->rb_left) ||
			      !t41_pw_known_node(n->rb_right) ||
			      !t41_pw_known_node(rb_parent(n));
		}
		if (!bad) {
			t41_pw[i].rb_bad = false;
			continue;
		}
		if (t41_pw[i].rb_bad)
			continue;	/* already counted */
		t41_pw[i].rb_bad = true;
		t41_proc_watch_rb_hits++;
		if (t41_pw_reports >= T41_PW_REPORTS)
			continue;
		t41_pw_reports++;
		printk(KERN_ERR "t41-procwatch: /proc/jz/isp entry '%s' (pde %p) links outside the isp directory\n",
		       t41_pw[i].name, d);
		t41_pw_dump("pde", d, sizeof(*d) + T41_PW_NAME);
	}
}

static void t41_pw_tick(unsigned long unused)
{
	unsigned long flags;
	unsigned int i;

	spin_lock_irqsave(&t41_pw_lock, flags);
	if (!t41_pw_armed) {
		spin_unlock_irqrestore(&t41_pw_lock, flags);
		return;
	}
	for (i = 0; i < t41_pw_n; ++i)
		t41_pw_check_entry(&t41_pw[i]);
	if (!t41_pw_busy)
		t41_pw_check_tree();
	spin_unlock_irqrestore(&t41_pw_lock, flags);
	mod_timer(&t41_pw_timer, jiffies + msecs_to_jiffies(T41_PW_INTERVAL_MS));
}

/* Bracket proc_create_data()/proc_mkdir() so the rbtree check skips the
 * window in which the kernel has linked an entry not recorded yet. */
void t41_procwatch_begin(void)
{
	unsigned long flags;

	spin_lock_irqsave(&t41_pw_lock, flags);
	t41_pw_busy++;
	spin_unlock_irqrestore(&t41_pw_lock, flags);
}

void t41_procwatch_add(void *pde_ptr, const struct file_operations *fops)
{
	struct t41_pde *pde = pde_ptr;
	struct t41_pw_entry *e;
	unsigned long flags;

	BUILD_BUG_ON(sizeof(void *) == 4 &&
		     (offsetof(struct t41_pde, proc_fops) != 0x24 ||
		      offsetof(struct t41_pde, data) != 0x3c ||
		      offsetof(struct t41_pde, in_use) != 0x44));

	spin_lock_irqsave(&t41_pw_lock, flags);
	if (t41_pw_busy)
		t41_pw_busy--;
	if (!pde || t41_pw_n >= T41_PW_MAX || t41_proc_watch <= 0) {
		spin_unlock_irqrestore(&t41_pw_lock, flags);
		return;
	}
	e = &t41_pw[t41_pw_n];
	memset(e, 0, sizeof(*e));
	e->pde = pde;
	e->low_ino = pde->low_ino;
	e->mode = pde->mode;
	e->proc_iops = pde->proc_iops;
	e->proc_fops = pde->proc_fops;
	e->parent = pde->parent;
	e->data = pde->data;
	e->namelen = pde->namelen;
	memcpy(e->name, pde->name, min_t(unsigned int, pde->namelen + 1, T41_PW_NAME));
	e->name[T41_PW_NAME - 1] = 0;
	/* Directories carry the kernel's proc_dir_operations: watched too. */
	if (e->proc_fops)
		memcpy(&e->fops, e->proc_fops, sizeof(e->fops));
	(void)fops;
	t41_pw_n++;
	spin_unlock_irqrestore(&t41_pw_lock, flags);
}

void t41_procwatch_start(void)
{
	unsigned long flags;

	if (t41_proc_watch <= 0)
		return;
	spin_lock_irqsave(&t41_pw_lock, flags);
	if (t41_pw_armed || !t41_pw_n) {
		spin_unlock_irqrestore(&t41_pw_lock, flags);
		return;
	}
	t41_pw_armed = true;
	spin_unlock_irqrestore(&t41_pw_lock, flags);
	setup_timer(&t41_pw_timer, t41_pw_tick, 0);
	mod_timer(&t41_pw_timer, jiffies + msecs_to_jiffies(T41_PW_INTERVAL_MS));
}

/* Must run before the isp directory is removed. */
void t41_procwatch_stop(void)
{
	unsigned long flags;
	bool armed;

	spin_lock_irqsave(&t41_pw_lock, flags);
	armed = t41_pw_armed;
	t41_pw_armed = false;
	spin_unlock_irqrestore(&t41_pw_lock, flags);
	if (armed)
		del_timer_sync(&t41_pw_timer);
	spin_lock_irqsave(&t41_pw_lock, flags);
	t41_pw_n = 0;
	t41_pw_busy = 0;
	spin_unlock_irqrestore(&t41_pw_lock, flags);
}

#else

void t41_procwatch_begin(void) { }
void t41_procwatch_add(void *pde, const struct file_operations *fops) { }
void t41_procwatch_start(void) { }
void t41_procwatch_stop(void) { }

#endif
