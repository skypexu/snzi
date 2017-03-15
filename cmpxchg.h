#ifndef CMPXCHG_H
#define CMPXCHG_H

typedef int bool;
#define false (0 == 1)
#define true (1 == 1)

#define cmpxchg(p, o, n) __sync_val_compare_and_swap(p, o, n)

#define NR_CPUS 24

#endif
