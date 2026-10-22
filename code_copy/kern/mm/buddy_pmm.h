#ifndef __KERN_MM_BUDDY_PMM_H__
#define __KERN_MM_BUDDY_PMM_H__

#include <pmm.h>

/* ================= 吴佳璐：Buddy 容量常量（BUDDY_MAX_ORDER / BUDDY_MAX_PAGES） =================
 * 这两个宏定义本 Buddy 分配器支持的「最大阶」与「最大页数」，是整个模块的容量天花板：
 *   - BUDDY_MAX_ORDER = 15 表示最高支持 order-15 的块；
 *   - order-k 块包含 2^k 个连续物理页，所以 order-15 块 = 2^15 = 32768 页；
 *   - 当前 DRAM 窗口是 128 MiB，按 4 KiB/页正好 32768 页，因此 15 阶刚好覆盖整窗，
 *     既不会限制真实可管理内存，也不会让 meta/seen 数组无谓变大。
 * 注意 BUDDY_MAX_PAGES 用 (size_t)1 << BUDDY_MAX_ORDER 计算，避免 int 溢出；
 * 以它为界的数组（buddy.meta / buddy.seen）都是编译期常量大小，全程使用静态存储。
 */
/* Orders 0..15 cover 1..32768 pages, the current 128 MiB DRAM window. */
#define BUDDY_MAX_ORDER 15
#define BUDDY_MAX_PAGES ((size_t)1 << BUDDY_MAX_ORDER)
/* ================= 吴佳璐END ================= */

/* ================= 吴佳璐：对外暴露的 Buddy 物理内存管理器 =================
 * 声明 buddy_pmm_manager 这个 struct pmm_manager 实例，供内核启动时把它注册到全局
 * pmm_manager 指针上。它把 buddy_init / buddy_init_memmap / buddy_alloc_pages /
 * buddy_free_pages / buddy_nr_free_pages / buddy_check 六个函数指针汇总成一套标准页分配接口，
 * 让上层「只认 pmm_manager 接口、不认具体算法」。这里只做 extern 声明，
 * 真正的实例定义在 buddy_pmm.c 文件末尾。
 */
extern const struct pmm_manager buddy_pmm_manager;
/* ================= 吴佳璐END ================= */

#endif /* !__KERN_MM_BUDDY_PMM_H__ */
