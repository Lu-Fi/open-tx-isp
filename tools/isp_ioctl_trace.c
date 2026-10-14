/*
 * isp_ioctl_trace.c - LD_PRELOAD open/ioctl tracer for the ISP userspace stack.
 *
 * Logs open/close and every ioctl (request, return, errno, and the argument
 * words in and out) on the frame-channel and ISP device nodes to
 * /tmp/isp-ioctl-trace.txt. It is what pinned IMP_FrameSource_EnableChn to its
 * last driver ioctl (SET_FORMAT) on the open T31 driver - see
 * docs/dev/cinnado-d1-t31-5.15-open-isp-status.md.
 *
 * Build for the target (uClibc) and run against any dynamically linked app:
 *   mipsel-linux-gcc -shared -fPIC -O2 -o isp_ioctl_trace.so isp_ioctl_trace.c -ldl
 *   LD_PRELOAD=/tmp/isp_ioctl_trace.so /usr/bin/timpsd -c /etc/timps.conf
 *
 * uClibc redirects the source-level open/openat to open64/openat64 via macros,
 * so the wrappers bind to the literal dynamic symbols with __asm__ labels;
 * without that, libimp's open() calls are not interposed.
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

typedef int (*open_fn)(const char *, int, ...);
typedef int (*openat_fn)(int, const char *, int, ...);
typedef int (*ioctl_fn)(int, unsigned long, ...);
typedef int (*close_fn)(int);

static open_fn real_open;
static open_fn real_open64;
static openat_fn real_openat;
static openat_fn real_openat64;
static ioctl_fn real_ioctl;
static close_fn real_close;

#define TRACE_FDS 256
static char fd_path[TRACE_FDS][64];
static int trace_fd = -2;
static unsigned int sequence;

static void log_line(const char *fmt, ...)
{
	va_list ap;
	char line[1024];
	int len;

	if (trace_fd == -2)
		trace_fd = open("/tmp/isp-ioctl-trace.txt",
				O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
	if (trace_fd < 0)
		return;
	va_start(ap, fmt);
	len = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (len > 0)
		(void)write(trace_fd, line, (size_t)len);
}

static int interesting(const char *path)
{
	if (!path)
		return 0;
	return strstr(path, "framechan") || strstr(path, "tx-isp") ||
	       strstr(path, "isp-m0") || strstr(path, "rmem") ||
	       strstr(path, "/dev/tisp") || strstr(path, "avpu") ||
	       strstr(path, "pmem");
}

static void note_fd(int fd, const char *path)
{
	if (fd < 0 || fd >= TRACE_FDS)
		return;
	if (interesting(path)) {
		strncpy(fd_path[fd], path, sizeof(fd_path[fd]) - 1);
		fd_path[fd][sizeof(fd_path[fd]) - 1] = '\0';
		log_line("%u OPEN  fd=%d %s\n", __sync_add_and_fetch(&sequence, 1), fd, path);
	} else {
		fd_path[fd][0] = '\0';
	}
}

static unsigned int req_size(unsigned long request)
{
	return (unsigned int)((request >> 16) & 0x3fffUL);
}

/*
 * uClibc redirects the source-level names open/openat to open64/openat64 via
 * macros, so bind each wrapper to its literal dynamic symbol with an asm
 * label.  libimp imports "open"/"ioctl"/"close"; busybox-free apps may use the
 * 64-bit variants, so cover both.
 */
int tp_open(const char *path, int flags, ...) __asm__("open");
int tp_open64(const char *path, int flags, ...) __asm__("open64");
int tp_openat(int dirfd, const char *path, int flags, ...) __asm__("openat");
int tp_openat64(int dirfd, const char *path, int flags, ...) __asm__("openat64");

int tp_open(const char *path, int flags, ...)
{
	va_list ap;
	mode_t mode;
	int fd;

	va_start(ap, flags);
	mode = (mode_t)va_arg(ap, int);
	va_end(ap);
	if (!real_open)
		real_open = (open_fn)dlsym(RTLD_NEXT, "open");
	fd = real_open(path, flags, mode);
	note_fd(fd, path);
	return fd;
}

int tp_open64(const char *path, int flags, ...)
{
	va_list ap;
	mode_t mode;
	int fd;

	va_start(ap, flags);
	mode = (mode_t)va_arg(ap, int);
	va_end(ap);
	if (!real_open64)
		real_open64 = (open_fn)dlsym(RTLD_NEXT, "open64");
	fd = real_open64(path, flags, mode);
	note_fd(fd, path);
	return fd;
}

int tp_openat(int dirfd, const char *path, int flags, ...)
{
	va_list ap;
	mode_t mode;
	int fd;

	va_start(ap, flags);
	mode = (mode_t)va_arg(ap, int);
	va_end(ap);
	if (!real_openat)
		real_openat = (openat_fn)dlsym(RTLD_NEXT, "openat");
	fd = real_openat(dirfd, path, flags, mode);
	note_fd(fd, path);
	return fd;
}

int tp_openat64(int dirfd, const char *path, int flags, ...)
{
	va_list ap;
	mode_t mode;
	int fd;

	va_start(ap, flags);
	mode = (mode_t)va_arg(ap, int);
	va_end(ap);
	if (!real_openat64)
		real_openat64 = (openat_fn)dlsym(RTLD_NEXT, "openat64");
	fd = real_openat64(dirfd, path, flags, mode);
	note_fd(fd, path);
	return fd;
}

int close(int fd)
{
	if (!real_close)
		real_close = (close_fn)dlsym(RTLD_NEXT, "close");
	if (fd >= 0 && fd < TRACE_FDS && fd_path[fd][0])
		log_line("%u CLOSE fd=%d %s\n", __sync_add_and_fetch(&sequence, 1), fd, fd_path[fd]);
	if (fd >= 0 && fd < TRACE_FDS)
		fd_path[fd][0] = '\0';
	return real_close(fd);
}

int ioctl(int fd, unsigned long request, ...)
{
	va_list ap;
	void *arg;
	int result;
	int saved_errno;
	unsigned int count = 0;
	unsigned int i;
	uint32_t in[28] = { 0 };
	uint32_t out[28] = { 0 };
	char buf[1024];
	int off;
	int tracked;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);
	if (!real_ioctl)
		real_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");

	tracked = (fd >= 0 && fd < TRACE_FDS && fd_path[fd][0]);
	if (tracked) {
		count = req_size(request) / 4;
		if (count > sizeof(in) / sizeof(in[0]))
			count = sizeof(in) / sizeof(in[0]);
		if (arg && count)
			memcpy(in, arg, count * sizeof(in[0]));
	}

	result = real_ioctl(fd, request, arg);
	saved_errno = errno;

	if (tracked) {
		off = snprintf(buf, sizeof(buf),
			       "%u IOCTL fd=%d %s req=%08lx ret=%d errno=%d in=",
			       __sync_add_and_fetch(&sequence, 1), fd, fd_path[fd],
			       request, result, saved_errno);
		for (i = 0; i < count && off < (int)sizeof(buf) - 12; i++)
			off += snprintf(buf + off, sizeof(buf) - off, "%08x.", in[i]);
		if (arg && count) {
			memcpy(out, arg, count * sizeof(out[0]));
			off += snprintf(buf + off, sizeof(buf) - off, " out=");
			for (i = 0; i < count && off < (int)sizeof(buf) - 12; i++)
				off += snprintf(buf + off, sizeof(buf) - off, "%08x.", out[i]);
		}
		off += snprintf(buf + off, sizeof(buf) - off, "\n");
		if (off > 0)
			log_line("%s", buf);
	}
	errno = saved_errno;
	return result;
}
