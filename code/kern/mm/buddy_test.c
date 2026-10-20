#include <buddy_test.h>

#if defined(PMM_USE_BUDDY) && defined(ucore_test)
#include <buddy_pmm.h>
#include <stdio.h>
#include <string.h>

/* Independent behavioral oracles: expected lengths use multiplication, and
 * availability uses a page bitmap, never the allocator's order/search helpers.
 * All records are bounded static storage. Only an owned real arena is mapped
 * for data tests; the order-15 fixture uses descriptors, never physical data. */
#define TEST_CAP 64
#define TEST_ROUNDS 32
#define TEST_STEPS 128
#define TEST_SEED 0x02410439U

struct test_record {
    struct Page *page;
    size_t requested, actual, offset;
    uint8_t tag;
    bool live;
};

static struct test_record records[TEST_CAP];
static uint8_t registered[TEST_CAP], occupied[TEST_CAP];
static struct Page *arena;
static size_t arena_length, registered_count;
static bool cleanup_safe;
static unsigned int current_case, current_round, current_step;
static const struct buddy_test_region *selected_regions;
static size_t selected_count;
static buddy_test_body selected_body;
static const struct buddy_test_region whole_region[] = {{0, TEST_CAP}};

#define CHECK(condition, ...)                                           \
    do {                                                               \
        if (!(condition)) {                                            \
            cprintf("[buddy_test] FAIL case%u round%u step%u %s:%d: ", \
                    current_case, current_round, current_step,          \
                    __func__, __LINE__);                                \
            cprintf(__VA_ARGS__);                                      \
            cprintf("\n");                                             \
            return -1;                                                \
        }                                                              \
    } while (0)

static size_t
expected_length(size_t requested) {
    size_t actual = 1;
    if (requested == 0 || requested > 32768) {
        return 0;
    }
    while (actual < requested) {
        actual *= 2;
    }
    return actual;
}

/* Numeric validation comes before pointer subtraction or descriptor access. */
static int
descriptor_offset(struct Page *page, struct Page *base, size_t capacity,
                  size_t actual, size_t *offset) {
    uintptr_t address = (uintptr_t)page, first = (uintptr_t)base;
    if (page == NULL || address < first || actual == 0 || actual > capacity) {
        return -1;
    }
    uintptr_t bytes = address - first;
    if (bytes % sizeof(struct Page) != 0) {
        return -1;
    }
    *offset = bytes / sizeof(struct Page);
    return *offset <= capacity - actual ? 0 : -1;
}

static uint8_t
pattern(uint8_t tag, size_t page, size_t byte) {
    return (uint8_t)(tag ^ (page * 37 + byte * 17 + (byte >> 5)));
}

static uint8_t *
data_address(struct Page *page) {
    return (uint8_t *)(page2pa(page) + va_pa_offset);
}

static void
fill_record(const struct test_record *record) {
    uint8_t *data = data_address(record->page);
    for (size_t page = 0; page < record->requested; page++) {
        for (size_t byte = 0; byte < PGSIZE; byte++) {
            data[page * PGSIZE + byte] = pattern(record->tag, page, byte);
        }
    }
}

static int
check_record(size_t slot, bool full) {
    const struct test_record *record = &records[slot];
    CHECK(record->live, "slot %lu is not live", (unsigned long)slot);
    uint8_t *data = data_address(record->page);
    for (size_t page = 0; page < record->requested; page++) {
        if (full) {
            for (size_t byte = 0; byte < PGSIZE; byte++) {
                CHECK(data[page * PGSIZE + byte] == pattern(record->tag, page, byte),
                      "slot %lu page %lu byte %lu data changed", (unsigned long)slot,
                      (unsigned long)page, (unsigned long)byte);
            }
        } else {
            CHECK(data[page * PGSIZE] == pattern(record->tag, page, 0) &&
                  data[(page + 1) * PGSIZE - 1] == pattern(record->tag, page, PGSIZE - 1),
                  "slot %lu page %lu endpoint changed", (unsigned long)slot,
                  (unsigned long)page);
        }
    }
    return 0;
}

static int
check_all(bool full) {
    for (size_t slot = 0; slot < TEST_CAP; slot++) {
        if (records[slot].live && check_record(slot, full) != 0) {
            return -1;
        }
    }
    return 0;
}

/* Returns 0 for a validated allocation, 1 for NULL, -1 for a defect. */
static int
take(size_t slot, size_t requested, uint8_t tag) {
    CHECK(slot < TEST_CAP && !records[slot].live, "invalid record slot");
    size_t before = nr_free_pages(), actual = expected_length(requested);
    struct Page *page = alloc_pages(requested);
    if (page == NULL) {
        CHECK(nr_free_pages() == before, "NULL changed free-page count");
        return 1;
    }
    size_t offset, absolute;
    if (descriptor_offset(page, arena, arena_length, actual, &offset) != 0 ||
        descriptor_offset(page, pages, npage - nbase, actual, &absolute) != 0) {
        cleanup_safe = 0;
        CHECK(0, "allocation returned an out-of-arena descriptor");
    }
    if (absolute % actual != 0) {
        cleanup_safe = 0;
        CHECK(0, "block index %lu is not aligned to %lu", (unsigned long)absolute,
              (unsigned long)actual);
    }
    for (size_t i = 0; i < actual; i++) {
        if (!registered[offset + i] || occupied[offset + i] ||
            PageReserved(page + i) || PageProperty(page + i) ||
            page_ref(page + i) != 0 || page[i].property != (i == 0 ? requested : 0)) {
            cleanup_safe = 0;
            CHECK(0, "bad, overlapping, or unregistered page at arena offset %lu",
                  (unsigned long)(offset + i));
        }
    }
    records[slot].page = page;
    records[slot].requested = requested;
    records[slot].actual = actual;
    records[slot].offset = offset;
    records[slot].tag = tag;
    records[slot].live = 1;
    for (size_t i = 0; i < actual; i++) {
        occupied[offset + i] = 1;
    }
    CHECK(before >= actual && nr_free_pages() == before - actual,
          "request %lu must consume %lu pages", (unsigned long)requested,
          (unsigned long)actual);
    fill_record(&records[slot]);
    return 0;
}

static int
release(size_t slot) {
    CHECK(slot < TEST_CAP && records[slot].live, "free of unrecorded slot");
    CHECK(check_record(slot, 1) == 0, "data damaged before release");
    struct test_record *record = &records[slot];
    size_t before = nr_free_pages();
    free_pages(record->page, record->requested);
    record->live = 0;
    for (size_t i = 0; i < record->actual; i++) {
        occupied[record->offset + i] = 0;
    }
    CHECK(nr_free_pages() == before + record->actual,
          "original n=%lu did not return %lu pages", (unsigned long)record->requested,
          (unsigned long)record->actual);
    return 0;
}

static int
release_all(void) {
    int result = 0;
    for (size_t slot = 0; slot < TEST_CAP; slot++) {
        if (records[slot].live && release(slot) != 0) {
            result = -1;
        }
    }
    return result;
}

/* A complete, read-only state digest supplements public count/data checks.
 * It is only used at deterministic failure points, not at every stress step. */
static int
expect_failure(size_t requested) {
    size_t before = nr_free_pages();
    uint64_t digest = buddy_test_state_digest();
    struct Page *page = alloc_pages(requested);
    if (page != NULL) {
        cleanup_safe = 0;
        CHECK(0, "alloc_pages(%lu) unexpectedly succeeded", (unsigned long)requested);
    }
    CHECK(nr_free_pages() == before, "failed request changed page count");
    CHECK(buddy_test_state_digest() == digest, "failed request changed allocator state");
    CHECK(check_all(1) == 0, "failed request damaged live data");
    return 0;
}

static int
find_offset(size_t offset) {
    for (size_t slot = 0; slot < TEST_CAP; slot++) {
        if (records[slot].live && records[slot].offset == offset) {
            return (int)slot;
        }
    }
    return -1;
}

static int
release_offset(size_t offset) {
    int slot = find_offset(offset);
    CHECK(slot >= 0, "missing live page at offset %lu", (unsigned long)offset);
    return release((size_t)slot);
}

static int
recover_whole(void) {
    CHECK(release_all() == 0, "cleanup failed");
    CHECK(nr_free_pages() == TEST_CAP, "capacity not restored");
    CHECK(take(0, TEST_CAP, 0xb3) == 0, "original 64-page block not recoverable");
    CHECK(records[0].offset == 0, "large block came from outside target arena");
    return release(0);
}

static int
check_unregistered_descriptors(void) {
    for (size_t i = 0; i < arena_length; i++) {
        if (!registered[i]) {
            CHECK(arena[i].flags == ((uint64_t)1 << PG_reserved) &&
                  page_ref(arena + i) == 0 && arena[i].property == 0 &&
                  occupied[i] == 0,
                  "unregistered descriptor %lu was modified", (unsigned long)i);
        }
    }
    return 0;
}

static int
scope_body(struct Page *base, size_t length) {
    CHECK(length == TEST_CAP, "unexpected isolated arena length");
    arena = base;
    arena_length = length;
    registered_count = 0;
    cleanup_safe = 1;
    memset(records, 0, sizeof(records));
    memset(registered, 0, sizeof(registered));
    memset(occupied, 0, sizeof(occupied));
    for (size_t r = 0; r < selected_count; r++) {
        CHECK(selected_regions[r].offset <= length &&
              selected_regions[r].length <= length - selected_regions[r].offset,
              "invalid fixture region");
        for (size_t i = 0; i < selected_regions[r].length; i++) {
            size_t offset = selected_regions[r].offset + i;
            CHECK(!registered[offset], "fixture regions overlap");
            registered[offset] = 1;
            registered_count++;
        }
    }
    CHECK(nr_free_pages() == registered_count, "initial registration count mismatch");
    CHECK(check_unregistered_descriptors() == 0, "initialization changed a gap");
    int result = selected_body(base, length);
    if (cleanup_safe) {
        if (release_all() != 0 || nr_free_pages() != registered_count) {
            cprintf("[buddy_test] FAIL case%u: scope cleanup did not restore capacity\n",
                    current_case);
            result = -1;
        }
    }
    if (check_unregistered_descriptors() != 0) {
        result = -1;
    }
    if (result == 0) {
        buddy_test_check_structure();
    }
    return result;
}

static int
run_scope(const struct buddy_test_region *regions, size_t count,
          buddy_test_body body) {
    selected_regions = regions;
    selected_count = count;
    selected_body = body;
    return buddy_test_with_regions(regions, count, scope_body);
}

static int
case1_small(struct Page *base, size_t length) {
    (void)base; (void)length;
    static const size_t requests[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17,
                                      31, 32, 33, 63, 64};
    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
        current_step = i;
        CHECK(take(0, requests[i], (uint8_t)(0x20 + i)) == 0,
              "boundary request %lu failed", (unsigned long)requests[i]);
        CHECK(release(0) == 0 && nr_free_pages() == TEST_CAP,
              "boundary release did not restore baseline");
    }
    return 0;
}

/* page_init currently registers one free suffix. The known entry baseline
 * gives its lower bound, independently of flags the tested allocator changes.
 * Global smoke tests do not reuse the 64-page fixture or map descriptor memory. */
static int
validate_global_allocation(struct Page *page, size_t requested, size_t actual,
                           size_t before) {
    for (size_t i = 0; i < actual; i++) {
        CHECK(!PageReserved(page + i) && !PageProperty(page + i) &&
              page_ref(page + i) == 0 &&
              page[i].property == (i == 0 ? requested : 0),
              "global n=%lu bad descriptor %lu", (unsigned long)requested,
              (unsigned long)i);
    }
    CHECK(before >= actual && nr_free_pages() == before - actual,
          "high-order rounding count mismatch");
    volatile uint8_t *data = data_address(page);
    data[0] = 0x35;
    data[requested * PGSIZE - 1] = 0xca;
    CHECK(data[0] == 0x35 && data[requested * PGSIZE - 1] == 0xca,
          "real high-order endpoint data mismatch");
    return 0;
}

static int
global_request(size_t requested, size_t free_start, size_t limit) {
    size_t actual = expected_length(requested), before = nr_free_pages();
    bool must_fail = actual > 16384;
    uint64_t digest = must_fail ? buddy_test_state_digest() : 0;
    struct Page *page = alloc_pages(requested);
    if (page == NULL) {
        CHECK(nr_free_pages() == before, "global NULL changed page count");
        if (must_fail) {
            CHECK(buddy_test_state_digest() == digest,
                  "unavailable real maximum block changed state");
            return 0;
        }
        cprintf("[buddy_test] case1 real high-order n=%lu: NOT COVERED (unavailable)\n",
                (unsigned long)requested);
        return -2;
    }

    size_t index;
    if (descriptor_offset(page, pages, limit, actual, &index) != 0 ||
        index < free_start || index % actual != 0) {
        cprintf("[buddy_test] FAIL case1 global n=%lu: invalid/unowned return %p; "
                "unsafe to write or release\n", (unsigned long)requested, page);
        panic("buddy_test: unsafe global allocation; cannot continue safely");
    }

    /* From here the original pointer/n pair is retained until cleanup. A
     * validator failure must never return past this owned live allocation. */
    int result;
    if (must_fail) {
        cprintf("[buddy_test] FAIL case1: unavailable real n=%lu unexpectedly succeeded\n",
                (unsigned long)requested);
        result = -1;
    } else {
        result = validate_global_allocation(page, requested, actual, before);
    }
    free_pages(page, requested);
    CHECK(nr_free_pages() == before, "real high-order cleanup did not restore baseline");
    return result;
}

static int
case1_global(size_t baseline) {
    size_t limit = npage - nbase, free_start = limit - baseline;
    for (size_t power = 64; power <= 16384; power *= 2) {
        for (int delta = -1; delta <= 1; delta++) {
            int result = global_request(power + delta, free_start, limit);
            if (result != 0) { return result; }
        }
    }
    buddy_test_check_structure();
    cprintf("[buddy_test] case1 real boundaries 2^k +/- 1, k=6..14: PASS\n");
    return 0;
}

static int
case2_split(struct Page *base, size_t length) {
    (void)base; (void)length;
    for (size_t i = 0; i < TEST_CAP; i++) {
        current_step = i;
        CHECK(take(i, 1, (uint8_t)(i + 1)) == 0, "split lost capacity at page %lu",
              (unsigned long)i);
    }
    CHECK(nr_free_pages() == 0, "64 one-page allocations did not exhaust arena");
    for (size_t i = 0; i < TEST_CAP; i++) {
        CHECK(occupied[i] == 1, "split failed to expose every page");
    }
    CHECK(expect_failure(1) == 0, "exhausted split arena accepted another page");
    CHECK(check_all(1) == 0, "split allocations overlap in data");
    return recover_whole();
}

static int
case3_merge(struct Page *base, size_t length) {
    (void)base; (void)length;
    for (unsigned int order = 0; order < 3; order++) {
        current_round = order;
        for (size_t i = 0; i < TEST_CAP; i++) {
            CHECK(take(i, 1, (uint8_t)(i + 19)) == 0, "cannot create 64 small blocks");
        }
        for (size_t i = 0; i < TEST_CAP; i++) {
            size_t offset = order == 0 ? i : order == 1 ? TEST_CAP - 1 - i : (i * 17) % TEST_CAP;
            CHECK(release_offset(offset) == 0, "release order %u failed", order);
        }
        CHECK(recover_whole() == 0, "six merge levels failed, order %u", order);
    }
    return 0;
}

static int
case4_nonbuddy(struct Page *base, size_t length) {
    (void)base; (void)length;
    for (size_t i = 0; i < 4; i++) {
        CHECK(take(i, 1, (uint8_t)(0x70 + i)) == 0, "four-page setup failed");
    }
    CHECK(release_offset(1) == 0, "cannot free page 1");
    CHECK(expect_failure(2) == 0, "occupied buddy must prevent merge");
    CHECK(release_offset(2) == 0 && nr_free_pages() == 2, "nonbuddy setup count");
    /* Relative offsets 1 and 2 are adjacent, but 1 xor 1 == 0, not 2. */
    CHECK(expect_failure(2) == 0, "adjacent non-buddies incorrectly merged");
    CHECK(release_offset(0) == 0, "cannot release true buddy");
    int slot = 0;
    while (records[slot].live) { slot++; }
    CHECK(take(slot, 2, 0xc1) == 0 && records[slot].offset == 0,
          "true buddies 0 and 1 did not merge");
    CHECK(check_all(1) == 0, "still-live page 3 changed");
    return release_all();
}

static int
case4_holes(struct Page *base, size_t length) {
    (void)base; (void)length;
    CHECK(nr_free_pages() == 2, "hole fixture capacity");
    CHECK(expect_failure(2) == 0, "unregistered gap was merged");
    CHECK(take(0, 1, 0x31) == 0 && take(1, 1, 0x52) == 0, "registered singleton unavailable");
    CHECK(expect_failure(1) == 0, "unregistered or reserved page became available");
    return release_all();
}

static int
case5_data(struct Page *base, size_t length) {
    (void)base; (void)length;
    static const size_t sizes[] = {1, 3, 5, 2, 7, 1};
    for (size_t i = 0; i < 6; i++) {
        CHECK(take(i, sizes[i], (uint8_t)(0x90 + 7 * i)) == 0, "mixed setup allocation failed");
    }
    CHECK(check_all(1) == 0, "mixed objects are not independent");
    CHECK(release(1) == 0 && release(3) == 0 && release(5) == 0, "interleaved release failed");
    CHECK(check_all(1) == 0, "release damaged a surviving block");
    CHECK(take(1, 3, 0xe1) == 0 && take(3, 2, 0xe2) == 0 && take(5, 1, 0xe3) == 0,
          "interleaved reallocation failed");
    CHECK(check_all(1) == 0, "reuse overwrote a surviving allocation");
    CHECK(release(4) == 0 && release(0) == 0 && release(2) == 0,
          "alternate release order failed");
    CHECK(check_all(1) == 0, "remaining objects damaged after alternate release");
    return recover_whole();
}

static int
case6_failure(struct Page *base, size_t length) {
    (void)base; (void)length;
    CHECK(take(0, 3, 0xd5) == 0, "cannot allocate survivor");
    static const size_t invalid[] = {0, 32769, (size_t)-1, (size_t)-2,
                                     ((size_t)1 << 63), ((size_t)1 << 63) - 1};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        CHECK(expect_failure(invalid[i]) == 0, "invalid request was not atomic");
    }
    CHECK(release_all() == 0, "survivor release failed");
    for (size_t i = 0; i < TEST_CAP; i++) {
        CHECK(take(i, 1, (uint8_t)(0x40 + i)) == 0, "exhaustion setup lost capacity");
    }
    CHECK(nr_free_pages() == 0 && expect_failure(1) == 0, "real exhaustion not reached");
    cprintf("[buddy_test] case6: genuine exhaustion at 64/64 pages (extra request returned NULL)\n");
    for (size_t i = 0; i < TEST_CAP; i += 2) {
        CHECK(release_offset(i) == 0, "fragmentation setup failed");
    }
    CHECK(nr_free_pages() == TEST_CAP / 2, "fragmented free count must be 32");
    CHECK(expect_failure(2) == 0 && expect_failure(32) == 0,
          "fragmented arena returned unavailable higher order");
    CHECK(check_all(1) == 0, "fragmentation damaged occupied buddies");
    return recover_whole();
}

static int
case7_regions(struct Page *base, size_t length) {
    (void)base; (void)length;
    for (size_t i = 0; i < registered_count; i++) {
        CHECK(take(i, 1, (uint8_t)(0x11 + i)) == 0, "registered tail page lost");
    }
    CHECK(nr_free_pages() == 0 && expect_failure(1) == 0,
          "region capacity or outer bound mismatch");
    for (size_t i = 0; i < TEST_CAP; i++) {
        CHECK(occupied[i] == registered[i], "registered page not counted exactly once");
    }
    CHECK(release_all() == 0, "irregular-region cleanup failed");
    if (registered_count == TEST_CAP) {
        CHECK(recover_whole() == 0, "adjacent separately registered regions did not coalesce");
    }
    return 0;
}

/* Descriptors only: the fixture supplies no corresponding 128 MiB of owned
 * physical memory. This function deliberately never calls data_address(). */
static int
case7_max_descriptors(struct Page *base, size_t length) {
    CHECK(base == pages && length == 32768 && BUDDY_MAX_ORDER == 15 &&
          BUDDY_MAX_PAGES == 32768 && nr_free_pages() == length,
          "maximum-order descriptor fixture mismatch");
    for (size_t power = 1; power <= 32768; power *= 2) {
        for (int delta = -1; delta <= 1; delta++) {
            size_t requested = power + delta;
            if (requested == 0 || requested > 32768) {
                continue;
            }
            size_t actual = expected_length(requested), offset;
            struct Page *page = alloc_pages(requested);
            CHECK(descriptor_offset(page, base, length, actual, &offset) == 0 &&
                  offset % actual == 0, "max fixture failed n=%lu", (unsigned long)requested);
            CHECK(nr_free_pages() == length - actual, "max fixture rounding count wrong");
            for (size_t i = 0; i < actual; i++) {
                CHECK(!PageReserved(page + i) && !PageProperty(page + i) &&
                      page_ref(page + i) == 0 &&
                      page[i].property == (i == 0 ? requested : 0),
                      "bad maximum-range descriptor");
            }
            free_pages(page, requested);
            CHECK(nr_free_pages() == length, "max fixture release lost capacity");
        }
    }
    size_t before = nr_free_pages();
    uint64_t digest = buddy_test_state_digest();
    CHECK(alloc_pages(32769) == NULL && alloc_pages((size_t)-1) == NULL,
          "maximum-order overflow accepted");
    CHECK(nr_free_pages() == before && buddy_test_state_digest() == digest,
          "maximum-order failure changed state");
    buddy_test_check_structure();
    cprintf("[buddy_test] case7: orders 0..15 descriptor-only boundaries: PASS\n");
    return 0;
}

static uint32_t
next_random(uint32_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

/* Bitmap oracle: a block exists iff an aligned all-free interval exists.
 * Neither list order nor the allocator's choice among such blocks is assumed. */
static bool
oracle_available(size_t actual) {
    if (actual == 0 || actual > arena_length) {
        return 0;
    }
    size_t arena_index = ((uintptr_t)arena - (uintptr_t)pages) / sizeof(struct Page);
    for (size_t start = 0; start <= arena_length - actual; start++) {
        if ((arena_index + start) % actual != 0) {
            continue;
        }
        bool available = 1;
        for (size_t i = 0; i < actual; i++) {
            if (!registered[start + i] || occupied[start + i]) {
                available = 0;
                break;
            }
        }
        if (available) {
            return 1;
        }
    }
    return 0;
}

static int
case8_stress(struct Page *base, size_t length) {
    (void)base; (void)length;
    static const size_t requests[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33};
    uint32_t rng = TEST_SEED;
    cprintf("[buddy_test] case8: seed=0x%08x, rounds=%u, operations/round=%u\n",
            (unsigned int)TEST_SEED, TEST_ROUNDS, TEST_STEPS);
    for (current_round = 0; current_round < TEST_ROUNDS; current_round++) {
        for (current_step = 0; current_step < TEST_STEPS; current_step++) {
            size_t live = 0, empty = TEST_CAP;
            for (size_t i = 0; i < TEST_CAP; i++) {
                if (records[i].live) { live++; }
                else if (empty == TEST_CAP) { empty = i; }
            }
            uint32_t choice = next_random(&rng);
            if (live != 0 && ((choice & 3) == 0 || empty == TEST_CAP)) {
                size_t nth = next_random(&rng) % live;
                for (size_t i = 0; i < TEST_CAP; i++) {
                    if (records[i].live && nth-- == 0) {
                        CHECK(release(i) == 0, "stress release failed");
                        break;
                    }
                }
            } else {
                size_t requested = requests[next_random(&rng) %
                    (sizeof(requests) / sizeof(requests[0]))];
                bool available = oracle_available(expected_length(requested));
                int result = take(empty, requested, (uint8_t)next_random(&rng));
                CHECK(result >= 0 && ((result == 0) == available),
                      "bitmap oracle disagrees for n=%lu, available=%d result=%d",
                      (unsigned long)requested, available, result);
            }
            CHECK(check_all((current_step % 16) == 0) == 0,
                  "stress changed surviving data");
        }
        CHECK(recover_whole() == 0, "stress round did not recover original large block");
    }
    return 0;
}

static void
report_case(unsigned int number, const char *name, int result, unsigned int *passed) {
    cprintf("[buddy_test] case%u (%s): %s\n", number, name,
            result == 0 ? "PASS" : result == -2 ? "NOT COVERED" : "FAIL");
    if (result == 0) { (*passed)++; }
}

void
buddy_test(void) {
    assert(pmm_manager == &buddy_pmm_manager);
    size_t baseline = nr_free_pages();
    unsigned int passed = 0;
    int result;
    cprintf("[buddy_test] start, baseline free pages = %lu\n", (unsigned long)baseline);

    current_case = 1; current_round = current_step = 0;
    result = run_scope(whole_region, 1, case1_small);
    if (result == 0) { result = case1_global(baseline); }
    report_case(1, "basic, rounding and real size boundaries", result, &passed);

    current_case = 2; current_round = current_step = 0;
    report_case(2, "all split remainders and true exhaustion",
                run_scope(whole_region, 1, case2_split), &passed);

    current_case = 3; current_round = current_step = 0;
    report_case(3, "six-level merge in three release orders",
                run_scope(whole_region, 1, case3_merge), &passed);

    current_case = 4; current_round = current_step = 0;
    static const struct buddy_test_region four[] = {{0, 4}};
    static const struct buddy_test_region holes[] = {{0, 1}, {2, 1}};
    result = run_scope(four, 1, case4_nonbuddy);
    if (result == 0) { result = run_scope(holes, 2, case4_holes); }
    report_case(4, "non-buddies, occupied buddies and unregistered holes", result, &passed);

    current_case = 5; current_round = current_step = 0;
    report_case(5, "full requested-page data and interleaved reuse",
                run_scope(whole_region, 1, case5_data), &passed);

    current_case = 6; current_round = current_step = 0;
    report_case(6, "invalid, overflow, exhausted and fragmented failures",
                run_scope(whole_region, 1, case6_failure), &passed);

    current_case = 7; current_round = current_step = 0;
    static const struct buddy_test_region irregular[] = {{3, 29}};
    static const struct buddy_test_region singleton[] = {{17, 1}};
    static const struct buddy_test_region disjoint[] = {{41, 7}, {2, 9}, {20, 13}};
    static const struct buddy_test_region adjacent[] = {{32, 32}, {0, 16}, {16, 16}};
    result = run_scope(irregular, 1, case7_regions);
    if (result == 0) { result = run_scope(singleton, 1, case7_regions); }
    if (result == 0) { result = run_scope(disjoint, 3, case7_regions); }
    if (result == 0) { result = run_scope(adjacent, 3, case7_regions); }
    if (result == 0) { result = buddy_test_with_max_descriptors(case7_max_descriptors); }
    report_case(7, "irregular/multiple regions and max-order descriptors", result, &passed);
    cprintf("[buddy_test] NOT COVERED: actual order-15 (32768-page) physical allocation; "
            "firmware/kernel reserve part of the 128 MiB RAM\n");

    current_case = 8; current_round = current_step = 0;
    report_case(8, "fixed-seed bitmap-oracle allocation/recovery",
                run_scope(whole_region, 1, case8_stress), &passed);

    bool restored = nr_free_pages() == baseline;
    if (!restored) {
        cprintf("[buddy_test] FAIL final baseline: expected %lu, got %lu\n",
                (unsigned long)baseline, (unsigned long)nr_free_pages());
    }
    if (passed == 8 && restored) {
        buddy_test_check_structure();
        cprintf("[buddy_test] 8/8 required groups passed (coverage exclusions listed above); "
                "baseline restored to %lu\n", (unsigned long)baseline);
    } else {
        cprintf("[buddy_test] %u/8 groups passed; failures/uncovered groups retained above\n", passed);
    }
    /* Prevent the caller's generic check_alloc_page success message on failure. */
    assert(passed == 8 && restored);
}
#endif /* PMM_USE_BUDDY && ucore_test */
