#ifndef __KERN_MM_SLUB_TEST_H__
#define __KERN_MM_SLUB_TEST_H__

#ifdef ucore_test
/* SLUB 行为测试入口：在 slub_init() 之后调用一次。
 * 串行运行，假定此时不存在其他调用者持有的 SLUB 对象。
 * 仅测试构建（make test，-D ucore_test）可用；普通构建不编译、不运行。 */
void slub_check(void);
#endif /* ucore_test */

#endif /* !__KERN_MM_SLUB_TEST_H__ */
