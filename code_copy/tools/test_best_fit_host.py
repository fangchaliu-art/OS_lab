#!/usr/bin/env python3

# ================= 吴佳璐：模块说明与导入依赖 =================
# 这一块要解决的问题：在不启动 QEMU 的前提下，用宿主 PC 上的 GCC 直接验证真实的
#   Best-Fit 分配器实现是否正确，作为 RISC-V/QEMU 内核自检的补充（不替代后者）。
# 关键约束：
#   - 不把分配器实现抄进脚本，而是把工程里真实的 best_fit_pmm.c 直接 #include，
#     保证“测试对象”与内核运行的是同一份源码；
#   - 机器相关的 pmm.h 接口与整型宽度由脚本生成的适配层（DEFS/PMM）替换；
#   - Page、free_area_t、链表操作、MMU 常量、Best-Fit 全部来自工程本身。
# 算法思路：用字符串模板 DEFS / PMM / HARNESS 现场拼出 check.c，再交给宿主编译器
#   编译运行；下面先导入所依赖的标准库模块。
# 参数与返回值：本块只做导入，无参数、无返回值。
# 与其它块的协作：DEFS/PMM 产出适配层声明，HARNESS 是测试主体，run/main 负责驱动。
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
# ================= 吴佳璐END =================


# ================= 吴佳璐：DEFS 模板（宿主端基础类型与通用宏） =================
# 这一块要解决的问题：工程的 defs.h 依赖机器相关定义，宿主测试无法直接使用；
#   这里用同名头 DEFS 顶替它，只提供所需的最小集合。
# 关键点：
#   - 借 <stdbool.h>/<stddef.h>/<stdint.h> 提供 bool、offsetof 与整型；
#   - ppn_t 定义为 size_t（内核里通常也是无符号整型，保持宽度一致）；
#   - to_struct 用“成员地址 - 成员偏移”反推结构体首地址，等价 Linux 的
#     container_of 宏，是 list_entry / le2page 这类链表操作的基础。
# 参数与返回值：本块是字符串模板，稍后写成 defs.h 供 check.c 包含。
# 与其它块的协作：PMM 与 HARNESS 生成的 C 代码都 #include <defs.h>，即本模板。
DEFS = r"""
#ifndef HOST_DEFS_H
#define HOST_DEFS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef size_t ppn_t;
/* to_struct：由成员地址反推所在结构体首地址（container_of 的等价实现） */
#define to_struct(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif
"""
# ================= 吴佳璐END =================

# ================= 吴佳璐：PMM 模板（替换机器相关的 pmm.h 接口） =================
# 这一块要解决的问题：真实 pmm.h 含 struct pmm_manager 声明与一批机器相关接口，
#   宿主端无法直接使用。这里生成同名 pmm.h：结构体声明用占位符
#   @MANAGER_DECLARATION@ 注入（由 Python 侧从真实 pmm.h 正则抽取，保证布局一致），
#   其余入口改成宿主可实现的形式。
# 关键点：
#   - 声明 alloc_pages / free_pages / nr_free_pages，并提供 alloc_page/free_page 宏；
#   - cprintf 直接映射为 printf（宿主有标准输出）；
#   - page2pa 把“Page 数组下标 + nbase”换算成物理页号，再左移 PGSHIFT(12) 得物理地址；
#   - page_ref / set_page_ref 直接读写 Page.ref，充当引用计数。
# 参数与返回值：本块是字符串模板，稍后写成 pmm.h 供 check.c 包含。
# 与其它块的协作：HARNESS 包含的 best_fit_pmm.c 依赖这些接口；Placeholder 由 main() 填充。
PMM = r"""
#ifndef HOST_PMM_H
#define HOST_PMM_H
#include <assert.h>
#include <defs.h>
#include <memlayout.h>
#include <mmu.h>
#include <stdio.h>
/* 此处由 Python 侧注入从真实 pmm.h 抽取的 struct pmm_manager 声明，保证字段布局一致 */
@MANAGER_DECLARATION@
extern const struct pmm_manager *pmm_manager;
extern struct Page *pages;
extern size_t npage;
extern const size_t nbase;
struct Page *alloc_pages(size_t n);
void free_pages(struct Page *p, size_t n);
size_t nr_free_pages(void);
#define alloc_page() alloc_pages(1)          /* 分配单页的便捷宏 */
#define free_page(p) free_pages((p), 1)      /* 释放单页的便捷宏 */
#define cprintf printf                        /* 把内核打印重定向到宿主 printf */
static inline uintptr_t page2pa(struct Page *p) {   /* 由描述符反算物理地址 */
    return ((uintptr_t)(p - pages) + nbase) << PGSHIFT;   /* 页号=(p-pages)+nbase，再左移 12 位 */
}
static inline int page_ref(struct Page *p) { return p->ref; }         /* 读引用计数 */
static inline void set_page_ref(struct Page *p, int n) { p->ref = n; } /* 写引用计数 */
#endif
"""
# ================= 吴佳璐END =================

HARNESS = r"""
/* ================= 吴佳璐：HARNESS 常量、被测分配器模型与页面容器 =================
 * 这一块要解决的问题：为宿主运行搭建一个“最小内核环境”——提供 Page 描述符数组、
 *   页号基址/上界，以及被测分配器所需的全局接口，并把真实的 best_fit_pmm.c 直接包含。
 * 关键不变量：
 *   - test_pages 是固定 256 页的 Page 数组，全局 pages 指向它；
 *   - model 是与之一一对应的“每页状态位图”（UNAVAILABLE/ALLOCATED/FREE），
 *     它是独立于空闲链表的 oracle，后续用来交叉验证分配器的行为；
 *   - pmm_manager 固定指向 best_fit_pmm_manager（本次被测对象）。
 * 算法思路：用函数桩把内核的 alloc_pages/free_pages/nr_free_pages 转发到
 *   pmm_manager 的对应回调，使下面的测试代码与真实内核算语义一致。
 * 参数与返回值：本块只定义静态变量与函数桩，无外部参数/返回值。
 * 与其它块的协作：verify/oracle/take/release 等均以 model 为准绳、以 pages 为现场。
 */
#include <stdio.h>
#include <string.h>
#include <best_fit_pmm.c>                       /* 直接包含被测的真实实现（不复制源码） */

enum { TEST_PAGES = 256, UNAVAILABLE = 0, ALLOCATED = 1, FREE = 2 };  /* 单页三态：不可用/已分配/空闲 */
static struct Page test_pages[TEST_PAGES];      /* 宿主模拟的物理页描述符数组 */
static unsigned char model[TEST_PAGES];         /* 独立 oracle：每页状态位图 */
struct Page *pages = test_pages;                /* 内核全局 pages 指针的替身 */
const size_t nbase = 0x80000000UL / PGSIZE;     /* 页号基址 = DRAM_BASE 对应的页号 */
size_t npage = 0x80000000UL / PGSIZE + TEST_PAGES;  /* 页号上界 = 基址页号 + 页数 */
const struct pmm_manager *pmm_manager = &best_fit_pmm_manager;  /* 被测管理器 */

struct Page *alloc_pages(size_t n) { return pmm_manager->alloc_pages(n); }   /* 内核接口桩：分配 */
void free_pages(struct Page *p, size_t n) { pmm_manager->free_pages(p, n); } /* 内核接口桩：释放 */
size_t nr_free_pages(void) { return pmm_manager->nr_free_pages(); }          /* 内核接口桩：查询 */
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：verify() —— 位图 oracle 交叉校验（核心断言） =================
 * 这一块要解决的问题：每一步操作后，把“分配器的内部状态”（free_list + Page 描述符）
 *   与“独立的每页状态位图 model”逐页比对，任何不一致立即断言失败。
 * 关键不变量（本函数逐一检查）：
 *   - 链表节点数不超总页数，且每个块首页落在 pages 数组内；
 *   - 每个块 length>0、区间不越界，首页 PageProperty=1 且非 reserved；
 *   - 链表双向链接自洽（le->prev 与前驱、前驱->next 与 le 一致）；
 *   - 相邻空闲区间必须已经合并（前块尾 < 本块头，即 blocks==1 或 previous_end < start）；
 *   - 每个空闲块覆盖的页：model 必须是 FREE 且不被重复覆盖，页非 reserved、ref=0；
 *   - 全表比对：covered[i] 必须等于 (model[i]==FREE)；非 UNAVAILABLE 页非 reserved，
 *     且“是否块首页”(PageProperty) 恰好等于 heads[i]，非首页 property 必须为 0；
 *     UNAVAILABLE 页必须仍为 reserved；
 *   - 累计空闲页数 total 必须等于 nr_free_pages()。
 * 算法思路：第一趟沿空闲链表遍历，标记每个块覆盖的页（covered）与块首页（heads），
 *   同时累加 total；第二趟扫描全部页做双向核对；最后校验计数器。
 * 参数与返回值：无参、无返回（只在断言失败时 abort）。
 * 与其它块的协作：reset/add_region/take/release 每次动作后都会调用它。
 */
/* The independent oracle is a per-page availability map, not a free list. */
static void verify(void) {
    unsigned char covered[TEST_PAGES] = {0};    /* 标记该页是否已被某个空闲块覆盖 */
    unsigned char heads[TEST_PAGES] = {0};      /* 标记该页是否为某个空闲块的首页 */
    size_t total = 0, previous_end = 0, blocks = 0;
    list_entry_t *le = list_next(&free_list), *previous = &free_list;  /* 从头遍历，previous 用于校验双向链接 */
    while (le != &free_list) {
        assert(++blocks <= TEST_PAGES);         /* 块数不可能超过总页数 */
        struct Page *p = le2page(le, page_link);
        assert(p >= pages && p < pages + TEST_PAGES);   /* 块首页必须落在 Page 数组内 */
        size_t start = (size_t)(p - pages), length = p->property;  /* start=首页下标，length=块页数 */
        assert(length > 0 && start + length <= TEST_PAGES);        /* 块大小合法且不越界 */
        assert(PageProperty(p) && !PageReserved(p));               /* 空闲块首页：property=1 且非 reserved */
        assert(le->prev == previous && previous->next == le);      /* 双向链接自洽 */
        /* Adjacent free ranges must already have coalesced. */
        assert(blocks == 1 || previous_end < start);   /* 相邻空闲区间必须已合并：前块尾 < 本块头 */
        heads[start] = 1;                       /* 登记本块首页位置 */
        for (size_t i = start; i < start + length; ++i) {
            assert(model[i] == FREE && !covered[i]);   /* oracle 认为空闲，且未被重复覆盖 */
            assert(!PageReserved(pages + i) && page_ref(pages + i) == 0);  /* 空闲页非 reserved 且 ref=0 */
            covered[i] = 1;                     /* 标记该页已被覆盖 */
        }
        total += length;                        /* 累计空闲页数 */
        previous_end = start + length;          /* 记录本块结束位置，供下一轮做合并检查 */
        previous = le;                          /* 记录本节点，用于校验链表回环 */
        le = list_next(le);
    }
    assert(free_list.prev == previous && previous->next == &free_list);  /* 链表尾部回到表头，环形自洽 */
    for (size_t i = 0; i < TEST_PAGES; ++i) {
        assert(covered[i] == (model[i] == FREE));   /* 覆盖情况必须与 oracle 的空闲判定完全一致 */
        if (model[i] != UNAVAILABLE) {              /* 可用页（空闲或已分配）：不应 reserved */
            assert(!PageReserved(pages + i));
            assert(!!PageProperty(pages + i) == heads[i]);   /* 块首页标记必须与首位置一一对应 */
            assert(heads[i] || pages[i].property == 0);      /* 非首页的 property 必须为 0 */
        } else {                                    /* 被排除的页必须仍被保留 */
            assert(PageReserved(pages + i));
        }
    }
    assert(total == nr_free_pages());           /* 累计页数必须等于分配器自报的空闲页数 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：reset()/add_region() —— 场景准备与内存登记 =================
 * 这一块要解决的问题：
 *   - reset()：把测试现场恢复到“什么都没分配”的干净初始态，供每个用例独立开始；
 *   - add_region()：向被测分配器登记一段可用内存（等价内核 page_init 的 init_memmap），
 *     同步更新 oracle 位图。
 * 关键不变量：
 *   - reset 后所有页都置为 reserved（不可用）、ref/property 故意填成脏值，
 *     这样能验证分配器会正确覆盖这些字段；model 全为 UNAVAILABLE；
 *   - add_region 只允许登记此前 UNAVAILABLE 的页，把它们在 model 中标记为 FREE，
 *     并调用 init_memmap 让分配器接纳这段内存。
 * 算法思路：reset 先清零 Page 数组、重置 model、逐页标 reserved 并写入哨兵值，
 *   再调用 pmm_manager->init() 清空空闲链表，最后 verify 确认初始状态自洽；
 *   add_region 先更新位图再调用 init_memmap，随后 verify。
 * 参数与返回值：reset 无参无返回；add_region(start,length) 中 start 为起始页下标、
 *   length 为页数（>0 且不越界），无返回。
 * 与其它块的协作：所有测试用例都以 reset 开头、以 add_region 搭建初始内存布局。
 */
static void reset(void) {
    memset(test_pages, 0, sizeof(test_pages));   /* 清空 Page 描述符数组 */
    memset(model, UNAVAILABLE, sizeof(model));   /* oracle：所有页初始不可用 */
    for (size_t i = 0; i < TEST_PAGES; ++i) {
        SetPageReserved(pages + i);              /* 初始全部标记为保留（不可分配） */
        pages[i].ref = 9;                        /* 故意写入脏 ref，验证后续会被清零 */
        pages[i].property = 123;                 /* 故意写入脏 property，验证后续会被重置 */
    }
    pmm_manager->init();                         /* 清空分配器的空闲链表与计数 */
    verify();                                    /* 校验初始状态自洽 */
}

static void add_region(size_t start, size_t length) {
    assert(length > 0 && start + length <= TEST_PAGES);   /* 区间合法 */
    for (size_t i = start; i < start + length; ++i) {
        assert(model[i] == UNAVAILABLE);         /* 只允许登记尚未使用的页 */
        model[i] = FREE;                         /* oracle：标记为可用 */
    }
    pmm_manager->init_memmap(pages + start, length);   /* 交给分配器接纳这段内存 */
    verify();                                    /* 校验登记后的内部状态与 oracle 一致 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：oracle()/take()/release()/reclaim_all() —— 分配与释放的操作模型 =================
 * 这一块要解决的问题：以位图 oracle 为基准，模拟并校验被测分配器的 alloc/free 行为。
 * 关键不变量/算法思路：
 *   - oracle(requested)：独立于链表，直接在 model 位图上做一次 Best-Fit 扫描，
 *     返回“容量>=requested 的最小空闲区间”的起始下标；没有则返回 -1。它是期望值来源。
 *   - take(requested)：先在 oracle 里算出期望起点，再真正调用 alloc_pages，断言返回值
 *     与期望完全一致（起点相同或同为 NULL）；若分配失败，还要求 Page 数组、free_area
 *     完全未被改动（失败不可产生副作用）；成功则把 model 对应页标为 ALLOCATED，
 *     并检查这些页已清除 PageProperty、ref=0；最后 verify，返回期望起点。
 *   - release(start,length)：释放一段已分配区间，故意先把 ref 写脏，验证 free 会重置；
 *     更新 model 为 FREE 后调用 free_pages，再 verify。
 *   - reclaim_all()：把 model 中所有 ALLOCATED 连续段逐段 release，实现“全部回收”，
 *     用于在用例间恢复满空闲状态。
 * 参数与返回值：oracle/take 返回起始页下标（-1 表示失败）；release/reclaim_all 无返回。
 * 与其它块的协作：三者共同构成“期望计算—实际操作—状态比对”的闭环，全部依赖 verify 与 model。
 */
static int oracle(size_t requested) {
    size_t best_length = TEST_PAGES + 1;        /* 当前最优块长度，初始化为“无穷大” */
    int best = -1;                              /* 最优块起始下标，-1 表示找不到 */
    for (size_t i = 0; i < TEST_PAGES;) {
        if (model[i] != FREE) { ++i; continue; }/* 跳过非空闲页 */
        size_t start = i;
        while (i < TEST_PAGES && model[i] == FREE) ++i;  /* 找到一整段连续空闲区间 */
        size_t length = i - start;
        if (length >= requested && length < best_length) {  /* 够用且比当前最优更小 */
            best = (int)start;
            best_length = length;
        }
    }
    return best;                                /* 返回最小够用区间起点；没有则 -1 */
}

static int take(size_t requested) {
    struct Page before[TEST_PAGES];
    free_area_t before_area;
    memcpy(before, pages, sizeof(before));              /* 快照：Page 数组 */
    memcpy(&before_area, &free_area, sizeof(before_area));  /* 快照：空闲链表与计数 */
    int expected = oracle(requested);           /* 期望的分配起点 */
    struct Page *p = alloc_pages(requested);    /* 调用被测分配器 */
    assert(p == (expected < 0 ? NULL : pages + expected));  /* 实际结果必须与期望一致 */
    if (p == NULL) {
        /* Failed allocation must not mutate descriptors, links or counters. */
        assert(memcmp(before, pages, sizeof(before)) == 0);          /* 失败不得改动描述符 */
        assert(memcmp(&before_area, &free_area, sizeof(before_area)) == 0);  /* 也不得改动链表/计数 */
    } else {
        for (size_t i = 0; i < requested; ++i) {
            assert(model[expected + i] == FREE);                     /* 分配区间在 oracle 中必为空闲 */
            assert(!PageProperty(p + i) && page_ref(p + i) == 0);    /* 已分配页不得是块首页、ref 归零 */
            model[expected + i] = ALLOCATED;                         /* oracle 同步为已分配 */
        }
    }
    verify();                                   /* 校验整体内部状态 */
    return expected;                            /* 返回分配的起始下标（-1 表示失败） */
}

static void release(size_t start, size_t length) {
    assert(length > 0 && start + length <= TEST_PAGES);   /* 区间合法 */
    for (size_t i = start; i < start + length; ++i) {
        assert(model[i] == ALLOCATED);          /* 只能释放已分配的页 */
        /* Deliberately dirty refs: free must reset them. */
        pages[i].ref = 7;                       /* 故意写脏引用计数，验证 free 会清零 */
        model[i] = FREE;                        /* oracle 同步为空闲 */
    }
    free_pages(pages + start, length);          /* 调用被测释放接口 */
    verify();                                   /* 校验释放后的状态 */
}

static void reclaim_all(void) {
    for (size_t i = 0; i < TEST_PAGES;) {
        if (model[i] != ALLOCATED) { ++i; continue; }   /* 跳过非已分配页 */
        size_t start = i;
        while (i < TEST_PAGES && model[i] == ALLOCATED) ++i;  /* 找出一整段已分配区间 */
        release(start, i - start);              /* 整段释放 */
    }
    verify();                                   /* 全部回收后校验应为满空闲 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：deterministic_checks() —— 确定性对照测试（离线手工场景） =================
 * 这一块要解决的问题：用若干人工构造的小场景，精确验证 Best-Fit 的关键行为，
 *   每个断言都给出精确期望的分配起点，出错时便于定位。
 * 覆盖行为：最小够用优先 / 切分后剩余块立即可复用 / 碎片化导致分配失败 /
 *   失败不改动状态 / 相同大小候选取地址更低者 / 双向合并 / 前驱与后继分别合并 /
 *   多个相邻分配一次释放 / init_memmap 乱序且互不相邻的区间。
 * 关键不变量：每个场景都以 reset() 开始、以 reclaim_all() 或显式 release 收尾，
 *   并用 assert 精确比对 take()/nr_free_pages() 的返回值。
 * 参数与返回值：无参、无返回；结果通过断言与 puts 输出。
 * 与其它块的协作：依赖 add_region/reset/take/release/reclaim_all 等模型函数。
 */
static void deterministic_checks(void) {
    /* --- 场景1：最小够用优先、切分复用、碎片化失败、失败不可变 --- */
    reset();
    add_region(64, 4); add_region(0, 12); add_region(32, 7);   /* 造出 [0,12)/[32,40)/[64,68) 三段空闲 */
    assert(take(5) == 32);    /* Smallest sufficient, not first address. */ /* 期望 32：选最小够用块而非地址最低的 0 */
    assert(take(2) == 37);    /* Split remainder is immediately reusable. */ /* 期望 37：上一步切分出的剩余块立即可复用 */
    assert(take(3) == 64);
    assert(nr_free_pages() == 13 && take(13) == -1); /* Fragmentation. */ /* 总页数够但无连续 13 页，分配应失败 */
    assert(take(TEST_PAGES + 1) == -1);
    reclaim_all();
    assert(nr_free_pages() == 23);
    puts("PASS: best fit, splitting, fragmented failure, failure immutability");

    /* --- 场景2：大小相同的候选块，应选物理地址更低的那一个 --- */
    reset();
    add_region(32, 6); add_region(8, 6);                  /* 两个同为 6 页的块：8 的地址更低 */
    assert(take(6) == 8);    /* Lowest-address exact match. */  /* 精确匹配平手时取最低地址 */
    reclaim_all();
    reset();
    add_region(32, 7); add_region(8, 7);                  /* 两个同为 7 页：8 的地址更低 */
    assert(take(5) == 8);    /* Lowest-address non-exact tie. */  /* 非精确平手时同样取最低地址 */
    reclaim_all();
    puts("PASS: equal-size candidates choose the lower address");

    /* --- 场景3：双向合并、前后驱分别合并、多块相邻一起释放 --- */
    reset();
    add_region(8, 16);
    assert(take(16) == 8);
    release(8, 4); release(20, 4); release(12, 8); /* Merge both sides. */  /* 中间块同时与左右两侧合并 */
    assert(take(16) == 8);
    release(12, 4); release(8, 4); release(16, 8); /* Successor then predecessor. */  /* 先后继后前驱的合并顺序 */
    assert(take(4) == 8 && take(4) == 12);
    release(8, 8);          /* Free multiple adjacent allocations together. */  /* 相邻的多个分配一次性释放 */
    assert(nr_free_pages() == 16);
    puts("PASS: partial frees, combined frees, predecessor/successor coalescing");

    /* --- 场景4：init_memmap 乱序登记相邻/不相邻区间 --- */
    reset();
    add_region(32, 8); add_region(16, 8); add_region(24, 8);   /* 乱序叠加三段，最终应合并为 [16,40) */
    add_region(80, 5); add_region(100, 7);                     /* 两段互不相邻的区间 */
    assert(take(24) == 16 && take(5) == 80 && take(7) == 100);
    assert(take(1) == -1);
    reclaim_all();
    assert(nr_free_pages() == 36);
    puts("PASS: out-of-order adjacent and disjoint init_memmap regions");
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：next_random()/randomized_checks() —— 随机序列循环与位图对照 =================
 * 这一块要解决的问题：用可复现的伪随机序列长时间、大范围地混合 alloc/free，
 *   每一步都与位图 oracle 比对，最后要求能完整恢复到满空闲状态（无泄漏/无碎片残留）。
 * 关键不变量：
 *   - next_random() 是 xorshift32 伪随机数发生器：同一 seed 产生同一序列，
 *     因此失败可复现、便于定位；
 *   - 每个种子都从一个固定布局开始（151+91 / 3+57 / 71+67 三段），
 *     跑 5000 步后 reclaim_all()，空闲页数必须精确回到 215；
 *   - 随机过程本身不变量由 take/release 内部的 verify() 逐步保证。
 * 算法思路：每步按 58% : 42% 概率选择“分配 1~40 页”或“释放一段已分配区间”；
 *   释放时先在 model 里找一个已分配的起点，并统计其连续长度以限制释放范围；
 *   若找不到任何已分配页则跳过本轮。
 * 参数与返回值：next_random 无参，返回新的 32 位随机数；randomized_checks 无参无返回。
 * 与其它块的协作：依赖 reset/add_region/take/release/reclaim_all；其中 seed 之一
 *   2410439 正是吴佳璐的学号，作为随机种子的一部分，便于辨识与复现。
 */
static uint32_t rng_state;                       /* xorshift32 的状态寄存器 */
static uint32_t next_random(void) {
    rng_state ^= rng_state << 13;                /* xorshift32：左移 13 位异或 */
    rng_state ^= rng_state >> 17;                /* 右移 17 位异或 */
    rng_state ^= rng_state << 5;                 /* 左移 5 位异或 */
    return rng_state;                            /* 返回本步随机数（同 seed 可复现） */
}

static void randomized_checks(void) {
    const uint32_t seeds[] = {1, 2410439, 0x13579BDF, 0xC0FFEE};  /* 多个固定种子（含学号 2410439） */
    for (size_t seed = 0; seed < sizeof(seeds) / sizeof(seeds[0]); ++seed) {
        rng_state = seeds[seed];                 /* 用当前种子初始化随机序列 */
        reset();
        add_region(151, 91); add_region(3, 57); add_region(71, 67);  /* 固定初始内存布局 */
        for (size_t step = 0; step < 5000; ++step) {
            if ((next_random() % 100) < 58) {    /* 58% 概率：执行分配 */
                (void)take(1 + next_random() % 40);   /* 分配 1~40 页，结果由 take 内部校验 */
            } else {                             /* 42% 概率：执行释放 */
                size_t start = next_random() % TEST_PAGES;
                for (size_t attempt = 0; attempt < TEST_PAGES; ++attempt) {  /* 环形查找一个已分配页 */
                    if (model[start] == ALLOCATED) break;
                    start = (start + 1) % TEST_PAGES;
                }
                if (model[start] != ALLOCATED) continue;   /* 一个都没找到：跳过本轮 */
                size_t end = start;
                while (end < TEST_PAGES && model[end] == ALLOCATED) ++end;  /* 统计连续已分配段长度 */
                release(start, 1 + next_random() % (end - start));   /* 随机释放该段的一部分 */
            }
        }
        reclaim_all();                           /* 回收全部，检验恢复能力 */
        assert(nr_free_pages() == 215);         /* 必须精确回到初始 215 页空闲（无泄漏） */
        printf("PASS: bitmap oracle seed %lu, 5000 iterations, full recovery\n",
               (unsigned long)seeds[seed]);
    }
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：HARNESS main() —— 宿主测试总入口 =================
 * 这一块要解决的问题：编排整个宿主测试流程，先用框架自带的检查、再跑自研的
 *   确定性与随机对照测试。
 * 关键不变量/算法思路：
 *   - 先断言 pmm_manager 确实指向 best_fit_pmm_manager 且 .check 就是框架的
 *     best_fit_check，防止“测错对象”；
 *   - reset() 后 add_region(0, TEST_PAGES) 把 256 页全部登记为空闲；
 *   - 调用 pmm_manager->check() 执行框架原生的 basic_check 与 best_fit_check
 *     （不对其做任何改动），随后 verify 并断言恢复满空闲；
 *   - 再依次执行 deterministic_checks() 与 randomized_checks()。
 * 参数与返回值：无参；正常返回 0（断言失败则 abort）。
 * 与其它块的协作：把前述所有块串联为一条可执行的验证链。
 */
int main(void) {
    assert(strcmp(pmm_manager->name, "best_fit_pmm_manager") == 0);   /* 确认被测对象名称正确 */
    assert(pmm_manager->check == best_fit_check);                     /* 确认 .check 指向框架自检函数 */
    reset();
    add_region(0, TEST_PAGES);               /* 把全部 256 页登记为空闲 */
    pmm_manager->check();    /* Executes unmodified basic_check and best_fit_check. */  /* 跑框架原生检查 */
    verify();
    assert(nr_free_pages() == TEST_PAGES);   /* 框架检查后应完整恢复为满空闲 */
    puts("PASS: original basic_check + best_fit_check, full recovery");
    deterministic_checks();                  /* 自研：确定性场景 */
    randomized_checks();                     /* 自研：随机对照 */
    puts("ALL HOST ALGORITHM CHECKS PASSED (not a QEMU/kernel boot test)");
    return 0;
}
/* ================= 吴佳璐END ================= */
"""


# ================= 吴佳璐：run() —— 命令执行助手 =================
# 这一块要解决的问题：统一“打印即将执行的命令 + 以 check=True 的方式执行”，
#   任何步骤（编译或运行测试程序）失败都会立刻抛出异常并终止脚本。
# 关键约束：命令列表里的每个元素先转成字符串再用 shlex.join 拼成可读形式打印；
#   subprocess.run(..., check=True) 保证非零退出码被视为失败。
# 参数与返回值：command 为命令与其参数的列表/序列；无返回（失败时抛异常）。
# 与其它块的协作：main() 里的 --version、编译、运行可执行文件都通过它调用。
def run(command):
    print("+ " + shlex.join(str(part) for part in command), flush=True)  # 回显将要执行的命令
    subprocess.run(command, check=True)                                  # 执行，非零退出码即报错
# ================= 吴佳璐END =================


# ================= 吴佳璐：Python 驱动 main() 与入口 =================
# 这一块要解决的问题：把工程里的真实源码“拼装”成一个可编译的宿主测试程序并运行。
# 关键步骤与约束：
#   - 定位 code 根目录（脚本所在 tools 的上一级），取得 kern/mm 路径；
#   - 读取真实的 pmm.h，用正则抽取 struct pmm_manager 声明，保证注入到 PMM 模板后
#     结构体布局与内核一致；抽不到就直接报错，避免“悄悄测了错的东西”；
#   - 在临时目录写出 defs.h / pmm.h / check.c 三个文件；
#     check.c 即 HARNESS（其中 #include 了真实的 best_fit_pmm.c）；
#   - 用宿主编译器以 gnu11 标准编译（打开 -Wall -Wextra、定义 ucore_test、
#     并正确设置 include 路径），可选 -fsanitize 打开 ASan/UBSan；
#   - 最后运行生成的可执行文件，验证算法正确性。
# 参数与返回值：无参（命令行参数由 argparse 解析）；无返回值，失败抛异常。
# 与其它块的协作：串联 DEFS/PMM/HARNESS 模板与 run()，构成完整测试流水线。
def main():
    parser = argparse.ArgumentParser(description=__doc__)   # 构造命令行解析器
    parser.add_argument("--cc", default="gcc", help="Host GCC-compatible compiler executable")  # 指定编译器
    parser.add_argument("--sanitize", action="store_true",
                        help="Enable ASan/UBSan if this host compiler supports them")           # 可选：开 sanitizer
    args = parser.parse_args()                              # 解析参数
    code = Path(__file__).resolve().parents[1]              # 定位 code 根目录（tools 的上一级）
    mm = code / "kern" / "mm"                               # 内核 MM 目录
    source = (mm / "pmm.h").read_text(encoding="utf-8")     # 读取真实 pmm.h
    manager = re.search(r"struct pmm_manager\s*\{.*?\};", source, re.DOTALL)  # 抽取结构体声明
    if manager is None:
        raise RuntimeError("Cannot locate the real pmm_manager declaration in pmm.h")  # 抽取失败即中止
    run([args.cc, "--version"])                             # 先确认编译器可用
    with tempfile.TemporaryDirectory(prefix="ucore-best-fit-host-") as temporary:
        work = Path(temporary)                              # 临时工作目录
        (work / "defs.h").write_text(DEFS, encoding="utf-8")   # 写出适配层 defs.h
        (work / "pmm.h").write_text(
            PMM.replace("@MANAGER_DECLARATION@", manager.group(0)), encoding="utf-8")  # 注入真实声明后写 pmm.h
        (work / "check.c").write_text(HARNESS, encoding="utf-8")   # 写出测试主体 check.c
        executable = work / ("check.exe" if os.name == "nt" else "check")  # 可执行文件名随平台变化
        command = [args.cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra",
                   "-Wno-sign-compare", "-Ducore_test", "-I", str(work),
                   "-I", str(mm), "-idirafter", str(code / "libs"),
                   str(work / "check.c"), "-o", str(executable)]   # 组装编译命令
        if args.sanitize:
            command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]  # 追加 sanitizer 选项
        run(command)                                        # 编译
        run([str(executable)])                              # 运行测试


# 脚本入口：作为主程序运行时调用 main()
if __name__ == "__main__":
    main()
# ================= 吴佳璐END =================
