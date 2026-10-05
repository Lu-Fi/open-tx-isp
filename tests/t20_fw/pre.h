/*
 * Force-included before driver/t20/tx_isp_t20_firmware.c for the host build
 * (no REGTRACE_KERNEL_TREE_BUILD: the file's own "seed" prelude supplies the
 * kernel types).  Only macros the kernel provides and the seed prelude lacks.
 */
#define KERN_EMERG "<0>"
#define KERN_ALERT "<1>"
#define KERN_CRIT "<2>"
#define KERN_ERR "<3>"
#define KERN_WARNING "<4>"
#define KERN_NOTICE "<5>"
#define KERN_INFO "<6>"
#define KERN_DEBUG "<7>"
#define KERN_CONT ""
#define __always_inline inline __attribute__((always_inline))
#define noinline __attribute__((noinline))
#define min_t(type, x, y) ({ type __x = (x); type __y = (y); __x < __y ? __x : __y; })
#define max_t(type, x, y) ({ type __x = (x); type __y = (y); __x > __y ? __x : __y; })
#define clamp_t(type, val, lo, hi) min_t(type, max_t(type, val, lo), hi)
#define min(x, y) ({ typeof(x) _a = (x); typeof(y) _b = (y); _a < _b ? _a : _b; })
#define max(x, y) ({ typeof(x) _a = (x); typeof(y) _b = (y); _a > _b ? _a : _b; })
#define clamp(v, lo, hi) min(max(v, lo), hi)
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define READ_ONCE(x) (*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, v) (*(volatile typeof(x) *)&(x) = (v))
#define barrier() __asm__ __volatile__("" ::: "memory")
#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#define offsetof(t, m) __builtin_offsetof(t, m)
#define BUILD_BUG_ON(c) ((void)sizeof(char[1 - 2 * !!(c)]))
#define abs(x) ({ int __a = (x); __a < 0 ? -__a : __a; })
#define BUG() t20fw_bug(__FILE__, __LINE__)
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define local_irq_save(f) ((f) = 0)
#define local_irq_restore(f) ((void)(f))
void t20fw_bug(const char *file, int line) __attribute__((noreturn));
long long div64_s64(long long, long long);
unsigned long long div64_u64(unsigned long long, unsigned long long);
void usleep_range(unsigned long, unsigned long);
#include "../../driver/include/tx_isp/tx_isp_math.h"
#include "../../driver/include/tx_isp/tx_isp_modulation.h"
/* module_param() takes the variable's address in the kernel build, so -Os
 * must not drop the (otherwise write-only) statistics variables here either */
#define module_param(name, type, perm) \
	static void *__t20fw_param_##name __attribute__((used)) = (void *)&(name)
#define module_param_named(pname, value, type, perm) \
	static void *__t20fw_param_##pname __attribute__((used)) = (void *)&(value)
