/*
 * T41 RAW inject: feed a synthetic Bayer frame from DDR through the whole
 * ISP pipeline (RAW RDMA, OEM "raw row control", tuning ioctl 0x80000a9).
 *
 * The T41 ISP core has a RAW read-DMA (register block 0x21000) and a RAW
 * write-DMA (0x20000).  The OEM driver uses them for its raw ring / raw
 * replay (tisp_s_raw_rw_control) and for a single-frame "DDR -> ISP"
 * injection (tisp_s_raw_row_control mode 1 -> tisp_ddr_to_isp_raw):
 *
 *   0x801a8 = 0x4444      input mux: ISP front end fed from RDMA (1 s settle)
 *   0x21004 = 1           RDMA enable (1 s settle, first time only)
 *   0x21024 = dtype       DMA data type (OEM default 0x20)
 *   0x21008 = lines       frame height (+6 with the two-frame/WDR flag)
 *   0x21014 = w*lines     frame size
 *   0x2100c = paddr       frame address (0x21010/0x21018: second frame)
 *   -> tisp_raw_rdma_fd_interrupt (ISP irq 69) completes rpars+16
 *
 * Interface (proof of concept, local test tool, not for release images):
 *   /proc/t41_rawinject   text:  "alloc <bytes>" | "inject [lines] [dtype]"
 *                                | "stop" | "free";  read = status + paddr
 *   /proc/t41_rawbuf      binary: pwrite/pread the frame buffer (uncached)
 *
 * Host side: tools/rawchart/ generates the Bayer chart and evaluates the
 * encoder/snapshot output.
 */
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/kernel.h>

extern int32_t isp_malloc_buffer(int32_t size);
extern int isp_free_buffer(int32_t addr);
extern uint32_t t41_rawinject_par(unsigned int idx);	/* tisp_par_info[idx] */
extern uint32_t t41_rawinject_rpars(void);		/* OEM raw-DMA state, 0 = not initialised */
extern int32_t tisp_raw_init(uint32_t channel);
#include <linux/dma-mapping.h>

static dma_addr_t rj_dma;	/* set when the buffer came from dma_alloc_coherent */
static void *rj_dma_virt;
static uint32_t rj_prealloc = 10 << 20;	/* at load time, before memory fragments */
module_param_named(t41_rawinject_mb, rj_prealloc, uint, 0444);

static uint32_t rj_paddr;
static uint32_t rj_size;
static void __iomem *rj_virt;
static int rj_active;
static int rj_frames;

static int rj_alloc(uint32_t bytes)
{
	if (rj_paddr && bytes <= rj_size)
		return 0;	/* preallocated at load time */
	if (rj_paddr)
		return -EBUSY;
	bytes = (bytes + 0xfff) & ~0xfffU;
	rj_dma_virt = dma_alloc_coherent(NULL, bytes, &rj_dma, GFP_KERNEL);
	if (rj_dma_virt) {
		rj_paddr = (uint32_t)rj_dma;
		rj_virt = (void __iomem *)rj_dma_virt;
		rj_size = bytes;
		pr_info("t41_rawinject: dma buffer paddr=0x%08x size=%u\n", rj_paddr, rj_size);
		return 0;
	}
	/* fallback: ISP private pool (needs ispmem=, not set on thingino) */
	rj_paddr = isp_malloc_buffer(bytes);
	if (!rj_paddr) {
		pr_err("t41_rawinject: isp_malloc_buffer(%u) failed (ispmem/rmem?)\n", bytes);
		return -ENOMEM;
	}
	rj_virt = ioremap_nocache(rj_paddr, bytes);
	if (!rj_virt) {
		isp_free_buffer(rj_paddr);
		rj_paddr = 0;
		return -ENOMEM;
	}
	rj_size = bytes;
	pr_info("t41_rawinject: buffer paddr=0x%08x size=%u\n", rj_paddr, rj_size);
	return 0;
}

static void rj_free(void)
{
	if (rj_dma_virt) {
		dma_free_coherent(NULL, rj_size, rj_dma_virt, rj_dma);
		rj_dma_virt = NULL;
	} else if (rj_virt) {
		iounmap(rj_virt);
		if (rj_paddr)
			isp_free_buffer(rj_paddr);
	}
	rj_virt = NULL;
	rj_paddr = 0;
	rj_size = 0;
}

/* user struct of tuning ioctl 0x80000a9 (9 words):
 * [0] mode (0 off, 1 DDR->ISP, 2 ISP->DDR)
 * [1..4] rdma: addr, enable, lines, dtype(0 -> 0x20)
 * [5..8] wdma: addr, enable, lines, dtype(0 -> 0x20) */
extern int32_t system_reg_write(uint32_t addr, uint32_t val);
extern int32_t system_reg_read(int32_t addr);
#include <linux/completion.h>
#include <linux/delay.h>

/* rpars (OEM, 48 bytes): [1] rw_en [2] rdma_en [3] wdma_en, +16 rdma done
 * completion, +32 wdma done completion. */
#define RP_RW_EN(rp)   (((uint32_t *)(rp))[1])
#define RP_RDMA_EN(rp) (((uint32_t *)(rp))[2])
#define RP_RDMA_DONE(rp) ((struct completion *)((char *)(rp) + 16))

static int rj_prepare_rpars(void)
{
	int ret;

	if (t41_rawinject_rpars())
		return 0;
	ret = tisp_raw_init(0);	/* OEM init-list entry did not run: lazy init */
	pr_info("t41_rawinject: lazy tisp_raw_init ret=%d rpars=%08x\n", ret, t41_rawinject_rpars());
	return t41_rawinject_rpars() ? 0 : -ENODEV;
}

/* OEM tisp_ddr_to_isp_raw(ctrl) with ctrl = {w, _, flag=0, paddr, _, lines, dtype},
 * written out plainly (the recovered copy in tx_isp_t41_recovered.c scales
 * the register offsets by 4 and gets no ctrl pointer from
 * tisp_raw_set_row_control - do not use it). */
static int rj_inject(uint32_t lines, uint32_t dtype)
{
	uint32_t w = t41_rawinject_par(0), h = t41_rawinject_par(1);
	void *rp;
	long t;

	if (!rj_paddr)
		return -ENOENT;
	if (!w || !h) {
		pr_err("t41_rawinject: ISP not initialised (sensor %ux%u)\n", w, h);
		return -ENODEV;
	}
	if (rj_prepare_rpars())
		return -ENODEV;
	rp = (void *)(uintptr_t)t41_rawinject_rpars();
	if (!lines)
		lines = h;
	if (!(dtype & 0xff))
		dtype |= 0x20;
	if (w * lines * 2 > rj_size)
		pr_warn("t41_rawinject: buffer %u < %ux%u x2 bytes\n", rj_size, w, lines);
	pr_info("t41_rawinject: inject paddr=0x%08x %ux%u lines=%u dtype=0x%x mux=0x%x\n",
		rj_paddr, w, h, lines, dtype, system_reg_read(0x801a8));
	if (RP_RW_EN(rp) == 0 && !rj_active) {
		system_reg_write(0x801a8, 0x4444);	/* front end input <- RDMA */
		msleep(1000);
	}
	if (RP_RDMA_EN(rp) == 0) {
		system_reg_write(0x21004, 1);		/* RDMA enable */
		msleep(1000);
	}
	system_reg_write(0x21024, dtype & 0xff);
	system_reg_write(0x21008, lines);
	system_reg_write(0x21014, w * lines);
	system_reg_write(0x2100c, rj_paddr);
	RP_RDMA_EN(rp) = 1;
	rj_active = 1;
	/* OEM raw rw-control ends its enable sequence with the mux "run" value;
	 * the plain single-shot path left the front end at 0x4444 and never
	 * produced an RDMA frame-done (run a3), so kick it here. */
	if (dtype & 0x100)
		system_reg_write(0x801a8, 0x4404);
	dtype &= 0xff;
	t = wait_for_completion_interruptible_timeout(RP_RDMA_DONE(rp), 2 * HZ);
	rj_frames++;
	pr_info("t41_rawinject: rdma done wait=%ld (0 = timeout) frames=%d\n", t, rj_frames);
	return 0;
}

/* OEM tisp_raw_set_row_control mode 0 */
static int rj_stop(void)
{
	void *rp = (void *)(uintptr_t)t41_rawinject_rpars();

	if (!rj_active || !rp)
		return 0;
	msleep(100);
	system_reg_write(0x21024, 0);
	system_reg_write(0x801a8, 0x4404);	/* front end input <- VIC */
	RP_RDMA_EN(rp) = 0;
	RP_RW_EN(rp) = 0;
	rj_active = 0;
	pr_info("t41_rawinject: stopped, mux=0x%x\n", system_reg_read(0x801a8));
	return 0;
}

static ssize_t rj_ctl_write(struct file *f, const char __user *ub, size_t n, loff_t *pos)
{
	char cmd[64];
	unsigned int a = 0, b = 0;
	int ret = 0;

	if (n >= sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, ub, n))
		return -EFAULT;
	cmd[n] = 0;
	if (!strncmp(cmd, "alloc", 5)) {
		if (sscanf(cmd + 5, "%u", &a) != 1)
			return -EINVAL;
		ret = rj_alloc(a);
	} else if (!strncmp(cmd, "inject", 6)) {
		sscanf(cmd + 6, "%u %u", &a, &b);
		ret = rj_inject(a, b);
	} else if (!strncmp(cmd, "stop", 4)) {
		ret = rj_stop();
	} else if (!strncmp(cmd, "free", 4)) {
		if (rj_active)
			rj_stop();
		rj_free();
	} else {
		return -EINVAL;
	}
	return ret ? ret : n;
}

static int rj_ctl_show(struct seq_file *m, void *v)
{
	seq_printf(m, "paddr=0x%08x size=%u active=%d frames=%d sensor=%ux%u\n",
		   rj_paddr, rj_size, rj_active, rj_frames,
		   t41_rawinject_par(0), t41_rawinject_par(1));
	return 0;
}

static int rj_ctl_open(struct inode *i, struct file *f)
{
	return single_open(f, rj_ctl_show, NULL);
}

static const struct file_operations rj_ctl_fops = {
	.owner = THIS_MODULE,
	.open = rj_ctl_open,
	.read = seq_read,
	.write = rj_ctl_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static ssize_t rj_buf_write(struct file *f, const char __user *ub, size_t n, loff_t *pos)
{
	size_t off = *pos;
	char *tmp;
	size_t chunk, done = 0;

	if (!rj_virt)
		return -ENOENT;
	if (off >= rj_size)
		return -ENOSPC;
	if (n > rj_size - off)
		n = rj_size - off;
	tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	while (done < n) {
		chunk = min_t(size_t, PAGE_SIZE, n - done);
		if (copy_from_user(tmp, ub + done, chunk)) {
			kfree(tmp);
			return done ? done : -EFAULT;
		}
		memcpy_toio((char __iomem *)rj_virt + off + done, tmp, chunk);
		done += chunk;
	}
	kfree(tmp);
	*pos = off + done;
	return done;
}

static ssize_t rj_buf_read(struct file *f, char __user *ub, size_t n, loff_t *pos)
{
	size_t off = *pos;
	char *tmp;
	size_t chunk, done = 0;

	if (!rj_virt)
		return -ENOENT;
	if (off >= rj_size)
		return 0;
	if (n > rj_size - off)
		n = rj_size - off;
	tmp = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;
	while (done < n) {
		chunk = min_t(size_t, PAGE_SIZE, n - done);
		memcpy_fromio(tmp, (char __iomem *)rj_virt + off + done, chunk);
		if (copy_to_user(ub + done, tmp, chunk)) {
			kfree(tmp);
			return done ? done : -EFAULT;
		}
		done += chunk;
	}
	kfree(tmp);
	*pos = off + done;
	return done;
}

static const struct file_operations rj_buf_fops = {
	.owner = THIS_MODULE,
	.read = rj_buf_read,
	.write = rj_buf_write,
	.llseek = default_llseek,
};

static struct proc_dir_entry *rj_ctl_pde, *rj_buf_pde;

int t41_rawinject_init(void)
{
	if (rj_prealloc)
		rj_alloc(rj_prealloc);
	rj_ctl_pde = proc_create("t41_rawinject", 0644, NULL, &rj_ctl_fops);
	rj_buf_pde = proc_create("t41_rawbuf", 0644, NULL, &rj_buf_fops);
	return 0;
}

void t41_rawinject_exit(void)
{
	if (rj_active)
		rj_stop();
	rj_free();
	if (rj_ctl_pde)
		remove_proc_entry("t41_rawinject", NULL);
	if (rj_buf_pde)
		remove_proc_entry("t41_rawbuf", NULL);
	rj_ctl_pde = rj_buf_pde = NULL;
}
