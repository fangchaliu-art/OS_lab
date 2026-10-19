#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

/* Orders 0..15 cover 1..32768 pages, the current 128 MiB DRAM window. */
#define BUDDY_MAX_ORDER 15
#define BUDDY_MAX_PAGES ((size_t)1 << BUDDY_MAX_ORDER)

extern const struct pmm_manager buddy_pmm_manager;

#endif /* !__KERN_MM_BUDDY_PMM_H__ */
