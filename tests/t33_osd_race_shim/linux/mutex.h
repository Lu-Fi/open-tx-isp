/* host shim (openimp #578): a kernel mutex is a pthread mutex */
#ifndef SHIM_LINUX_MUTEX_H
#define SHIM_LINUX_MUTEX_H
#include <pthread.h>
struct mutex { pthread_mutex_t m; };
#define DEFINE_MUTEX(n) struct mutex n = { PTHREAD_MUTEX_INITIALIZER }
#define mutex_lock(x) pthread_mutex_lock(&(x)->m)
#define mutex_unlock(x) pthread_mutex_unlock(&(x)->m)
#endif
