// SPDX-License-Identifier: GPL-2.0
/*
 * T33 /proc/tisp debug view (openimp #589), see tx_isp_t33_tisp.h for the
 * vendor functions.  One sensor (VIC0 / S0), as the T33 driver runs.
 *
 * Differences from the vendor, named on purpose:
 *  - the bayer names cover the four RGB orders our front end accepts (the
 *    vendor also names the RGB-IR orders 0xc..0x13, which the T33 driver
 *    does not run);
 *  - "s0 cnt".."s3 cnt" (lib_tisp_rcfg_cfg_trig calls) stay 0: our register
 *    lists start through t33_rcfg_write_all / the module triggers, never
 *    through cfg_trig (the vendor's counts are 0 in a normal stream as well);
 *  - the counters start at module load (the vendor clears them in
 *    lib_tisp_restart / lib_tisp_aov_wake_up, which the T33 driver has not).
 */
#ifdef TX_ISP_T33
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>

#include "tx_isp_t33_tisp.h"

#define T33_TISP_VERSION	"isp_20260305a"	/* lib_tisp_debug_info: the T33 tisp id (as T32_ISP_VERSION_STR) */
#define STARS32	"********************************"

struct t33_tisp_cnt t33_tisp_cnt;

static DEFINE_MUTEX(t33_tisp_lock);	/* procLock */
static u8 t33_tisp_help;		/* showHelp */
static u8 t33_tisp_level = 1;		/* printLevels (vendor .data: 1) */
static struct {
	u32 width, height, order, depth, direct, dbus, valid;
} t33_tisp_par;

void t33_tisp_set_par(u32 width, u32 height, u32 order, u32 depth, u32 direct, u32 dbus)
{
	t33_tisp_par.width = width;
	t33_tisp_par.height = height;
	t33_tisp_par.order = order;
	t33_tisp_par.depth = depth;
	t33_tisp_par.direct = direct;
	t33_tisp_par.dbus = dbus;
	t33_tisp_par.valid = 1;
}

static const char *t33_tisp_bayer(u32 b)
{
	static const char *const n[] = { "RGGB", "BGGR", "GRBG", "GBRG" };

	return b < 4 ? n[b] : "ERROR";
}

/* tisp_isp_par_in_info */
static void t33_tisp_par_in(struct seq_file *m)
{
	if (!t33_tisp_par.valid)
		return;
	seq_printf(m, STARS32 " TISP S0 BASIC INFO **************************************\n");
	seq_printf(m, "%9s%9s%16s%16s\n", "width", "height", "bayer", "directMode");
	seq_printf(m, "%9d%9d%16s%16d\n", t33_tisp_par.width & 0xffff, t33_tisp_par.height & 0xffff,
		   t33_tisp_bayer(t33_tisp_par.order), t33_tisp_par.direct & 0xff);
	seq_printf(m, "\n");
}

/* tisp_vic_debug_error_intp_info */
static void t33_tisp_vic_err(struct seq_file *m)
{
	const u32 *v = t33_tisp_cnt.vic[0];

	seq_printf(m, STARS32 " TISP VIC ERR INTP INFO ***********************************\n");
	seq_printf(m, "**** VIC%d ERR0 INTP INFO ****\n", 0);
	seq_printf(m, "%11s%11s%11s%11s%11s%11s%11s%11s\n", "errNum", "fifoOvf", "errHvf", "errDvpHcom",
		   "dmaFifoOvf", "errCtlLim", "errLineBuff", "mipiFidOvf");
	seq_printf(m, "%11d%11d%11d%11d%11d%11d%11d%11d\n", t33_tisp_cnt.vic_err[0], v[12], v[21], v[22],
		   v[23], v[24], v[25], v[26]);
	seq_printf(m, "**** VIC%d ERR1 INTP INFO ****\n", 0);
	seq_printf(m, "%11s%11s%11s%11s%11s%11s%11s%11s\n", "errCh0W", "errCh1W", "errCh2W", "errCh3W",
		   "errCh0H", "errCh1H", "errCh2H", "errCh3H");
	seq_printf(m, "%11d%11d%11d%11d%11d%11d%11d%11d\n", v[13], v[14], v[15], v[16], v[17], v[18], v[19],
		   v[20]);
	seq_printf(m, "\n");
}

/* tisp_isp_debug_error_intp_info */
static void t33_tisp_isp_err(struct seq_file *m)
{
	const struct t33_tisp_cnt *c = &t33_tisp_cnt;
	const u32 *e = c->err;

	seq_printf(m, STARS32 " TISP ISP ERR INTP INFO ***********************************\n");
	seq_printf(m, "**** ERR INTP INFO ****\n");
	seq_printf(m, "%9s%9s%12s%12s%12s%12s%12s%12s%12s%12s\n", "errorNum", "value", "errCh0Crop",
		   "errCh1Crop", "errCh2Crop", "errFraStart", "errFmSize", "errOverflow", "errIpcComp",
		   "errMdnsComp");
	seq_printf(m, "%9d%9d%12d%12d%12d%12d%12d%12d%12d%12d\n", c->err_num, c->err_value, e[0], e[1], e[2],
		   e[3], e[4], e[5], e[11], e[15]);
	seq_printf(m, "%9s%9s%12s%12s%12s%12s%12s%12s%12s\n", "errAwbSta", "errAeSta", "errAeHist", "errAfSta",
		   "errOdvp", "errLsfAuto", "errMdnsComp[1]", "errMdnsComp[2]", "errDmacLast");
	seq_printf(m, "%9d%9d%12d%12d%12d%12d%12d%12d%12d\n", e[6], e[7], e[8], e[9], e[10], e[12], e[16],
		   e[17], e[24]);
	seq_printf(m, "\n");
}

/* tisp_vic_debug_intp_static_info */
static void t33_tisp_vic_sta(struct seq_file *m)
{
	const u32 *v = t33_tisp_cnt.vic[0];

	seq_printf(m, STARS32 " TISP VIC INTP STA INFO ***********************************\n");
	seq_printf(m, "**** VIC%d INTP INFO ****\n", 0);
	seq_printf(m, "%11s%11s%11s%11s%11s%11s%11s%11s\n", "ch0 done", "ch1 done", "ch0 start", "ch1 start",
		   "dma0 done", "dma1 done", "dma2 done", "dma3 done");
	seq_printf(m, "%11d%11d%11d%11d%11d%11d%11d%11d\n", v[0], v[1], v[4], v[5], v[8], v[9], v[10], v[11]);
	seq_printf(m, "\n");
}

/* tisp_isp_debug_intp_static_info */
static void t33_tisp_isp_sta(struct seq_file *m)
{
	const struct t33_tisp_cnt *c = &t33_tisp_cnt;
	const u32 *s = c->stat, *b = c->back0, *r0 = c->rcfg0;

	seq_printf(m, STARS32 " TISP ISP INTP STA INFO ***********************************\n");
	seq_printf(m, "**** ISP INTP INFO ****\n");
	seq_printf(m, "%9s%9s%9s%9s\n", "frameNum", "ch0 Done", "ch1 Done", "ch2 Done");
	seq_printf(m, "%9d%9d%9d%9d\n", c->frame_num, c->comm1[0], c->comm1[1], c->comm1[2]);
	seq_printf(m, "**** ISP STAT INTP INFO ****\n");
	seq_printf(m, "%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s\n", "AWB", "AE_S", "AE_H", "AF", "GSM", "LCE", "TMO",
		   "DEFOG", "LSF", "FBF");
	seq_printf(m, "%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d\n", s[3], s[4], s[5], s[6], s[7], s[8], s[9], s[10],
		   s[30], s[2]);
	seq_printf(m, "**** MODULE DONE INFO ****\n");
	seq_printf(m, "%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s\n", "ipc start", "ipc done", "tpraw", "lsc", "awbg",
		   "dpc", "rdns", "blc", "dg", "tmo", "dmsc");
	seq_printf(m, "%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d\n", b[0], b[27], b[2], b[4], b[5], b[6], b[7], b[8],
		   b[9], b[10], b[11]);
	seq_printf(m, "%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s\n", "trgb", "ccm", "gamma", "defog", "csc", "tpyuv",
		   "mdns", "ydns", "bcsh", "clm", "ysp");
	seq_printf(m, "%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d\n", b[12], b[13], b[14], b[15], b[16], b[17], b[18],
		   b[19], b[20], b[21], b[22]);
	seq_printf(m, "%9s%9s%9s%9s%9s%9s%9s%9s%9s\n", "sdns", "cdns", "csccr", "lce", "awb", "ae", "af", "gsm",
		   "msca");
	seq_printf(m, "%9d%9d%9d%9d%9d%9d%9d%9d%9d\n", b[23], b[24], b[25], b[26], b[28], b[29], b[30], b[31],
		   b[1]);
	seq_printf(m, "**** ISP RCFG INFO ****\n");
	seq_printf(m, "%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s\n", "all", "dpc", "rdns", "blc", "dg", "lsc", "awbg",
		   "tmo", "dmsc", "ccm", "gamma");
	seq_printf(m, "%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d\n", c->rcfg1[1], r0[2], r0[3], r0[4], r0[5], r0[6],
		   r0[7], r0[8], r0[9], r0[10], r0[11]);
	seq_printf(m, "%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s\n", "defog", "csc", "ysp", "mdns", "lce", "ydns",
		   "bcsh", "clm", "sdns", "cdns", "csccr");
	seq_printf(m, "%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d\n", r0[12], r0[13], r0[14], r0[15], r0[16], r0[17],
		   r0[18], r0[19], r0[20], r0[21], r0[22]);
	seq_printf(m, "%9s%9s%9s%9s%9s%9s%9s%9s\n", "msca", "ldc", "ch0", "ch1", "awb", "ae", "af", "lsf");
	seq_printf(m, "%9d%9d%9d%9d%9d%9d%9d%9d\n", r0[23], r0[25], r0[26], r0[27], r0[29], r0[30], r0[31],
		   c->rcfg1[5]);
	seq_printf(m, "%9s%9s%9s%9s\n", "s0 cnt", "s1 cnt", "s2 cnt", "s3 cnt");
	seq_printf(m, "%9d%9d%9d%9d\n", c->trig[0], c->trig[1], c->trig[2], c->trig[3]);
	seq_printf(m, "\n");
}

/* tisp_isp_ldc_debug_info */
static void t33_tisp_ldc(struct seq_file *m)
{
	u32 done = 0, err = 0;

	t33_tisp_ldc_info(&done, &err);
	seq_printf(m, STARS32 " TISP LDC INTP INFO **************************************\n");
	seq_printf(m, "%6s%6s\n", "NUM", "ERROR");
	seq_printf(m, "%6d%6d\n", done, err);
}

/* tisp_isp_top_debug_info: the bypass bytes of stTopPar */
static void t33_tisp_top(struct seq_file *m)
{
	const u8 *t = t33_tisp_top_par();

	if (!t)
		return;
	seq_printf(m, STARS32 " TISP MODULE TOP INFO ************************************\n");
	seq_printf(m, STARS32 " S%d BYPASS INFO ******************************************\n", 0);
	seq_printf(m, "%6s%6s%6s%6s%6s%6s%6s%6s%6s%6s\n", "DPC", "BLC", "DG", "LSC", "AWBG", "TMO", "DMSC",
		   "CCM", "GAMMA", "DEFOG");
	seq_printf(m, "%6d%6d%6d%6d%6d%6d%6d%6d%6d%6d\n", t[0xc], t[0xd], t[0xe], t[0xf], t[0x10], t[0x11],
		   t[0x12], t[0x13], t[0x14], t[0x15]);
	seq_printf(m, "%6s%6s%6s%6s%6s%6s%6s%6s%6s%6s%6s\n", "CSC", "LCE", "MDNS", "YDNS", "BCSH", "CLM", "YSP",
		   "SDNS", "CDNS", "CSCCR", "RDNS");
	seq_printf(m, "%6d%6d%6d%6d%6d%6d%6d%6d%6d%6d%6d\n", t[0x16], t[0x17], t[0x18], t[0x19], t[0x1a],
		   t[0x1b], t[0x1c], t[0x1d], t[0x1e], t[0x1f], t[0x23]);
	seq_printf(m, "\n");
}

/* lib_tisp_vic_debug_info */
static void t33_tisp_vic_info(struct seq_file *m)
{
	static const char *const fmt[] = { "raw8", "raw10", "raw12" };

	if (!t33_tisp_par.valid)
		return;
	seq_printf(m, STARS32 " TISP VIC%d INFO *******************************************\n", 0);
	seq_printf(m, "%8s%8s%8s%8s\n", "width", "height", "type", "format");
	/* sensor dbus 1 = MIPI, 2 = DVP (the vendor VIC cache: 2 = mipi, 1 = dvp) */
	seq_printf(m, "%8d%8d%8s%8s\n", t33_tisp_par.width & 0xffff, t33_tisp_par.height & 0xffff,
		   t33_tisp_par.dbus == 1 ? "mipi" : t33_tisp_par.dbus == 2 ? "dvp" : "other",
		   t33_tisp_par.depth < 3 ? fmt[t33_tisp_par.depth] : "other");
	seq_printf(m, "\n");
}

/* lib_tisp_debug_info */
static void t33_tisp_info(struct seq_file *m)
{
	const struct t33_tisp_cnt *c = &t33_tisp_cnt;
	const u32 *s = c->stat;

	seq_printf(m, STARS32 " TISP INFO ***********************************************\n");
	seq_printf(m, "%14s \n", "version");
	seq_printf(m, "%14s \n", T33_TISP_VERSION);
	seq_printf(m, "\n");
	seq_printf(m, STARS32 " TISP INTP INFO ******************************************\n");
	seq_printf(m, "%9s%9s%9s%9s%9s%9s%9s%9s%9s%9s\n", "frameNum", "AWB", "AE_S", "AE_H", "AF", "GSM", "LCE",
		   "TMO", "DEFOG", "LSF");
	seq_printf(m, "%9d%9d%9d%9d%9d%9d%9d%9d%9d%9d\n", c->frame_num, s[3], s[4], s[5], s[6], s[7], s[8],
		   s[9], s[10], s[30]);
	seq_printf(m, "\n");
	seq_printf(m, STARS32 " TISP BACK0 INFO *****************************************\n");
	seq_printf(m, "%9s%9s%9s\n", "csccrDone", "mscaDone", "ch0Done");
	seq_printf(m, "%9d%9d%9d\n", c->back0[25], c->back0[1], c->comm1[0]);
	seq_printf(m, "\n");
	seq_printf(m, STARS32 " TISP INTP1 INFO *****************************************\n");
	seq_printf(m, "%9s%9s\n", "wdma", "rdma");
	seq_printf(m, "%9d%9d\n", c->dma[0], c->dma[1]);
	seq_printf(m, "\n");
	if (!t33_tisp_par.valid)
		return;
	seq_printf(m, STARS32 " TISP SENSOR%d INFO ****************************************\n", 0);
	seq_printf(m, "%9s%9s\n", "width", "height");
	seq_printf(m, "%9d%9d\n", t33_tisp_par.width & 0xffff, t33_tisp_par.height & 0xffff);
	seq_printf(m, "\n");
}

/* tisp_show_debug_info */
static int t33_tisp_show(struct seq_file *m, void *v)
{
	mutex_lock(&t33_tisp_lock);
	if (t33_tisp_help) {
		seq_printf(m, "===== TISP log information level selection =====\n");
		seq_printf(m, "0: ISP basic message\n");
		seq_printf(m, "1: ISP error message\n");
		seq_printf(m, "2: ISP static message\n");
		seq_printf(m, "3: sensor  message\n");
		seq_printf(m, "4: other   message\n");
		seq_printf(m, "==== Current mode %d ====\n", t33_tisp_level);
		seq_printf(m, "Write 'echo n > /proc/tisp' to switch mode.\n");
		t33_tisp_help = 0;
	} else if (t33_tisp_level == 0) {
		t33_tisp_par_in(m);
	} else if (t33_tisp_level == 1) {
		t33_tisp_vic_err(m);
		t33_tisp_isp_err(m);
		t33_tisp_vic_sta(m);
		t33_tisp_isp_sta(m);
		t33_tisp_ldc(m);
	} else if (t33_tisp_level == 3) {
		t33_tisp_top(m);
	} else if (t33_tisp_level == 4) {
		t33_tisp_vic_info(m);
		t33_tisp_info(m);
	}
	mutex_unlock(&t33_tisp_lock);
	return 0;
}

/* tisp_write_proc */
static ssize_t t33_tisp_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char buf[256];
	size_t n = count < 256 ? count : 255;
	int val;

	memset(buf, 0, sizeof(buf));
	if (copy_from_user(buf, ubuf, n))
		return -EFAULT;
	mutex_lock(&t33_tisp_lock);
	if (!strncmp(buf, "help", 4)) {
		t33_tisp_help = 1;
	} else if (kstrtoint(buf, 10, &val)) {
		mutex_unlock(&t33_tisp_lock);
		return -EINVAL;
	} else if ((u32)val < 5) {
		t33_tisp_level = (u8)val;
	}
	mutex_unlock(&t33_tisp_lock);
	return count;
}

static int t33_tisp_open(struct inode *inode, struct file *file)
{
	return single_open(file, t33_tisp_show, NULL);
}

static const struct file_operations t33_tisp_fops = {
	.owner = THIS_MODULE,
	.open = t33_tisp_open,
	.read = seq_read,
	.write = t33_tisp_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static struct proc_dir_entry *t33_tisp_pde;

int t33_tisp_proc_init(void)
{
	t33_tisp_pde = proc_create("tisp", 0444, NULL, &t33_tisp_fops);
	return t33_tisp_pde ? 0 : -ENOMEM;
}

void t33_tisp_proc_exit(void)
{
	if (t33_tisp_pde)
		remove_proc_entry("tisp", NULL);
	t33_tisp_pde = NULL;
}
#endif
