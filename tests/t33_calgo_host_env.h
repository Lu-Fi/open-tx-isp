/* Host shims for driver/t32/tx_isp_t32_calgo.c (tests/t33_calgo_host.c, openimp #805). */
#ifndef T33_CALGO_HOST_ENV_H
#define T33_CALGO_HOST_ENV_H
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef uint8_t u8;
typedef uint32_t u32;
#define ENOIOCTLCMD 515
#define __user
#define READ_ONCE(x) (x)
#define WRITE_ONCE(x, v) ((x) = (v))
#define module_param(a, b, c)
#define MODULE_PARM_DESC(a, b)
#define GFP_KERNEL 0
#define kmalloc(n, f) malloc(n)
#define kfree(p) free(p)
#define msecs_to_jiffies(ms) ((void)(ms), 1)

typedef struct { int v; } atomic_t;
#define atomic_read(a) ((a)->v)
#define atomic_inc(a) ((a)->v++)

struct mutex { int held; };
#define DEFINE_MUTEX(m) struct mutex m
void host_mutex_lock(struct mutex *m);
void host_mutex_unlock(struct mutex *m);
#define mutex_lock(m) host_mutex_lock(m)
#define mutex_unlock(m) host_mutex_unlock(m)

typedef struct { int wakes; } wait_queue_head_t;
#define init_waitqueue_head(q) ((q)->wakes = 0)
#define wake_up_interruptible(q) ((q)->wakes++)
#define wake_up_interruptible_all(q) ((q)->wakes++)
/* the hook runs "while the get sleeps" (ticks, close, signal); its result < 0 = a signal */
long host_wait_hook(void);
#define wait_event_interruptible_timeout(q, cond, t) \
	((void)(t), (cond) ? 1L : ({ long _h = host_wait_hook(); _h < 0 ? _h : ((cond) ? 1L : 0L); }))

unsigned long copy_from_user(void *d, const void *s, unsigned long n);
unsigned long copy_to_user(void *d, const void *s, unsigned long n);
int isp_printf(unsigned int level, const char *fmt, ...);
struct seq_file;
int seq_printf(struct seq_file *m, const char *fmt, ...);

#include "tx_isp_t33_calgo.h"
long t33_calgo_ioctl(unsigned int cmd, unsigned long arg);
void t33_calgo_ae_tick(void);
void t33_calgo_awb_tick(void);
void t33_calgo_show(struct seq_file *m);
int t33_tb_calgo_ae_info(u32 ch, u8 *info);
void t33_tb_calgo_ae_init(u32 ch, u8 *init);
int t33_tb_calgo_ae_apply(u32 ch, const u32 *v, u8 *save, bool first);
void t33_tb_calgo_ae_restore(u32 ch, const u8 *save);
int t33_tb_calgo_awb_info(u32 ch, u8 *info);
int t33_tb_calgo_awb_apply(u32 ch, u32 r, u32 b, u32 *save, bool first);
void t33_tb_calgo_awb_restore(u32 ch, const u32 *save);
#endif
