#ifndef __KERN_MM_SLUB_TEST_H__
#define __KERN_MM_SLUB_TEST_H__

/* SLUB 行为测试入口：在 slub_init() 之后调用一次。
 * 串行运行，假定此时不存在其他调用者持有的 SLUB 对象。 */
void slub_check(void);

#endif /* !__KERN_MM_SLUB_TEST_H__ */
