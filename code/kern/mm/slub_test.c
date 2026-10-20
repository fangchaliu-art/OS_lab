#ifdef ucore_test
#include <defs.h>
#include <mmu.h>
#include <pmm.h>
#include <stdio.h>
#include <string.h>
#include <slub.h>
#include <slub_test.h>

/* 独立 SLUB 行为测试。
 *
 * 只依赖公开接口 slub_alloc / slub_free 与底层观测接口 nr_free_pages()，
 * 不访问 slub.c 的任何 static 内部状态；测试辅助记录用有明确容量的静态数组，
 * 避免用被测分配器保存自身记录。 */

#define SLUB_TEST_MIN_ALIGN   16
#define SLUB_TEST_MAX_ALLOCS  8192
#define SLUB_TEST_PROBE_MAX   512                    /* 单页对象容量上限（实现最大 252） */
#define SLUB_TEST_OOM_SIZE    (16 * PGSIZE)          /* 每个大对象约 17 页 */
#define SLUB_TEST_OOM_CAP     4096                   /* 压力测试数量上限 */

/* 与 slub.c 中 slub_sizes[] 完全一致的大小类别（依据实际实现抄录）。
 * 若实现调整大小类别，需同步此处。 */
static const size_t slub_test_sizes[] = {
    16,  32,  48,  64,  80,  96,  112, 128,
    160, 192, 224, 256, 320, 384, 448, 512,
    640, 768, 896, 1024,
};
#define SLUB_TEST_NR_SIZES (sizeof(slub_test_sizes) / sizeof(slub_test_sizes[0]))

struct slub_test_rec {
    void *ptr;
    size_t size;
    uint8_t seed;
    int live;
};

static struct slub_test_rec slub_test_recs[SLUB_TEST_MAX_ALLOCS];
static size_t slub_test_nrec;
static size_t slub_test_baseline;   /* slub_check 开始时的空闲页数 */
static void *slub_test_probe[SLUB_TEST_PROBE_MAX];

/* 每个用例失败时跳到 fail: 标签，统一释放已记录对象后返回 -1。 */
#define T_CHECK(cond, ...)                                            \
    do {                                                              \
        if (!(cond)) {                                                \
            cprintf("[slub_test] FAIL %s:%d: ", __func__, __LINE__);  \
            cprintf(__VA_ARGS__);                                     \
            cprintf("\n");                                            \
            goto fail;                                                \
        }                                                             \
    } while (0)

static int t_is_aligned(void *p) {
    return ((uintptr_t)p & (SLUB_TEST_MIN_ALIGN - 1)) == 0;
}

static void t_fill(void *p, size_t size, uint8_t seed) {
    memset(p, seed, size);
}

/* 返回首个不匹配字节下标；全部匹配返回 (size_t)-1。 */
static size_t t_find_mismatch(void *p, size_t size, uint8_t seed) {
    uint8_t *b = (uint8_t *)p;
    size_t i;
    for (i = 0; i < size; i++) {
        if (b[i] != seed) {
            return i;
        }
    }
    return (size_t)-1;
}

static void t_rec_reset(void) {
    slub_test_nrec = 0;
}

static int t_record(void *ptr, size_t size, uint8_t seed) {
    if (slub_test_nrec >= SLUB_TEST_MAX_ALLOCS) {
        return -1;
    }
    slub_test_recs[slub_test_nrec].ptr = ptr;
    slub_test_recs[slub_test_nrec].size = size;
    slub_test_recs[slub_test_nrec].seed = seed;
    slub_test_recs[slub_test_nrec].live = 1;
    slub_test_nrec++;
    return 0;
}

static void t_free_all(void) {
    size_t i;
    for (i = 0; i < slub_test_nrec; i++) {
        if (slub_test_recs[i].live) {
            slub_free(slub_test_recs[i].ptr);
            slub_test_recs[i].live = 0;
        }
    }
    slub_test_nrec = 0;
}

/* 检查 [p, p+size) 是否与任一存活记录区间重叠。 */
static int t_ranges_overlap(void *p, size_t size) {
    uintptr_t lo = (uintptr_t)p;
    uintptr_t hi = lo + size;
    size_t i;
    for (i = 0; i < slub_test_nrec; i++) {
        uintptr_t rlo, rhi;
        if (!slub_test_recs[i].live) {
            continue;
        }
        rlo = (uintptr_t)slub_test_recs[i].ptr;
        rhi = rlo + slub_test_recs[i].size;
        if (lo < rhi && rlo < hi) {
            return 1;
        }
    }
    return 0;
}

static int t_check_restored(const char *name) {
    if (nr_free_pages() != slub_test_baseline) {
        cprintf("[slub_test] FAIL %s: free pages %lu != baseline %lu\n",
                name, (unsigned long)nr_free_pages(),
                (unsigned long)slub_test_baseline);
        return -1;
    }
    return 0;
}

/* 分配 size 并检查：非空、16 字节对齐、请求范围首尾字节可读写，然后释放。
 * 返回 0 成功 / -1 失败（自行打印诊断，供 T_CHECK 包装）。 */
static int t_check_size(size_t size) {
    void *p;
    if (size == 0) {
        p = slub_alloc(0);
        if (p != NULL) {
            cprintf("[slub_test] FAIL: slub_alloc(0) = %p (expected NULL)\n", p);
            return -1;
        }
        return 0;
    }
    p = slub_alloc(size);
    if (p == NULL) {
        cprintf("[slub_test] FAIL: slub_alloc(%lu) = NULL\n", (unsigned long)size);
        return -1;
    }
    if (!t_is_aligned(p)) {
        cprintf("[slub_test] FAIL: slub_alloc(%lu) -> %p misaligned\n",
                (unsigned long)size, p);
        slub_free(p);
        return -1;
    }
    if (size == 1) {
        ((uint8_t *)p)[0] = 0xAB;
        if (((uint8_t *)p)[0] != 0xAB) {
            cprintf("[slub_test] FAIL: size 1 byte read-back mismatch\n");
            slub_free(p);
            return -1;
        }
    } else {
        ((uint8_t *)p)[0] = 0xAB;
        ((uint8_t *)p)[size - 1] = 0xCD;
        if (((uint8_t *)p)[0] != 0xAB || ((uint8_t *)p)[size - 1] != 0xCD) {
            cprintf("[slub_test] FAIL: size %lu first/last byte read-back mismatch\n",
                    (unsigned long)size);
            slub_free(p);
            return -1;
        }
    }
    slub_free(p);
    return 0;
}

/* 探测 size 对应大小类别在单个 slab 页中的对象容量：
 * 连续分配并观察页数第一次下降（建立首页）到第二次下降（进入第二页）之间的对象数。
 * 前提：该类别当前无其它存活对象，且空闲内存充足。 */
static size_t t_probe_capacity(size_t size) {
    size_t i, nalloc = 0, cap = 0;
    size_t start = nr_free_pages();
    size_t cur = start;

    for (i = 0; i < SLUB_TEST_PROBE_MAX; i++) {
        void *p = slub_alloc(size);
        if (p == NULL) {
            break;
        }
        slub_test_probe[i] = p;
        nalloc++;
        if (nr_free_pages() < cur) {
            if (cur == start) {
                cur = nr_free_pages();   /* 首页建立 */
            } else {
                break;                   /* 进入第二页：本对象是新页第一个 */
            }
        } else {
            cap = nalloc;                /* 仍在首页内 */
        }
    }

    for (i = 0; i < nalloc; i++) {
        slub_free(slub_test_probe[i]);
    }
    return cap;
}

/* Case 1：基本与大小边界。覆盖 0、1 字节、各大小类别边界前后、多页大对象。 */
static int check_case1(void) {
    size_t i, j;
    static const size_t large_sizes[] = {2048, 4096, 8192, 16384};
    size_t nlarge = sizeof(large_sizes) / sizeof(large_sizes[0]);

    t_rec_reset();

    /* 0 字节与空指针释放 */
    T_CHECK(t_check_size(0) == 0, "size 0");
    {
        size_t before = nr_free_pages();
        slub_free(NULL);
        T_CHECK(nr_free_pages() == before, "slub_free(NULL) changed free pages");
    }

    /* 1 字节 */
    T_CHECK(t_check_size(1) == 0, "size 1");

    /* 每个大小类别边界前后（S-1, S, S+1，其中 S+1 会跨到下一类别或大对象路径） */
    for (i = 0; i < SLUB_TEST_NR_SIZES; i++) {
        size_t s = slub_test_sizes[i];
        size_t szs[3] = {s - 1, s, s + 1};
        for (j = 0; j < 3; j++) {
            T_CHECK(t_check_size(szs[j]) == 0, "class %lu boundary size %lu",
                    (unsigned long)s, (unsigned long)szs[j]);
        }
    }

    /* 多页大对象 */
    for (i = 0; i < nlarge; i++) {
        T_CHECK(t_check_size(large_sizes[i]) == 0, "large size %lu",
                (unsigned long)large_sizes[i]);
    }

    t_free_all();
    return t_check_restored("case1");
fail:
    t_free_all();
    t_check_restored("case1");
    return -1;
}

/* Case 2：对象独立性。持有混合大小对象，交错分配释放，核对数据与区间不重叠。 */
static int check_case2(void) {
    size_t i, j;
    static const size_t sizes[] = {16, 32, 48, 64, 128, 256, 512, 1024, 2048, 4096};
    size_t n = sizeof(sizes) / sizeof(sizes[0]);

    t_rec_reset();

    for (i = 0; i < n; i++) {
        void *p = slub_alloc(sizes[i]);
        T_CHECK(p != NULL, "alloc(%lu) NULL", (unsigned long)sizes[i]);
        T_CHECK(t_is_aligned(p), "alloc(%lu) misaligned", (unsigned long)sizes[i]);
        T_CHECK(!t_ranges_overlap(p, sizes[i]), "alloc(%lu) overlaps earlier object",
                (unsigned long)sizes[i]);
        T_CHECK(t_record(p, sizes[i], (uint8_t)(0x10 + i)) == 0, "record overflow");
        t_fill(p, sizes[i], (uint8_t)(0x10 + i));
    }

    /* 交错释放奇数下标对象并重新申请，写入新模式 */
    for (j = 1; j < n; j += 2) {
        slub_free(slub_test_recs[j].ptr);
        slub_test_recs[j].live = 0;
        {
            void *q = slub_alloc(sizes[j]);
            T_CHECK(q != NULL, "realloc(%lu) NULL", (unsigned long)sizes[j]);
            T_CHECK(!t_ranges_overlap(q, sizes[j]), "realloc(%lu) overlaps",
                    (unsigned long)sizes[j]);
            slub_test_recs[j].ptr = q;
            slub_test_recs[j].seed = (uint8_t)(0x60 + j);
            slub_test_recs[j].live = 1;
            t_fill(q, sizes[j], (uint8_t)(0x60 + j));
        }
    }

    /* 核对所有存活对象的整个请求范围 */
    for (i = 0; i < n; i++) {
        size_t off = t_find_mismatch(slub_test_recs[i].ptr, sizes[i],
                                     slub_test_recs[i].seed);
        T_CHECK(off == (size_t)-1, "object %lu corrupted at byte %lu",
                (unsigned long)i, (unsigned long)off);
    }

    t_free_all();
    return t_check_restored("case2");
fail:
    t_free_all();
    t_check_restored("case2");
    return -1;
}

/* Case 3：页内复用与跨页扩展、空位复用。 */
static int check_case3(void) {
    size_t cap, i;
    size_t before;

    t_rec_reset();

    cap = t_probe_capacity(16);
    T_CHECK(cap >= 2, "probe capacity %lu < 2 (unexpected)", (unsigned long)cap);

    /* (a) 页内共享：cap 个 16 字节对象只占用 1 页，而非每对象一页 */
    before = nr_free_pages();
    for (i = 0; i < cap; i++) {
        void *p = slub_alloc(16);
        T_CHECK(p != NULL, "alloc(16) NULL at %lu", (unsigned long)i);
        T_CHECK(t_record(p, 16, (uint8_t)(i % 251 + 1)) == 0, "record overflow");
    }
    T_CHECK(nr_free_pages() == before - 1,
            "expected 1 page for %lu objects, used %lu",
            (unsigned long)cap, (unsigned long)(before - nr_free_pages()));

    /* (b) 跨页：再多 1 个对象触发第 2 页 */
    {
        void *p = slub_alloc(16);
        T_CHECK(p != NULL, "alloc(16) (cap+1) NULL");
        T_CHECK(t_record(p, 16, 0xEE) == 0, "record overflow");
    }
    T_CHECK(nr_free_pages() == before - 2,
            "expected 2 pages for cap+1 objects, used %lu",
            (unsigned long)(before - nr_free_pages()));

    /* (c) 空位复用：释放 5 个再申请 5 个，不应新增页 */
    for (i = 0; i < 5; i++) {
        slub_free(slub_test_recs[i].ptr);
        slub_test_recs[i].live = 0;
    }
    before = nr_free_pages();   /* 释放后仍占 2 页 */
    for (i = 0; i < 5; i++) {
        void *p = slub_alloc(16);
        T_CHECK(p != NULL, "reuse alloc NULL");
        T_CHECK(t_record(p, 16, (uint8_t)(0xA0 + i)) == 0, "record overflow");
    }
    T_CHECK(nr_free_pages() == before,
            "slot reuse should not allocate new pages, added %lu",
            (unsigned long)(before - nr_free_pages()));

    t_free_all();
    return t_check_restored("case3");
fail:
    t_free_all();
    t_check_restored("case3");
    return -1;
}

/* Case 4：回收。部分释放保留页，全部释放归还页；大对象完整归还多轮。 */
static int check_case4(void) {
    size_t i, cap;
    size_t before;

    t_rec_reset();

    /* (a) 小对象：部分释放保留页且剩余对象有效，全释放后整页归还 */
    cap = t_probe_capacity(16);
    T_CHECK(cap >= 2, "probe capacity %lu < 2", (unsigned long)cap);
    before = nr_free_pages();
    for (i = 0; i < cap; i++) {
        void *p = slub_alloc(16);
        T_CHECK(p != NULL, "alloc(16) NULL at %lu", (unsigned long)i);
        T_CHECK(t_record(p, 16, (uint8_t)(i % 251 + 1)) == 0, "record overflow");
        t_fill(p, 16, (uint8_t)(i % 251 + 1));
    }
    T_CHECK(nr_free_pages() == before - 1,
            "expected 1 page for %lu objects, used %lu",
            (unsigned long)cap, (unsigned long)(before - nr_free_pages()));

    /* 释放前一半：页不归还，剩余对象数据完好 */
    for (i = 0; i < cap / 2; i++) {
        slub_free(slub_test_recs[i].ptr);
        slub_test_recs[i].live = 0;
    }
    T_CHECK(nr_free_pages() == before - 1, "partial free must not return the page");
    for (i = cap / 2; i < cap; i++) {
        size_t off = t_find_mismatch(slub_test_recs[i].ptr, 16, slub_test_recs[i].seed);
        T_CHECK(off == (size_t)-1, "remaining object %lu corrupted at byte %lu",
                (unsigned long)i, (unsigned long)off);
    }

    /* 释放后一半：整页归还 */
    for (i = cap / 2; i < cap; i++) {
        slub_free(slub_test_recs[i].ptr);
        slub_test_recs[i].live = 0;
    }
    T_CHECK(nr_free_pages() == before, "all objects freed but page not returned");

    /* (b) 大对象完整归还，重复多轮暴露泄漏 */
    {
        static const size_t large_sizes[] = {1025, 2048, 4096, 16384};
        size_t n = sizeof(large_sizes) / sizeof(large_sizes[0]);
        size_t round;
        for (round = 0; round < 8; round++) {
            void *p[4];
            size_t j;
            size_t rbefore = nr_free_pages();
            for (j = 0; j < n; j++) {
                p[j] = slub_alloc(large_sizes[j]);
                T_CHECK(p[j] != NULL, "large alloc NULL (round %lu)", (unsigned long)round);
                t_fill(p[j], large_sizes[j], (uint8_t)(round + 1));
            }
            for (j = 0; j < n; j++) {
                size_t off = t_find_mismatch(p[j], large_sizes[j], (uint8_t)(round + 1));
                T_CHECK(off == (size_t)-1, "large object %lu corrupted (round %lu)",
                        (unsigned long)j, (unsigned long)round);
            }
            for (j = 0; j < n; j++) {
                slub_free(p[j]);
            }
            T_CHECK(nr_free_pages() == rbefore,
                    "large objects not fully returned (round %lu, leaked %lu)",
                    (unsigned long)round, (unsigned long)(rbefore - nr_free_pages()));
        }
    }

    t_free_all();
    return t_check_restored("case4");
fail:
    t_free_all();
    t_check_restored("case4");
    return -1;
}

/* Case 5：失败与恢复。溢出/超大请求返回 NULL 且页数不变；
 * 可控 OOM 不破坏已存活对象，清理后可重新申请。 */
static int check_case5(void) {
    size_t i, j;
    size_t before;
    void *keep[3] = {NULL, NULL, NULL};

    t_rec_reset();

    /* (a) 溢出与超大请求：NULL 且页数不变 */
    before = nr_free_pages();
    T_CHECK(slub_alloc(0) == NULL, "slub_alloc(0) != NULL");
    T_CHECK(slub_alloc((size_t)-1) == NULL, "slub_alloc(SIZE_MAX) != NULL");
    T_CHECK(slub_alloc((size_t)-2) == NULL, "slub_alloc(SIZE_MAX-1) != NULL");
    T_CHECK(slub_alloc((size_t)-16) == NULL, "slub_alloc(SIZE_MAX-15) != NULL");
    T_CHECK(slub_alloc((size_t)1 << 40) == NULL, "slub_alloc(1<<40) != NULL");
    T_CHECK(nr_free_pages() == before, "overflow requests changed free pages");

    /* (b) 先持有少量对象，再耗尽内存，验证失败不破坏它们、清理后可再申请 */
    for (j = 0; j < 3; j++) {
        keep[j] = slub_alloc(64);
        T_CHECK(keep[j] != NULL, "keep alloc NULL");
        t_fill(keep[j], 64, (uint8_t)(0x20 + j));
    }

    {
        size_t nbig = 0;
        int exhausted = 0;
        size_t before_fail = 0;
        for (i = 0; i < SLUB_TEST_OOM_CAP; i++) {
            before_fail = nr_free_pages();
            void *p = slub_alloc(SLUB_TEST_OOM_SIZE);
            if (p == NULL) {
                exhausted = 1;
                break;
            }
            T_CHECK(t_record(p, SLUB_TEST_OOM_SIZE, 0) == 0,
                    "record overflow at %lu", (unsigned long)i);
            nbig++;
        }
        if (exhausted) {
            T_CHECK(nr_free_pages() == before_fail, "failed alloc leaked pages");
            cprintf("[slub_test] case5: exhaustion reached after %lu big objects\n",
                    (unsigned long)nbig);
        } else {
            cprintf("[slub_test] case5: WARNING reached OOM cap %lu without exhaustion; "
                    "allocation-failure path NOT covered\n",
                    (unsigned long)SLUB_TEST_OOM_CAP);
        }
    }

    /* 失败未破坏已存活对象 */
    for (j = 0; j < 3; j++) {
        size_t off = t_find_mismatch(keep[j], 64, (uint8_t)(0x20 + j));
        T_CHECK(off == (size_t)-1, "keep object %lu corrupted during OOM",
                (unsigned long)j);
    }

    /* 清理大对象后可重新申请 */
    t_free_all();
    {
        void *p = slub_alloc(64);
        T_CHECK(p != NULL, "recovery alloc NULL after freeing big objects");
        slub_free(p);
    }

    /* 释放 keep 对象，恢复基线 */
    for (j = 0; j < 3; j++) {
        slub_free(keep[j]);
    }

    return t_check_restored("case5");
fail:
    t_free_all();
    for (j = 0; j < 3; j++) {
        if (keep[j] != NULL) {
            slub_free(keep[j]);
        }
    }
    t_check_restored("case5");
    return -1;
}

void slub_check(void) {
    int pass = 1;
    int r;

    slub_test_baseline = nr_free_pages();
    t_rec_reset();
    cprintf("[slub_test] start, baseline free pages = %lu\n",
            (unsigned long)slub_test_baseline);

    r = check_case1();
    cprintf("[slub_test] case1 (basic & size boundaries): %s\n", r ? "FAIL" : "PASS");
    pass = pass && (r == 0);

    r = check_case2();
    cprintf("[slub_test] case2 (object independence): %s\n", r ? "FAIL" : "PASS");
    pass = pass && (r == 0);

    r = check_case3();
    cprintf("[slub_test] case3 (in-page reuse & cross-page): %s\n", r ? "FAIL" : "PASS");
    pass = pass && (r == 0);

    r = check_case4();
    cprintf("[slub_test] case4 (reclaim): %s\n", r ? "FAIL" : "PASS");
    pass = pass && (r == 0);

    r = check_case5();
    cprintf("[slub_test] case5 (failure & recovery): %s\n", r ? "FAIL" : "PASS");
    pass = pass && (r == 0);

    if (pass) {
        cprintf("[slub_test] ALL CASES PASSED\n");
    } else {
        cprintf("[slub_test] SOME CASES FAILED\n");
    }
}
#endif /* ucore_test */
