#!/usr/bin/env python3
"""Run the real Best-Fit C module against a host-side page model.

This supplements, and does not replace, RISC-V/QEMU kernel checks. The generated
adapter substitutes the machine-dependent pmm.h entry points and integer types;
Page, free_area_t, list operations, MMU constants and Best-Fit come from the
project. No allocator implementation is copied into this script.

Examples:
    python tools/test_best_fit_host.py --cc gcc
    python tools/test_best_fit_host.py --cc E:/Apps/MinGW/bin/gcc.exe
"""

import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


DEFS = r"""
#ifndef HOST_DEFS_H
#define HOST_DEFS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef size_t ppn_t;
#define to_struct(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif
"""

PMM = r"""
#ifndef HOST_PMM_H
#define HOST_PMM_H
#include <assert.h>
#include <defs.h>
#include <memlayout.h>
#include <mmu.h>
#include <stdio.h>
@MANAGER_DECLARATION@
extern const struct pmm_manager *pmm_manager;
extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *p, size_t n);
size_t nr_free_pages(void);
#define alloc_page() alloc_pages(1)
#define free_page(p) free_pages((p), 1)
#define cprintf printf
static inline uintptr_t page2pa(struct Page *p) {
    return ((uintptr_t)(p - pages) + nbase) << PGSHIFT;
}
static inline int page_ref(struct Page *p) { return p->ref; }
static inline void set_page_ref(struct Page *p, int n) { p->ref = n; }
#endif
"""

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <best_fit_pmm.c>

enum { TEST_PAGES = 256, UNAVAILABLE = 0, ALLOCATED = 1, FREE = 2 };
static struct Page test_pages[TEST_PAGES];
static unsigned char model[TEST_PAGES];
struct Page *pages = test_pages;
const size_t nbase = 0x80000000UL / PGSIZE;
size_t npage = 0x80000000UL / PGSIZE + TEST_PAGES;
const struct pmm_manager *pmm_manager = &best_fit_pmm_manager;

struct Page *alloc_pages(size_t n) { return pmm_manager->alloc_pages(n); }
void free_pages(struct Page *p, size_t n) { pmm_manager->free_pages(p, n); }
size_t nr_free_pages(void) { return pmm_manager->nr_free_pages(); }

/* The independent oracle is a per-page availability map, not a free list. */
static void verify(void) {
    unsigned char covered[TEST_PAGES] = {0};
    unsigned char heads[TEST_PAGES] = {0};
    size_t total = 0, previous_end = 0, blocks = 0;
    list_entry_t *le = list_next(&free_list), *previous = &free_list;
    while (le != &free_list) {
        assert(++blocks <= TEST_PAGES);
        struct Page *p = le2page(le, page_link);
        assert(p >= pages && p < pages + TEST_PAGES);
        size_t start = (size_t)(p - pages), length = p->property;
        assert(length > 0 && start + length <= TEST_PAGES);
        assert(PageProperty(p) && !PageReserved(p));
        assert(le->prev == previous && previous->next == le);
        /* Adjacent free ranges must already have coalesced. */
        assert(blocks == 1 || previous_end < start);
        heads[start] = 1;
        for (size_t i = start; i < start + length; ++i) {
            assert(model[i] == FREE && !covered[i]);
            assert(!PageReserved(pages + i) && page_ref(pages + i) == 0);
            covered[i] = 1;
        }
        total += length;
        previous_end = start + length;
        previous = le;
        le = list_next(le);
    }
    assert(free_list.prev == previous && previous->next == &free_list);
    for (size_t i = 0; i < TEST_PAGES; ++i) {
        assert(covered[i] == (model[i] == FREE));
        if (model[i] != UNAVAILABLE) {
            assert(!PageReserved(pages + i));
            assert(!!PageProperty(pages + i) == heads[i]);
            assert(heads[i] || pages[i].property == 0);
        } else {
            assert(PageReserved(pages + i));
        }
    }
    assert(total == nr_free_pages());
}

static void reset(void) {
    memset(test_pages, 0, sizeof(test_pages));
    memset(model, UNAVAILABLE, sizeof(model));
    for (size_t i = 0; i < TEST_PAGES; ++i) {
        SetPageReserved(pages + i);
        pages[i].ref = 9;
        pages[i].property = 123;
    }
    pmm_manager->init();
    verify();
}

static void add_region(size_t start, size_t length) {
    assert(length > 0 && start + length <= TEST_PAGES);
    for (size_t i = start; i < start + length; ++i) {
        assert(model[i] == UNAVAILABLE);
        model[i] = FREE;
    }
    pmm_manager->init_memmap(pages + start, length);
    verify();
}

static int oracle(size_t requested) {
    size_t best_length = TEST_PAGES + 1;
    int best = -1;
    for (size_t i = 0; i < TEST_PAGES;) {
        if (model[i] != FREE) { ++i; continue; }
        size_t start = i;
        while (i < TEST_PAGES && model[i] == FREE) ++i;
        size_t length = i - start;
        if (length >= requested && length < best_length) {
            best = (int)start;
            best_length = length;
        }
    }
    return best;
}

static int take(size_t requested) {
    struct Page before[TEST_PAGES];
    free_area_t before_area;
    memcpy(before, pages, sizeof(before));
    memcpy(&before_area, &free_area, sizeof(before_area));
    int expected = oracle(requested);
    struct Page *p = alloc_pages(requested);
    assert(p == (expected < 0 ? NULL : pages + expected));
    if (p == NULL) {
        /* Failed allocation must not mutate descriptors, links or counters. */
        assert(memcmp(before, pages, sizeof(before)) == 0);
        assert(memcmp(&before_area, &free_area, sizeof(before_area)) == 0);
    } else {
        for (size_t i = 0; i < requested; ++i) {
            assert(model[expected + i] == FREE);
            assert(!PageProperty(p + i) && page_ref(p + i) == 0);
            model[expected + i] = ALLOCATED;
        }
    }
    verify();
    return expected;
}

static void release(size_t start, size_t length) {
    assert(length > 0 && start + length <= TEST_PAGES);
    for (size_t i = start; i < start + length; ++i) {
        assert(model[i] == ALLOCATED);
        /* Deliberately dirty refs: free must reset them. */
        pages[i].ref = 7;
        model[i] = FREE;
    }
    free_pages(pages + start, length);
    verify();
}

static void reclaim_all(void) {
    for (size_t i = 0; i < TEST_PAGES;) {
        if (model[i] != ALLOCATED) { ++i; continue; }
        size_t start = i;
        while (i < TEST_PAGES && model[i] == ALLOCATED) ++i;
        release(start, i - start);
    }
    verify();
}

static void deterministic_checks(void) {
    reset();
    add_region(64, 4); add_region(0, 12); add_region(32, 7);
    assert(take(5) == 32);    /* Smallest sufficient, not first address. */
    assert(take(2) == 37);    /* Split remainder is immediately reusable. */
    assert(take(3) == 64);
    assert(nr_free_pages() == 13 && take(13) == -1); /* Fragmentation. */
    assert(take(TEST_PAGES + 1) == -1);
    reclaim_all();
    assert(nr_free_pages() == 23);
    puts("PASS: best fit, splitting, fragmented failure, failure immutability");

    reset();
    add_region(32, 6); add_region(8, 6);
    assert(take(6) == 8);    /* Lowest-address exact match. */
    reclaim_all();
    reset();
    add_region(32, 7); add_region(8, 7);
    assert(take(5) == 8);    /* Lowest-address non-exact tie. */
    reclaim_all();
    puts("PASS: equal-size candidates choose the lower address");

    reset();
    add_region(8, 16);
    assert(take(16) == 8);
    release(8, 4); release(20, 4); release(12, 8); /* Merge both sides. */
    assert(take(16) == 8);
    release(12, 4); release(8, 4); release(16, 8); /* Successor then predecessor. */
    assert(take(4) == 8 && take(4) == 12);
    release(8, 8);          /* Free multiple adjacent allocations together. */
    assert(nr_free_pages() == 16);
    puts("PASS: partial frees, combined frees, predecessor/successor coalescing");

    reset();
    add_region(32, 8); add_region(16, 8); add_region(24, 8);
    add_region(80, 5); add_region(100, 7);
    assert(take(24) == 16 && take(5) == 80 && take(7) == 100);
    assert(take(1) == -1);
    reclaim_all();
    assert(nr_free_pages() == 36);
    puts("PASS: out-of-order adjacent and disjoint init_memmap regions");
}

static uint32_t rng_state;
static uint32_t next_random(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static void randomized_checks(void) {
    const uint32_t seeds[] = {1, 2410439, 0x13579BDF, 0xC0FFEE};
    for (size_t seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); ++seed) {
        rng_state = seeds[seed];
        reset();
        add_region(151, 91); add_region(3, 57); add_region(71, 67);
        for (size_t step = 0; step < 5000; ++step) {
            if ((next_random() % 100) < 58) {
                (void)take(1 + next_random() % 40);
            } else {
                size_t start = next_random() % TEST_PAGES;
                for (size_t attempt = 0; attempt < TEST_PAGES; ++attempt) {
                    if (model[start] == ALLOCATED) break;
                    start = (start + 1) % TEST_PAGES;
                }
                if (model[start] != ALLOCATED) continue;
                size_t end = start;
                while (end < TEST_PAGES && model[end] == ALLOCATED) ++end;
                release(start, 1 + next_random() % (end - start));
            }
        }
        reclaim_all();
        assert(nr_free_pages() == 215);
        printf("PASS: bitmap oracle seed %lu, 5000 iterations, full recovery\n",
               (unsigned long)seeds[seed]);
    }
}

int main(void) {
    assert(strcmp(pmm_manager->name, "best_fit_pmm_manager") == 0);
    assert(pmm_manager->check == best_fit_check);
    reset();
    add_region(0, TEST_PAGES);
    pmm_manager->check();    /* Executes unmodified basic_check and best_fit_check. */
    verify();
    assert(nr_free_pages() == TEST_PAGES);
    puts("PASS: original basic_check + best_fit_check, full recovery");
    deterministic_checks();
    randomized_checks();
    puts("ALL HOST ALGORITHM CHECKS PASSED (not a QEMU/kernel boot test)");
    return 0;
}
"""


def run(command):
    print("+ " + shlex.join(str(part) for part in command), flush=True)
    subprocess.run(command, check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="gcc", help="Host GCC-compatible compiler executable")
    parser.add_argument("--sanitize", action="store_true",
                        help="Enable ASan/UBSan if this host compiler supports them")
    args = parser.parse_args()
    code = Path(__file__).resolve().parents[1]
    mm = code / "kern" / "mm"
    source = (mm / "pmm.h").read_text(encoding="utf-8")
    manager = re.search(r"struct pmm_manager\s*\{.*?\};", source, re.DOTALL)
    if manager is None:
        raise RuntimeError("Cannot locate the real pmm_manager declaration in pmm.h")
    run([args.cc, "--version"])
    with tempfile.TemporaryDirectory(prefix="ucore-best-fit-host-") as temporary:
        work = Path(temporary)
        (work / "defs.h").write_text(DEFS, encoding="utf-8")
        (work / "pmm.h").write_text(
            PMM.replace("@MANAGER_DECLARATION@", manager.group(0)), encoding="utf-8")
        (work / "check.c").write_text(HARNESS, encoding="utf-8")
        executable = work / ("check.exe" if os.name == "nt" else "check")
        command = [args.cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
                   "-Wno-sign-compare", "-Ducore_test", "-I", str(work),
                   "-I", str(mm), "-idirafter", str(code / "libs"),
                   str(work / "check.c"), "-o", str(executable)]
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
        run(command)
        run([str(executable)])


if __name__ == "__main__":
    main()
