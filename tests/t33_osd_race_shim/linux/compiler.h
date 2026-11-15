/* host shim (openimp #578) */
#ifndef SHIM_LINUX_COMPILER_H
#define SHIM_LINUX_COMPILER_H
#define noinline __attribute__((noinline))
#define noinline_for_stack noinline
#define __maybe_unused __attribute__((unused))
#endif
