#ifndef __KERN_MM_SLUB_H__
#define __KERN_MM_SLUB_H__

#include <defs.h>

/* 简化的 SLUB 对象分配器：底层复用现有页分配器，上层在页内切分小对象。
 *
 * 仅在 pmm_init()（页分配器与直接映射就绪）之后调用一次 slub_init()。
 * 单核串行使用，无每 CPU 缓存 / NUMA / 并发保证。 */

void slub_init(void);
void *slub_alloc(size_t size);
void slub_free(void *ptr);

#endif /* !__KERN_MM_SLUB_H__ */
