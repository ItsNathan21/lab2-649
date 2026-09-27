#ifndef TEST_KERNEL_H
#define TEST_KERNEL_H
#define K_MUTEX_DEFINE(name) int name
#define K_FOREVER (-1)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
static inline int k_mutex_lock(int *lock, int timeout)
{ (void)lock; (void)timeout; return 0; }
static inline int k_mutex_unlock(int *lock) { (void)lock; return 0; }
void k_busy_wait(unsigned int usec);
#endif
