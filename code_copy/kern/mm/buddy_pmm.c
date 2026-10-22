/* ================= 吴佳璐：文件头与 Buddy 算法总体设计说明 =================
 * 本文件实现 lab2 任务三的 Buddy System（伙伴系统）物理页分配算法（学号 2410439）。
 * 引入的头文件：
 *   - buddy_pmm.h：本模块的容量常量与 pmm_manager 声明；
 *   - buddy_test.h：配套测试框架接口（仅 PMM_USE_BUDDY 下才有实质内容）；
 *   - list.h：内核双向链表，用于把空闲块“头页”串成各阶空闲链表；
 *   - stdio.h / string.h：cprintf 打印与 memset/memcpy。
 *
 * 总体设计要点（下面这段原有英文注释是核心不变量，必须理解）：
 *   1) 全模块用「pages[] 数组下标」描述页面，下标相对 DRAM_BASE，而不是“第几个空闲页”。
 *      这样下标与物理页号一一对应，便于用位运算直接找伙伴。
 *   2) 一个 order-k 块由 2^k 个连续页组成，块起始下标 i 必是 2^k 的倍数，
 *      其伙伴块起始下标就是 i ^ (1 << k)（异或回一位即可）。
 *   3) 只有“空闲块的头页”（FREE_HEAD）才携带 PageProperty，且 property 等于该块长度（页数）；
 *      分配出去时，头页 property 记录“用户最初请求的页数 n”，而真正分配的阶（可能向上取整）
 *      保存在私有元数据 meta 里。
 *   4) 所有“尾页”（空闲块尾 / 分配块尾）的 property 与 order 一律为 0。
 *   5) 上述信息全部记在私有的 meta 数组里，不需要修改公共 struct Page 的语义。
 */
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
/* ================= 吴佳璐END ================= */
/* ================= 吴佳璐：页状态枚举、私有元数据与“活跃实例”切换 =================
 * 这一块定义 Buddy 分配器的“记账方式”。内核公共的 struct Page 只提供 ref/flags/property
 * 等有限字段，不足以表达伙伴系统需要的“这块的阶是多少、它是头还是尾”。因此这里额外维护
 * 一份私有元数据数组，并配合少量公共标记（PageProperty + property 值）来表达块信息。
 *
 * enum buddy_page_state：每个页在 Buddy 视角下的 5 种状态：
 *   - BUDDY_UNMANAGED ：还没被 init_memmap 登记（保留区 / 未登记空洞）；
 *   - BUDDY_FREE_HEAD ：某空闲块的头页，页上有 PageProperty 且 property == 块长；
 *   - BUDDY_FREE_TAIL ：某空闲块的尾页，property/order 均为 0；
 *   - BUDDY_ALLOC_HEAD：已分配块头页，property == 用户原始请求 n，order == 实际阶；
 *   - BUDDY_ALLOC_TAIL：已分配块尾页，property/order 均为 0。
 *
 * struct buddy_page_meta：每页 2 字节的私有记录（state + order），与 pages[] 下标一一对应。
 *
 * struct buddy_state：整个分配器的状态：
 *   - free_list[order]：各阶空闲链表头（order 从 0 到 BUDDY_MAX_ORDER，共 16 条），
 *     链表节点借用每个空闲块“头页”的 page_link 字段串接；
 *   - nr_free：当前空闲页总数（对外 nr_free_pages() 的答案，必须与链表内容一致）；
 *   - nr_managed：已被 init_memmap 登记管理的总页数（含已分配出去的），供 check 对账；
 *   - meta[]：上面说的私有元数据，大小为 BUDDY_MAX_PAGES；
 *   - seen[]：check_structure 专用的“重复/覆盖检测”位图，必须放静态存储而不能放内核栈
 *     （32768 字节栈放不下），所以它和 meta 一样是编译期静态数组。
 */
enum buddy_page_state
{
    BUDDY_UNMANAGED = 0, /* 尚未被 init_memmap 登记（保留区 / 未登记空洞） */
    BUDDY_FREE_HEAD,     /* 空闲块头页：带 PageProperty 且 property == 块长 */
    BUDDY_FREE_TAIL,     /* 空闲块尾页：property/order 均为 0 */
    BUDDY_ALLOC_HEAD,    /* 已分配块头页：property == 用户请求 n，order == 实际阶 */
    BUDDY_ALLOC_TAIL,    /* 已分配块尾页：property/order 均为 0 */
};

struct buddy_page_meta
{
    uint8_t state; /* 上表中的 5 种状态，与 pages[] 下标一一对应 */
    uint8_t order; /* 仅头页有意义：该块是 order-k 块 */
};

struct buddy_state
{
    list_entry_t free_list[BUDDY_MAX_ORDER + 1];  /* order 0..15 共 16 条空闲链表头 */
    size_t nr_free;                               /* 当前空闲页总数（对外 nr_free_pages 的答案） */
    size_t nr_managed;                            /* 已登记管理的总页数（含已分配出去的），用于 check 对账 */
    struct buddy_page_meta meta[BUDDY_MAX_PAGES]; /* 每页私有元数据 */
    /* Static scratch: a 32768-byte overlap map must not use the kernel stack. */
    uint8_t seen[BUDDY_MAX_PAGES]; /* check 专用覆盖位图，必须静态存放（32768 字节，栈放不下） */
};
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：活跃状态实例的切换（buddy_live / buddy_active） =================
 * 分配器内部统一用宏 buddy 访问当前“活跃的状态结构体”，默认活跃的是生产实例 buddy_live。
 * 之所以引入 buddy_active 指针而不是直接用全局变量：测试需要把分配器临时切换到一份独立的
 * 测试状态 buddy_test_state 上运行，跑完再切回来，从而不污染生产分配器的空闲链表和元数据
 * （见文件后半部分 buddy_test_with_regions / buddy_test_with_max_descriptors）。
 *
 * 关键约束：切实例时绝不去“拷贝”活跃链表头。所有链表哨兵必须始终留在各自原本的存储里，
 * 否则复制出来的 free_list 哨兵会指向错误地址、破坏链表。因此在非测试构建
 * （未定义 PMM_USE_BUDDY）下就退化为一个普通的静态 buddy 实例。
 */
#ifdef PMM_USE_BUDDY
/* The test instance is selected only while the live allocator owns its arena.
 * Never copy live list heads: every sentinel remains in its original storage. */
static struct buddy_state buddy_live;                  /* 生产实例：真正在用的分配器状态 */
static struct buddy_state *buddy_active = &buddy_live; /* 当前活跃实例，默认指向生产实例 */
#define buddy (*buddy_active)                          /* 全模块统一经由该宏访问活跃状态 */
#else
static struct buddy_state buddy; /* 非测试构建：直接用一个静态实例 */
#endif
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：辅助函数 buddy_block_pages（阶 -> 页数换算） =================
 * 输入 order（阶），返回该阶块包含的页数，即 2^order。
 * 用移位而不是 pow/循环，既快又能避免浮点；assert 保证 order 不越界（<= 15），
 * 否则 1<<order 可能溢出无符号范围。它是整个模块“阶 <-> 页数”换算的唯一入口，
 * 后续的分裂、合并、链表长度、边界检查都基于它。
 */
static size_t
buddy_block_pages(unsigned int order)
{
    assert(order <= BUDDY_MAX_ORDER); /* 阶不能超过最高支持阶，防左移溢出 */
    return (size_t)1 << order;        /* 2^order：order-k 块含 2^k 个连续页 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：辅助函数 buddy_page_limit（可管理页数上限） =================
 * 返回“可管理的页数上限” = npage - nbase。
 *   - nbase 是物理内存起始页号，npage 是总页数，二者之差即真实可管理的页数；
 *   - 断言 npage>=nbase 且该差值不超过 BUDDY_MAX_PAGES，保证所有下标运算都在容量内。
 * 这个上限在 buddy_in_range、check_structure、以及页号换算里反复出现，是边界检查的基准。
 */
static size_t
buddy_page_limit(void)
{
    assert(npage >= nbase && npage - nbase <= BUDDY_MAX_PAGES); /* 保证容量不越界 */
    return npage - nbase;                                       /* 可管理页数 = 总页数 - 起始页号 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：辅助函数 buddy_in_range（区间越界检查） =================
 * 判断「从 index 开始、长度 length 的连续区间」是否完全落在 [0, limit) 内。
 * 采用 index < limit && length <= limit - index 的写法：先比较 index 与 limit，
 * 再做减法，避免无符号下溢（若先算 limit-index 且 index>limit 会得到一个巨大的正数）。
 * 任何“构造/解引用候选伙伴地址”之前，都必须先用它确认数值在界内。
 */
/* Check numeric bounds before constructing/dereferencing a candidate buddy. */
static bool
buddy_in_range(size_t index, size_t length)
{
    size_t limit = buddy_page_limit();
    return index < limit && length <= limit - index; /* 起始在界内 且 长度不越过上界 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：辅助函数 buddy_page_index（描述符指针 -> 页下标） =================
 * 把 struct Page *（页描述符指针）换算成它相对 pages[] 起始的“页下标”：
 *   1) 断言 pages 非空且 page 地址不小于 pages 首地址（禁止向前越界）；
 *   2) 用字节地址差 offset = address - first；
 *   3) 断言 offset 能被 sizeof(struct Page) 整除，否则说明传入了非对齐/非法指针；
 *   4) 除以描述符大小得到下标；
 *   5) 最后用 buddy_in_range(index,1) 再确认这个下标确实是单页且在界内。
 * 该下标即为伙伴计算所用的统一坐标（相对 DRAM_BASE）。
 */
static size_t
buddy_page_index(struct Page *page)
{
    uintptr_t address = (uintptr_t)page;         /* 页描述符的真实字节地址 */
    uintptr_t first = (uintptr_t)pages;          /* pages[] 数组首地址 */
    assert(pages != NULL && address >= first);   /* pages 非空且指针不指向数组之前 */
    size_t offset = address - first;             /* 相对首地址的字节偏移 */
    assert(offset % sizeof(struct Page) == 0);   /* 必须整描述符对齐 */
    size_t index = offset / sizeof(struct Page); /* 换算为页下标 */
    assert(buddy_in_range(index, 1));            /* 该下标必须是合法单页 */
    return index;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：辅助函数 buddy_request_order（请求页数 -> 向上取整阶） =================
 * 把“用户请求的页数 n”向上取整成最小的 order，使 2^order >= n，写入 *order。
 *   - 先做容量校验：n==0 或 n > BUDDY_MAX_PAGES 直接返回 0（失败），
 *     这样后面的循环左移才不可能溢出，也把非法请求挡在分配逻辑之外；
 *   - 再从 order=0 起循环翻倍（buddy_block_pages(*order) < n 就 order++）。
 * 返回值：1 表示成功并已写回 *order；0 表示请求非法/超容量。
 * 它是 alloc/free 做“阶换算 + 合法性检查”的统一入口，保证请求不因取整而溢出。
 */
/* Reject out-of-capacity requests before rounding; shifts cannot overflow. */
static bool
buddy_request_order(size_t n, unsigned int *order)
{
    if (n == 0 || n > BUDDY_MAX_PAGES)
    { /* 0 页非法；超过最大页数直接拒绝 */
        return 0;
    }
    *order = 0;
    while (buddy_block_pages(*order) < n)
    { /* 翻倍直到块长 >= 请求页数 */
        (*order)++;
    }
    return 1;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：辅助函数 buddy_unlink_free（把空闲块头摘链） =================
 * 把一个当前处于空闲链表中的头页（index 处、状态 FREE_HEAD）从链表里摘下来，
 * 并把它“降级”为 FREE_TAIL：
 *   - 断言该页状态确为 FREE_HEAD 且带 PageProperty（否则说明调用者记账有误）；
 *   - list_del_init 把自己从双向链表移除并复位自身前后指针；
 *   - 清 PageProperty、property 归 0（尾页不再表示块头信息）；
 *   - 私有元数据改为 FREE_TAIL、order 归 0。
 * 注意：本函数只“摘链并降级”，不负责合并（合并逻辑在 buddy_insert_free 里）。
 */
static void
buddy_unlink_free(size_t index)
{
    struct Page *page = pages + index;
    assert(buddy.meta[index].state == BUDDY_FREE_HEAD); /* 只能摘空闲块头 */
    assert(PageProperty(page));                         /* 头页必须带 property 标记 */
    list_del_init(&page->page_link);                    /* 从对应阶链表摘除自身 */
    ClearPageProperty(page);                            /* 清公共标记 */
    page->property = 0;                                 /* 尾页 property 归 0 */
    buddy.meta[index].state = BUDDY_FREE_TAIL;          /* 降级为尾页 */
    buddy.meta[index].order = 0;                        /* 尾页 order 归 0 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：辅助函数 buddy_link_free（把空闲块头挂链） =================
 * 把下标 index 处、阶为 order 的空闲块头登记到 free_list[order] 上，并标记为 FREE_HEAD：
 *   - 断言区间在界内且 index 按 2^order 对齐（块首必须对齐，否则伙伴算法不成立）；
 *   - 断言该页当前状态是 FREE_TAIL（只能把“干净的尾页”提升为头，防止重复挂链）；
 *   - 写入私有 meta：state=FREE_HEAD、order=order；
 *   - 公共 Page：property=块长(2^order) 并 SetPageProperty（头页携带块长是核心不变量）；
 *   - list_add 挂到对应阶的空闲链表头。
 * 它是“分裂”和“合并”共用的落点：任何变回空闲的块最终都经由它进入链表。
 */
static void
buddy_link_free(size_t index, unsigned int order)
{
    size_t length = buddy_block_pages(order);
    assert(buddy_in_range(index, length) && (index & (length - 1)) == 0); /* 区间合法且块首对齐 */
    assert(buddy.meta[index].state == BUDDY_FREE_TAIL);                   /* 只能提升干净的尾页为头 */
    struct Page *page = pages + index;
    buddy.meta[index].state = BUDDY_FREE_HEAD;           /* 标记为空闲块头 */
    buddy.meta[index].order = order;                     /* 记录该块的阶 */
    page->property = length;                             /* 头页 property = 块长（核心不变量） */
    SetPageProperty(page);                               /* 置 PageProperty 标记 */
    list_add(&buddy.free_list[order], &page->page_link); /* 挂入对应阶空闲链表 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：辅助函数 buddy_insert_free（插入空闲块并向上合并） =================
 * 把一个刚变成空闲、阶为 order、起点 index 的块“先合并再挂链”。
 * 前置条件：该块内所有页都已是 FREE_TAIL（调用者已把状态清干净）；只有已登记且同阶的
 * 空闲头才能参与合并，相邻但非伙伴的块、以及未登记的空洞都不能合并。
 *
 * 循环思路（不断尝试把 index 这一块与其伙伴合成更高阶）：
 *   - 计算伙伴下标 other = index ^ length（length = 2^order）；
 *   - 若伙伴越界、或伙伴不是“同阶空闲头”，则无法合并，break 退出循环；
 *   - 否则把伙伴摘链（buddy_unlink_free(other)），合并后的新起点取两者较小下标
 *     （保证块首仍对齐），阶 +1，继续向上尝试；
 *   - 循环结束后用 buddy_link_free 把最终形成的块挂到 free_list[最终阶]。
 * 关键点：伙伴判定的前提是“同阶”——低阶的邻近块不能和本块直接合并。
 */
/* All pages in the input block are FREE_TAIL; only registered, same-order
 * free heads can participate. Adjacent non-buddies and holes cannot merge. */
static void
buddy_insert_free(size_t index, unsigned int order)
{
    while (order < BUDDY_MAX_ORDER)
    { /* 逐级尝试向上合并，最高到 MAX_ORDER */
        size_t length = buddy_block_pages(order);
        size_t other = index ^ length;                    /* 同阶伙伴块的起始下标 */
        if (!buddy_in_range(other, length) ||             /* 伙伴越界 */
            buddy.meta[other].state != BUDDY_FREE_HEAD || /* 伙伴不是空闲头 */
            buddy.meta[other].order != order)
        {          /* 伙伴不是同阶 */
            break; /* 三个条件任一成立即不能合并，退出 */
        }
        buddy_unlink_free(other); /* 伙伴可合并：先把它摘链降级 */
        if (other < index)
        {
            index = other; /* 合并后块首取较小下标，保持对齐 */
        }
        order++; /* 阶 +1，继续尝试与更高级伙伴合并 */
    }
    buddy_link_free(index, order); /* 把最终合并成的块登记入链 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_init（初始化分配器状态） =================
 * 把状态结构体整体清零，并把全部 16 条空闲链表初始化为“空链表哨兵”：
 *   - memset(&buddy,0,...) 清掉 meta/seen/nr_free/nr_managed，等价于“没有管理任何页”；
 *   - 对 order=0..BUDDY_MAX_ORDER 逐个 list_init，建立空链表头；
 *   - 最后的断言 (nbase & (BUDDY_MAX_PAGES-1)) == 0 保证物理内存起始页号按最大块
 *     （2^15 页）对齐。这样“相对 pages[] 的下标对齐”就等价于“真实物理块对齐”，
 *     伙伴算法成立所需的两条对齐条件同时满足。
 * 注意：init 只负责“清空记账”，页的登记由 buddy_init_memmap 负责。
 */
static void
buddy_init(void)
{
    memset(&buddy, 0, sizeof(buddy)); /* 清空全部状态：meta/seen/nr_free/nr_managed 归零 */
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++)
    {
        list_init(&buddy.free_list[order]); /* 初始化 16 条空闲链表为空链表哨兵 */
    }
    /* This makes relative-index alignment also physical block alignment. */
    assert((nbase & (BUDDY_MAX_PAGES - 1)) == 0); /* 起始页号按最大块(2^15 页)对齐 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_init_memmap（登记一段连续可用内存） =================
 * 参数 base 是这段内存的首个页描述符，n 是页数。功能：把 [base, base+n) 这段页纳入
 * Buddy 管理，并把它们组织成若干“尽可能大的、对齐的空闲块”。过程分两段：
 *   (1) 逐页“标准化”：断言这些页原来是 Reserved（来自内存探测的可用区），且尚未被
 *       UNMANAGED 之外的状态登记过；然后把 flags/property 清零、ref 置 0，
 *       私有 meta 先统一置为 FREE_TAIL / order=0（先假设都是散页尾页，稍后再合块）。
 *   (2) 贪心地把剩余页切成“对齐的最大块”：
 *       - 从当前 index 出发，在 order 上尽量往大推：只要更大的块 (2^(order+1) 页)
 *         既不超过 remaining、index 又按该大小对齐，就 order++；
 *       - 得到本段可用最大阶后，调 buddy_insert_free(index, order) 入链
 *         （插入时会顺手和左右伙伴合并，所以这里不必自己合并）；
 *       - index += length、remaining -= length，继续处理下一段。
 *   最后累加 nr_free（空闲总数）与 nr_managed（被管理的总页数，含之后会分配出去的）。
 * 这样登记后，空闲链表里就是若干“大小不一但都对齐”的块，且 nr_free == n。
 */
static void
buddy_init_memmap(struct Page *base, size_t n)
{
    assert(n > 0);
    size_t index = buddy_page_index(base); /* 把首页描述符换算成 pages[] 下标 */
    assert(buddy_in_range(index, n));      /* 整段区间必须落在可管理范围内 */
    for (size_t i = index; i < index + n; i++)
    {
        assert(PageReserved(pages + i));                /* 原为 Reserved（内存探测的可用区） */
        assert(buddy.meta[i].state == BUDDY_UNMANAGED); /* 尚未登记过，防止重复登记 */
        pages[i].flags = 0;                             /* 清公共标志 */
        pages[i].property = 0;                          /* 清 property */
        set_page_ref(pages + i, 0);                     /* ref 归 0：不再是保留状态 */
        buddy.meta[i].state = BUDDY_FREE_TAIL;          /* 先统一当作空闲尾页 */
        buddy.meta[i].order = 0;
    }

    size_t remaining = n; /* 尚未切块的剩余页数 */
    while (remaining > 0)
    {
        unsigned int order = 0;
        while (order < BUDDY_MAX_ORDER)
        {
            size_t larger = buddy_block_pages(order + 1);
            if (larger > remaining || (index & (larger - 1)) != 0)
            {          /* 更大块超剩余 或 起点未按该大小对齐 */
                break; /* 不能继续增大阶，停止 */
            }
            order++;
        }
        size_t length = buddy_block_pages(order); /* 本段能切出的最大对齐块长 */
        buddy_insert_free(index, order);          /* 入链（内部会自动向上合并） */
        index += length;                          /* 前进到下一段的起点 */
        remaining -= length;                      /* 扣掉已切出的页数 */
    }
    buddy.nr_free += n;    /* 空闲总页数 += n */
    buddy.nr_managed += n; /* 被管理总页数 += n */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_alloc_pages（分配页，含向上取整与“分裂”） =================
 * 输入请求页数 n，返回一个已分配块的头页 Page*，失败返回 NULL。步骤：
 *   1) 用 buddy_request_order(n,&order) 把 n 向上取整成 order；若非法、或取整后的块长
 *      > nr_free（空闲总量不够），则直接失败（快速拒绝）。
 *   2) 从 order 阶开始向上找第一条非空空闲链表 found（首次适配策略）；
 *      到 BUDDY_MAX_ORDER 仍为空则失败。
 *   3) 取该链表第一个节点作为 base，换算 index，断言其元数据阶 == found、区间合法，
 *      然后 buddy_unlink_free(index) 把它从链表摘下（此时它变成 FREE_TAIL）。
 *   4) “分裂”：从 found 阶逐级降到 order 阶，每降一级就把后半块
 *      index + 2^found 以该阶挂回对应空闲链表（buddy_link_free）。
 *      于是原大块被拆成“要分配的那半块”+ 各级更小的半块。
 *   5) 把最终 length=2^order 的整块逐页标成 ALLOC_TAIL（头页随后单独标），
 *      同时清 property、ref 归 0（用户拿到的页应像“全新页”）。
 *   6) 头页置 BUDDY_ALLOC_HEAD、order=order；公共 Page 的 property 记录
 *      “用户原始请求 n”（不是块长，用于 free 时用同一 n 还原）。
 *   7) nr_free 减去实际分配的块长 length（注意不是 n，向上取整多出的部分也要扣）。
 * 返回的就是 base（相对请求可能已被向上取整，故调用者应使用返回指针而非自行推算）。
 */
static struct Page *
buddy_alloc_pages(size_t n)
{
    unsigned int order;
    if (!buddy_request_order(n, &order) || /* 请求非法/超容量 */
        buddy_block_pages(order) > buddy.nr_free)
    {                /* 或取整后块长超过空闲总量 */
        return NULL; /* 直接失败 */
    }
    unsigned int found = order;
    while (found <= BUDDY_MAX_ORDER && list_empty(&buddy.free_list[found]))
    {
        found++; /* 向上找第一条非空空闲链表 */
    }
    if (found > BUDDY_MAX_ORDER)
    {
        return NULL; /* 没有任何可用的更大块 */
    }

    struct Page *base = le2page(list_next(&buddy.free_list[found]), page_link); /* 取链表首个块头页 */
    size_t index = buddy_page_index(base);                                      /* 换算成 pages[] 下标 */
    assert(buddy.meta[index].order == found);                                   /* 该块阶必须与所在链表阶一致 */
    assert(buddy_in_range(index, buddy_block_pages(found)));
    buddy_unlink_free(index); /* 先把它从链表摘下（降级为尾页） */
    while (found > order)
    {
        found--;                                                  /* 逐级分裂：降一阶 */
        buddy_link_free(index + buddy_block_pages(found), found); /* 后半块以该阶挂回空闲链表 */
    }

    size_t length = buddy_block_pages(order);
    for (size_t i = index; i < index + length; i++)
    {
        assert(buddy.meta[i].state == BUDDY_FREE_TAIL);               /* 分裂后这整块应是干净尾页 */
        assert(!PageReserved(pages + i) && !PageProperty(pages + i)); /* 非保留、无 property */
        buddy.meta[i].state = BUDDY_ALLOC_TAIL;                       /* 先整块标为已分配尾页 */
        buddy.meta[i].order = 0;
        pages[i].property = 0;
        set_page_ref(pages + i, 0); /* ref 归 0：干净的分配结果 */
    }
    buddy.meta[index].state = BUDDY_ALLOC_HEAD; /* 头页单独标为已分配头 */
    buddy.meta[index].order = order;            /* 记录实际分配的阶 */
    base->property = n;                         /* 头页 property 记录“用户原始请求 n” */
    buddy.nr_free -= length;                    /* 空闲总页数按实际块长扣减（非 n） */
    return base;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_free_pages（释放页并触发“合并”） =================
 * 参数 base 是当初 alloc 返回的头页，n 是当初分配的原始请求值（必须与 alloc 的 n 一致）。
 * 步骤：
 *   1) 用 buddy_request_order(n,&order) 还原阶；断言该页确实是 ALLOC_HEAD，且元数据阶==order、
 *      property==n（三者必须自洽，用来检查调用者配对是否正确）；
 *   2) 断言 index 按 2^order 对齐、区间在界内（块首必须对齐）；
 *   3) 逐页把状态从 ALLOC_HEAD/ALLOC_TAIL 恢复成 FREE_TAIL：flags/property 清零、ref 置 0
 *      （准备回到空闲池），私有 meta 置 FREE_TAIL/order=0；
 *   4) nr_free += length（length=2^order，按“实际块长”归还，与分配时一致）；
 *   5) 调 buddy_insert_free(index,order)：它会自动与同阶伙伴向上合并，逐级合成尽可能大的
 *      空闲块，从而完成“合并”语义。
 * 说明：free 必须传入与 alloc 相同的 n，才能对上 property/order；分配时保存原始请求 n
 * 正是为了让“归还的页数”与“当初向上取整的块”精确匹配。
 */
static void
buddy_free_pages(struct Page *base, size_t n)
{
    unsigned int order;
    assert(buddy_request_order(n, &order));                          /* 用原始 n 还原实际阶，n 合法 */
    size_t index = buddy_page_index(base);                           /* 头页下标 */
    assert(buddy.meta[index].state == BUDDY_ALLOC_HEAD);             /* 必须是当初分配返回的头页 */
    assert(buddy.meta[index].order == order && base->property == n); /* 阶与 property 均须自洽 */
    size_t length = buddy_block_pages(order);
    assert(buddy_in_range(index, length) && (index & (length - 1)) == 0); /* 区间合法且块首对齐 */
    for (size_t i = index; i < index + length; i++)
    {
        assert(buddy.meta[i].state ==
               (i == index ? BUDDY_ALLOC_HEAD : BUDDY_ALLOC_TAIL));   /* 头页是 HEAD，其余是 TAIL */
        assert(!PageReserved(pages + i) && !PageProperty(pages + i)); /* 非保留、无 property */
        pages[i].flags = 0;                                           /* 清公共标志 */
        pages[i].property = 0;                                        /* 清 property */
        set_page_ref(pages + i, 0);                                   /* ref 归 0：回归空闲 */
        buddy.meta[i].state = BUDDY_FREE_TAIL;                        /* 标为空闲尾页 */
        buddy.meta[i].order = 0;
    }
    buddy.nr_free += length;         /* 空闲总页数按实际块长归还 */
    buddy_insert_free(index, order); /* 插入空闲链表并自动向上合并 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_nr_free_pages（查询空闲页数） =================
 * 直接返回 buddy.nr_free。该计数在 alloc 时按实际块长 length 扣减、free 时按实际块长
 * length 加回，因此它表示“当前空闲的物理页总数”（而非空闲块个数），
 * 会被 check_structure 与测试断言反复用来对账。
 */
static size_t
buddy_nr_free_pages(void)
{
    return buddy.nr_free;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_check_structure（结构一致性自检） =================
 * 这是分配器的“体检”函数，可在不递归进入 .check 的情况下被测试代码调用
 * （见后面 buddy_check / buddy_test_check_structure）。它把全部不变量写成断言：
 *
 * 第一遍：遍历 order=0..15 的每条空闲链表
 *   - 统计本次遍历过的块数 blocks（不应超过页数上限，防止链表成环/重复访问）；
 *   - 对每个节点：确认它按 2^order 对齐、区间在界、链表前后指针互指正确；
 *     元数据状态是 FREE_HEAD 且阶==order；公共 Page 上有 PageProperty 且 property==块长；
 *   - 用 seen[] 位图把它覆盖的 length 页逐个标记为 1，检查没有重复覆盖，
 *     并确认每页非 Reserved、ref==0；非头页必须是 FREE_TAIL 且 property/order 为 0；
 *   - 若 order<15，还检查“同阶伙伴不能同时是空闲头”（否则说明该合并却没合并）；
 *   - 累加 free_total；最后校验链表首尾哨兵闭合。
 *
 * 第二遍：沿 [0,limit) 线性扫描所有页，按 meta 状态分类
 *   - UNMANAGED：必须没被 seen 标记（不能出现在任何空闲链表里），指针前进 1 页；
 *   - ALLOC_HEAD：读出阶 order、块长 length，检查对齐与区间；公共 property 记为
 *     “原始请求 requested”，并断言 requested 落在 (length/2, length] 区间
 *     （这正是“向上取整”的数学特征：请求大于半个块、不超过整块）；
 *     再把它覆盖的页在 seen 里标成 2，非头页必须是 ALLOC_TAIL 且 property/order 为 0；
 *     累加 allocated_total 与 managed_total，指针跳过整块；
 *   - 其它状态（FREE_HEAD/FREE_TAIL）：必须在 seen 里是 1（被空闲链表覆盖），
 *     managed_total 加一，指针前进 1 页。
 *
 * 最后三条等式对账：
 *   free_total == buddy.nr_free（空闲链表的实际内容 == 计数器）；
 *   managed_total == buddy.nr_managed（线性扫描出的被管理页数 == 计数器）；
 *   free_total + allocated_total == managed_total（空闲 + 已分配 == 被管理，不能有第四类页）。
 * 通过后打印一行统计信息。任何不变量被破坏都会在 assert 处立即暴露。
 */
/* Structural invariants, callable without recursively entering .check. */
static void
buddy_check_structure(void)
{
    size_t limit = buddy_page_limit();
    size_t free_total = 0, allocated_total = 0, managed_total = 0, blocks = 0;
    memset(buddy.seen, 0, sizeof(buddy.seen)); /* 清空覆盖位图，准备记录本遍统计 */

    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++)
    { /* 第一遍：逐阶遍历空闲链表 */
        list_entry_t *head = &buddy.free_list[order];
        list_entry_t *prev = head, *le = list_next(head);
        size_t length = buddy_block_pages(order);
        while (le != head)
        {                              /* 绕链表一圈 */
            assert(++blocks <= limit); /* 块数不能超过页数上限（防成环） */
            struct Page *base = le2page(le, page_link);
            size_t index = buddy_page_index(base);                  /* 该块头页下标 */
            assert(buddy_in_range(index, length));                  /* 区间在界 */
            assert((index & (length - 1)) == 0);                    /* 块首按块长对齐 */
            assert(le->prev == prev && prev->next == le);           /* 链表双向指针一致 */
            assert(buddy.meta[index].state == BUDDY_FREE_HEAD);     /* 元数据为空闲头 */
            assert(buddy.meta[index].order == order);               /* 阶与所在链表一致 */
            assert(PageProperty(base) && base->property == length); /* property == 块长 */
            for (size_t i = index; i < index + length; i++)
            {                               /* 标记该块覆盖的每一页 */
                assert(buddy.seen[i] == 0); /* 不能被别的空闲块重复覆盖 */
                buddy.seen[i] = 1;
                assert(!PageReserved(pages + i) && page_ref(pages + i) == 0); /* 非保留、ref=0 */
                if (i != index)
                { /* 非头页必须是干净尾页 */
                    assert(buddy.meta[i].state == BUDDY_FREE_TAIL);
                    assert(buddy.meta[i].order == 0);
                    assert(!PageProperty(pages + i) && pages[i].property == 0);
                }
            }
            if (order < BUDDY_MAX_ORDER)
            { /* 检查同阶伙伴是否也空闲（若也空闲=漏合并） */
                size_t other = index ^ length;
                assert(!buddy_in_range(other, length) ||
                       buddy.meta[other].state != BUDDY_FREE_HEAD ||
                       buddy.meta[other].order != order);
            }
            free_total += length; /* 累计空闲页数 */
            prev = le;
            le = list_next(le);
        }
        assert(head->prev == prev && prev->next == head); /* 链表首尾哨兵闭合 */
    }

    for (size_t index = 0; index < limit;)
    { /* 第二遍：线性扫描全部页并分类 */
        unsigned int state = buddy.meta[index].state;
        if (state == BUDDY_UNMANAGED)
        {
            assert(buddy.seen[index] == 0); /* 未登记页不应出现在任何空闲链表里 */
            index++;                        /* 逐页前进 */
        }
        else if (state == BUDDY_ALLOC_HEAD)
        {
            unsigned int order = buddy.meta[index].order;
            size_t length = buddy_block_pages(order);
            assert(buddy_in_range(index, length));
            assert((index & (length - 1)) == 0);                      /* 分配块首也须按块长对齐 */
            size_t requested = pages[index].property;                 /* 头页 property = 用户原始请求 */
            assert(requested > (length >> 1) && requested <= length); /* 向上取整的特征区间 */
            for (size_t i = index; i < index + length; i++)
            {
                assert(buddy.seen[i] == 0);                                   /* 已分配块不能与空闲块重叠 */
                buddy.seen[i] = 2;                                            /* 用 2 标记“已分配” */
                assert(!PageReserved(pages + i) && !PageProperty(pages + i)); /* 非保留、无标记 */
                if (i != index)
                {
                    assert(buddy.meta[i].state == BUDDY_ALLOC_TAIL);
                    assert(buddy.meta[i].order == 0 && pages[i].property == 0);
                }
            }
            allocated_total += length;
            managed_total += length; /* 已分配页也算“被管理” */
            index += length;         /* 跳过整块 */
        }
        else
        { /* 其余只能是空闲头/空闲尾 */
            assert(state == BUDDY_FREE_HEAD || state == BUDDY_FREE_TAIL);
            assert(buddy.seen[index] == 1); /* 必须被空闲链表覆盖过 */
            managed_total++;
            index++;
        }
    }
    assert(free_total == buddy.nr_free);                   /* 空闲链表实际 == 计数器 */
    assert(managed_total == buddy.nr_managed);             /* 扫描出的被管理页 == 计数器 */
    assert(free_total + allocated_total == managed_total); /* 空闲 + 已分配 == 被管理 */
    cprintf("buddy: structural check passed, %lu free pages in %lu blocks, "
            "%lu managed pages, max order %u\n",
            (unsigned long)free_total, (unsigned long)blocks,
            (unsigned long)managed_total, (unsigned int)BUDDY_MAX_ORDER);
}
/* ================= 吴佳璐END ================= */

#if defined(PMM_USE_BUDDY) && defined(ucore_test)
/* ================= 吴佳璐：测试专用状态与静态缓冲区声明 =================
 * 仅在测试构建（PMM_USE_BUDDY 与 ucore_test 同时开启，即 make test 时）编译。
 * 普通构建（make qemu）下整个测试块被预处理器剔除，测试代码零编入。
 * 这一块声明测试框架要用的私有资源：
 *   - buddy_test_state：一份独立的分配器状态，测试期间把它设为活跃实例，
 *     生产实例 buddy_live 在此期间保持不动；
 *   - buddy_test_saved_pages：保存真实 arena 那 64 个页描述符的原始内容，
 *     测试会先“备份再清零”这些描述符，结束后再原样恢复，避免污染内核；
 *   - buddy_test_max_pages：纯描述符数组（BUDDY_MAX_PAGES 个 Page），
 *     只为 order-15 边界测试提供“页描述符”，它们并不对应可写的真实物理内存；
 *   - buddy_test_isolated：标记当前是否处于测试隔离状态（活跃实例是否已切换）。
 */
static struct buddy_state buddy_test_state;                        /* 测试用的独立分配器状态 */
static struct Page buddy_test_saved_pages[BUDDY_TEST_ARENA_PAGES]; /* arena 描述符备份 */
/* Descriptors only: these entries do not represent writable physical RAM. */
static struct Page buddy_test_max_pages[BUDDY_MAX_PAGES]; /* 纯描述符夹具（无真实内存） */
static bool buddy_test_isolated;                          /* 是否处于测试隔离态 */
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_test_check_structure（测试用自检入口） =================
 * 单纯转发到内部 buddy_check_structure，让测试代码可以触发一次结构对账，
 * 而不用直接依赖内部函数名。
 */
void buddy_test_check_structure(void)
{
    buddy_check_structure();
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_test_hash_word（FNV 风格的哈希混合步） =================
 * 对状态指纹的“单步混合”：先把当前 hash 与输入 word 异或，再乘以 FNV 的大质数
 * 1099511628211。它本身不是证明，只是让状态摘要对小改动敏感。
 */
static uint64_t
buddy_test_hash_word(uint64_t hash, uint64_t word)
{
    return (hash ^ word) * 1099511628211ULL;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_test_state_digest（分配器状态指纹） =================
 * 生成一个 64 位“指纹”，用于断言“被拒绝的请求不改变分配器状态”
 * （测试在请求前后各算一次，比较是否相等）。强调：它是诊断性指纹，不是相等性证明。
 * 只把“已初始化的字段”逐个喂进哈希，绝不去哈希 Page/元数据里的结构体填充字节
 * （填充字节内容不确定，参与哈希会造成假失败）；seen[] 仅供 check 使用，不参与。
 *
 * 参与内容：活跃实例指针、pages 基址、npage、nr_free、nr_managed、每条空闲链表的
 *   prev/next、每个页的 meta.state/order，以及在界内的 pages[i].ref / flags /
 *   property / page_link.prev / page_link.next。
 */
/* A diagnostic fingerprint, not a proof of equality. Hash initialized fields
 * individually, never Page/metadata padding. seen[] is check-only scratch. */
uint64_t
buddy_test_state_digest(void)
{
    uint64_t hash = 14695981039346656037ULL; /* FNV 初始常量 */
    size_t limit = buddy_page_limit();
    hash = buddy_test_hash_word(hash, (uintptr_t)buddy_active); /* 活跃实例指针 */
    hash = buddy_test_hash_word(hash, (uintptr_t)pages);        /* pages 基址 */
    hash = buddy_test_hash_word(hash, npage);                   /* 总页数 */
    hash = buddy_test_hash_word(hash, buddy.nr_free);           /* 空闲页计数 */
    hash = buddy_test_hash_word(hash, buddy.nr_managed);        /* 被管理页计数 */
    for (unsigned int order = 0; order <= BUDDY_MAX_ORDER; order++)
    {
        hash = buddy_test_hash_word(hash,
                                    (uintptr_t)buddy.free_list[order].prev); /* 各阶链表 prev */
        hash = buddy_test_hash_word(hash,
                                    (uintptr_t)buddy.free_list[order].next); /* 各阶链表 next */
    }
    for (size_t i = 0; i < BUDDY_MAX_PAGES; i++)
    {
        hash = buddy_test_hash_word(hash, buddy.meta[i].state); /* 每页状态 */
        hash = buddy_test_hash_word(hash, buddy.meta[i].order); /* 每页阶 */
        if (i < limit)
        { /* 仅对在界页哈希公共字段 */
            hash = buddy_test_hash_word(hash, (uint64_t)pages[i].ref);
            hash = buddy_test_hash_word(hash, pages[i].flags);
            hash = buddy_test_hash_word(hash, pages[i].property);
            hash = buddy_test_hash_word(hash, (uintptr_t)pages[i].page_link.prev);
            hash = buddy_test_hash_word(hash, (uintptr_t)pages[i].page_link.next);
        }
    }
    return hash;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_test_with_regions（真实 arena 隔离测试钩子） =================
 * 目的：给测试主体 body 提供一个“只属于它、且状态可完全还原”的真实物理内存环境。
 * 参数：regions 描述要在 arena 内登记的子区域（可多个、不重叠），count 是数量，body 是主体。
 * 流程：
 *   1) 前置断言：当前管理器就是 buddy、尚未处于隔离态、regions/body 合法；
 *   2) 校验所有 regions：offset 与 length 落在 [0,BUDDY_TEST_ARENA_PAGES) 内，
 *      且两两不重叠（用 end<=offset 或另一段 end<=offset 判断），累加 registered 总数；
 *   3) 记下 baseline=nr_free_pages()，再从真实内存 alloc_pages(64) 独占一块 arena；
 *      失败则打印 UNTESTED 并返回 -2（不破坏状态）；
 *   4) 校验返回块正是 64 页、起点对齐、property==64、空闲数恰好少了 64；
 *   5) 备份这 64 个页描述符到 buddy_test_saved_pages，然后清零它们，并逐个 SetPageReserved
 *      （模拟“已保留”，之后 init_memmap 会把这些还原为可用）；
 *   6) 切换到测试实例：buddy_active=&buddy_test_state、buddy_init() 清状态，
 *      再对每个 region 调 buddy_init_memmap(base+offset, length) 登记；
 *      断言此刻空闲数==registered，跑一次结构自检；
 *   7) 调用 body(base, BUDDY_TEST_ARENA_PAGES)；
 *   8) 若 body 成功，额外断言“被管理页数==registered”且“空闲数==被管理数”
 *      （即测试结束时所有登记页都已归还），再自检一次；
 *   9) 无论成败都恢复现场：把备份的页描述符写回、切回 buddy_live、清隔离标志、
 *      free_pages(base,64) 把 arena 还给生产分配器，断言空闲数回到 baseline；返回 body 结果。
 * 这样即使 body 中途失败，也不会污染生产分配器或内核描述符。
 */
/* Register only caller-described subregions of a privately allocated block.
 * Even when a body reports failure, restore all descriptors and the live
 * context before returning that failure. No production initializer is reset. */
int buddy_test_with_regions(const struct buddy_test_region *regions, size_t count,
                            buddy_test_body body)
{
    assert(pmm_manager == &buddy_pmm_manager);                   /* 必须正跑 buddy 管理器 */
    assert(!buddy_test_isolated && buddy_active == &buddy_live); /* 尚未隔离，活跃的是生产实例 */
    assert(regions != NULL && count > 0 && count <= 8 && body != NULL);
    size_t registered = 0; /* 本次要登记的总页数 */
    for (size_t i = 0; i < count; i++)
    {
        assert(regions[i].offset < BUDDY_TEST_ARENA_PAGES);
        assert(regions[i].length > 0);
        assert(regions[i].length <= BUDDY_TEST_ARENA_PAGES - regions[i].offset); /* 区域在 arena 内 */
        size_t end = regions[i].offset + regions[i].length;
        for (size_t j = 0; j < i; j++)
        { /* 与之前的区域比较，确保互不重叠 */
            assert(end <= regions[j].offset ||
                   regions[j].offset + regions[j].length <= regions[i].offset);
        }
        registered += regions[i].length;
    }
    size_t baseline = nr_free_pages();                       /* 记录测试前的空闲页数 */
    struct Page *base = alloc_pages(BUDDY_TEST_ARENA_PAGES); /* 独占申请 64 页 arena */
    if (base == NULL)
    {
        assert(nr_free_pages() == baseline); /* 失败不得改变计数 */
        cprintf("buddy tests: UNTESTED, no exclusive 64-page arena\n");
        return -2; /* 环境受限，未覆盖 */
    }
    size_t index = buddy_page_index(base);
    assert(buddy_in_range(index, BUDDY_TEST_ARENA_PAGES));
    assert((index & (BUDDY_TEST_ARENA_PAGES - 1)) == 0);                          /* 起点按 64 页对齐 */
    assert(buddy.meta[index].state == BUDDY_ALLOC_HEAD);                          /* 返回的是已分配头页 */
    assert(buddy_block_pages(buddy.meta[index].order) == BUDDY_TEST_ARENA_PAGES); /* 阶正好 64 页 */
    assert(base->property == BUDDY_TEST_ARENA_PAGES);                             /* property == 64 */
    assert(nr_free_pages() == baseline - BUDDY_TEST_ARENA_PAGES);                 /* 正好消耗 64 页 */
    memcpy(buddy_test_saved_pages, base, sizeof(buddy_test_saved_pages));         /* 备份描述符 */
    memset(base, 0, sizeof(buddy_test_saved_pages));                              /* 清零 arena 描述符 */
    for (size_t i = 0; i < BUDDY_TEST_ARENA_PAGES; i++)
    {
        SetPageReserved(base + i); /* 先标记为保留，交由 init_memmap 还原可用 */
    }
    buddy_test_isolated = 1;          /* 进入隔离态 */
    buddy_active = &buddy_test_state; /* 活跃实例切到测试状态 */
    buddy_init();                     /* 清空测试实例状态 */
    for (size_t i = 0; i < count; i++)
    {
        buddy_init_memmap(base + regions[i].offset, regions[i].length); /* 只登记指定子区域 */
    }
    assert(nr_free_pages() == registered);           /* 空闲数应等于登记页数 */
    buddy_check_structure();                         /* 登记后先自检一次 */
    int result = body(base, BUDDY_TEST_ARENA_PAGES); /* 执行测试主体 */
    if (result == 0)
    {                                                /* 主体成功则追加一致性检查 */
        assert(buddy.nr_managed == registered);      /* 被管理页数仍是登记数 */
        assert(nr_free_pages() == buddy.nr_managed); /* 且全部已归还（空闲==被管理） */
        buddy_check_structure();
    }
    memcpy(base, buddy_test_saved_pages, sizeof(buddy_test_saved_pages)); /* 恢复描述符原样 */
    buddy_active = &buddy_live;                                           /* 切回生产实例 */
    buddy_test_isolated = 0;                                              /* 退出隔离态 */
    free_pages(base, BUDDY_TEST_ARENA_PAGES);                             /* 把 arena 还给生产分配器 */
    assert(nr_free_pages() == baseline);                                  /* 空闲数必须完全复原 */
    return result;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_test_with_max_descriptors（order-15 描述符夹具） =================
 * 目的：在不占用真实 128 MiB 物理内存的前提下，测试最高阶 order-15 的边界行为。
 * 做法：临时把全局 pages 指针换成 buddy_test_max_pages（一份纯描述符数组），
 *       把 npage 设为 nbase+BUDDY_MAX_PAGES，于是可管理页数变成 32768。
 * 流程：
 *   - 保存原有 pages/npage；清零描述符数组并逐个 SetPageReserved；
 *   - 切到测试实例、buddy_init()、buddy_init_memmap(pages,32768)，
 *     断言空闲数==32768 并自检；
 *   - 调 body(pages,32768)（body 内部只用描述符、绝不调用 data_address 访问数据，
 *     因此不会去映射不存在的物理内存）；
 *   - 成功时再断言“被管理==32768、空闲==被管理”并自检；
 *   - 恢复 pages/npage、切回 buddy_live、清隔离标志，断言空闲数回到 baseline。
 */
/* Exercise the highest supported order without treating reserved firmware or
 * kernel RAM as free. The body must never map these synthetic descriptors. */
int buddy_test_with_max_descriptors(buddy_test_body body)
{
    assert(pmm_manager == &buddy_pmm_manager);
    assert(!buddy_test_isolated && buddy_active == &buddy_live && body != NULL);
    size_t baseline = nr_free_pages();
    struct Page *saved_pages = pages;                              /* 备份全局 pages 指针 */
    size_t saved_npage = npage;                                    /* 备份全局 npage */
    memset(buddy_test_max_pages, 0, sizeof(buddy_test_max_pages)); /* 清零描述符夹具 */
    for (size_t i = 0; i < BUDDY_MAX_PAGES; i++)
    {
        SetPageReserved(buddy_test_max_pages + i); /* 夹具描述符先全部标为保留 */
    }
    buddy_test_isolated = 1;                    /* 进入隔离态 */
    buddy_active = &buddy_test_state;           /* 切到测试实例 */
    pages = buddy_test_max_pages;               /* 全局页数组指向夹具 */
    npage = nbase + BUDDY_MAX_PAGES;            /* 令可管理页数=32768 */
    buddy_init();                               /* 清空测试实例状态 */
    buddy_init_memmap(pages, BUDDY_MAX_PAGES);  /* 登记全部 32768 页（描述符） */
    assert(nr_free_pages() == BUDDY_MAX_PAGES); /* 登记后应全部空闲 */
    buddy_check_structure();
    int result = body(pages, BUDDY_MAX_PAGES); /* 执行测试主体（只用描述符） */
    if (result == 0)
    {
        assert(buddy.nr_managed == BUDDY_MAX_PAGES); /* 被管理页数仍为 32768 */
        assert(nr_free_pages() == buddy.nr_managed); /* 全部已归还 */
        buddy_check_structure();
    }
    pages = saved_pages;                 /* 恢复全局 pages */
    npage = saved_npage;                 /* 恢复全局 npage */
    buddy_active = &buddy_live;          /* 切回生产实例 */
    buddy_test_isolated = 0;             /* 退出隔离态 */
    assert(nr_free_pages() == baseline); /* 空闲数复原 */
    return result;
}
/* ================= 吴佳璐END ================= */
#endif /* PMM_USE_BUDDY && ucore_test */

/* ================= 吴佳璐：buddy_check（pmm_manager 的 check 回调） =================
 * 这是 pmm_manager 要求的 .check 入口：先做结构自检（任何构建都保留，作为快速
 * 回归保障）；仅在测试构建（PMM_USE_BUDDY 与 ucore_test 同时开启，即 make test 时）
 * 再跑整套 buddy_test() 功能测试，最后再自检一次，确保测试结束后结构仍然完好。
 */
static void
buddy_check(void)
{
    buddy_check_structure(); /* 先做一次结构自检 */
#if defined(PMM_USE_BUDDY) && defined(ucore_test)
    buddy_test();            /* 跑 case1..case8 完整测试（仅测试构建） */
    buddy_check_structure(); /* 测试后再次自检，确认结构未破坏 */
#endif
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_pmm_manager（对外注册的管理器实例） =================
 * 把本模块的 6 个函数装配成内核统一认的 struct pmm_manager：
 *   init / init_memmap / alloc_pages / free_pages / nr_free_pages 是标准页分配接口，
 *   check 是自检 + 测试入口。名字取 "buddy_pmm_manager" 便于日志区分。
 */
const struct pmm_manager buddy_pmm_manager = {
    .name = "buddy_pmm_manager",          /* 管理器名字 */
    .init = buddy_init,                   /* 初始化分配器状态 */
    .init_memmap = buddy_init_memmap,     /* 登记可用物理内存 */
    .alloc_pages = buddy_alloc_pages,     /* 分配页（向上取整 + 分裂） */
    .free_pages = buddy_free_pages,       /* 释放页（含向上合并） */
    .nr_free_pages = buddy_nr_free_pages, /* 查询空闲页数 */
    .check = buddy_check,                 /* 自检 + 功能测试入口 */
};
/* ================= 吴佳璐END ================= */
