#include <buddy_pmm.h>
#include <buddy_test.h>
#include <list.h>
#include <stdio.h>
#include <string.h>

// lab2 task3 2410439
/* Index i describes pages[i], relative to DRAM_BASE, not the first free page.
 * For an order-k block, i is a multiple of 2^k and its buddy is i ^ (1 << k).
 * Only FREE_HEAD pages carry PageProperty; their property is the block length.
 * ALLOC_HEAD.property stores the original request n; its actual order is in
 * metadata. Every tail has property/order zero. No public Page changes needed.
 */
enum buddy_page_state {
    BUDDY_UNMANAGED = 0,
    BUDDY_FREE_HEAD,
    BUDDY_FREE_TAIL,
    BUDDY_ALLOC_HEAD,
    BUDDY_ALLOC_TAIL,
};

struct buddy_page_meta {
    uint8_t state;
    uint8_t order;
};

struct buddy_state {
    list_entry_t free_list[BUDDY_MAX_ORDER + 1];
    size_t nr_free;
    size_t nr_managed;
    struct buddy_page_meta meta[BUDDY_MAX_PAGES];
    /* Static scratch: a 32768-byte overlap map must not use the kernel stack. */
    uint8_t seen[BUDDY_MAX_PAGES];
};

#ifdef PMM_USE_BUDDY
/* The test instance is selected only while the live allocator owns its arena.
 * Never copy live list heads: every sentinel remains in its original storage. */
static struct buddy_state buddy_live;
static struct buddy_state *buddy_active = &buddy_live;
#define buddy (*buddy_active)
#else
static struct buddy_state buddy;
#endif

static size_t
buddy_block_pages(unsigned int order) {
    assert(order <= BUDDY_MAX_ORDER);
    return (size_t)1 << order;
}

static size_t
buddy_page_limit(void) {
    assert(npage >= nbase && npage - nbase <= BUDDY_MAX_PAGES);
    return npage - nbase;
}

/* Check numeric bounds before constructing/dereferencing a candidate buddy. */
static bool
buddy_in_range(size_t index, size_t length) {
    size_t limit = buddy_page_limit();
    return index < limit && length <= limit - index;
}

static size_t
buddy_page_index(struct Page *page) {
    uintptr_t address = (uintptr_t)page;
    uintptr_t first = (uintptr_t)pages;
    assert(pages != NULL && address >= first);
    size_t offset = address - first;
    assert(offset % sizeof(struct Page) == 0);
    size_t index = offset / sizeof(struct Page);
    assert(buddy_in_range(index, 1));
    return index;
}

/* Reject out-of-capacity requests before rounding; shifts cannot overflow. */
static bool
buddy_request_order(size_t n, unsigned int *order) {
    if (n == 0 || n > BUDDY_MAX_PAGES) {
        return 0;
    }
    *order = 0;
    while (buddy_block_pages(*order) < n) {
        (*order)++;
    }
    return 1;
}

static void
buddy_unlink_free(size_t index) {
    struct Page *page = pages + index;
    assert(buddy.meta[index].state == BUDDY_FREE_HEAD);
    assert(PageProperty(page));
    list_del_init(&page->page_link);
    ClearPageProperty(page);
    page->property = 0;
    buddy.meta[index].state = BUDDY_FREE_TAIL;
    buddy.meta[index].order = 0;
}

static void
buddy_link_free(size_t index, unsigned int order) {
    size_t length = buddy_block_pages(order);
    assert(buddy_in_range(index, length) && (index & (length - 1)) == 0);
    assert(buddy.meta[index].state == BUDDY_FREE_TAIL);
    struct Page *page = pages + index;
    buddy.meta[index].state = BUDDY_FREE_HEAD;
    buddy.meta[index].order = order;
    page->property = length;
    SetPageProperty(page);
    list_add(&buddy.free_list[order], &page->page_link);
}

/* All pages in the input block are FREE_TAIL; only registered, same-order
 * free heads can participate. Adjacent non-buddies and holes cannot merge. */
static void
buddy_insert_free(size_t index, unsigned int order) {
    while (order < BUDDY_MAX_ORDER) {
        size_t length = buddy_block_pages(order);
        size_t other = index ^ length;
        if (!buddy_in_range(other, length) ||
            buddy.meta[other].state != BUDDY_FREE_HEAD ||
            buddy.meta[other].order != order) {
            break;
        }
        buddy_unlink_free(other);
        if (other < index) {
            index = other;
        }
        order++;
    }
    buddy_link_free(index, order);
}

static void
buddy_init(void) {
    memset(&buddy, 0, sizeof(buddy));
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++) {
        list_init(&buddy.free_list[order]);
    }
    /* This makes relative-index alignment also physical block alignment. */
    assert((nbase & (BUDDY_MAX_PAGES - 1)) == 0);
}

static void
buddy_init_memmap(struct Page *base, size_t n) {
    assert(n > 0);
    size_t index = buddy_page_index(base);
    assert(buddy_in_range(index, n));
    for (size_t i = index; i < index + n; i++) {
        assert(PageReserved(pages + i));
        assert(buddy.meta[i].state == BUDDY_UNMANAGED);
        pages[i].flags = 0;
        pages[i].property = 0;
        set_page_ref(pages + i, 0);
        buddy.meta[i].state = BUDDY_FREE_TAIL;
        buddy.meta[i].order = 0;
    }

    size_t remaining = n;
    while (remaining > 0) {
        unsigned int order = 0;
        while (order < BUDDY_MAX_ORDER) {
            size_t larger = buddy_block_pages(order + 1);
            if (larger > remaining || (index & (larger - 1)) != 0) {
                break;
            }
            order++;
        }
        size_t length = buddy_block_pages(order);
        buddy_insert_free(index, order);
        index += length;
        remaining -= length;
    }
    buddy.nr_free += n;
    buddy.nr_managed += n;
}

static struct Page *
buddy_alloc_pages(size_t n) {
    unsigned int order;
    if (!buddy_request_order(n, &order) ||
        buddy_block_pages(order) > buddy.nr_free) {
        return NULL;
    }
    unsigned int found = order;
    while (found <= BUDDY_MAX_ORDER && list_empty(&buddy.free_list[found])) {
        found++;
    }
    if (found > BUDDY_MAX_ORDER) {
        return NULL;
    }

    struct Page *base = le2page(list_next(&buddy.free_list[found]), page_link);
    size_t index = buddy_page_index(base);
    assert(buddy.meta[index].order == found);
    assert(buddy_in_range(index, buddy_block_pages(found)));
    buddy_unlink_free(index);
    while (found > order) {
        found--;
        buddy_link_free(index + buddy_block_pages(found), found);
    }

    size_t length = buddy_block_pages(order);
    for (size_t i = index; i < index + length; i++) {
        assert(buddy.meta[i].state == BUDDY_FREE_TAIL);
        assert(!PageReserved(pages + i) && !PageProperty(pages + i));
        buddy.meta[i].state = BUDDY_ALLOC_TAIL;
        buddy.meta[i].order = 0;
        pages[i].property = 0;
        set_page_ref(pages + i, 0);
    }
    buddy.meta[index].state = BUDDY_ALLOC_HEAD;
    buddy.meta[index].order = order;
    base->property = n;
    buddy.nr_free -= length;
    return base;
}

static void
buddy_free_pages(struct Page *base, size_t n) {
    unsigned int order;
    assert(buddy_request_order(n, &order));
    size_t index = buddy_page_index(base);
    assert(buddy.meta[index].state == BUDDY_ALLOC_HEAD);
    assert(buddy.meta[index].order == order && base->property == n);
    size_t length = buddy_block_pages(order);
    assert(buddy_in_range(index, length) && (index & (length - 1)) == 0);
    for (size_t i = index; i < index + length; i++) {
        assert(buddy.meta[i].state ==
               (i == index ? BUDDY_ALLOC_HEAD : BUDDY_ALLOC_TAIL));
        assert(!PageReserved(pages + i) && !PageProperty(pages + i));
        pages[i].flags = 0;
        pages[i].property = 0;
        set_page_ref(pages + i, 0);
        buddy.meta[i].state = BUDDY_FREE_TAIL;
        buddy.meta[i].order = 0;
    }
    buddy.nr_free += length;
    buddy_insert_free(index, order);
}

static size_t
buddy_nr_free_pages(void) {
    return buddy.nr_free;
}

/* Structural invariants, callable without recursively entering .check. */
static void
buddy_check_structure(void) {
    size_t limit = buddy_page_limit();
    size_t free_total = 0, allocated_total = 0, managed_total = 0, blocks = 0;
    memset(buddy.seen, 0, sizeof(buddy.seen));

    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++) {
        list_entry_t *head = &buddy.free_list[order];
        list_entry_t *prev = head, *le = list_next(head);
        size_t length = buddy_block_pages(order);
        while (le != head) {
            assert(++blocks <= limit);
            struct Page *base = le2page(le, page_link);
            size_t index = buddy_page_index(base);
            assert(buddy_in_range(index, length));
            assert((index & (length - 1)) == 0);
            assert(le->prev == prev && prev->next == le);
            assert(buddy.meta[index].state == BUDDY_FREE_HEAD);
            assert(buddy.meta[index].order == order);
            assert(PageProperty(base) && base->property == length);
            for (size_t i = index; i < index + length; i++) {
                assert(buddy.seen[i] == 0);
                buddy.seen[i] = 1;
                assert(!PageReserved(pages + i) && page_ref(pages + i) == 0);
                if (i != index) {
                    assert(buddy.meta[i].state == BUDDY_FREE_TAIL);
                    assert(buddy.meta[i].order == 0);
                    assert(!PageProperty(pages + i) && pages[i].property == 0);
                }
            }
            if (order < BUDDY_MAX_ORDER) {
                size_t other = index ^ length;
                assert(!buddy_in_range(other, length) ||
                       buddy.meta[other].state != BUDDY_FREE_HEAD ||
                       buddy.meta[other].order != order);
            }
            free_total += length;
            prev = le;
            le = list_next(le);
        }
        assert(head->prev == prev && prev->next == head);
    }

    for (size_t index = 0; index < limit;) {
        unsigned int state = buddy.meta[index].state;
        if (state == BUDDY_UNMANAGED) {
            assert(buddy.seen[index] == 0);
            index++;
        } else if (state == BUDDY_ALLOC_HEAD) {
            unsigned int order = buddy.meta[index].order;
            size_t length = buddy_block_pages(order);
            assert(buddy_in_range(index, length));
            assert((index & (length - 1)) == 0);
            size_t requested = pages[index].property;
            assert(requested > (length >> 1) && requested <= length);
            for (size_t i = index; i < index + length; i++) {
                assert(buddy.seen[i] == 0);
                buddy.seen[i] = 2;
                assert(!PageReserved(pages + i) && !PageProperty(pages + i));
                if (i != index) {
                    assert(buddy.meta[i].state == BUDDY_ALLOC_TAIL);
                    assert(buddy.meta[i].order == 0 && pages[i].property == 0);
                }
            }
            allocated_total += length;
            managed_total += length;
            index += length;
        } else {
            assert(state == BUDDY_FREE_HEAD || state == BUDDY_FREE_TAIL);
            assert(buddy.seen[index] == 1);
            managed_total++;
            index++;
        }
    }
    assert(free_total == buddy.nr_free);
    assert(managed_total == buddy.nr_managed);
    assert(free_total + allocated_total == managed_total);
    cprintf("buddy: structural check passed, %lu free pages in %lu blocks, "
            "%lu managed pages, max order %u\n",
            (unsigned long)free_total, (unsigned long)blocks,
            (unsigned long)managed_total, (unsigned int)BUDDY_MAX_ORDER);
}

#if defined(PMM_USE_BUDDY) && defined(ucore_test)
static struct buddy_state buddy_test_state;
static struct Page buddy_test_saved_pages[BUDDY_TEST_ARENA_PAGES];
/* Descriptors only: these entries do not represent writable physical RAM. */
static struct Page buddy_test_max_pages[BUDDY_MAX_PAGES];
static bool buddy_test_isolated;

void
buddy_test_check_structure(void) {
    buddy_check_structure();
}

static uint64_t
buddy_test_hash_word(uint64_t hash, uint64_t word) {
    return (hash ^ word) * 1099511628211ULL;
}

/* A diagnostic fingerprint, not a proof of equality. Hash initialized fields
 * individually, never Page/metadata padding. seen[] is check-only scratch. */
uint64_t
buddy_test_state_digest(void) {
    uint64_t hash = 14695981039346656037ULL;
    size_t limit = buddy_page_limit();
    hash = buddy_test_hash_word(hash, (uintptr_t)buddy_active);
    hash = buddy_test_hash_word(hash, (uintptr_t)pages);
    hash = buddy_test_hash_word(hash, npage);
    hash = buddy_test_hash_word(hash, buddy.nr_free);
    hash = buddy_test_hash_word(hash, buddy.nr_managed);
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++) {
        hash = buddy_test_hash_word(hash,
                                   (uintptr_t)buddy.free_list[order].prev);
        hash = buddy_test_hash_word(hash,
                                   (uintptr_t)buddy.free_list[order].next);
    }
    for (size_t i = 0; i < BUDDY_MAX_PAGES; i++) {
        hash = buddy_test_hash_word(hash, buddy.meta[i].state);
        hash = buddy_test_hash_word(hash, buddy.meta[i].order);
        if (i < limit) {
            hash = buddy_test_hash_word(hash, (uint64_t)pages[i].ref);
            hash = buddy_test_hash_word(hash, pages[i].flags);
            hash = buddy_test_hash_word(hash, pages[i].property);
            hash = buddy_test_hash_word(hash, (uintptr_t)pages[i].page_link.prev);
            hash = buddy_test_hash_word(hash, (uintptr_t)pages[i].page_link.next);
        }
    }
    return hash;
}

/* Register only caller-described subregions of a privately allocated block.
 * Even when a body reports failure, restore all descriptors and the live
 * context before returning that failure. No production initializer is reset. */
int
buddy_test_with_regions(const struct buddy_test_region *regions, size_t count,
                        buddy_test_body body) {
    assert(pmm_manager == &buddy_pmm_manager);
    assert(!buddy_test_isolated && buddy_active == &buddy_live);
    assert(regions != NULL && count > 0 && count <= 8 && body != NULL);
    size_t registered = 0;
    for (size_t i = 0; i < count; i++) {
        assert(regions[i].offset < BUDDY_TEST_ARENA_PAGES);
        assert(regions[i].length > 0);
        assert(regions[i].length <= BUDDY_TEST_ARENA_PAGES - regions[i].offset);
        size_t end = regions[i].offset + regions[i].length;
        for (size_t j = 0; j < i; j++) {
            assert(end <= regions[j].offset ||
                   regions[j].offset + regions[j].length <= regions[i].offset);
        }
        registered += regions[i].length;
    }
    size_t baseline = nr_free_pages();
    struct Page *base = alloc_pages(BUDDY_TEST_ARENA_PAGES);
    if (base == NULL) {
        assert(nr_free_pages() == baseline);
        cprintf("buddy tests: UNTESTED, no exclusive 64-page arena\n");
        return -2;
    }
    size_t index = buddy_page_index(base);
    assert(buddy_in_range(index, BUDDY_TEST_ARENA_PAGES));
    assert((index & (BUDDY_TEST_ARENA_PAGES - 1)) == 0);
    assert(buddy.meta[index].state == BUDDY_ALLOC_HEAD);
    assert(buddy_block_pages(buddy.meta[index].order) == BUDDY_TEST_ARENA_PAGES);
    assert(base->property == BUDDY_TEST_ARENA_PAGES);
    assert(nr_free_pages() == baseline - BUDDY_TEST_ARENA_PAGES);
    memcpy(buddy_test_saved_pages, base, sizeof(buddy_test_saved_pages));
    memset(base, 0, sizeof(buddy_test_saved_pages));
    for (size_t i = 0; i < BUDDY_TEST_ARENA_PAGES; i++) {
        SetPageReserved(base + i);
    }
    buddy_test_isolated = 1;
    buddy_active = &buddy_test_state;
    buddy_init();
    for (size_t i = 0; i < count; i++) {
        buddy_init_memmap(base + regions[i].offset, regions[i].length);
    }
    assert(nr_free_pages() == registered);
    buddy_check_structure();
    int result = body(base, BUDDY_TEST_ARENA_PAGES);
    if (result == 0) {
        assert(buddy.nr_managed == registered);
        assert(nr_free_pages() == buddy.nr_managed);
        buddy_check_structure();
    }
    memcpy(base, buddy_test_saved_pages, sizeof(buddy_test_saved_pages));
    buddy_active = &buddy_live;
    buddy_test_isolated = 0;
    free_pages(base, BUDDY_TEST_ARENA_PAGES);
    assert(nr_free_pages() == baseline);
    return result;
}

/* Exercise the highest supported order without treating reserved firmware or
 * kernel RAM as free. The body must never map these synthetic descriptors. */
int
buddy_test_with_max_descriptors(buddy_test_body body) {
    assert(pmm_manager == &buddy_pmm_manager);
    assert(!buddy_test_isolated && buddy_active == &buddy_live && body != NULL);
    size_t baseline = nr_free_pages();
    struct Page *saved_pages = pages;
    size_t saved_npage = npage;
    memset(buddy_test_max_pages, 0, sizeof(buddy_test_max_pages));
    for (size_t i = 0; i < BUDDY_MAX_PAGES; i++) {
        SetPageReserved(buddy_test_max_pages + i);
    }
    buddy_test_isolated = 1;
    buddy_active = &buddy_test_state;
    pages = buddy_test_max_pages;
    npage = nbase + BUDDY_MAX_PAGES;
    buddy_init();
    buddy_init_memmap(pages, BUDDY_MAX_PAGES);
    assert(nr_free_pages() == BUDDY_MAX_PAGES);
    buddy_check_structure();
    int result = body(pages, BUDDY_MAX_PAGES);
    if (result == 0) {
        assert(buddy.nr_managed == BUDDY_MAX_PAGES);
        assert(nr_free_pages() == buddy.nr_managed);
        buddy_check_structure();
    }
    pages = saved_pages;
    npage = saved_npage;
    buddy_active = &buddy_live;
    buddy_test_isolated = 0;
    assert(nr_free_pages() == baseline);
    return result;
}
#endif /* PMM_USE_BUDDY && ucore_test */

static void
buddy_check(void) {
    buddy_check_structure();
#if defined(PMM_USE_BUDDY) && defined(ucore_test)
    buddy_test();
    buddy_check_structure();
#endif
}

const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",
    .init = buddy_init,
    .init_memmap = buddy_init_memmap,
    .alloc_pages = buddy_alloc_pages,
    .free_pages = buddy_free_pages,
    .nr_free_pages = buddy_nr_free_pages,
    .check = buddy_check,
};
