#ifndef __KERN_MM_BUDDY_TEST_H__
#define __KERN_MM_BUDDY_TEST_H__

#include <pmm.h>

#if defined(PMM_USE_BUDDY) && defined(ucore_test)
#define BUDDY_TEST_ARENA_PAGES 64

struct buddy_test_region {
    size_t offset;
    size_t length;
};

/* Return 0 on success, nonzero on failure; hooks restore the live context
 * before returning. The maximum-descriptor body must not touch page data. */
typedef int (*buddy_test_body)(struct Page *base, size_t length);

void buddy_test(void);
int buddy_test_with_regions(const struct buddy_test_region *regions, size_t count,
                            buddy_test_body body);
int buddy_test_with_max_descriptors(buddy_test_body body);
void buddy_test_check_structure(void);
uint64_t buddy_test_state_digest(void);
#endif /* PMM_USE_BUDDY && ucore_test */

#endif /* !__KERN_MM_BUDDY_TEST_H__ */
