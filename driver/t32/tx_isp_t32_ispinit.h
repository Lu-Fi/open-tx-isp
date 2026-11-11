/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T32 ISP core glue, part 24: ispcore_core_ops_init (0xb9bbc), the core subdev's init(sd, req)
 * with req = { u32 enable, u32 pipe } (sd + 280 = core private block "priv").
 *
 *  enable: per pipe state priv + 300 + 4 * pipe must be 2 (checked under the lock priv + 288;
 *    1 = nothing to do).  A 904 B tuning start block is built (vmalloc): sensor size / format
 *    code / bus type / image size / name of the pipe's sensor (priv + 324 + 104 * pipe ..
 *    + 392), memopt, the sensor's answer to command 0x5000007 (sd + 152), the sensor attribute
 *    word priv + 1196, the dual / tuning flags d9014 / d9004 and, without d9008, the four tuning
 *    bin names (priv + 1844 + 544 * pipe + 136 * i, or "<fmt i>" with the sensor name).  The
 *    sensor size is written into the format table users (priv + 1132, 284 B entries).  Then
 *    lib_tisp_init, tisp_process_init (pipe 0 state 2), the PM event thread (d9004) and the
 *    pipe's firmware thread isp_fw_process[pipe]; state 3, priv + 748 = -9.
 *  disable: stream off (state 4), state 3 -> 2, tisp_process_deinit, threads stopped,
 *    lib_tisp_deinit, the pipe's bin names cleared, priv + 1196 = 1, + 1200 = 0.
 *
 * The media bus code (priv + 332 + 104 * pipe) maps to the tuning format: 0x5000..3 / 0x5100..3
 * -> 0x30000; 0x5200..0x520b -> 0x10000 * ((c - 0x5200) / 4) + { 1, 3, 2, 0 }[c & 3];
 * 0x5300..0x532f -> 0x10000 * ((c - 0x5300) / 16) + 4 + (c & 15); else -1 (printed twice).
 *
 * Vendor behaviour kept: a failed vmalloc is only printed (memset of NULL follows); the bin
 * names copied from snprintf carry the stack bytes after the string (128 B copy); the sensor
 * name is copied without a bound into the 16 B field + 20; pipe 0 state priv + 300 (not the
 * pipe's) decides tisp_process_init; the -22 / -1 returns of the error paths.
 *
 * Checked against the vendor function (Matt Davis' recovered C, PR #15) by
 * tests/t32_oracle (t32_ispinit_oracle).
 */
#ifndef TX_ISP_T32_ISPINIT_H
#define TX_ISP_T32_ISPINIT_H

#include "tx_isp_t32_isppad.h"

#define T32_CI_LOCK		288
#define T32_CI_STATE		300	/* + 4 * pipe */
#define T32_CI_NINIT		316
#define T32_CI_NDEINIT		320
#define T32_CI_PIPE		324	/* + 104 * pipe: w, h, code (+ 8), name (+ 52), sensor (+ 56), + 60 / 64 / 68 */
#define T32_CI_PIPE_SIZE	104
#define T32_CI_EVWAIT		748
#define T32_CI_DONE		1064	/* + 12 * pipe */
#define T32_CI_W		1116	/* u16 */
#define T32_CI_H		1118	/* u16 */
#define T32_CI_FMT_IDX		1128
#define T32_CI_USERS		1132
#define T32_CI_NUSERS		1136
#define T32_CI_USER_SIZE	284
#define T32_CI_MODE		1164	/* + 4 * pipe, 10 = extra flag */
#define T32_CI_BUS		1180	/* + 4 * pipe */
#define T32_CI_SATTR		1196
#define T32_CI_SATTR2		1200
#define T32_CI_FWTHREAD		1688	/* + 4 * pipe */
#define T32_CI_BINS		1836	/* + 544 * pipe: 4 x 136 B, names at + 8 */
#define T32_CI_BINS_SIZE	544
#define T32_CI_BIN_SIZE		136
#define T32_CI_STARTED		4016	/* u8 */
#define T32_CI_SD_NAME		8
#define T32_CI_SD_SENSOR_IOCTL	152
#define T32_CI_CMD		0x05000007U
#define T32_CI_ATTR_SIZE	904
#define T32_CI_ERR(p)		((p) >= 0xfffff001U)

/* isp_printf formats (index into t32_ci_glob.fmt) */
enum { T32_CI_P_NOMEM, T32_CI_P_RESET, T32_CI_P_STATE, T32_CI_P_SIZE, T32_CI_P_CODE, T32_CI_P_SATTR,
       T32_CI_P_BIN, T32_CI_P_LIB, T32_CI_P_PROC, T32_CI_P_PMT, T32_CI_P_THREAD, T32_CI_P_STOP, T32_CI_P_N };

struct t32_ci_glob {
	u8 *first;		/* f334c */
	u32 *npipe;		/* f3354 */
	u8 *done;		/* f335c */
	u8 *pdone;		/* f3358 [4] */
	u32 *dual;		/* d9014 */
	u32 *tuning;		/* d9004 */
	u32 *nobin;		/* d9008 */
	u32 *pm;		/* f332c [4]: thread, ..., + 12 priv */
	u32 fmts;		/* d8ce0 */
	u32 fmt[T32_CI_P_N];
	u32 func, func_code;	/* function names c36ac, c3678 */
	u32 bin_fmt[4];		/* cd1cc cd1ec cd20c cd230 */
	u32 pm_name, fw_name[4];	/* cd2ac, cd300 cd310 cd320 cd330 */
	u32 pm_fn, fw_fn[4];	/* pm_core_event_process_thread, isp_fw_process[0..3] */
};

struct t32_ci_ops {
	void (*print)(u32 level, u32 fmt, u32 a2, u32 a3, u32 s16, u32 s20, u32 s24);	/* isp_printf */
	u32 (*memopt)(void);					/* get_isp_memopt */
	u32 (*vmalloc)(u32 size);
	void (*vfree)(u32 p);
	u32 (*reset)(u32 a);					/* private_reset_tx_isp_module */
	void (*lock)(u32 lock, u32 *flags);			/* __private_spin_lock_irqsave */
	void (*unlock)(u32 lock, u32 flags);			/* private_spin_unlock_irqrestore */
	u32 (*call)(u32 fn, u32 sd, u32 cmd, u32 *arg);		/* sensor ioctl sd + 152 */
	u32 (*lib_init)(u32 bus, u32 attr);			/* lib_tisp_init */
	void (*lib_deinit)(u32 pipe);				/* lib_tisp_deinit */
	void (*proc_init)(void);				/* tisp_process_init */
	void (*proc_deinit)(u32 pipe);				/* tisp_process_deinit */
	u32 (*kthread_run)(u32 fn, u32 arg, u32 name);
	u32 (*kthread_stop)(u32 t);
	void (*init_completion)(u32 c);
	void (*s_stream)(u32 sd, u32 req);			/* ispcore_video_s_stream */
	const struct t32_ci_glob *g;
};

/* media bus code -> tuning format, -1 (printed by both vendor switches) when unknown */
static inline u32 t32_ci_code(const struct t32_ci_ops *ops, u32 c)
{
	static const u8 rgb[4] = { 1, 3, 2, 0 };
	const struct t32_ci_glob *g = ops->g;

	if ((c >= 0x5000 && c <= 0x5003) || (c >= 0x5100 && c <= 0x5103))
		return 0x30000;
	if (c >= 0x5200 && c <= 0x520b)
		return ((c - 0x5200) >> 2 << 16) + rgb[c & 3];
	if (c >= 0x5300 && c <= 0x532f)
		return ((c - 0x5300) >> 4 << 16) + 4 + (c & 15);
	ops->print(2, g->fmt[T32_CI_P_CODE], g->func_code, 3250, c, 0, 0);
	ops->print(2, g->fmt[T32_CI_P_CODE], g->func_code, 3368, c, 0, 0);
	return 0xffffffffU;
}

/* the sensor size into every active user (priv + 1132, 284 B, + 128 = active) of pixel format f */
static inline void t32_ci_users_fmt(u32 priv, u32 f)
{
	u32 i, e, w, h, line;

	for (i = 0; i < t32_ic_r32(priv, T32_CI_NUSERS); i++) {
		e = t32_ic_r32(priv, T32_CI_USERS) + i * T32_CI_USER_SIZE;
		if (!t32_ic_r32(e, 128))
			continue;
		w = *(u16 *)T32_IC_P(priv + T32_CI_W);
		t32_ic_w32(e, 4, w);
		h = *(u16 *)T32_IC_P(priv + T32_CI_H);
		t32_ic_w32(e, 8, h);
		t32_ic_w32(e, 12, t32_ic_r32(f, 32));
		line = (w * t32_ic_r32(f, 36)) >> 3;
		t32_ic_w32(e, 20, line);
		t32_ic_w32(e, 24, h * line);
	}
}

/* the core private block of sd, 0 when sd is NULL or an error pointer */
static inline u32 t32_ci_priv(u32 sd)
{
	return t32_pe_valid(sd) ? t32_ic_r32(sd, T32_IS_PRIV) : 0;
}

/* stop a thread word: cleared when kthread_stop succeeds, else printed */
static inline void t32_ci_stop(const struct t32_ci_ops *ops, u32 *t, u32 line)
{
	if (!*t)
		return;
	if (!ops->kthread_stop(*t))
		*t = 0;
	else
		ops->print(2, ops->g->fmt[T32_CI_P_STOP], ops->g->func, line, 0, 0, 0);
}

static inline int t32_ci_deinit(const struct t32_ci_ops *ops, u32 sd, u32 req, u32 priv, u32 state)
{
	const struct t32_ci_glob *g = ops->g;
	u32 pipe;

	if (state == 4)
		ops->s_stream(sd, req);
	pipe = t32_ic_r32(req, 4);
	if (t32_ic_r32(priv, T32_CI_STATE + 4 * pipe) == 3) {
		t32_ic_w32(priv, T32_CI_STATE + 4 * pipe, 2);
		t32_ic_w32(priv, T32_CI_NDEINIT, t32_ic_r32(priv, T32_CI_NDEINIT) + 1);
		t32_ic_w32(priv, T32_CI_NINIT, t32_ic_r32(priv, T32_CI_NINIT) - 1);
	}
	ops->proc_deinit(t32_ic_r32(req, 4));
	if (*g->tuning)
		t32_ci_stop(ops, &g->pm[0], 3672);
	pipe = t32_ic_r32(req, 4);
	if (pipe < 4)
		t32_ci_stop(ops, (u32 *)T32_IC_P(priv + T32_CI_FWTHREAD + 4 * pipe), 3683 + 11 * pipe);
	ops->lib_deinit(*T32_IC_P(req + 4));
	priv = t32_ci_priv(sd);
	*T32_IC_P(priv + T32_CI_STARTED) = 0;
	memset(T32_IC_P(priv + T32_CI_BINS + T32_CI_BINS_SIZE * t32_ic_r32(req, 4)), 0, T32_CI_BIN_SIZE);
	t32_ic_w32(priv, T32_CI_SATTR, 1);
	t32_ic_w32(priv, T32_CI_SATTR2, 0);
	return 0;
}

/* fill the 904 B tuning start block a of pipe; 0, or -1 (sensor attribute word below d9014) */
static inline int t32_ci_attr(const struct t32_ci_ops *ops, u32 sd, u32 priv, u32 pipe, u32 a, u32 memopt)
{
	const struct t32_ci_glob *g = ops->g;
	u32 p = priv + T32_CI_PIPE_SIZE * pipe, ans = 0, i, nbin = 0, n;
	char buf[128];
	const char *s;
	char *d;

	t32_ic_w32(a, 8, t32_ci_code(ops, t32_ic_r32(p, T32_CI_PIPE + 8)));
	*T32_IC_P(a + 38) = *(u16 *)T32_IC_P(t32_ic_r32(p, T32_CI_PIPE + 56) + 92);
	t32_ic_w32(a, 0, t32_ic_r32(p, T32_CI_PIPE + 60));
	t32_ic_w32(a, 4, t32_ic_r32(p, T32_CI_PIPE + 64));
	*T32_IC_P(a + 39) = *g->dual;
	t32_ic_w32(a, 16, t32_ic_r32(p, T32_CI_PIPE + 68));
	s = (const char *)T32_IC_P(t32_ic_r32(t32_ic_r32(p, T32_CI_PIPE + 52), 0));
	d = (char *)T32_IC_P(a + 20);
	while ((*d++ = *s++))
		;
	*T32_IC_P(a + 40) = 0;
	*T32_IC_P(a + 36) = t32_ic_r32(priv, T32_CI_BUS + 4 * pipe);
	*T32_IC_P(a + 43) = (u32)((int)memopt >> 8);
	*T32_IC_P(a + 42) = (memopt & 0xf) + 10;
	if (sd && t32_ic_r32(sd, T32_CI_SD_SENSOR_IOCTL))
		ops->call(t32_ic_r32(sd, T32_CI_SD_SENSOR_IOCTL), sd, T32_CI_CMD, &ans);
	*T32_IC_P(a + 40) = ans;
	memcpy(T32_IC_P(a + 900), T32_IC_P(priv + T32_CI_SATTR), 4);
	if (t32_ic_r32(priv, T32_CI_SATTR) < *g->dual) {
		ops->print(1, g->fmt[T32_CI_P_SATTR], t32_ic_r32(priv, T32_CI_SATTR), *g->dual, 0, 0, 0);
		return -1;
	}
	*T32_IC_P(a + 41) = *g->tuning;
	if (!*g->nobin) {
		for (i = 0; i < 4; i++) {
			n = priv + T32_CI_BINS + 8 + T32_CI_BINS_SIZE * pipe + T32_CI_BIN_SIZE * i;
			if (!strlen((const char *)T32_IC_P(n))) {
				snprintf(buf, 128, (const char *)T32_IC_P(g->bin_fmt[i]), (const char *)T32_IC_P(a + 20));
				memcpy(T32_IC_P(a + 132 + 128 * i), buf, 128);
			} else {
				nbin++;
				memcpy(T32_IC_P(a + 132 + 128 * i), T32_IC_P(n), 128);
				ops->print(0, g->fmt[T32_CI_P_BIN], i, a + 132 + 128 * i, 0, 0, 0);
			}
		}
		if (nbin) {
			t32_ic_w32(a, 48, 1);
			*T32_IC_P(a + 124) = 4;
		} else {
			t32_ic_w32(a, 48, 0);
		}
	}
	if (t32_ic_r32(priv, T32_CI_MODE + 4 * pipe) == 10) {
		*T32_IC_P(a + 125) += 1;
		*T32_IC_P(a + 37) = 1;
	}
	return 0;
}

/* start a thread word unless it already holds a thread: 0, or -1 when it fails */
static inline int t32_ci_run(const struct t32_ci_ops *ops, u32 *t, u32 fn, u32 arg, u32 name)
{
	if (*t && !T32_CI_ERR(*t))
		return 0;
	*t = ops->kthread_run(fn, arg, name);
	return *t && !T32_CI_ERR(*t) ? 0 : -1;
}

static inline int t32_ispcore_core_ops_init(const struct t32_ci_ops *ops, u32 sd, u32 req)
{
	static const u16 fw_line[4] = { 3598, 3610, 3621, 3632 };
	const struct t32_ci_glob *g = ops->g;
	u32 priv = t32_ci_priv(sd), memopt, pipe, state, a, flags = 0, w, h, bus;
	int ret;

	memopt = ops->memopt();
	if (!priv || T32_CI_ERR(priv))
		return -22;
	pipe = t32_ic_r32(req, 4);
	if (!pipe) {
		*g->first = 0;
		*g->npipe = 0;
		*g->done = 0;
		g->pdone[0] = 0;
		g->pdone[1] = 0;
		g->pdone[2] = 0;
		g->pdone[3] = 0;
	}
	state = t32_ic_r32(priv, T32_CI_STATE + 4 * pipe);
	if (state == 1)
		return 0;
	if (!t32_ic_r32(req, 0))
		return t32_ci_deinit(ops, sd, req, priv, state);

	a = ops->vmalloc(T32_CI_ATTR_SIZE);
	if (!a)
		ops->print(2, g->fmt[T32_CI_P_NOMEM], g->func, 3439, 0, 0, 0);
	memset(T32_IC_P(a), 0, T32_CI_ATTR_SIZE);
	if (!t32_ic_r32(req, 4) && ops->reset(0)) {
		ops->print(2, g->fmt[T32_CI_P_RESET], g->func, 3445, t32_ic_r32(sd, T32_CI_SD_NAME), 0, 0);
		ret = -22;
		goto out;
	}
	ops->lock(priv + T32_CI_LOCK, &flags);
	state = t32_ic_r32(priv, T32_CI_STATE + 4 * t32_ic_r32(req, 4));
	ops->unlock(priv + T32_CI_LOCK, flags);
	if (state != 2) {
		ops->print(2, g->fmt[T32_CI_P_STATE], g->func, 3454,
			   t32_ic_r32(priv, T32_CI_STATE + 4 * t32_ic_r32(req, 4)), 2, 3);
		ret = -1;
		goto out;
	}

	w = t32_ic_r32(priv + T32_CI_PIPE_SIZE * t32_ic_r32(req, 4), T32_CI_PIPE);
	h = t32_ic_r32(priv + T32_CI_PIPE_SIZE * t32_ic_r32(req, 4), T32_CI_PIPE + 4);
	if (w > 4096 || h > 4096) {
		ops->print(2, g->fmt[T32_CI_P_SIZE], 0, 0, 0, 0, 0);
	} else {
		*(u16 *)T32_IC_P(priv + T32_CI_W) = w;
		*(u16 *)T32_IC_P(priv + T32_CI_H) = h;
		if (t32_ic_r32(priv, T32_CI_STATE + 4 * t32_ic_r32(req, 4)) != 4)
			t32_ci_users_fmt(priv, g->fmts + 44 * t32_ic_r32(priv, T32_CI_FMT_IDX));
	}
	t32_ic_w32(priv, T32_CI_NINIT, t32_ic_r32(priv, T32_CI_NINIT) + 1);

	if (t32_ci_attr(ops, sd, priv, pipe, a, memopt)) {
		ret = -1;
		goto out;
	}
	bus = *T32_IC_P(t32_ic_r32(priv + T32_CI_PIPE_SIZE * pipe, T32_CI_PIPE + 56) + 92);
	if (ops->lib_init(bus, a)) {
		ops->print(2, g->fmt[T32_CI_P_LIB], 0, 0, 0, 0, 0);
		ret = -1;
		goto lib;
	}
	if (t32_ic_r32(priv, T32_CI_STATE) == 2) {
		ops->print(0, g->fmt[T32_CI_P_PROC], g->func, 3577, 0, 0, 0);
		ops->proc_init();
	}
	if (*g->tuning && (!g->pm[0] || T32_CI_ERR(g->pm[0]))) {
		g->pm[0] = ops->kthread_run(g->pm_fn, sd, g->pm_name);
		ops->print(1, g->fmt[T32_CI_P_PMT], 0, 0, 0, 0, 0);
		if (!g->pm[0] || T32_CI_ERR(g->pm[0])) {
			ops->print(2, g->fmt[T32_CI_P_THREAD], g->func, 3586, 0, 0, 0);
			goto proc;
		}
		g->pm[3] = priv;
	}
	pipe = t32_ic_r32(req, 4);
	if (pipe < 4 && t32_ci_run(ops, (u32 *)T32_IC_P(priv + T32_CI_FWTHREAD + 4 * pipe), g->fw_fn[pipe], 0,
				   g->fw_name[pipe])) {
		ops->print(2, g->fmt[T32_CI_P_THREAD], g->func, fw_line[pipe], 0, 0, 0);
		goto proc;
	}
	ops->init_completion(priv + T32_CI_DONE + 12 * t32_ic_r32(req, 4));
	t32_ic_w32(priv, T32_CI_EVWAIT, (u32)-9);
	if (a)
		ops->vfree(a);
	t32_ic_w32(priv, T32_CI_STATE + 4 * t32_ic_r32(req, 4), 3);
	return 0;

proc:
	ops->proc_deinit(t32_ic_r32(req, 4));
	ret = -22;
lib:
	ops->lib_deinit(*T32_IC_P(req + 4));
out:
	if (a)
		ops->vfree(a);
	return ret;
}

#endif
