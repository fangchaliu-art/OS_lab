#include <defs.h>
#include <list.h>
#include <memlayout.h>
#include <mmu.h>
#include <pmm.h>
#include <stdio.h>
#include <slub.h>

/* 简化的 SLUB 对象分配器。
 *
 * 两层结构：
 *   - 底层：复用现有页分配器 alloc_pages / free_pages 获取与归还整页；
 *   - 上层：在一页内切分若干固定大小的小对象，用内嵌空闲链表复用。
 *
 * 设计要点：
 *   - 小对象按固定大小类别（16 的倍数）管理，保证每个对象至少 16 字节对齐；
 *   - 页内元数据（slub_slab 头）放在页起始处，对象区紧随其后，
 *     元数据不占用任何用户请求空间，对象容量 = (PGSIZE - 头大小) / 对象大小；
 *   - 超过 SLUB_LARGE_MAX 的请求走连续多页大对象路径，头中记录页数以便释放；
 *   - 不修改 struct Page、不切换 pmm_manager，也不递归调用本模块分配自身元数据。
 *
 * 简化之处（与真实 SLUB 的差异）：
 *   - 无每 CPU 缓存 / 部分链表 / NUMA / 并发，所有 cache 用静态数组存储；
 *   - 空闲链表指针内嵌在空闲对象首 8 字节，无独立 freelist 元数据；
 *   - 复用空闲 slab 时按 cache 链表线性扫描，而非 O(1) 的部分链表；单核串行下足够。 */

#define SLUB_MIN_ALIGN  16              /* 最小对象对齐，也是所有对象大小的粒度 */
#define SLUB_LARGE_MAX  (PGSIZE / 4)    /* 1024：超过该值的请求按多页大对象处理 */

/* 大小类别：从小到大，均为 16 的倍数。请求 size 取“不小于 size”的最小类别。 */
static const size_t slub_sizes[] = {
    16,  32,  48,  64,  80,  96,  112, 128,
    160, 192, 224, 256, 320, 384, 448, 512,
    640, 768, 896, 1024,
};
#define SLUB_NR_CACHES  (sizeof(slub_sizes) / sizeof(slub_sizes[0]))

/* 每个大小类别一个 cache，其下挂所有属于该类别的 slab 页。 */
struct slub_cache {
    size_t obj_size;          /* 该类别的对象字节数（16 的倍数） */
    list_entry_t slabs;       /* 该类别的 slab 链表（部分/全满不区分，扫描复用） */
};

/* slab 头，存放在每个由本模块管理的物理页起始处。
 * 小对象 slab 与多页大对象共用同一结构，用 magic 区分。 */
#define SLUB_MAGIC_SLAB   0x534C5542u   /* "SLUB"：小对象 slab */
#define SLUB_MAGIC_LARGE  0x4C415247u   /* "LARG"：多页大对象 */

struct slub_slab {
    uint32_t magic;           /* SLUB_MAGIC_SLAB / SLUB_MAGIC_LARGE */
    uint32_t pad;             /* 使后续成员对齐到 8 字节边界 */
    list_entry_t link;        /* 挂入所属 cache 的 slab 链表（仅小对象） */
    struct slub_cache *cache; /* 所属 cache（仅小对象） */
    void *freelist;           /* 空闲对象链表头（仅小对象） */
    size_t obj_count;         /* 小对象：本页对象数；大对象：占用页数 */
    size_t free_count;        /* 小对象：剩余空闲对象数 */
};

static struct slub_cache slub_caches[SLUB_NR_CACHES];

/* 页内头部的字节大小，按最小对齐取整（不占用对象区之外的用户空间）。 */
static inline size_t slub_header_size(void) {
    return ROUNDUP(sizeof(struct slub_slab), SLUB_MIN_ALIGN);
}

/* 页描述符 -> 内核虚拟地址。KADDR 当前不可用，用已建立直接映射的偏移换算。 */
static inline void *page2kva(struct Page *page) {
    return (void *)(page2pa(page) + va_pa_offset);
}

/* 返回包含 ptr 的物理页起始处对应的 slab 头（内核虚拟地址）。
 * 约定：free 只会收到 slub_alloc 返回的原始指针，该指针必位于其所属页的首页内。 */
static inline struct slub_slab *slab_of(void *ptr) {
    uintptr_t pa = PADDR(ptr);
    return (struct slub_slab *)(ROUNDDOWN(pa, PGSIZE) + va_pa_offset);
}

#define le2slab(le)  to_struct((le), struct slub_slab, link)

/* 返回能容纳 size（<= SLUB_LARGE_MAX）的最小大小类别。 */
static struct slub_cache *slub_cache_for(size_t size) {
    size_t i;
    for (i = 0; i < SLUB_NR_CACHES; i++) {
        if (slub_sizes[i] >= size) {
            return &slub_caches[i];
        }
    }
    return NULL;
}

/* 初始化一个新 slab 页：设置头，并按对象大小建立内嵌空闲链表。 */
static void slub_slab_init(struct slub_slab *slab, struct slub_cache *cache) {
    char *base = (char *)slab + slub_header_size();
    size_t i;

    slab->magic = SLUB_MAGIC_SLAB;
    slab->pad = 0;
    slab->cache = cache;
    slab->obj_count = (PGSIZE - slub_header_size()) / cache->obj_size;
    slab->free_count = slab->obj_count;
    slab->freelist = NULL;
    for (i = 0; i < slab->obj_count; i++) {
        void *obj = base + i * cache->obj_size;
        *(void **)obj = slab->freelist;   /* 空闲对象首 8 字节存下一空闲对象 */
        slab->freelist = obj;
    }
}

static void *slub_alloc_small(size_t size) {
    struct slub_cache *cache = slub_cache_for(size);
    struct slub_slab *slab = NULL;
    list_entry_t *le;
    void *obj;

    /* 优先复用已有空闲对象的 slab。 */
    for (le = list_next(&cache->slabs); le != &cache->slabs; le = list_next(le)) {
        struct slub_slab *s = le2slab(le);
        if (s->free_count > 0) {
            slab = s;
            break;
        }
    }

    /* 容量不足时才申请新页；失败则返回 NULL，不泄漏。 */
    if (slab == NULL) {
        struct Page *page = alloc_page();
        if (page == NULL) {
            return NULL;
        }
        slab = (struct slub_slab *)page2kva(page);
        slub_slab_init(slab, cache);
        list_add(&cache->slabs, &slab->link);
    }

    obj = slab->freelist;
    slab->freelist = *(void **)obj;
    slab->free_count--;
    return obj;
}

static void *slub_alloc_large(size_t size) {
    size_t total, total_bytes, npages;
    struct Page *page;
    struct slub_slab *slab;

    /* 逐级做溢出检查：对齐取整 -> 叠加页头 -> 按页取整。 */
    if (size > ~(size_t)0 - (SLUB_MIN_ALIGN - 1)) {
        return NULL;
    }
    total = ROUNDUP(size, SLUB_MIN_ALIGN);
    if (total > ~(size_t)0 - slub_header_size()) {
        return NULL;
    }
    total_bytes = slub_header_size() + total;
    /* ROUNDUP adds PGSIZE - 1 before rounding down; reject overflow. */
    if (total_bytes > ~(size_t)0 - (PGSIZE - 1)) {
        return NULL;
    }
    npages = ROUNDUP(total_bytes, PGSIZE) / PGSIZE;

    page = alloc_pages(npages);
    if (page == NULL) {
        return NULL;
    }
    slab = (struct slub_slab *)page2kva(page);
    slab->magic = SLUB_MAGIC_LARGE;
    slab->pad = 0;
    slab->cache = NULL;
    slab->freelist = NULL;
    slab->obj_count = npages;   /* 复用 obj_count 记录占用页数 */
    slab->free_count = 0;
    return (void *)((char *)slab + slub_header_size());
}

static void slub_free_small(void *ptr, struct slub_slab *slab) {
    *(void **)ptr = slab->freelist;   /* 挂回空闲链表头 */
    slab->freelist = ptr;
    slab->free_count++;

    /* 最后一个对象释放后，将整页归还底层页分配器。 */
    if (slab->free_count == slab->obj_count) {
        struct Page *page;
        list_del(&slab->link);
        page = pa2page(PADDR((uintptr_t)slab));
        free_page(page);
    }
}

static void slub_free_large(struct slub_slab *slab) {
    struct Page *page = pa2page(PADDR((uintptr_t)slab));
    free_pages(page, slab->obj_count);   /* obj_count 存的是页数 */
}

void slub_init(void) {
    size_t i;
    for (i = 0; i < SLUB_NR_CACHES; i++) {
        slub_caches[i].obj_size = slub_sizes[i];
        list_init(&slub_caches[i].slabs);
    }
    cprintf("slub: %lu size classes (16..%lu), header %lu bytes, "
            "large-object threshold %lu, free-page baseline %lu\n",
            (unsigned long)SLUB_NR_CACHES,
            (unsigned long)slub_sizes[SLUB_NR_CACHES - 1],
            (unsigned long)slub_header_size(),
            (unsigned long)SLUB_LARGE_MAX,
            (unsigned long)nr_free_pages());
}

void *slub_alloc(size_t size) {
    if (size == 0) {
        return NULL;
    }
    if (size <= SLUB_LARGE_MAX) {
        return slub_alloc_small(size);
    }
    return slub_alloc_large(size);
}

void slub_free(void *ptr) {
    struct slub_slab *slab;

    if (ptr == NULL) {
        return;
    }

    slab = slab_of(ptr);
    /* 仅做页头 magic 校验：能拦下绝大多数非本模块指针；
     * 对页内偏移指针 / 重复释放不做完整检测（不属于有效接口输入）。 */
    if (slab->magic == SLUB_MAGIC_SLAB) {
        slub_free_small(ptr, slab);
    } else if (slab->magic == SLUB_MAGIC_LARGE) {
        slub_free_large(slab);
    } else {
        panic("slub_free: pointer 0x%016lx is not managed by slub",
              (uintptr_t)ptr);
    }
}
