/* host shim (openimp #578) */
#ifndef SHIM_LINUX_LOCKDEP_H
#define SHIM_LINUX_LOCKDEP_H
#define lockdep_assert_held(x) ((void)(x))
#endif
