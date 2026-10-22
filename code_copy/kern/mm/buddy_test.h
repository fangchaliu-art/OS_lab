#ifndef __KERN_MM_BUDDY_TEST_H__
#define __KERN_MM_BUDDY_TEST_H__

#include <pmm.h>

/* ================= 吴佳璐：Buddy 测试用常量与数据结构 =================
 * 这一块定义「伙伴分配测试框架」对外暴露的规模常量与数据结构：
 *   - BUDDY_TEST_ARENA_PAGES = 64：测试会先从真实内存里独占申请一个 64 页 arena
 *     （即调用 alloc_pages(64)），把被测的 Buddy 算法限制在这块私有区域内操作；
 *     这样既能在“真的物理页”上验证数据隔离，又不会破坏内核其它内存。
 *   - struct buddy_test_region { offset, length }：描述 arena 内部的一段「子区域」，
 *     offset 是相对 arena 首页的页偏移，length 是页数。用例可让分配器只登记 arena 的
 *     某几个子区域（而非整块 64 页），用来验证空洞、非连续区域等场景。
 *   - typedef int (*buddy_test_body)(struct Page *base, size_t length)：测试主体函数指针，
 *     由调用者提供，被测试钩子在一个“隔离好”的环境中调用；约定返回 0 成功、非 0 失败。
 */
#if defined(PMM_USE_BUDDY) && defined(ucore_test)
#define BUDDY_TEST_ARENA_PAGES 64

struct buddy_test_region {
    size_t offset;
    size_t length;
};

/* Return 0 on success, nonzero on failure; hooks restore the live context
 * before returning. The maximum-descriptor body must not touch page data. */
typedef int (*buddy_test_body)(struct Page *base, size_t length);
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：Buddy 测试框架对外接口 =================
 * 这 5 个函数是测试模块提供给内核（最终由 buddy_check 触发）的钩子：
 *   - buddy_test()：跑完 case1..case8 全部用例并汇总 PASS/FAIL；
 *   - buddy_test_with_regions()：在真实 64 页 arena 上只登记指定子区域后执行 body，
 *     无论 body 成败都会先恢复原分配器状态再返回；
 *   - buddy_test_with_max_descriptors()：用纯“页描述符数组”（不映射真实物理内存）
 *     构造 order-15（32768 页）场景，专门压测最高阶边界，body 内禁止访问页数据；
 *   - buddy_test_check_structure()：触发分配器内部的结构自检；
 *   - buddy_test_state_digest()：返回一个 64 位状态指纹，用于断言“失败请求不改变状态”。
 */
void buddy_test(void);
int buddy_test_with_regions(const struct buddy_test_region *regions, size_t count,
                            buddy_test_body body);
int buddy_test_with_max_descriptors(buddy_test_body body);
void buddy_test_check_structure(void);
uint64_t buddy_test_state_digest(void);
/* ================= 吴佳璐END ================= */
#endif /* PMM_USE_BUDDY && ucore_test */

#endif /* !__KERN_MM_BUDDY_TEST_H__ */
