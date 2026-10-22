#include <pmm.h>
#include <list.h>
#include <best_fit_pmm.h>
#include <stdio.h>

// lab2 function2-2 2410439
/* ================= 吴佳璐：静态状态与宏（free_area / free_list / nr_free / 前置声明） =================
 * 这一块要解决的问题：Best-Fit 分配器需要一份可在 alloc/free 之间持久保存的
 *   “全局空闲区状态”。框架测试 basic_c heck / best_fit_check 会直接读写
 *   free_list 与 nr_free，所以状态必须是文件级静态变量，并用宏起短名便于引用。
 * 关键不变量（贯穿整个文件）：
 *   - free_list 是按【物理地址升序】串起的空闲块链表，链表头为 &free_list；
 *     每个链表节点是“某空闲块首页”的 page_link，块大小只记在该首页的 property。
 *   - 链表中相邻的两个空闲块一定不相接（每次插入/释放后都会尝试合并）。
 *   - nr_free = 所有空闲块 property 之和，表示当前空闲页总数。
 * 算法思路：这里只做定义与声明；用 #define 把 free_area.free_list /
 *   free_area.nr_free 映射成 free_list / nr_free，后面各函数直接使用。
 * 参数与返回值：无参数、无返回值。
 * 与其它块的协作：init / init_memmap / alloc_pages / free_pages /
 *   nr_free_pages 全部依赖这三个名字；pmm_manager 结构体的 .check 指向本文件
 *   末尾框架自带的 best_fit_check，故此处先做前置声明。
 */
/* Keep the writable state used by the framework tests in one free_area. */
static free_area_t free_area;
#define free_list (free_area.free_list)
#define nr_free (free_area.nr_free)

static void best_fit_check(void);
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：best_fit_insert_block（按地址序插入并合并相邻块） =================
 * 这一块要解决的问题：把一个新的空闲块挂进 free_list 时，必须同时维持两个约束：
 *   (1) 链表保持物理地址升序；(2) 若新块与左/右邻居地址连续，必须立即合并成
 *   一个更大的块，防止外部碎片。
 * 关键不变量：链表有序；任意相邻两块在地址上不相接；块的大小仅记录在块首页的
 *   property，块内其余页的 PageProperty 位必须为 0。
 * 算法思路：
 *   a) 从表头向后遍历，找到第一个物理地址大于 base 的节点 le，把 base 插到它
 *      前面（list_add_before），从而维持升序；
 *   b) 检查前驱 prev：若 prev + prev->property == base（前驱尾页的下一页正好是
 *      base 首页），说明二者物理连续，把 base 的页数并入 prev、清除 base 的块标记
 *      并从链表摘除，然后把 base 改指向合并后的首页 prev，便于继续向后合并；
 *   c) 检查后继 next：若 base + base->property == next，把 next 并入 base，
 *      同样清除 next 的块标记并从链表摘除。
 * 参数与返回值：base 为待插入空闲块的首页（调用前其 property 已设为块页数），无返回值。
 * 与其它块的协作：free_pages / init_memmap 在把 base 初始化并累加 nr_free 之后
 *   调用本函数；本函数只负责“挂链 + 合并”，不改变 nr_free（合并只是拼接，页数不变）。
 */
/* Insert an initialized free block in address order, then merge neighbors.
 * The caller accounts for the newly freed pages; merging does not add pages. */
static void
best_fit_insert_block(struct Page *base)
{
    list_entry_t *le = list_next(&free_list); /* 从链表头开始查找插入位置 */
    while (le != &free_list && le2page(le, page_link) < base)
    { /* 跳过所有地址比 base 小的空闲块 */
        le = list_next(le);
    }
    list_add_before(le, &base->page_link); /* 插到 le 之前：保持链表按物理地址升序 */

    le = list_prev(&base->page_link); /* 取新块的前驱，尝试向前合并 */
    if (le != &free_list)
    { /* 前驱存在（不是链表头） */
        struct Page *prev = le2page(le, page_link);
        if (prev + prev->property == base)
        {                                     /* 前驱尾页紧挨 base 首页：物理连续 */
            prev->property += base->property; /* 前驱吞并 base，页数累加 */
            ClearPageProperty(base);          /* base 不再是块首页：清除块标记 */
            base->property = 0;               /* 并清空其 property */
            list_del(&base->page_link);       /* 把 base 从链表摘除 */
            base = prev;                      /* base 指向合并后的首页，便于再与后继比较 */
        }
    }

    le = list_next(&base->page_link); /* 取合并后新块的后继，尝试向后合并 */
    if (le != &free_list)
    { /* 后继存在 */
        struct Page *next = le2page(le, page_link);
        if (base + base->property == next)
        {                                     /* 本块尾页紧挨 next 首页：物理连续 */
            base->property += next->property; /* 把 next 并入 base，页数累加 */
            ClearPageProperty(next);          /* next 不再是块首页：清除块标记 */
            next->property = 0;               /* 并清空其 property */
            list_del(&next->page_link);       /* 把 next 从链表摘除 */
        }
    }
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：best_fit_init（初始化空闲链表） =================
 * 这一块要解决的问题：把分配器的全局状态恢复到“空”的初始状态。
 * 关键不变量：free_list 必须是合法的双向循环链表（表头自指），nr_free 归零；
 *   此后 init_memmap 再把真正可用的内存块插入进来。
 * 算法思路：list_init 让链表头指向自身形成空链表；nr_free 置零。
 * 参数与返回值：无参、无返回。
 * 协作关系：pmm_manager->init 会被 pmm_init 调用一次；框架测试里也用它重置状态。
 */
static void
best_fit_init(void)
{
    list_init(&free_list); /* 让链表头自指，形成一个空的双向循环链表 */
    nr_free = 0;           /* 空闲页计数清零 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：best_fit_init_memmap（登记一段连续物理内存为空闲块） =================
 * 这一块要解决的问题：把 page_init 检测出来、尚未被内核占用的连续 n 页内存
 *   （从 base 开始）交给分配器管理，使其成为初始空闲块。
 * 关键不变量：交给本函数的这些页此前必须被标记为 reserved（框架约定），
 *   进入本函数后要清掉 reserved/property 等标志、把引用计数置 0；
 *   最终只由块首页 base 携带 property=n，其余页 property=0。
 * 算法思路：逐页清空描述符 → 在首页设置块大小并标记为“空闲块首页” →
 *   nr_free 累加 n → 交给 best_fit_insert_block 按地址序插入并合并邻居。
 * 参数与返回值：base 为起始页描述符，n 为连续页数（>0）；无返回值。
 * 与其它块的协作：pmm_init → page_init 在末尾调用 init_memmap →
 *   本函数 → best_fit_insert_block，形成首次填充空闲链表的过程。
 */
static void
best_fit_init_memmap(struct Page *base, size_t n)
{
    assert(n > 0); /* 至少要把一个页纳入管理 */
    for (struct Page *p = base; p != base + n; p++)
    {                            /* 逐页清理描述符 */
        assert(PageReserved(p)); /* 约定：待管理的页此前必须是 reserved 状态 */
        p->flags = 0;            /* 清空所有标志位（含 reserved / property） */
        p->property = 0;         /* 块大小先清零，稍后只设置到首页 */
        set_page_ref(p, 0);      /* 空闲页的引用计数为 0 */
    }
    base->property = n;          /* 整块共 n 页，把块大小记在首页 */
    SetPageProperty(base);       /* 标记 base 为“空闲块首页” */
    nr_free += n;                /* 空闲页总数增加 n */
    best_fit_insert_block(base); /* 按地址序插入链表并与相邻块合并 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：best_fit_alloc_pages（Best-Fit 分配：选最小够用块并切分） =================
 * 这一块要解决的问题：从空闲链表中找出“容量 >= n 且尽可能小”的块（最贴合），
 *   分配这 n 页；若该块更大则把剩余部分切下来重新挂回链表。
 * 关键不变量：链表始终按物理地址升序；分配后返回的块首页 property 必须清零、
 *   且不再是空闲块首页；剩余块要重新标记为块首页；失败时不得改动任何状态。
 * 算法思路：
 *   a) 快速失败：请求页数超过 nr_free 总数则直接返回 NULL；
 *   b) 线性扫描整个链表，维护“当前最优块 best”：只要 p 满足 >= n 且比 best 更小
 *      就更新 best；由于链表按地址升序，遇到第一个恰好奇全匹配（property == n）
 *      即可提前退出——它就是地址最低的那个精确匹配；
 *   c) 若没有任何块够用则返回 NULL（“总页数够”不等于“存在够大的连续块”）；
 *   d) 摘除 best 整块，若其大于 n 就把从 best+n 开始的剩余部分作为新块
 *      （设置 property、标记首页）插回原位置；最后扣减 nr_free、清掉 best 的
 *      空闲标记，返回其首页。
 * 参数与返回值：n 为请求的连续页数（>0）；成功返回块首页指针，失败返回 NULL。
 * 与其它块的协作：free_pages 负责把内存还回链表；两者共同维护 free_list 的有序与
 *   不相接不变量；alloc_page / alloc_pages 是对本函数的宏/包装。
 */
static struct Page *
best_fit_alloc_pages(size_t n)
{
    assert(n > 0); /* 请求页数必须为正 */
    if (n > nr_free)
    { /* 空闲页总数都不够，直接失败 */
        return NULL;
    }

    struct Page *best = NULL; /* 记录当前最优（最贴合）的候选块 */
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list)
    { /* 遍历按地址升序的空闲链表，寻找最小够用块 */
        struct Page *p = le2page(le, page_link);
        if (p->property >= n &&
            (best == NULL || p->property < best->property))
        { /* 更贴合（更小）则更新最优块 */
            best = p;
            /* Address order makes the first exact match the lowest one. */
            if (p->property == n)
            { /* 找到恰好相等的大小即最优，可提前退出 */
                break;
            }
        }
    }
    /* A nonzero page count does not guarantee a suitable contiguous block. */
    if (best == NULL)
    { /* 总页数够但没有单个够大的连续块 */
        return NULL;
    }

    list_entry_t *prev = list_prev(&best->page_link); /* 先记住 best 的前驱，作为剩余块插回的锚点 */
    list_del(&best->page_link);                       /* 把 best 整块从链表摘除 */
    if (best->property > n)
    {                                           /* 块比请求大，需要切分 */
        struct Page *rest = best + n;           /* 剩余部分的首页 */
        rest->property = best->property - n;    /* 剩余页数 */
        SetPageProperty(rest);                  /* 标记 rest 为新的空闲块首页 */
        list_add_after(prev, &rest->page_link); /* 插回原 best 位置（prev 之后），维持地址序 */
    }
    nr_free -= n;            /* 扣除已分配页数，更新空闲计数 */
    ClearPageProperty(best); /* 分配出去的首页不再是空闲块首页 */
    best->property = 0;      /* 并清空其块大小 */
    return best;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：best_fit_free_pages（释放连续 n 页并归还空闲链表） =================
 * 这一块要解决的问题：把一段此前被分配的连续 n 页内存（从 base 开始）还回
 *   空闲链表，并尽量与相邻空闲块合并，恢复大块。
 * 关键不变量：待释放的页不能是 reserved、也不能已经是空闲块首页
 *   （即 !PageReserved 且 !PageProperty）；释放后这些页标志清零、引用计数清零、
 *   只由首页 base 携带 property=n；nr_free 相应增加。
 * 算法思路：逐页校验并清空描述符 → 首页设置块大小并标记为块首页 →
 *   nr_free 累加 n → 交给 best_fit_insert_block 按地址序插入并就地合并。
 * 参数与返回值：base 为被释放块的首页，n 为连续页数（>0）；无返回值。
 * 与其它块的协作：与 best_fit_alloc_pages 互为逆操作；合并逻辑全部复用
 *   best_fit_insert_block，因此释放后不会留下相接的碎片。
 */
static void
best_fit_free_pages(struct Page *base, size_t n)
{
    assert(n > 0); /* 释放页数必须为正 */
    for (struct Page *p = base; p != base + n; p++)
    {                                                 /* 逐页校验并重置描述符 */
        assert(!PageReserved(p) && !PageProperty(p)); /* 不能释放 reserved 页或已空闲的块首页 */
        p->flags = 0;                                 /* 清空标志位 */
        p->property = 0;                              /* 块大小先清零，稍后只设置到首页 */
        set_page_ref(p, 0);                           /* 引用计数归零 */
    }
    base->property = n;          /* 该空闲块共 n 页，记在首页 */
    SetPageProperty(base);       /* 标记 base 为“空闲块首页” */
    nr_free += n;                /* 空闲页总数增加 n */
    best_fit_insert_block(base); /* 插入链表并与左右相邻块合并 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：best_fit_nr_free_pages（查询空闲页总数） =================
 * 这一块要解决的问题：向框架/测试提供一个查询当前空闲页数量的接口。
 * 关键不变量：返回值必须等于 free_list 中所有块 property 之和，即 nr_free。
 * 算法思路：直接返回全局计数 nr_free（alloc/free 时已同步维护）。
 * 参数与返回值：无参；返回当前空闲页数 size_t。
 * 协作关系：pmm_manager->nr_free_pages 以及上层 nr_free_pages() 都走这里；
 *   框架测试用它校验 total == nr_free_pages()。
 */
static size_t
best_fit_nr_free_pages(void)
{
    return nr_free; /* 直接返回维护好的空闲页计数 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：best_fit_pmm_manager 结构体（实现 pmm_manager 接口绑定） =================
 * 这一块要解决的问题：把上面各个静态函数按 pmm_manager 约定的函数指针字段组装成
 *   一个可注册到框架的“物理内存管理器”实例。
 * 关键不变量：name 必须唯一且与测试期望一致（"best_fit_pmm_manager"）；
 *   init / init_memmap / alloc_pages / free_pages / nr_free_pages / check 六个回调
 *   必须全部指向本文件对应的实现函数。
 * 算法思路：C99 指定初始化器（.field = value）按字段名绑定函数地址。
 * 参数与返回值：这是一个 const 全局对象，供 pmm.c 的 init_pmm_manager 通过
 *   pmm_manager = &best_fit_pmm_manager 注册使用。
 * 注意：.check 指向的是框架自带的 best_fit_check（不属于吴佳璐负责区域）。
 */
const struct pmm_manager best_fit_pmm_manager = {
    .name = "best_fit_pmm_manager",          /* 管理器名称，与测试断言一致 */
    .init = best_fit_init,                   /* 初始化：清空链表与计数 */
    .init_memmap = best_fit_init_memmap,     /* 登记一段可用物理内存为空闲块 */
    .alloc_pages = best_fit_alloc_pages,     /* Best-Fit 分配入口 */
    .free_pages = best_fit_free_pages,       /* 释放并入链入口 */
    .nr_free_pages = best_fit_nr_free_pages, /* 空闲页数查询入口 */
    .check = best_fit_check,                 /* 框架自带的正确性自检函数 */
};
/* ================= 吴佳璐END ================= */

// Framework tests: students must not modify basic_check or best_fit_check.
static void
basic_check(void)
{
    struct Page *p0, *p1, *p2;
    p0 = p1 = p2 = NULL;
    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(p0 != p1 && p0 != p2 && p1 != p2);
    assert(page_ref(p0) == 0 && page_ref(p1) == 0 && page_ref(p2) == 0);

    assert(page2pa(p0) < npage * PGSIZE);
    assert(page2pa(p1) < npage * PGSIZE);
    assert(page2pa(p2) < npage * PGSIZE);

    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));

    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    assert(alloc_page() == NULL);

    free_page(p0);
    free_page(p1);
    free_page(p2);
    assert(nr_free == 3);

    assert((p0 = alloc_page()) != NULL);
    assert((p1 = alloc_page()) != NULL);
    assert((p2 = alloc_page()) != NULL);

    assert(alloc_page() == NULL);

    free_page(p0);
    assert(!list_empty(&free_list));

    struct Page *p;
    assert((p = alloc_page()) == p0);
    assert(alloc_page() == NULL);

    assert(nr_free == 0);
    free_list = free_list_store;
    nr_free = nr_free_store;

    free_page(p);
    free_page(p1);
    free_page(p2);
}

static void
best_fit_check(void)
{
    int score = 0, sumscore = 6;
    int count = 0, total = 0;
    list_entry_t *le = &free_list;
    while ((le = list_next(le)) != &free_list)
    {
        struct Page *p = le2page(le, page_link);
        assert(PageProperty(p));
        count++, total += p->property;
    }
    assert(total == nr_free_pages());

    basic_check();

#ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
    struct Page *p0 = alloc_pages(5), *p1, *p2;
    assert(p0 != NULL);
    assert(!PageProperty(p0));

#ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
    list_entry_t free_list_store = free_list;
    list_init(&free_list);
    assert(list_empty(&free_list));
    assert(alloc_page() == NULL);

#ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
    unsigned int nr_free_store = nr_free;
    nr_free = 0;

    // * - - * -
    free_pages(p0 + 1, 2);
    free_pages(p0 + 4, 1);
    assert(alloc_pages(4) == NULL);
    assert(PageProperty(p0 + 1) && p0[1].property == 2);
    // * - - * *
    assert((p1 = alloc_pages(1)) != NULL);
    assert(alloc_pages(2) != NULL);
    assert(p0 + 4 == p1);

#ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
    p2 = p0 + 1;
    free_pages(p0, 5);
    assert((p0 = alloc_pages(5)) != NULL);
    assert(alloc_page() == NULL);

#ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
    assert(nr_free == 0);
    nr_free = nr_free_store;

    free_list = free_list_store;
    free_pages(p0, 5);

    le = &free_list;
    while ((le = list_next(le)) != &free_list)
    {
        struct Page *p = le2page(le, page_link);
        count--, total -= p->property;
    }
    assert(count == 0);
    assert(total == 0);
#ifdef ucore_test
    score += 1;
    cprintf("grading: %d / %d points\n", score, sumscore);
#endif
}
