/* ================= 吴佳璐：Buddy 测试模块的文件头 =================
 * 本文件是 lab2 任务三 Buddy System 的配套测试（学号 2410439）。
 *   - 仅当 PMM_USE_BUDDY 与 ucore_test 同时定义（测试构建，make test）时才有实体，
 *     否则整个文件为空；普通构建（make qemu）下测试代码零编入；
 *   - buddy_test.h 提供测试接口，buddy_pmm.h 提供被测分配器的常量与接口，
 *     stdio.h / string.h 用于打印与内存操作。
 *
 * 测试总体思路（见原有英文注释）：
 *   用“独立的行为预言（oracle）”判定对错——期望块长用“乘法翻倍”自己算，可用性用一张
 *   “页位图”自己判断，绝不调用被测分配器自己的阶/search 辅助函数，从而避免用被测代码
 *   去验证被测代码。所有记录都放在有界的静态存储里；数据测试只在一段真实“自有 arena”
 *   上做映射，order-15 夹具只用页描述符、不碰物理数据。
 */
#include <buddy_test.h>

#if defined(PMM_USE_BUDDY) && defined(ucore_test)
#include <buddy_pmm.h>
#include <stdio.h>
#include <string.h>

/* Independent behavioral oracles: expected lengths use multiplication, and
 * availability uses a page bitmap, never the allocator's order/search helpers.
 * All records are bounded static storage. Only an owned real arena is mapped
 * for data tests; the order-15 fixture uses descriptors, never physical data. */
/* ================= 吴佳璐END ================= */
/* ================= 吴佳璐：测试规模常量与记录结构 =================
 * 常量：
 *   - TEST_CAP = 64         ：测试 arena 的页数，也是所有记录数组的容量；
 *   - TEST_ROUNDS = 32      ：case8 随机压力测试的轮数；
 *   - TEST_STEPS = 128      ：case8 每轮的随机操作步数；
 *   - TEST_SEED = 0x02410439：固定随机种子（含学号 2410439 的痕迹），保证每次运行序列
 *     完全可复现，便于定位与回归。
 *
 * struct test_record：记录一条“已分配但尚未释放”的分配，用于交叉检查：
 *   page=分配返回的头页；requested=用户原始请求 n；actual=向上取整后的实际页数；
 *   offset=该块相对 arena 首页的页偏移；tag=写入数据的随机标记（用于检测数据被踩）；
 *   live=该槽位当前是否有效。
 */
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
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：测试全局状态与整块区域常量 =================
 * 这些是测试框架的运行状态（全部静态、有界）：
 *   records[] ：每个槽位的分配记录；registered[] / occupied[] 是两张位图，
 *               registered 标记“该页属于已登记区域”，occupied 标记“该页当前被占用”；
 *   arena / arena_length：当前被测真实 arena 的首描述符与页数；
 *   registered_count ：已登记页总数（用于对账 nr_free_pages）；
 *   cleanup_safe ：body 是否仍处于“可安全清理”状态（一旦发现越界等缺陷就置 0，
 *                  避免后续错误地 free 一个不可信指针）；
 *   current_case/round/step：失败信息里打印的定位坐标；
 *   selected_regions/count/body：run_scope 传给钩子的参数缓存；
 *   whole_region：最常用的“整块 64 页作为一个区域”的区域描述。
 */
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
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：CHECK 断言宏 =================
 * 一个带上下文信息的断言宏：条件不满足时，打印 [buddy_test] FAIL 加 case/round/step、
 * 函数名与行号，再打印用户给的格式化消息，最后 return -1 让用例失败。
 * 约定：所有测试主体返回值都是 int，0 成功、非 0 失败，故失败即 return -1。
 * 用 do{...}while(0) 包裹，保证在 if/else 中作为单条语句使用时不破坏语法。
 * 注意：宏体每行都以反斜杠续行，插入注释时必须保持这些续行符在行尾。
 */
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
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：expected_length（独立计算期望块长） =================
 * 与分配器“向上取整到 2 的幂”等价，但由测试自己用乘法翻倍实现（不调用被测代码）：
 *   - requested==0 或 >32768 视为非法请求，返回 0；
 *   - 从 actual=1 起反复 actual*=2 直到 actual>=requested。
 * 返回 0 表示“该请求应失败”，否则返回期望的 2^k 页数，作为预言值用于对比。
 */
static size_t
expected_length(size_t requested) {
    size_t actual = 1;
    if (requested == 0 || requested > 32768) {   /* 非法请求：应失败 */
        return 0;
    }
    while (actual < requested) {                 /* 乘法翻倍直到 >= 请求页数 */
        actual *= 2;
    }
    return actual;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：descriptor_offset（页指针 -> 相对偏移并校验） =================
 * 判断 page 是否落在 [base, base+capacity) 且满足对齐/长度约束，并输出相对页偏移 *offset。
 * 先做数值校验再做指针相减（见原注释）：page 非空、地址>=base、actual 非 0 且不超 capacity，
 * 否则返回 -1；接着要求字节差能被 sizeof(struct Page) 整除；再要求偏移不超 capacity-actual。
 * 成功返回 0。它是 take() 判断“分配结果是否越出 arena / 是否对齐”的核心工具。
 */
/* Numeric validation comes before pointer subtraction or descriptor access. */
static int
descriptor_offset(struct Page *page, struct Page *base, size_t capacity,
                  size_t actual, size_t *offset) {
    uintptr_t address = (uintptr_t)page, first = (uintptr_t)base;
    if (page == NULL || address < first || actual == 0 || actual > capacity) {   /* 先做数值校验 */
        return -1;
    }
    uintptr_t bytes = address - first;
    if (bytes % sizeof(struct Page) != 0) {      /* 必须整描述符对齐 */
        return -1;
    }
    *offset = bytes / sizeof(struct Page);       /* 换算出相对页偏移 */
    return *offset <= capacity - actual ? 0 : -1;   /* 块整体须落在容量内 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：pattern（生成可校验的数据模式） =================
 * 由 tag（每个分配对象的标记）、页序号 page、字节偏移 byte 通过
 * tag ^ (page*37 + byte*17 + (byte>>5)) 生成一个字节。它是确定性的伪随机模式，
 * 用于把数据写进分配到的页面后，后续再逐字节比对，检测“数据被后续分配踩掉/重叠”。
 */
static uint8_t
pattern(uint8_t tag, size_t page, size_t byte) {
    return (uint8_t)(tag ^ (page * 37 + byte * 17 + (byte >> 5)));
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：data_address（页描述符 -> 可写内核虚拟地址） =================
 * 用 page2pa(page)+va_pa_offset 得到该物理页映射到内核可访问空间后的虚拟地址，
 * 测试用它来真正读写页内容。注意：order-15 的“描述符夹具”没有真实物理内存，
 * 那条路径禁止调用本函数。
 */
static uint8_t *
data_address(struct Page *page) {
    return (uint8_t *)(page2pa(page) + va_pa_offset);
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：fill_record（按记录把数据写入已分配页） =================
 * 对一条记录，从它的头页起，按 requested 页、每页 PGSIZE 字节，用 pattern(tag,page,byte)
 * 填满。只填“用户请求的前 requested 页”（不含取整多出的页），这样释放后用同一 n 校验。
 */
static void
fill_record(const struct test_record *record) {
    uint8_t *data = data_address(record->page);
    for (size_t page = 0; page < record->requested; page++) {
        for (size_t byte = 0; byte < PGSIZE; byte++) {
            data[page * PGSIZE + byte] = pattern(record->tag, page, byte);   /* 写入确定性模式 */
        }
    }
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：check_record（校验一条记录的数据是否完好） =================
 * 对每个有效槽位：遍历它 requested 个页，
 *   - full=true ：逐字节比对 pattern，能发现任何字节被改动；
 *   - full=false：只比对每页首字节和末字节（端点），代价小、用于高频压力校验。
 * 任何不符即 CHECK 失败并返回 -1。用它来捕捉“分配互相覆盖”的 bug。
 */
static int
check_record(size_t slot, bool full) {
    const struct test_record *record = &records[slot];
    CHECK(record->live, "slot %lu is not live", (unsigned long)slot);
    uint8_t *data = data_address(record->page);
    for (size_t page = 0; page < record->requested; page++) {
        if (full) {                              /* 全量逐字节比对 */
            for (size_t byte = 0; byte < PGSIZE; byte++) {
                CHECK(data[page * PGSIZE + byte] == pattern(record->tag, page, byte),
                      "slot %lu page %lu byte %lu data changed", (unsigned long)slot,
                      (unsigned long)page, (unsigned long)byte);
            }
        } else {                                 /* 轻量：只比对每页首尾字节 */
            CHECK(data[page * PGSIZE] == pattern(record->tag, page, 0) &&
                  data[(page + 1) * PGSIZE - 1] == pattern(record->tag, page, PGSIZE - 1),
                  "slot %lu page %lu endpoint changed", (unsigned long)slot,
                  (unsigned long)page);
        }
    }
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：check_all（校验所有在册记录） =================
 * 遍历全部槽位，对每个 live 记录调用 check_record；任一失败即返回 -1。
 * 用于在某步操作后统一确认“所有存活分配的数据都没被破坏”。
 */
static int
check_all(bool full) {
    for (size_t slot = 0; slot < TEST_CAP; slot++) {
        if (records[slot].live && check_record(slot, full) != 0) {   /* 只检查在册记录 */
            return -1;
        }
    }
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：take（申请一个块并做全套交叉校验） =================
 * 参数 slot 指定记录槽、requested 是请求页数、tag 是数据标记。
 * 返回值约定：0=申请成功并校验通过；1=申请返回 NULL（合法但当时无内存）；
 *            -1=发现缺陷（越界、未登记、重叠、计数不对等，属被测代码 bug）。
 * 关键步骤：
 *   1) 记下 before=nr_free_pages()，用 expected_length 算期望实际页数 actual；
 *   2) 调 alloc_pages(requested)；若返回 NULL，则断言空闲数未变（失败请求必须原子）后返回 1；
 *   3) 用 descriptor_offset 两次换算：相对 arena 的 offset，以及相对全局 pages 的 absolute，
 *      确保返回指针确实在被测 arena 内、且落在已登记区域；
 *   4) 断言 absolute 按 actual 对齐（块首必须对齐）；
 *   5) 逐页检查：属于已登记、未被占用、非 Reserved、无 PageProperty、ref==0，
 *      且只有头页 property==requested、其它页 property==0；
 *   6) 登记记录字段、把 occupied 位图对应页置 1；
 *   7) 断言空闲数恰好减少 actual（按取整后的块长扣，而非 requested）；
 *   8) fill_record 写入可校验数据。
 * 若中途发现问题会把 cleanup_safe 置 0（避免后续误 free 不可信指针）再 CHECK 失败。
 */
/* Returns 0 for a validated allocation, 1 for NULL, -1 for a defect. */
static int
take(size_t slot, size_t requested, uint8_t tag) {
    CHECK(slot < TEST_CAP && !records[slot].live, "invalid record slot");
    size_t before = nr_free_pages(), actual = expected_length(requested);   /* 记录计数并算期望页数 */
    struct Page *page = alloc_pages(requested);
    if (page == NULL) {                      /* 合法失败：NULL */
        CHECK(nr_free_pages() == before, "NULL changed free-page count");   /* 失败必须原子 */
        return 1;
    }
    size_t offset, absolute;
    if (descriptor_offset(page, arena, arena_length, actual, &offset) != 0 ||
        descriptor_offset(page, pages, npage - nbase, actual, &absolute) != 0) {   /* 必须落在 arena 内 */
        cleanup_safe = 0;
        CHECK(0, "allocation returned an out-of-arena descriptor");
    }
    if (absolute % actual != 0) {            /* 块首必须按实际块长对齐 */
        cleanup_safe = 0;
        CHECK(0, "block index %lu is not aligned to %lu", (unsigned long)absolute,
              (unsigned long)actual);
    }
    for (size_t i = 0; i < actual; i++) {    /* 逐页交叉校验描述符形态 */
        if (!registered[offset + i] || occupied[offset + i] ||
            PageReserved(page + i) || PageProperty(page + i) ||
            page_ref(page + i) != 0 || page[i].property != (i == 0 ? requested : 0)) {
            cleanup_safe = 0;
            CHECK(0, "bad, overlapping, or unregistered page at arena offset %lu",
                  (unsigned long)(offset + i));
        }
    }
    records[slot].page = page;               /* 登记该分配的完整信息 */
    records[slot].requested = requested;
    records[slot].actual = actual;
    records[slot].offset = offset;
    records[slot].tag = tag;
    records[slot].live = 1;
    for (size_t i = 0; i < actual; i++) {    /* 位图标占用 */
        occupied[offset + i] = 1;
    }
    CHECK(before >= actual && nr_free_pages() == before - actual,
          "request %lu must consume %lu pages", (unsigned long)requested,
          (unsigned long)actual);            /* 空闲数须恰好减 actual */
    fill_record(&records[slot]);             /* 写入可校验数据 */
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：release（释放一个已记录块并校验归还量） =================
 *   - 断言槽位有效，并先 check_record(slot,1) 确认释放前数据完好；
 *   - 记下 before，调 free_pages(record->page, record->requested) 用“原始请求 n”归还；
 *   - 清 live、把 occupied 位图对应页清零；
 *   - 断言空闲数恰好增加了 record->actual（即按实际块长归还，与分配时一致）。
 * 用 requested 而非 actual 归还，正是为了验证分配器“保存原始 n”这一设计的正确性。
 */
static int
release(size_t slot) {
    CHECK(slot < TEST_CAP && records[slot].live, "free of unrecorded slot");
    CHECK(check_record(slot, 1) == 0, "data damaged before release");   /* 释放前数据须完好 */
    struct test_record *record = &records[slot];
    size_t before = nr_free_pages();
    free_pages(record->page, record->requested);   /* 用原始请求 n 归还 */
    record->live = 0;
    for (size_t i = 0; i < record->actual; i++) {  /* 清 occupied 位图 */
        occupied[record->offset + i] = 0;
    }
    CHECK(nr_free_pages() == before + record->actual,
          "original n=%lu did not return %lu pages", (unsigned long)record->requested,
          (unsigned long)record->actual);          /* 归还量须等于实际块长 */
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：release_all（释放全部在册块） =================
 * 遍历所有槽位，对每个 live 记录调用 release；只要有一个失败就把整体结果记为 -1，
 * 但会继续尝试释放其余块（尽量回到干净状态）。
 */
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
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：expect_failure（断言某请求必须失败且状态不变） =================
 * 校验“非法/无内存请求”的原子性：
 *   - 记下 before 与状态指纹 digest；
 *   - 调 alloc_pages(requested)，若竟然成功则说明不该成功的请求成功了（缺陷，置 cleanup_safe=0 后失败）；
 *   - 断言空闲数不变；
 *   - 断言 buddy_test_state_digest() 与之前完全一致（分配器内部状态一点没动）；
 *   - 再 check_all(1) 确认存活数据也没被破坏。
 * 关于指纹：原注释说它是“对确定性失败点的补充”，只在失败用例里用、不在每步压力里用。
 */
/* A complete, read-only state digest supplements public count/data checks.
 * It is only used at deterministic failure points, not at every stress step. */
static int
expect_failure(size_t requested) {
    size_t before = nr_free_pages();
    uint64_t digest = buddy_test_state_digest();   /* 记录请求前的状态指纹 */
    struct Page *page = alloc_pages(requested);
    if (page != NULL) {                            /* 不该成功却成功了：缺陷 */
        cleanup_safe = 0;
        CHECK(0, "alloc_pages(%lu) unexpectedly succeeded", (unsigned long)requested);
    }
    CHECK(nr_free_pages() == before, "failed request changed page count");          /* 计数不变 */
    CHECK(buddy_test_state_digest() == digest, "failed request changed allocator state");  /* 状态不变 */
    CHECK(check_all(1) == 0, "failed request damaged live data");                   /* 数据不受损 */
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：find_offset（按 arena 偏移查找在册槽位） =================
 * 返回 offset 处那个 live 记录的槽号，找不到返回 -1。用例用“页偏移”定位要释放的块，
 * 因为分配返回的指针可能不固定，用偏移更稳定。
 */
static int
find_offset(size_t offset) {
    for (size_t slot = 0; slot < TEST_CAP; slot++) {
        if (records[slot].live && records[slot].offset == offset) {
            return (int)slot;
        }
    }
    return -1;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：release_offset（按偏移释放对应块） =================
 * 先 find_offset 找到槽号，断言存在，再调用 release。用例用它按“页序号”指定释放。
 */
static int
release_offset(size_t offset) {
    int slot = find_offset(offset);
    CHECK(slot >= 0, "missing live page at offset %lu", (unsigned long)offset);
    return release((size_t)slot);
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：recover_whole（恢复整块并验证“整块可再申请”） =================
 * 每次用例收尾的“归位”步骤：
 *   - 释放所有在册块，断言空闲数回到 TEST_CAP(64)；
 *   - 再申请一次 64 页整块，断言成功、且 offset==0（说明大块是从 arena 头开始的，
 *     即此前的分裂都已正确合并回一整块）；
 *   - 最后释放该块。
 */
static int
recover_whole(void) {
    CHECK(release_all() == 0, "cleanup failed");
    CHECK(nr_free_pages() == TEST_CAP, "capacity not restored");              /* 容量需复原 */
    CHECK(take(0, TEST_CAP, 0xb3) == 0, "original 64-page block not recoverable");
    CHECK(records[0].offset == 0, "large block came from outside target arena");   /* 整块须从 0 起 */
    return release(0);
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：check_unregistered_descriptors（检查未登记空洞未被改动） =================
 * 对 arena 内所有“未登记”的页，断言它们保持初始形态：flags 仅含 PG_reserved、
 * ref==0、property==0、且 occupied 位图为 0。用来证明分配器没有越界去动未登记区域。
 */
static int
check_unregistered_descriptors(void) {
    for (size_t i = 0; i < arena_length; i++) {
        if (!registered[i]) {                       /* 只看未登记的页 */
            CHECK(arena[i].flags == ((uint64_t)1 << PG_reserved) &&
                  page_ref(arena + i) == 0 && arena[i].property == 0 &&
                  occupied[i] == 0,
                  "unregistered descriptor %lu was modified", (unsigned long)i);
        }
    }
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：scope_body（隔离钩子里被调用的中转体） =================
 * 这是传给 buddy_test_with_regions 的 body。它建立测试所需全部记账：
 *   - 校验 arena 长度就是 TEST_CAP；
 *   - 记录 arena/arena_length，复位 registered_count、cleanup_safe，
 *     把 records/registered/occupied 三张表清零；
 *   - 按 selected_regions 把“已登记区域”逐页标进 registered 位图（并检查区域不重叠、
 *     落在 arena 内），统计 registered_count；
 *   - 断言此刻 nr_free_pages()==registered_count（分配器空闲数应等于登记页数），
 *     并检查空洞未被改动；
 *   - 调用真正的用例 selected_body(base,length)；
 *   - 若 cleanup_safe 仍为真，释放所有块并断言空闲数恢复；否则跳过清理；
 *   - 再查一次空洞；若用例成功则跑一次结构自检；
 *   - 返回用例结果。
 */
static int
scope_body(struct Page *base, size_t length) {
    CHECK(length == TEST_CAP, "unexpected isolated arena length");
    arena = base;                                 /* 记录当前 arena 首描述符 */
    arena_length = length;
    registered_count = 0;
    cleanup_safe = 1;                             /* 允许清理 */
    memset(records, 0, sizeof(records));          /* 清空三张记账表 */
    memset(registered, 0, sizeof(registered));
    memset(occupied, 0, sizeof(occupied));
    for (size_t r = 0; r < selected_count; r++) { /* 按夹具把区域标入 registered 位图 */
        CHECK(selected_regions[r].offset <= length &&
              selected_regions[r].length <= length - selected_regions[r].offset,
              "invalid fixture region");
        for (size_t i = 0; i < selected_regions[r].length; i++) {
            size_t offset = selected_regions[r].offset + i;
            CHECK(!registered[offset], "fixture regions overlap");   /* 区域不得重叠 */
            registered[offset] = 1;
            registered_count++;
        }
    }
    CHECK(nr_free_pages() == registered_count, "initial registration count mismatch");
    CHECK(check_unregistered_descriptors() == 0, "initialization changed a gap");
    int result = selected_body(base, length);     /* 执行真正的用例 */
    if (cleanup_safe) {                           /* 安全时才做清理对账 */
        if (release_all() != 0 || nr_free_pages() != registered_count) {
            cprintf("[buddy_test] FAIL case%u: scope cleanup did not restore capacity\n",
                    current_case);
            result = -1;
        }
    }
    if (check_unregistered_descriptors() != 0) {  /* 收尾再查空洞 */
        result = -1;
    }
    if (result == 0) {                            /* 成功则做一次结构自检 */
        buddy_test_check_structure();
    }
    return result;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：run_scope（启动一次隔离作用域） =================
 * 把 region 集合、数量、body 缓存到全局，然后调用 buddy_test_with_regions，
 * 传入的中转体是 scope_body。用例只需给“区域 + 用例函数”，不必关心隔离细节。
 */
static int
run_scope(const struct buddy_test_region *regions, size_t count,
          buddy_test_body body) {
    selected_regions = regions;                   /* 缓存给 scope_body 使用 */
    selected_count = count;
    selected_body = body;
    return buddy_test_with_regions(regions, count, scope_body);
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case1_small（小尺寸与边界幂次） =================
 * 用 requests 列表覆盖“1..64 中的各种边界值”（含 2^k 前后 ±1：3,5,7,9,15,17,31,33,63 等）。
 * 对每个请求：申请 1 个槽——成功后立即释放——断言空闲数回到 TEST_CAP(64)。
 * 验证点：向上取整的边界、以及“申请-释放”必须完全归位（不泄漏、不多还）。
 */
static int
case1_small(struct Page *base, size_t length) {
    (void)base; (void)length;                     /* 本用例不使用夹具参数 */
    static const size_t requests[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17,
                                      31, 32, 33, 63, 64};   /* 覆盖 2^k 及其 ±1 边界 */
    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
        current_step = i;
        CHECK(take(0, requests[i], (uint8_t)(0x20 + i)) == 0,
              "boundary request %lu failed", (unsigned long)requests[i]);
        CHECK(release(0) == 0 && nr_free_pages() == TEST_CAP,
              "boundary release did not restore baseline");   /* 立即归还须完全归位 */
    }
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：validate_global_allocation（校验真实全局内存上的大块分配） =================
 * 对从真实全局内存（非 64 页夹具）分配到的 page，检查：
 *   - 每页非 Reserved、无 PageProperty、ref==0，且只有头页 property==requested；
 *   - 空闲数恰好减少 actual；
 *   - 真实读写该块的首页首字节与“请求末页最后一字节”，确认物理地址确实可写且值正确。
 * 原注释强调：page_init 目前只登记一段空闲后缀，已知 baseline 提供了它的下界（不受被测
 * 分配器改 flags 的影响）；全局 smoke 测试不复用 64 页夹具，也不映射描述符内存。
 */
/* page_init currently registers one free suffix. The known entry baseline
 * gives its lower bound, independently of flags the tested allocator changes.
 * Global smoke tests do not reuse the 64-page fixture or map descriptor memory. */
static int
validate_global_allocation(struct Page *page, size_t requested, size_t actual,
                           size_t before) {
    for (size_t i = 0; i < actual; i++) {         /* 逐页校验描述符形态 */
        CHECK(!PageReserved(page + i) && !PageProperty(page + i) &&
              page_ref(page + i) == 0 &&
              page[i].property == (i == 0 ? requested : 0),
              "global n=%lu bad descriptor %lu", (unsigned long)requested,
              (unsigned long)i);
    }
    CHECK(before >= actual && nr_free_pages() == before - actual,
          "high-order rounding count mismatch");    /* 空闲数须按实际块长减少 */
    volatile uint8_t *data = data_address(page);    /* 拿到可写虚拟地址 */
    data[0] = 0x35;                                 /* 写首页首字节 */
    data[requested * PGSIZE - 1] = 0xca;            /* 写“请求末页最后一字节” */
    CHECK(data[0] == 0x35 && data[requested * PGSIZE - 1] == 0xca,
          "real high-order endpoint data mismatch");   /* 回读确认可写正确 */
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：global_request（在真实内存上做一次请求） =================
 *   - actual=expected_length(requested)，before=nr_free_pages()；
 *   - must_fail = (actual>16384)：真实 RAM 里 order-15（32768 页）块通常不可用，
 *     所以只有“必然不可满足”的大请求才是必须失败的情形；其余大请求若失败则记为 NOT COVERED(-2)。
 *   - 调用 alloc_pages；返回 NULL 时：若 must_fail 则断言空闲数与状态指纹都没变并成功返回，
 *     否则打印 NOT COVERED 返回 -2。
 *   - 成功时用 descriptor_offset 校验返回块在 [free_start, limit) 内且对齐；
 *     若不合规就 panic（原注释解释：此时已持有一块“不属于我们的”内存，
 *     继续写/释放都不安全，只能中止）。
 *   - 对于正常请求，调 validate_global_allocation 校验；无论成败都 free_pages 归还并断言归位。
 */
static int
global_request(size_t requested, size_t free_start, size_t limit) {
    size_t actual = expected_length(requested), before = nr_free_pages();
    bool must_fail = actual > 16384;              /* 超过 16384 页(>order-14)在真实 RAM 必失败 */
    uint64_t digest = must_fail ? buddy_test_state_digest() : 0;
    struct Page *page = alloc_pages(requested);
    if (page == NULL) {                           /* 分配失败 */
        CHECK(nr_free_pages() == before, "global NULL changed page count");
        if (must_fail) {                          /* 本就应该失败：成功返回 */
            CHECK(buddy_test_state_digest() == digest,
                  "unavailable real maximum block changed state");
            return 0;
        }
        cprintf("[buddy_test] case1 real high-order n=%lu: NOT COVERED (unavailable)\n",
                (unsigned long)requested);        /* 环境受限，记为未覆盖 */
        return -2;
    }

    size_t index;
    if (descriptor_offset(page, pages, limit, actual, &index) != 0 ||
        index < free_start || index % actual != 0) {   /* 返回块必须属于可管理区且对齐 */
        cprintf("[buddy_test] FAIL case1 global n=%lu: invalid/unowned return %p; "
                "unsafe to write or release\n", (unsigned long)requested, page);
        panic("buddy_test: unsafe global allocation; cannot continue safely");
    }

    /* From here the original pointer/n pair is retained until cleanup. A
     * validator failure must never return past this owned live allocation. */
    int result;
    if (must_fail) {                              /* 本应失败却成功了：缺陷 */
        cprintf("[buddy_test] FAIL case1: unavailable real n=%lu unexpectedly succeeded\n",
                (unsigned long)requested);
        result = -1;
    } else {
        result = validate_global_allocation(page, requested, actual, before);
    }
    free_pages(page, requested);                  /* 无论成败都归还该块 */
    CHECK(nr_free_pages() == before, "real high-order cleanup did not restore baseline");
    return result;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case1_global（真实内存上的 2^k±1 边界） =================
 * 对 power=64,128,...,16384，取 requested = power-1/power/power+1，
 * 依次调用 global_request 在真实内存上验证“跨阶边界（含向上取整与降阶）”。
 * 全部通过后跑一次结构自检并打印 PASS。
 */
static int
case1_global(size_t baseline) {
    size_t limit = npage - nbase, free_start = limit - baseline;   /* 空闲区起点（已知空闲后缀） */
    for (size_t power = 64; power <= 16384; power *= 2) {          /* k=6..14 */
        for (int delta = -1; delta <= 1; delta++) {                /* power-1 / power / power+1 */
            int result = global_request(power + delta, free_start, limit);
            if (result != 0) { return result; }
        }
    }
    buddy_test_check_structure();
    cprintf("[buddy_test] case1 real boundaries 2^k +/- 1, k=6..14: PASS\n");
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case2_split（最细粒度分裂与真正耗尽） =================
 * 连续申请 64 次“1 页”：每页都应成功，并把 arena 完全切成 64 个单页块。
 *   - 断言 64 次后空闲数==0（arena 被耗尽）；
 *   - 断言 occupied 位图 64 页全为 1（每页都真正暴露出来，没有合并/漏页）；
 *   - 用 expect_failure(1) 断言再申请一页会失败且状态不变；
 *   - check_all(1) 确认这 64 个分配互不重叠；
 *   - 最后 recover_whole 收尾（验证它们能重新合并成整块）。
 */
static int
case2_split(struct Page *base, size_t length) {
    (void)base; (void)length;
    for (size_t i = 0; i < TEST_CAP; i++) {      /* 逐页申请，把大块一路分裂到单页 */
        current_step = i;
        CHECK(take(i, 1, (uint8_t)(i + 1)) == 0, "split lost capacity at page %lu",
              (unsigned long)i);
    }
    CHECK(nr_free_pages() == 0, "64 one-page allocations did not exhaust arena");   /* 已耗尽 */
    for (size_t i = 0; i < TEST_CAP; i++) {
        CHECK(occupied[i] == 1, "split failed to expose every page");   /* 每页都被占用 */
    }
    CHECK(expect_failure(1) == 0, "exhausted split arena accepted another page");   /* 再要必失败 */
    CHECK(check_all(1) == 0, "split allocations overlap in data");      /* 数据不重叠 */
    return recover_whole();
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case3_merge（三种释放顺序下的六级合并） =================
 * 对 order=0,1,2 三种“释放顺序”各做一轮：
 *   - 先申请 64 个单页块（把整块打散到最细）；
 *   - 按顺序释放全部页：order0=正序；order1=逆序；order2=(i*17)%64 的伪随机序；
 *   - 断言每轮结束后 recover_whole 都能成功（即所有小页最终合并回一整块）。
 * 意义：合并正确性与“释放的先后顺序”无关（伙伴系统要求任意顺序都能正确向上合并）。
 */
static int
case3_merge(struct Page *base, size_t length) {
    (void)base; (void)length;
    for (unsigned int order = 0; order < 3; order++) {   /* 三种释放顺序 */
        current_round = order;
        for (size_t i = 0; i < TEST_CAP; i++) {
            CHECK(take(i, 1, (uint8_t)(i + 19)) == 0, "cannot create 64 small blocks");
        }
        for (size_t i = 0; i < TEST_CAP; i++) {
            size_t offset = order == 0 ? i : order == 1 ? TEST_CAP - 1 - i : (i * 17) % TEST_CAP;   /* 正序/逆序/伪随机序 */
            CHECK(release_offset(offset) == 0, "release order %u failed", order);
        }
        CHECK(recover_whole() == 0, "six merge levels failed, order %u", order);   /* 必须能合回整块 */
    }
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case4_nonbuddy（非伙伴、伙伴被占用时不得合并） =================
 * 用 4 个相邻单页 0,1,2,3：
 *   - 释放页 1；此时页 0 仍被占用，页 1 的伙伴（0）不可用 → expect_failure(2) 必须失败
 *     （证明“伙伴被占用时不能合并”）；
 *   - 再释放页 2，空闲数==2；此时 1 与 2 物理相邻但 1^1==0 != 2，二者不是伙伴
 *     （见原有注释）→ expect_failure(2) 必须失败（相邻但非伙伴不得合并）；
 *   - 释放页 0：0 与 1 是真伙伴 → 找空槽申请 2 页，断言成功且 offset==0
 *     （证明 0、1 已合并成从 0 起的 2 页块）；
 *   - check_all(1) 确认仍存活的页 3 数据没被改。
 * 这是对“伙伴判定规则”的正反两向验证。
 */
static int
case4_nonbuddy(struct Page *base, size_t length) {
    (void)base; (void)length;
    for (size_t i = 0; i < 4; i++) {             /* 先占住 0..3 四个相邻单页 */
        CHECK(take(i, 1, (uint8_t)(0x70 + i)) == 0, "four-page setup failed");
    }
    CHECK(release_offset(1) == 0, "cannot free page 1");            /* 释放页 1 */
    CHECK(expect_failure(2) == 0, "occupied buddy must prevent merge");   /* 伙伴 0 被占用，不得合并 */
    CHECK(release_offset(2) == 0 && nr_free_pages() == 2, "nonbuddy setup count");   /* 释放页 2 */
    /* Relative offsets 1 and 2 are adjacent, but 1 xor 1 == 0, not 2. */
    CHECK(expect_failure(2) == 0, "adjacent non-buddies incorrectly merged");   /* 1、2 相邻但非伙伴 */
    CHECK(release_offset(0) == 0, "cannot release true buddy");     /* 释放页 0（1 的真伙伴） */
    int slot = 0;
    while (records[slot].live) { slot++; }       /* 找一个空记录槽 */
    CHECK(take(slot, 2, 0xc1) == 0 && records[slot].offset == 0,
          "true buddies 0 and 1 did not merge");  /* 0、1 应已合并成从 0 起的 2 页块 */
    CHECK(check_all(1) == 0, "still-live page 3 changed");   /* 存活的页 3 数据不受影响 */
    return release_all();
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case4_holes（未登记空洞不得参与合并/分配） =================
 * 该夹具只登记两个不连续的单页 {0,1} 和 {2,1}（中间的页作为“未登记空洞”）：
 *   - 断言空闲数==2；
 *   - expect_failure(2) 必须失败：虽然物理上 0、1、2 连续，但空洞使它们
 *     无法合并成一个 2 页块（证明合并只认“同阶、已登记的空闲头”）；
 *   - 分别申请两个单页成功；
 *   - expect_failure(1) 必须失败：说明未登记空洞/保留页不会被当成可用页；
 *   - release_all 收尾。
 */
static int
case4_holes(struct Page *base, size_t length) {
    (void)base; (void)length;
    CHECK(nr_free_pages() == 2, "hole fixture capacity");        /* 只登记了 2 页 */
    CHECK(expect_failure(2) == 0, "unregistered gap was merged"); /* 空洞不能参与合并 */
    CHECK(take(0, 1, 0x31) == 0 && take(1, 1, 0x52) == 0, "registered singleton unavailable");
    CHECK(expect_failure(1) == 0, "unregistered or reserved page became available");   /* 空洞不可用 */
    return release_all();
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case5_data（混合尺寸、全量数据隔离、交错复用） =================
 * 依次按 sizes={1,3,5,2,7,1} 申请 6 个块并填充数据；然后：
 *   - check_all(1) 全量逐字节校验：不同对象数据互不干扰；
 *   - 交错释放 1、3、5 三个块，再全量校验，确认释放不伤及存活块；
 *   - 交错重新申请 1→3、3→2、5→1（用不同 tag），再全量校验，
 *     确认“复用刚释放的空间”不会覆盖存活的其它分配；
 *   - 以另一种顺序释放 4、0、2，再全量校验剩余的 1、3、5；
 *   - 最后 recover_whole 收尾。
 * 重点验证“数据隔离 + 交错复用正确性”，是比 case1 更贴近真实使用的场景。
 */
static int
case5_data(struct Page *base, size_t length) {
    (void)base; (void)length;
    static const size_t sizes[] = {1, 3, 5, 2, 7, 1};    /* 混合尺寸请求 */
    for (size_t i = 0; i < 6; i++) {
        CHECK(take(i, sizes[i], (uint8_t)(0x90 + 7 * i)) == 0, "mixed setup allocation failed");
    }
    CHECK(check_all(1) == 0, "mixed objects are not independent");     /* 混合对象互不干扰 */
    CHECK(release(1) == 0 && release(3) == 0 && release(5) == 0, "interleaved release failed");
    CHECK(check_all(1) == 0, "release damaged a surviving block");     /* 释放不影响存活块 */
    CHECK(take(1, 3, 0xe1) == 0 && take(3, 2, 0xe2) == 0 && take(5, 1, 0xe3) == 0,
          "interleaved reallocation failed");                          /* 交错复用 */
    CHECK(check_all(1) == 0, "reuse overwrote a surviving allocation");   /* 复用不覆盖存活块 */
    CHECK(release(4) == 0 && release(0) == 0 && release(2) == 0,
          "alternate release order failed");                           /* 换一种释放顺序 */
    CHECK(check_all(1) == 0, "remaining objects damaged after alternate release");
    return recover_whole();
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case6_failure（非法/溢出/耗尽/碎片化四类失败） =================
 *   - 先申请一个 3 页幸存块；
 *   - 对非法请求集合 {0, 32769, (size_t)-1, (size_t)-2, 1<<63, (1<<63)-1} 逐个调用
 *     expect_failure：证明非法/超容量请求都被拒绝且不改变状态（原子性）；
 *   - 释放幸存块；再连续申请 64 个单页直至耗尽；
 *   - 断言空闲数==0 且 expect_failure(1) 仍成立（真正耗尽后多要一页必失败）；
 *   - 然后释放偶数序号页（0,2,...），得到 32 页空闲但都是“1 页的碎片”：断言空闲数==32；
 *   - expect_failure(2) 与 expect_failure(32) 必须失败：虽然总共 32 页空闲，
 *     但它们不连续、无 2 页的对齐块，所以更高阶(2 页/32 页)请求拿不到
 *     ——这是“空闲页总量够、但高阶块不可用”的碎片化证据；
 *   - check_all(1) 确认被占用的伙伴数据未被破坏；最后 recover_whole。
 */
static int
case6_failure(struct Page *base, size_t length) {
    (void)base; (void)length;
    CHECK(take(0, 3, 0xd5) == 0, "cannot allocate survivor");   /* 先占一个 3 页幸存块 */
    static const size_t invalid[] = {0, 32769, (size_t)-1, (size_t)-2,
                                     ((size_t)1 << 63), ((size_t)1 << 63) - 1};   /* 非法/溢出请求 */
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        CHECK(expect_failure(invalid[i]) == 0, "invalid request was not atomic");   /* 必须失败且原子 */
    }
    CHECK(release_all() == 0, "survivor release failed");
    for (size_t i = 0; i < TEST_CAP; i++) {                     /* 连续申请 64 个单页直至耗尽 */
        CHECK(take(i, 1, (uint8_t)(0x40 + i)) == 0, "exhaustion setup lost capacity");
    }
    CHECK(nr_free_pages() == 0 && expect_failure(1) == 0, "real exhaustion not reached");   /* 真耗尽 */
    cprintf("[buddy_test] case6: genuine exhaustion at 64/64 pages (extra request returned NULL)\n");
    for (size_t i = 0; i < TEST_CAP; i += 2) {                  /* 只释放偶数序号页 */
        CHECK(release_offset(i) == 0, "fragmentation setup failed");
    }
    CHECK(nr_free_pages() == TEST_CAP / 2, "fragmented free count must be 32");    /* 32 页空闲但碎片 */
    CHECK(expect_failure(2) == 0 && expect_failure(32) == 0,
          "fragmented arena returned unavailable higher order");   /* 高阶块不可用 */
    CHECK(check_all(1) == 0, "fragmentation damaged occupied buddies");   /* 占用伙伴数据无损 */
    return recover_whole();
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case7_regions（不规则/多个区域登记的正确性） =================
 * 本用例对“登记区域”的形状做检查（同一函数会被不同 region 夹具复用）：
 *   - 对 registered_count 个登记页逐个申请单页，全部应成功（登记的页不多不少刚好可用）；
 *   - 断言空闲数==0 且再申请 1 页失败（容量与外围边界都对）；
 *   - 断言 occupied[i]==registered[i]：每个已登记页恰好被占用一次，未登记页从不被占用；
 *   - release_all 释放全部；
 *   - 若 registered_count==64（即区域恰好拼成整块 64 页），再 recover_whole 验证
 *     “多段分别登记的区域能合并成一整块”。
 * buddy_test() 会为它准备 4 组夹具：单段 irregular、单页 singleton、三段 disjoint、
 * 以及三段 adjacent（用来验证“相邻的独立区域也能合并”）。
 */
static int
case7_regions(struct Page *base, size_t length) {
    (void)base; (void)length;
    for (size_t i = 0; i < registered_count; i++) {             /* 每个登记页各申请一次 */
        CHECK(take(i, 1, (uint8_t)(0x11 + i)) == 0, "registered tail page lost");
    }
    CHECK(nr_free_pages() == 0 && expect_failure(1) == 0,
          "region capacity or outer bound mismatch");           /* 容量刚好用尽，外围边界正确 */
    for (size_t i = 0; i < TEST_CAP; i++) {
        CHECK(occupied[i] == registered[i], "registered page not counted exactly once");   /* 占用==登记 */
    }
    CHECK(release_all() == 0, "irregular-region cleanup failed");
    if (registered_count == TEST_CAP) {                         /* 区域拼成整块时验证合并 */
        CHECK(recover_whole() == 0, "adjacent separately registered regions did not coalesce");
    }
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case7_max_descriptors（order-15 最高阶边界，纯描述符） =================
 * 由 buddy_test_with_max_descriptors 调用，环境是一份 32768 页的“纯描述符”数组。
 * 原注释强调：夹具不提供对应 128 MiB 真实物理内存，因此本函数绝不调用 data_address()。
 * 过程：
 *   - 断言夹具正确（base==pages、length==32768、BUDDY_MAX_ORDER==15、BUDDY_MAX_PAGES==32768、
 *     初始空闲数==length）；
 *   - 对 power=1..32768、requested=power-1/power/power+1（跳过 0 与越界值）逐个：
 *     申请、用 descriptor_offset 校验返回落在夹具内且对齐、空闲数恰好减 actual、
 *     每页描述符形态正确（仅头页 property==requested 等），然后释放并断言空闲数还原；
 *   - 最后断言 alloc_pages(32769) 与 alloc_pages((size_t)-1) 都返回 NULL，且空闲数与状态指纹
 *     不变（最高阶溢出被正确拒绝）；
 *   - 结构自检并打印 PASS。覆盖 orders 0..15 的全部边界。
 */
/* Descriptors only: the fixture supplies no corresponding 128 MiB of owned
 * physical memory. This function deliberately never calls data_address(). */
static int
case7_max_descriptors(struct Page *base, size_t length) {
    CHECK(base == pages && length == 32768 && BUDDY_MAX_ORDER == 15 &&
          BUDDY_MAX_PAGES == 32768 && nr_free_pages() == length,
          "maximum-order descriptor fixture mismatch");   /* 校验夹具正确性 */
    for (size_t power = 1; power <= 32768; power *= 2) {
        for (int delta = -1; delta <= 1; delta++) {
            size_t requested = power + delta;       /* power-1 / power / power+1 */
            if (requested == 0 || requested > 32768) {
                continue;                           /* 跳过越界边界值 */
            }
            size_t actual = expected_length(requested), offset;
            struct Page *page = alloc_pages(requested);
            CHECK(descriptor_offset(page, base, length, actual, &offset) == 0 &&
                  offset % actual == 0, "max fixture failed n=%lu", (unsigned long)requested);
            CHECK(nr_free_pages() == length - actual, "max fixture rounding count wrong");   /* 计数按块长 */
            for (size_t i = 0; i < actual; i++) {   /* 逐页校验描述符形态 */
                CHECK(!PageReserved(page + i) && !PageProperty(page + i) &&
                      page_ref(page + i) == 0 &&
                      page[i].property == (i == 0 ? requested : 0),
                      "bad maximum-range descriptor");
            }
            free_pages(page, requested);
            CHECK(nr_free_pages() == length, "max fixture release lost capacity");   /* 释放后复原 */
        }
    }
    size_t before = nr_free_pages();
    uint64_t digest = buddy_test_state_digest();
    CHECK(alloc_pages(32769) == NULL && alloc_pages((size_t)-1) == NULL,
          "maximum-order overflow accepted");       /* 超容量必须返回 NULL */
    CHECK(nr_free_pages() == before && buddy_test_state_digest() == digest,
          "maximum-order failure changed state");   /* 失败请求不得改变状态 */
    buddy_test_check_structure();
    cprintf("[buddy_test] case7: orders 0..15 descriptor-only boundaries: PASS\n");
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：next_random（xorshift32 伪随机数） =================
 * 32 位 xorshift 随机发生器：对状态做三次移位异或（<<13、>>17、<<5），返回更新后的状态。
 * 确定性、零依赖，配合固定种子 TEST_SEED 生成可复现的随机序列。
 */
static uint32_t
next_random(uint32_t *state) {
    *state ^= *state << 13;      /* 左移 13 位异或 */
    *state ^= *state >> 17;      /* 右移 17 位异或 */
    *state ^= *state << 5;       /* 左移 5 位异或 */
    return *state;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：oracle_available（独立位图预言：某阶是否真的可用） =================
 * 不调用分配器的任何辅助函数，纯粹用 registered[]/occupied[] 两张位图判断：
 *   - 在 arena 内枚举所有起点 start，只看“按 actual 对齐”的起点（对齐条件用 arena 的
 *     全局下标 (arena_index+start) 判断）；
 *   - 若存在一段长度 actual、全部“已登记且未占用”的连续区间，则该块可用，返回 true。
 * 这是 case8 的判定基准：分配器能不能给出 actual 大小的块，应该与这张位图一致；
 * 它不假设链表顺序，也不假设分配器会挑哪一块。
 */
/* Bitmap oracle: a block exists iff an aligned all-free interval exists.
 * Neither list order nor the allocator's choice among such blocks is assumed. */
static bool
oracle_available(size_t actual) {
    if (actual == 0 || actual > arena_length) {    /* 非法尺寸视为不可用 */
        return 0;
    }
    size_t arena_index = ((uintptr_t)arena - (uintptr_t)pages) / sizeof(struct Page);   /* arena 的全局下标 */
    for (size_t start = 0; start <= arena_length - actual; start++) {   /* 枚举候选起点 */
        if ((arena_index + start) % actual != 0) {  /* 只看按 actual 对齐的起点 */
            continue;
        }
        bool available = 1;
        for (size_t i = 0; i < actual; i++) {       /* 检查这一段是否全“已登记且空闲” */
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
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：case8_stress（固定种子随机压力 + 位图预言对照） =================
 * 用 TEST_SEED(0x02410439) 初始化 xorshift，做 TEST_ROUNDS(32) 轮、每轮 TEST_STEPS(128) 步
 * 随机“申请或释放”：
 *   - 每步先统计有多少 live 记录、以及第一个空槽；
 *   - 取随机数 choice：若已有分配且 (choice&3)==0（约 1/4 概率）或已无空槽，则随机释放一个
 *     live 记录；否则随机挑一个 requests 里的尺寸去申请；
 *   - 申请前先问 oracle_available(expected_length(requested)) 得到“位图是否认为可用”，
 *     再真正 take，并断言 (result==0)==available：分配器的成败必须与位图预言一致
 *     （这是本用例的核心正确性判据）；
 *   - 每步用过 check_all（仅在每 16 步做全量逐字节校验，其余做端点校验）确认存活数据完好；
 *   - 每轮结束用 recover_whole 断言能恢复成一整块 64 页。
 * 兼顾“随机性覆盖”和“可复现定位”，是最强的综合回归用例。
 */
static int
case8_stress(struct Page *base, size_t length) {
    (void)base; (void)length;
    static const size_t requests[] = {1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33};   /* 随机尺寸池 */
    uint32_t rng = TEST_SEED;                    /* 固定种子，保证可复现 */
    cprintf("[buddy_test] case8: seed=0x%08x, rounds=%u, operations/round=%u\n",
            (unsigned int)TEST_SEED, TEST_ROUNDS, TEST_STEPS);
    for (current_round = 0; current_round < TEST_ROUNDS; current_round++) {
        for (current_step = 0; current_step < TEST_STEPS; current_step++) {
            size_t live = 0, empty = TEST_CAP;
            for (size_t i = 0; i < TEST_CAP; i++) {     /* 统计在册数并找第一个空槽 */
                if (records[i].live) { live++; }
                else if (empty == TEST_CAP) { empty = i; }
            }
            uint32_t choice = next_random(&rng);
            if (live != 0 && ((choice & 3) == 0 || empty == TEST_CAP)) {   /* 释放分支 */
                size_t nth = next_random(&rng) % live;   /* 随机选第 nth 个在册记录 */
                for (size_t i = 0; i < TEST_CAP; i++) {
                    if (records[i].live && nth-- == 0) {
                        CHECK(release(i) == 0, "stress release failed");
                        break;
                    }
                }
            } else {                                     /* 申请分支 */
                size_t requested = requests[next_random(&rng) %
                    (sizeof(requests) / sizeof(requests[0]))];   /* 随机挑一个请求尺寸 */
                bool available = oracle_available(expected_length(requested));   /* 位图预言 */
                int result = take(empty, requested, (uint8_t)next_random(&rng));
                CHECK(result >= 0 && ((result == 0) == available),
                      "bitmap oracle disagrees for n=%lu, available=%d result=%d",
                      (unsigned long)requested, available, result);   /* 成败必须与预言一致 */
            }
            CHECK(check_all((current_step % 16) == 0) == 0,
                  "stress changed surviving data");   /* 每 16 步做一次全量校验 */
        }
        CHECK(recover_whole() == 0, "stress round did not recover original large block");
    }
    return 0;
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：report_case（打印并统计单个用例结果） =================
 * 把 result 归为三态：0=PASS 并给 passed 计数加一；-2=NOT COVERED（受环境限制未覆盖）；
 * 其它=FAIL。按 [buddy_test] caseN (名字): 结果 的格式打印，便于日志阅读。
 */
static void
report_case(unsigned int number, const char *name, int result, unsigned int *passed) {
    cprintf("[buddy_test] case%u (%s): %s\n", number, name,
            result == 0 ? "PASS" : result == -2 ? "NOT COVERED" : "FAIL");   /* 三态归类 */
    if (result == 0) { (*passed)++; }            /* 仅 PASS 计入通过组数 */
}
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：buddy_test（测试总入口，跑完 case1..case8） =================
 * 流程：
 *   - 断言当前管理器是 buddy；记下 baseline 空闲页数、passed 计数；
 *   - case1：先用 64 页夹具跑 case1_small，再在真实内存跑 case1_global(baseline)
 *     验证 2^k±1 边界（"basic, rounding and real size boundaries"）；
 *   - case2：case2_split（"all split remainders and true exhaustion"）；
 *   - case3：case3_merge（"six-level merge in three release orders"）；
 *   - case4：先四页夹具 case4_nonbuddy，再两段空洞夹具 case4_holes
 *     （"non-buddies, occupied buddies and unregistered holes"）；
 *   - case5：case5_data（"full requested-page data and interleaved reuse"）；
 *   - case6：case6_failure（"invalid, overflow, exhausted and fragmented failures"）；
 *   - case7：依次用 irregular/singleton/disjoint/adjacent 四组区域夹具跑 case7_regions，
 *     再跑 buddy_test_with_max_descriptors(case7_max_descriptors) 覆盖 order-15；
 *     之后打印说明：“真实的 order-15 物理分配未覆盖，因为固件/内核保留了部分 128MiB”；
 *   - case8：case8_stress（"fixed-seed bitmap-oracle allocation/recovery"）；
 *   - 最后检查空闲数是否回到 baseline；若 8 组全过且已恢复，跑结构自检并打印成功，
 *     否则打印通过组数并保留失败信息；
 *   - 末尾的 assert(passed==8 && restored)：故意在失败时断言，
 *     以免调用者（通用 check_alloc_page）误打印“成功”信息。
 * 每个 case 前用 current_case 标记，供 CHECK 宏打印定位信息。
 */
void
buddy_test(void) {
    assert(pmm_manager == &buddy_pmm_manager);   /* 必须是 buddy 管理器 */
    size_t baseline = nr_free_pages();           /* 测试前空闲页数基线 */
    unsigned int passed = 0;                     /* 通过的用例组数 */
    int result;
    cprintf("[buddy_test] start, baseline free pages = %lu\n", (unsigned long)baseline);

    current_case = 1; current_round = current_step = 0;
    result = run_scope(whole_region, 1, case1_small);           /* case1：夹具内小尺寸/边界 */
    if (result == 0) { result = case1_global(baseline); }       /* 再在真实内存验 2^k±1 */
    report_case(1, "basic, rounding and real size boundaries", result, &passed);

    current_case = 2; current_round = current_step = 0;
    report_case(2, "all split remainders and true exhaustion",
                run_scope(whole_region, 1, case2_split), &passed);   /* case2：分裂与耗尽 */

    current_case = 3; current_round = current_step = 0;
    report_case(3, "six-level merge in three release orders",
                run_scope(whole_region, 1, case3_merge), &passed);   /* case3：三种顺序合并 */

    current_case = 4; current_round = current_step = 0;
    static const struct buddy_test_region four[] = {{0, 4}};         /* 四页相邻夹具 */
    static const struct buddy_test_region holes[] = {{0, 1}, {2, 1}};/* 两段带空洞夹具 */
    result = run_scope(four, 1, case4_nonbuddy);
    if (result == 0) { result = run_scope(holes, 2, case4_holes); }
    report_case(4, "non-buddies, occupied buddies and unregistered holes", result, &passed);

    current_case = 5; current_round = current_step = 0;
    report_case(5, "full requested-page data and interleaved reuse",
                run_scope(whole_region, 1, case5_data), &passed);    /* case5：数据隔离+复用 */

    current_case = 6; current_round = current_step = 0;
    report_case(6, "invalid, overflow, exhausted and fragmented failures",
                run_scope(whole_region, 1, case6_failure), &passed); /* case6：四类失败 */

    current_case = 7; current_round = current_step = 0;
    static const struct buddy_test_region irregular[] = {{3, 29}};   /* 四组区域夹具 */
    static const struct buddy_test_region singleton[] = {{17, 1}};
    static const struct buddy_test_region disjoint[] = {{41, 7}, {2, 9}, {20, 13}};
    static const struct buddy_test_region adjacent[] = {{32, 32}, {0, 16}, {16, 16}};
    result = run_scope(irregular, 1, case7_regions);
    if (result == 0) { result = run_scope(singleton, 1, case7_regions); }
    if (result == 0) { result = run_scope(disjoint, 3, case7_regions); }
    if (result == 0) { result = run_scope(adjacent, 3, case7_regions); }   /* 相邻区域应能合并 */
    if (result == 0) { result = buddy_test_with_max_descriptors(case7_max_descriptors); }   /* order-15 */
    report_case(7, "irregular/multiple regions and max-order descriptors", result, &passed);
    cprintf("[buddy_test] NOT COVERED: actual order-15 (32768-page) physical allocation; "
            "firmware/kernel reserve part of the 128 MiB RAM\n");

    current_case = 8; current_round = current_step = 0;
    report_case(8, "fixed-seed bitmap-oracle allocation/recovery",
                run_scope(whole_region, 1, case8_stress), &passed);   /* case8：随机压力 */

    bool restored = nr_free_pages() == baseline;   /* 空闲数是否回到基线 */
    if (!restored) {
        cprintf("[buddy_test] FAIL final baseline: expected %lu, got %lu\n",
                (unsigned long)baseline, (unsigned long)nr_free_pages());
    }
    if (passed == 8 && restored) {                 /* 全部通过且已复原 */
        buddy_test_check_structure();
        cprintf("[buddy_test] 8/8 required groups passed (coverage exclusions listed above); "
                "baseline restored to %lu\n", (unsigned long)baseline);
    } else {
        cprintf("[buddy_test] %u/8 groups passed; failures/uncovered groups retained above\n", passed);
    }
    /* Prevent the caller's generic check_alloc_page success message on failure. */
    assert(passed == 8 && restored);               /* 失败时断言，避免调用者误报成功 */
}
/* ================= 吴佳璐END ================= */
#endif /* PMM_USE_BUDDY && ucore_test */
