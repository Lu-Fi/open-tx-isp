/*
 * host shim (openimp #578): an "electric fence" allocator - every kmalloc is
 * its own mapping, kfree drops its pages and makes it inaccessible (the
 * address is never reused), so a use after
 * kfree faults at once (no sanitizer needed).  shim_live counts the blocks.
 */
#ifndef SHIM_LINUX_SLAB_H
#define SHIM_LINUX_SLAB_H
#include <stdlib.h>
#include <sys/mman.h>
#define SHIM_SLAB_BYTES 8192
static volatile long shim_live;
static inline void *kmalloc(size_t n, unsigned int f)
{
	void *p;

	(void)f;
	if (n > SHIM_SLAB_BYTES)
		abort();
	p = mmap(NULL, SHIM_SLAB_BYTES, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return NULL;
	__sync_fetch_and_add(&shim_live, 1);
	return p;
}
static inline void kfree(const void *p)
{
	if (!p)
		return;
	__sync_fetch_and_sub(&shim_live, 1);
	madvise((void *)p, SHIM_SLAB_BYTES, MADV_DONTNEED);	/* pages back, the address stays reserved */
	mprotect((void *)p, SHIM_SLAB_BYTES, PROT_NONE);
}
#endif
