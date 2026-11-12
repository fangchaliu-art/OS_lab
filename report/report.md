# 操作系统实验报告

## 实验基本信息

| 项目         | 内容                                           |
| ------------ | ---------------------------------------------- |
| **实验名称** | Lab4：进程管理                                 |
| **小组成员** | 2412084-刘芳希；2410826-马伊汀；2410439-吴佳璐 |
| **完成日期** | 2026-11-                                       |




### 小组分工

| 成员                | 负责的练习/模块                                                                                                                                       | 报告分工                                                                                                                          |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------- |
| 2412084-刘芳希（A） | 使用实验二、三的提示词重新生成并适配基础代码；完成模块一：虚拟内存页操作（`page_insert`、`page_remove`）；完成 Challenge 2：分页模式与 `get_pte` 分析 | 负责报告前半部分（实验基本信息、小组分工、第一至三部分），以及本人模块和 Challenge 2 的功能说明、spec、prompt、实现迭代与问题分析 |
| 2410439-吴佳璐（B） | 完成模块二：进程创建与初始化（`alloc_proc`、`proc_init`、`kernel_thread`、`do_fork`、`copy_thread`、`copy_mm`）                                       | 负责本人模块的功能说明、spec、prompt、实现迭代与问题分析                                                                          |
| 2410826-马伊汀（C） | 完成模块三：进程调度与上下文切换（`cpu_idle`、`schedule`、`proc_run`、`switch_to`）；完成 Challenge 1：中断保存与恢复分析；负责最终联调与测试         | 负责本人模块和 Challenge 1 的功能说明、spec、prompt、实现迭代与问题分析，以及第五部分测试与验证、第六部分实验总结与收获           |


---

## 一、实验目的

本实验的主要目的是：

1. 在物理内存管理和基础页表机制之上，掌握虚拟地址与物理页之间映射的建立、更新和撤销，理解页表项、物理页引用计数与 TLB 一致性的关系。
2. 理解虚拟内存管理结构 `mm_struct` 和虚拟内存区域结构 `vma_struct` 的作用，认识地址空间描述与底层页表映射之间的联系。
3. 掌握内核线程的创建与初始化过程，理解进程控制块、内核栈、陷阱帧和上下文的作用，以及 `idleproc`、`initproc` 的建立方式。
4. 理解从选择可运行线程到切换页表、保存和恢复寄存器的完整调度过程，使内核能够将 CPU 从空闲线程交给新建的内核线程。
5. 分析中断状态的保存与恢复，以及 Sv32、Sv39、Sv48 分页模式的异同，理解临界区保护和多级页表设计的原理。
6. 实践基于 spec 和 prompt 的 AI 辅助开发：复用并适配此前实验的提示词，明确本次模块的接口与约束，通过代码审查、编译和运行测试验证生成结果，并记录问题与迭代过程。

---

## 二、实验环境

| 成员           | AI 编程工具             | 底层模型            | 备注 |
| -------------- | ----------------------- | ------------------- | ---- |
| 2412084-刘芳希 | Claude Code for VS Code | DeepSeek-V4-Flash   |
| 2410826-马伊汀 | Claude Code for VS Code | DeepSeek-V4-Flash   |
| 2410439-吴佳璐 | Claude Code for VS Code | DeepSeek-V4.1-Flash |

---

---

## 三、实验整体逻辑分析

### 3.1 本章节的逻辑主线

本实验围绕“虚拟内存页操作 → 进程创建与初始化 → 进程调度与上下文切换”展开。前面的实验已经提供物理页分配、基础页表和中断处理能力，本实验进一步解决如何在内核中创建多个执行实体，以及如何让其中一个执行实体真正获得 CPU 并开始运行的问题。

内存管理为线程运行提供基础：物理页分配器提供内核栈和页表所需的内存，页映射接口维护虚拟地址与物理页的对应关系，虚拟内存管理结构描述地址空间布局。在此基础上，进程创建模块将控制块、内核栈、PID、运行状态和首次执行现场组织为一个可被调度的内核线程；调度模块再选择可运行线程，切换当前线程、页表及寄存器上下文，使线程从“已经创建”进入“实际执行”。

本实验使用进程管理的数据结构和接口来管理内核线程。当前阶段的 `copy_mm` 保留地址空间处理接口，不进行真正的用户地址空间复制；线程创建与切换是本次实现的重点。

### 3.2 功能的逐步实现

1. **恢复并适配前序实验的基础能力。** 使用实验二、三中设计的提示词重新生成物理内存初始化、物理页分配和中断处理等代码，填写本次框架中标有 `lab2`、`lab3` 的位置，并检查其与实验四接口的兼容性。这些基础代码决定后续能否正确分配内存、处理陷阱并恢复执行现场。
2. **完善虚拟内存页操作。** 实现 `page_insert` 和 `page_remove`，在现有 `get_pte` 基础上建立、替换或撤销映射，同时维护引用计数并使对应 TLB 缓存失效。只有保证映射和物理页生命周期一致，后续内存使用才有可靠基础；框架中的 `vmm_init` 则负责虚拟内存管理相关初始化与检查。
3. **创建并登记内核线程。** `alloc_proc` 初始化进程控制块，`proc_init` 将当前执行现场组织为 `idleproc`，并通过 `kernel_thread`、`do_fork` 创建 `initproc`。创建过程中分配内核栈、设置父子关系和 PID、登记线程并使其进入可运行状态；`copy_thread` 准备陷阱帧和上下文，`copy_mm` 保留本阶段的地址空间处理接口。此时线程已经具备运行所需的信息，但仍需调度模块将其交给 CPU。
4. **选择线程并完成上下文切换。** 内核初始化后进入 `cpu_idle`，在需要重新调度时调用 `schedule`，从进程链表中选择可运行线程；没有合适线程时回退到 `idleproc`。`proc_run` 在中断保护的临界区内更新 `current`、切换页表，并调用 `switch_to` 保存旧线程、恢复新线程的寄存器上下文。新线程首次运行时，通过 `forkret → forkrets → __trapret` 恢复预先准备的陷阱帧，再进入线程入口执行。
5. **沿执行链联调并分析原理。** 按照 `kern_init → pmm_init → pic_init / idt_init → vmm_init → proc_init → cpu_idle → schedule → proc_run → switch_to` 检查各阶段的衔接，重点验证内存分配与映射、线程初始化、调度选择及首次运行现场。两个 Challenge 分别分析中断保护和多级页表，可与对应模块同步开展；最终运行结果和测试证据在第五部分记录。

---

## 四、实验内容与实现

<!-- 
说明：本部分按照 exercises 文档中的练习顺序组织
每个功能模块或者练习中可能包含多项任务，请根据实际情况调整
-->

### 4.1 前置任务：复用此前提示词生成并适配基础代码

**负责人：** 2412084-刘芳希

#### 模块功能描述与 spec

本任务使用实验二、三中设计的提示词，重新生成物理内存管理和陷阱处理的基础代码，并将其适配到实验四中标有 `lab2`、`lab3` 的位置，为后续页映射操作、线程创建和上下文切换提供基础。

涉及文件为 `kern/mm/default_pmm.c`、`kern/mm/pmm.c`、`kern/trap/trap.c` 和 `kern/trap/trapentry.S`；使用 `best_fit_pmm_manager` 命名时，还需通过 `kern/mm/best_fit_pmm.h` 声明管理器，并与 `pmm.c` 中选择的管理器保持一致。主要函数为：

```c
// kern/mm/default_pmm.c：Best Fit 物理页分配器
static void best_fit_init(void);
static void best_fit_init_memmap(struct Page *base, size_t n);
static struct Page *best_fit_alloc_pages(size_t n);
static void best_fit_free_pages(struct Page *base, size_t n);
static size_t best_fit_nr_free_pages(void);
static void best_fit_insert_block(struct Page *base); // 内部辅助函数

// kern/mm/pmm.c：物理内存探测与页描述符初始化
static void page_init(void);

// kern/trap/trap.c：中断和异常处理
void interrupt_handler(struct trapframe *tf);
void exception_handler(struct trapframe *tf);
```

汇编部分需要补全 `RESTORE_ALL` 宏，以及 `__alltraps` 中向 `trap()` 传递陷阱帧并调用该函数的代码。各部分的 spec 如下。

**Best Fit 要求（`default_pmm.c`）：**

- `best_fit_init` 初始化空闲块链表，并将空闲页总数 `nr_free` 置为 0。
- 空闲块链表按地址升序排列，每个空闲块仅在首页设置 `PG_property`，首页的 `property` 记录该块的连续页数。
- `best_fit_init_memmap` 接收待登记的连续页范围，检查页面原为保留状态，重置页面标志、块大小和引用计数，再将整块内存登记为空闲块。
- `best_fit_alloc_pages` 要求请求页数大于 0，在所有容量不小于请求的空闲块中选择容量最小的块；容量相同时选择地址较低的块。
- 大块分配后拆出剩余部分并保持链表的地址顺序，清除已分配块首页的空闲块标志；空闲页总数不足或不存在足够大的连续块时返回 `NULL`，失败时不改变链表和计数。
- `best_fit_free_pages` 重置待释放页的状态与引用计数，按地址插入空闲链表，并合并前后物理相邻的空闲块，清除被合并块原首页的块标志和块大小。
- 新登记或释放页时增加 `nr_free`，成功分配时减少 `nr_free`，合并操作不重复增加页数；`best_fit_nr_free_pages` 返回当前空闲页总数。
- 保留 `pmm.c` 中 `alloc_pages`、`free_pages`、`nr_free_pages` 接口的中断状态保护，退出临界区时恢复进入前的中断状态。
- 管理器结构体正确关联上述接口及 `best_fit_check`，不修改框架提供的 `basic_check`、`best_fit_check`，也不使用无法随测试一起保存和恢复的隐藏链表缓存。

**物理内存初始化要求（`pmm.c` 的 `page_init`）：**

- 通过 `get_memory_base()`、`get_memory_size()` 获取设备树解析出的内存信息，检查空范围、地址溢出及内核是否处于有效内存范围内。
- 使用与启动页表一致的 `PHYSICAL_MEMORY_OFFSET` 设置 `va_pa_offset`，保持 `PADDR`、`KADDR` 与实际内核映射一致。
- 将设备树报告的内存上界限制在内核允许管理的物理范围内，并向下按页对齐；`npage` 表示管理范围物理页号的上界，`pages[0]` 对应物理页号 `nbase`。
- 在内核结束地址 `end` 之后按页对齐放置 `struct Page` 数组，依据 `npage - nbase` 计算描述符数量，检查数组大小和地址边界，防止越界或整数溢出。
- 清零页描述符数组并将各页初始标记为保留状态，避免将固件、内核镜像及页描述符数组占用的页加入空闲链表。
- 将页描述符数组之后的第一个完整物理页作为空闲范围起点，仅在存在可用完整页时调用 `init_memmap`，登记正确的起始页和页数。
- 保留实验四已有的 `pmm_init`、`get_pte`、`get_page`、页表检查与 `kmalloc_init` 等逻辑；本节只补充旧实验的初始化能力，`page_insert`、`page_remove` 属于后续模块一。

**中断处理要求（`trap.c` 的 `interrupt_handler`）：**

- 从 `tf->cause` 中去掉 RV64 `scause` 的最高位，得到中断原因码，并按原因码分派处理。
- 对 S 模式时钟中断，先调用 `clock_set_next_event()` 设置下一次时钟事件，再递增 `ticks`，保证时钟中断能够持续产生。
- 每累计 `TICK_NUM` 次时钟中断调用一次 `print_ticks()`，不修改异步中断发生位置对应的 `tf->epc`。
- 对其他已知中断输出对应类型；对未知中断输出原因码并打印陷阱帧，便于定位问题。
- 若按旧提示词保留累计输出后关机的功能，应明确 `SHUTDOWN_AFTER` 的定义、计数方式和 `sbi_shutdown()` 的声明；实验四持续运行与联调时，应按运行需求禁用或隔离该逻辑，避免定时关机中断线程执行和测试。

**异常处理要求（`trap.c` 的 `exception_handler`）：**

- 根据 `tf->cause` 区分异常类型，对非法指令和断点异常输出异常种类及发生地址。
- 对本实验使用的 2 字节压缩指令和 4 字节普通指令，读取指令低 16 位判断长度，正确增加 `tf->epc`，使 `sret` 返回后能够跳过触发指令；指令读取以异常地址可访问为前提。
- 地址输出使用能够容纳 RV64 地址的格式，避免只显示低 32 位而影响调试。
- 对其他异常打印完整陷阱帧；该基础处理不等同于完成缺页修复，不能对所有异常统一增加 `epc` 后继续运行。无法恢复的异常需要明确诊断，避免无提示地反复陷入。
- 保留实验四已有的 `idt_init`、打印函数和 `trap` 分派逻辑，尤其保留 `SSTATUS_SUM` 设置，不重复定义已有函数。

**陷阱恢复与调用要求（`trapentry.S`）：**

- `RESTORE_ALL` 的槽位布局与 `SAVE_ALL`、`struct trapframe` 一致，陷阱帧大小为 `36 * REGBYTES`，不能自行改变字段顺序。
- 从第 32、33 个寄存器宽度偏移槽位恢复 `sstatus`、`sepc`，再恢复通用寄存器 `x1`、`x3` 至 `x31`；`x0` 为常量零寄存器，无需恢复。
- 恢复寄存器时保持 `sp` 指向陷阱帧，最后从第 2 个寄存器宽度偏移槽位恢复原 `sp`，避免提前丢失陷阱帧地址。
- 在 `__alltraps` 中完成 `SAVE_ALL` 后，执行 `mv a0, sp`，按调用约定将陷阱帧地址作为第一个参数传给 `trap()`，再通过 `jal trap` 调用处理函数；返回时 `sp` 应仍指向该陷阱帧。
- 保留 `__trapret` 中的 `RESTORE_ALL` 和 `sret`，以及实验四新增的 `forkrets`。`forkrets` 将传入的陷阱帧地址设置为 `sp` 后跳转至 `__trapret`，为新线程首次运行复用同一恢复路径。

#### 实现迭代过程

首次使用此前的提示词生成代码后，通过代码审查发现了以下实验场景适配问题，并据此提出调整方案。

---

##### 第一次迭代

**遇到的问题：**

生成的 `interrupt_handler()` 保留了实验三中“累计 300 次时钟中断后自动关机”的逻辑：每累计 `TICK_NUM = 100` 次时钟中断调用一次 `print_ticks()`，当累计输出次数达到 `SHUTDOWN_AFTER = 3` 时调用 `sbi_shutdown()`。这段代码符合旧提示词的要求，但实验四需要继续执行内核线程并进行调度与联调，自动关机可能提前中断运行，影响观察和测试。

原因在于复用旧提示词时，没有明确区分实验三的阶段性结束条件与实验四的持续运行需求，AI 因而保留了旧实验的关机行为。

**问题解决策略：**

在提示词的 spec 中补充实验四的运行约束：保留时钟中断重设、`ticks` 累加以及每 100 次中断输出统计信息的行为，删除累计输出三次后自动关机的逻辑，并同步删除 `SHUTDOWN_AFTER`、`print_count` 及相关过时注释。时钟中断处理函数不得因达到固定计数而主动关闭系统。

可追加的提示词约束为：

> 本次代码用于 Lab4 内核线程管理。请保留 `clock_set_next_event()`、`ticks++` 和每 `TICK_NUM` 次中断调用 `print_ticks()` 的逻辑，移除旧实验中通过 `print_count`、`SHUTDOWN_AFTER` 调用 `sbi_shutdown()` 的定时关机功能，并更新对应注释；不要修改其他中断和异常处理逻辑。

**最终结果：**

补充上述约束后，使用修改后的提示词重新生成代码。生成结果移除了累计 300 次时钟中断后自动关机的逻辑，保留了时钟事件重设、`ticks` 累加和周期性统计输出，并同步更新了相关注释。修改后的代码通过了前置任务相关测试，完成了实验四中标有 `lab2`、`lab3` 位置的基础代码补充，为后续虚拟内存页操作、内核线程创建和调度功能的实现提供了基础。

---


### 功能模块一：虚拟内存页操作

**负责人：** 2412084-刘芳希

#### 模块功能描述

**需要实现/修改的函数：**

涉及文件为 `kern/mm/pmm.c`，在 `//LAB4 function1 YOUR CODE` 对应位置实现以下两个接口：

```c
int page_insert(pde_t *pgdir, struct Page *page,
                uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

**功能说明：**

本模块在现有 Sv39 多级页表框架上，提供虚拟地址与物理页之间映射的建立、更新和撤销功能。`page_insert` 将指定虚拟地址所在的页映射到目标物理页，并设置访问权限；`page_remove` 撤销指定虚拟地址所在页的映射。两者需要同步维护页表项、物理页引用计数和 TLB 状态，为页表自检及后续地址空间相关操作提供基础。

`page_insert` 通过 `get_pte(pgdir, la, 1)` 查找目标页表项，必要时由现有接口分配中间级页表。需要分别处理目标地址尚未映射、已映射到同一物理页和已映射到其他物理页三种情况。建立新映射时增加目标页的引用计数；替换映射时撤销旧页的引用，并在旧页引用计数降为 0 时释放旧页；同一地址重复映射到同一物理页时，只更新权限，引用计数应保持不变，也不能误释放该页。成功返回 0，无法取得所需页表项时返回 `-E_NO_MEM`，且不能破坏已有映射或错误增加目标页的引用计数。

`page_remove` 通过 `get_pte(pgdir, la, 0)` 查找已有页表项，不为删除操作分配新的页表。若目标页表项不存在或无有效映射，直接返回；若存在有效映射，则减少对应物理页的引用计数，在计数降为 0 时调用 `free_page()` 释放物理页，并清空对应页表项。重复删除同一地址应保持安全，不重复释放物理页，也不影响其他地址的映射。

**主要要求（spec）：**

- 页表项、引用计数和 TLB 状态保持一致，映射建立、替换、权限更新和撤销后，调用现有 `tlb_invalidate()` 使对应地址的旧翻译失效。
- 同一物理页允许被多个虚拟地址映射；每新增一处映射增加一次引用，删除其中一处映射时不能释放仍被其他映射引用的物理页。
- 同一虚拟地址重新映射到同一物理页时，最终引用计数不变；新的权限应反映调用者本次传入的 `perm`，不能残留旧映射的权限。
- 通过现有 `pte_create()` 构造有效页表项，正确使用物理页号和权限位，避免将内核虚拟地址直接作为页表项中的物理地址。
- 对未映射地址的删除是幂等操作；对页表分配失败返回明确错误，不继续解引用空页表项，也不留下有效的半完成目标映射。
- 可根据需要增加内部辅助函数以复用撤销映射的逻辑，对外函数签名保持与 `pmm.h` 一致。
- 复用现有的 `get_pte()`、`pte2page()`、引用计数接口、`free_page()` 和 TLB 失效接口，保留老师提供的页表框架及检查代码。

本模块与前置任务的物理页分配器相衔接：中间页表分配依赖 `alloc_page()`，映射撤销后的物理页回收依赖 `free_page()`；`get_pte()` 负责定位页表项，本模块负责维护具体映射的生命周期。验证时重点检查新建映射、重复映射、替换物理页、权限变化、共享物理页及重复删除等场景，并结合框架中的 `check_pgdir()`、`check_boot_pgdir()` 和模块测试进行确认。

#### 最终提示词

以下是经过迭代优化后，最终成功实现该功能的提示词：

````markdown

## [PROMPT]

**任务**：在当前 Lab4 项目中修改 `code/kern/mm/pmm.c`（相对于代码目录为 `kern/mm/pmm.c`），实现 `page_insert()` 和 `page_remove()`，完成虚拟地址到物理页映射的建立、更新和撤销，并保持页表项、物理页引用计数与 TLB 状态一致。

**操作要求**：必须修改项目中的实际文件，不能只输出代码片段。先阅读现有实现和声明，只在 `//LAB4 function1 YOUR CODE` 对应区域补充这两个函数及必要的 `static` 辅助函数。保留 `LAB4 function1` 标记，将其中的 `YOUR CODE` 替换为学号 `2412084`。不修改现有页表框架、函数声明、测试代码及其他模块，不引入无关功能。

**输出要求**：以工程真实代码和下面三个部分为准完成修改，简要说明修改内容及验证结果。如果当前项目因其他 Lab4 模块尚未完成而无法完整编译或运行，应说明实际阻塞原因，不补写其他模块，也不声称测试已经通过。

## [RELY]

代码运行于 RV64/Sv39 环境，相关声明位于 `kern/mm/pmm.h`，类型和宏定义位于 `kern/mm/memlayout.h`、`libs/riscv.h` 等现有头文件。使用前以实际代码核对，不重新定义这些接口。

**类型与状态：**

- `pde_t`、`pte_t` 为工程提供的页表项类型。
- `struct Page` 描述物理页，其中 `ref` 为引用计数；`pages[0]` 对应物理页号 `nbase`，不能直接将描述符数组下标作为物理页号。
- `boot_pgdir_va` 为启动页表的内核虚拟地址，已有页表检查代码使用该页表验证映射操作。
- 权限位包括 `PTE_V`、`PTE_U`、`PTE_R`、`PTE_W` 等，`PTE_V` 表示页表项有效；`PGSIZE` 为页大小。
- `E_NO_MEM` 由 `error.h` 提供，内存不足时返回 `-E_NO_MEM`。

**可用接口：**

```c
pte_t *get_pte(pde_t *pgdir, uintptr_t la, bool create);
pte_t pte_create(uintptr_t ppn, int type);
ppn_t page2ppn(struct Page *page);
struct Page *pte2page(pte_t pte);
int page_ref(struct Page *page);
int page_ref_inc(struct Page *page);
int page_ref_dec(struct Page *page);
void set_page_ref(struct Page *page, int val);
void tlb_invalidate(pde_t *pgdir, uintptr_t la);
```

- `get_pte(..., 1)` 查找目标页表项，必要时分配中间级页表，失败返回 `NULL`；`get_pte(..., 0)` 只查找，不分配。
- `pte_create()` 接收物理页号和权限，现有实现自动设置 `PTE_V`；`page2ppn()` 将页描述符转换为正确的物理页号。
- 仅对有效映射调用 `pte2page()`；`page_ref_inc()`、`page_ref_dec()` 返回更新后的引用计数。
- `alloc_page()`、`free_page(page)` 为已有单页分配、释放宏；映射目标物理页由调用者提供，中间页表分配由 `get_pte()` 负责。
- `tlb_invalidate()` 使指定地址的翻译失效，工程也提供 `flush_tlb()`；复用现有接口，不自行重写 TLB 操作。

## [GUARANTEE]

必须实现以下两个公开接口，签名与 `pmm.h` 一致：

```c
int page_insert(pde_t *pgdir, struct Page *page,
                uintptr_t la, uint32_t perm);
void page_remove(pde_t *pgdir, uintptr_t la);
```

允许增加本文件内部使用的 `static` 辅助函数，以复用撤销映射等逻辑；不增加新的公开接口。

## [SPECIFICATION]

### page_insert

**Pre-Condition：**

- 物理内存管理已经初始化，`pgdir` 指向可用的页表，`page` 是调用者提供的有效、已分配物理页描述符。
- `la` 是待映射地址，不要求页对齐；操作对象为其所在的虚拟页。`perm` 是调用者要求的权限，需兼容现有测试中的 `0`、`PTE_U | PTE_W` 等输入。

**Post-Condition：**

- 成功返回 `0`，`la` 所在页有效映射到 `page`，权限更新为本次请求的状态，引用计数与实际映射关系一致，对应 TLB 旧翻译失效。
- 无法取得所需页表项时返回 `-E_NO_MEM`，不破坏原有映射，不错误增加目标物理页的引用计数，不留下有效的半完成目标映射。

**Case 1：目标地址原来没有有效映射。** 建立到 `page` 的映射，目标物理页引用计数增加 1，并设置调用者要求的权限。

**Case 2：目标地址已经映射到同一物理页。** 更新权限，最终引用计数保持不变，不能因撤销旧映射而误释放该物理页。

**Case 3：目标地址已经映射到其他物理页。** 撤销旧映射对旧页的引用，旧页引用计数降为 0 时释放旧页；建立新映射后，目标页引用计数增加 1，旧权限不能残留。

**Case 4：所需中间页表无法分配。** 返回 `-E_NO_MEM`，不继续访问空页表项，不安装目标映射，也不释放调用者传入的物理页。

**Requirements：**

- 构造页表项时使用物理页号，不能将 `struct Page *` 或内核虚拟地址直接写入页表项。
- 正确处理同一物理页被多个地址共享的情况，不通过重置引用计数破坏其他映射。
- 映射建立、替换和权限更新完成后，保证对应 TLB 翻译失效；不能仅覆盖旧页表项而忽略引用计数和 TLB。

### page_remove

**Pre-Condition：**

- `pgdir` 指向可用页表，`la` 是待删除地址，可以不是页对齐地址，也可以没有有效映射。

**Post-Condition：**

- 删除后，`la` 所在页无有效映射；相关物理页引用计数和释放状态正确，其他地址的映射不受影响。
- 该接口无返回值，重复删除同一地址应安全且幂等。

**Case 1：对应页表不存在，或目标页表项无有效映射。** 安静返回，不分配新页表，不减少引用计数，不释放物理页。

**Case 2：存在有效映射，物理页仍被其他映射引用。** 撤销当前映射，使引用计数减少 1，但不释放该物理页；清空目标页表项并使对应 TLB 翻译失效。

**Case 3：存在有效映射，删除后物理页引用计数为 0。** 清除当前映射，释放该物理页，并保证页表和 TLB 不再保留对它的有效映射。

**Requirements：**

- 删除操作只查找已有页表项，不能为了删除而创建中间级页表。
- 仅在物理页引用计数降为 0 时释放它，不重复释放；不将目标数据页的引用计数与中间级页表页的引用计数混淆。
- 本接口不承担回收整棵页表的任务，保留现有中间级页表及其管理逻辑，兼容框架后续检查和清理。

### 验证要求

检查新建映射、同页重复映射及权限变化、替换物理页、多地址共享物理页、删除不存在的映射、重复删除和页表分配失败等路径。优先使用现有 `check_pgdir()`、`check_boot_pgdir()` 及模块测试，不改动测试以迁就实现，并如实说明已执行与尚未执行的验证。

````


### Challenge 2：深入理解不同分页模式的工作原理

**负责人：** 2412084-刘芳希

#### 1. `get_pte()` 中两段代码为什么相像？

Sv32、Sv39 和 Sv48 都采用多级页表，将虚拟页号拆成多个索引，逐级定位页表项。对于普通的 4 KiB 页面，三者的页内偏移均为 12 位，主要区别如下：

| 分页模式 | 适用架构 | 虚拟地址划分（从高位索引到页内偏移） | 页表级数 | 每个页表项大小 | 每页页表项数量 |
| -------- | -------- | ------------------------------------ | -------- | -------------- | -------------- |
| Sv32     | RV32     | 10 + 10 + 12 位                      | 2 级     | 4 字节         | 1024           |
| Sv39     | RV64     | 9 + 9 + 9 + 12 位                    | 3 级     | 8 字节         | 512            |
| Sv48     | RV64     | 9 + 9 + 9 + 9 + 12 位                | 4 级     | 8 字节         | 512            |

Sv39 和 Sv48 的虚拟地址仍由 64 位寄存器保存，上表只列参与地址划分的低 39 位或低 48 位，其余高位必须分别等于第 38 位或第 47 位。这些模式也支持在较高层级使用叶子项映射大页，因此并非所有映射都必须走到最底层。

本实验的 `get_pte()` 按 Sv39 的三级页表、4 KiB 页面路径查找。代码中的两段相似逻辑分别用于保证两个下一级页表存在：

1. 第一段通过 `PDX1(la)` 在根页表中找到 `pdep1`。若该项无效且允许创建，就分配一个物理页作为中间页表，设置引用计数、清零页表页，并将其物理页号写入 `pdep1`。
2. 第二段沿 `pdep1` 指向的中间页表，通过 `PDX0(la)` 找到 `pdep0`。若该项无效，同样根据 `create` 决定是否分配并初始化下一级页表页，然后更新 `pdep0`。
3. 最后通过 `PTX(la)` 定位最底层页表项，并返回其地址，由 `page_insert()` 等调用者设置具体的物理页映射。

两段代码相像，是因为多级页表各层采用相同的结构，每向下一层都需要重复“检查页表项是否有效 → 必要时分配并清零下一级页表 → 建立指向下一级页表的链接”的操作，区别只在所处层级和使用的索引。`PDE_ADDR()` 用于取出下一级页表的物理地址，`KADDR()` 将其转换为内核可访问的虚拟地址。

在根页表已存在、使用普通 4 KiB 页面且沿用这一写法的前提下，Sv32 只需要一次这样的下钻，Sv39 需要两次，Sv48 则需要三次。因此可以将重复步骤抽成辅助函数，或根据分页级数用循环实现。需要注意，当前函数只检查中间项的 `PTE_V`，未区分中间层的大页叶子项，不能直接当作支持所有大页情况的通用页表遍历函数。

另外，`get_pte()` 创建的是保存页表项的页表页，不是最终映射的数据页；返回非空指针也只说明找到了页表项的位置，并不代表该项已经包含有效映射。

#### 2. 是否应该将查找和页表页分配拆开？

我认为当前写法适合本实验，没有必须拆开的必要，但在功能更复杂的内核中，可以将对外接口区分得更清楚，同时复用内部遍历逻辑。

当前函数通过 `create` 参数控制行为：`create = 0` 时只查找，缺少中间页表就返回 `NULL`；`create = 1` 时允许按需分配缺失的页表页。这让查找与创建共用一套地址索引和遍历代码，减少重复实现。比如 `page_remove()` 使用 `get_pte(pgdir, la, 0)`，删除不存在的映射时不会分配页表；`page_insert()` 使用 `get_pte(pgdir, la, 1)`，能够在建立映射前补齐所需页表。

这种设计的不足是函数名不能直接体现是否会修改页表，调用者必须注意 `create` 参数。开启创建后，函数还可能消耗物理页并因内存不足失败，错误处理比纯查找复杂。例如，当前代码在第一层成功分配页表、第二层分配失败时会返回 `NULL`，但已经挂入页表树的前一层页表页会保留，不会自动回滚。因此，失败不一定表示页表结构完全没有变化，虽然此时尚未建立目标数据页的映射。

如果需要改进，可以对外提供只读查找接口 `lookup_pte()` 和允许创建的接口 `ensure_pte()`，内部仍共用同一个页表遍历辅助函数，避免复制两套逻辑。创建接口还应明确分配失败时是保留已建的空页表，还是回收本次新分配的页表页；并发环境下也需要统一考虑页表修改的同步保护。这些问题不会仅靠拆分函数自动解决。

因此，本实验保留 `get_pte(pgdir, la, create)`，明确参数语义并正确处理 `NULL` 即可；若后续需要支持更多分页模式、大页或并发地址空间管理，再根据维护需求拆分接口并完善遍历与失败处理。

---

### 功能模块二：进程创建与初始化

**负责人：** 2410439-吴佳璐

#### 模块功能描述

**需要实现/修改的函数：**

涉及文件为 `kern/process/proc.c`，在 `//LAB4 function2 YOUR CODE` 对应位置实现以下函数：

```c
static struct proc_struct *alloc_proc(void);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
static void copy_thread(struct proc_struct *proc, uintptr_t esp,
                        struct trapframe *tf);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
void proc_init(void);
```

**功能说明：**

本模块在物理内存管理和虚拟内存管理之上，建立内核线程的管理结构。进程控制块、内核栈、PID、父子关系、首次执行现场和可运行状态在此组织中完成，使线程从“被创建”成为调度模块可以直接选择的实体。

`alloc_proc()` 使用 `kmalloc()` 申请进程控制块，并把 `state` 置为 `PROC_UNINIT`、`pid` 置为 `-1`，其余运行期字段清零，`context` 整体清零，`pgdir` 设置为启动页表的物理基址 `boot_pgdir_pa`。此时控制块尚未分配 PID 和内核栈，也不进入任何链表，因此不会被调度器看到。

`proc_init()` 负责建立进程管理的初始环境。它先初始化 `proc_list` 与全部 `hash_list` 桶，然后把当前启动执行现场组织为 `idleproc`：PID 为 0、状态为 `PROC_RUNNABLE`、内核栈为 `bootstack`、`need_resched` 置 1、名字为 `"idle"`，并把它设为 `current`、计入 `nr_process`。随后通过 `kernel_thread(init_main, "Hello world!!", 0)` 创建 init 线程，经 `find_proc()` 取回控制块后命名为 `"init"`。`idleproc` 不挂入进程链表，只作为当前执行现场和调度器无可用线程时的回退。

`kernel_thread()` 构造一个临时陷阱帧作为新线程的首次执行现场：`gpr.s0` 保存线程函数 `fn`，`gpr.s1` 保存参数 `arg`，`epc` 指向 `kernel_thread_entry`，`status` 在保留当前 `sstatus` 的基础上置位 `SPP`、`SPIE` 并清除 `SIE`，保证 `sret` 返回 S 模式并重新开启中断。随后以 `clone_flags | CLONE_VM` 调用 `do_fork()`，由后者完成实际创建。

`do_fork()` 依次检查进程数上限、申请控制块、设置父进程、分配内核栈、调用 `copy_mm()` 和 `copy_thread()`。在中断保护下完成 PID 分配、散列桶插入、进程链表插入和 `nr_process` 递增，退出临界区后调用 `wakeup_proc()` 使新线程进入 `PROC_RUNNABLE`，最后返回 PID。任一中间步骤失败都按相反顺序回收已分配资源，不留下半初始化线程。

`copy_thread()` 把调用者提供的陷阱帧复制到新线程内核栈顶部，设置 `gpr.a0 = 0` 表示子线程的返回值，并根据 `esp` 是否为 0 选择内核栈顶或调用者提供的栈指针；同时设置 `context.ra = forkret`、`context.sp = proc->tf`，使新线程第一次被切换时从 `forkret → forkrets → __trapret` 路径恢复陷阱帧并跳到线程入口。

`copy_mm()` 在本阶段不进行真正的地址空间复制。当前没有用户进程，`current->mm` 为 `NULL`，该函数断言这一前提后直接返回 0，仅作为后续实验地址空间复制的接口保留。

**主要要求（spec）：**

- `alloc_proc()` 只负责申请和初始化控制块，失败返回 `NULL` 且不产生其他副作用；不得在其中分配 PID、内核栈、插入链表或唤醒线程。
- `alloc_proc()` 必须清零 `context`，使 `idleproc` 的 `context.ra` 保持 0；`pgdir` 保存物理基址 `boot_pgdir_pa`，与后续 `proc_run()` 把 `proc->pgdir` 按 Sv39 格式直接编码进 `satp` 的约定一致。
- `proc_init()` 必须先初始化 `proc_list` 和所有散列桶，再创建线程；`idleproc` 分配失败或 init 线程创建失败时必须 `panic`，不得继续使用未初始化的控制块。
- `idleproc` 的 PID、状态、内核栈、`need_resched`、名字和 `current` 必须满足现有模块测试的断言；`initproc` 的 PID 为 1、父进程为 `idleproc`，并且已经进入 `PROC_RUNNABLE`。
- `kernel_thread()` 必须附加 `CLONE_VM`；`sstatus` 的设置需保证 `sret` 返回 S 模式并重新开启中断；陷阱帧是栈上临时对象，其内容由 `copy_thread()` 复制，不能长期保存其地址。
- `do_fork()` 中 PID 分配、`hash_proc()`、`list_add()` 和 `nr_process++` 必须位于同一个中断保护临界区内，`wakeup_proc()` 放在临界区之后；失败路径按“内核栈 → 控制块”逆序释放，不残留链表节点或 PID 占用。
- `do_fork()` 只创建并唤醒线程，不调用 `schedule()`、不切换 `current`；线程真正运行由功能模块三负责。
- `copy_thread()` 的陷阱帧使用 `(struct trapframe *)(proc->kstack + KSTACKSIZE) - 1` 定位；只设置 `context.ra` 与 `context.sp`，其余上下文寄存器保持清零状态。
- 复用现有的 `get_pid()`、`hash_proc()`、`setup_kstack()`、`put_kstack()`、`forkret`、`forkrets`、`wakeup_proc()`、`kmalloc()`、`kfree()` 等接口，不修改 `proc.h`、`sched.h` 及其他模块代码。

本模块与前置模块的衔接体现在资源来源上：控制块由 `kmalloc()` 分配，内核栈由 `setup_kstack()` 通过 `alloc_pages()` 分配，映射与页表操作由功能模块一保证；而 `do_fork()` 唤醒的新线程能否真正运行，取决于功能模块三的 `schedule()`、`proc_run()` 与 `switch_to`。因此本模块目前只进行目标文件编译和静态断言核对，运行阶段验证在联调时完成。

#### 实现迭代过程

##### 第一次迭代：`pgdir` 字段保存虚拟地址还是物理地址

**遇到的问题：**

初版模块描述没有明确 `alloc_proc()` 中 `pgdir` 应保存页表的内核虚拟地址还是物理地址。按 x86 版本 ucore 的写法，容易直接填入 `boot_pgdir_va`。

**原因分析：**

本实验运行在 RISC-V 上，模块三的 `proc_run()` 需要把页表基址编码进 `satp`。RV64/Sv39 的 `satp` 要求保存页表物理基址，而 `boot_pgdir_va` 位于 `KERNBASE` 之上，直接写入会得到非法的页表基址；工程中另设的 `boot_pgdir_pa` 才是 `satp` 需要的值。另外，`libs/riscv.h` 中的 `lsatp()` 是 Sv32 遗留实现，无法表达 Sv39 的 `satp`，模块三必须参照 `pmm.c` 的 `enable_paging()` 直接写 `satp`。

**问题解决策略：**

在提示词的 `[RELY]` 和 `alloc_proc()` 的 spec 中明确 `pgdir = boot_pgdir_pa`，并补充跨模块约定：`proc_run()` 直接把 `proc->pgdir` 按 Sv39 格式写入 `satp`，不能再套 `PADDR()`，也不使用 `lsatp()`；两个模块必须统一使用同一种地址形态。

**最终结果：**

实现中 `alloc_proc()` 写入 `boot_pgdir_pa`，该约定写入最终提示词，联调时由模块三按同一约定实现 `proc_run()`。

##### 第二次迭代：idleproc 是否挂入进程链表

**遇到的问题：**

若按“所有进程都要登记”的直觉，把 `idleproc` 也插入 `proc_list` 和 `hash_list`，调度器遍历可运行线程时可能选中它。

**原因分析：**

`idleproc` 的 `context` 保持全零，`context.ra == 0`；一旦被 `switch_to` 选中，保存现场后会跳转到地址 0，破坏系统执行。现有测试也明确要求 `idleproc->context.ra == 0`，说明它不应作为普通可切换线程。此外若把它插入链表，`get_pid()` 的扫描结果也会受到无关节点影响。

**问题解决策略：**

在 `proc_init()` 的 spec 中规定：`idleproc` 只设置字段、递增 `nr_process` 并赋给 `current`，不挂入任何链表；`schedule()` 在没有其他可运行线程时显式回退到 `idleproc`。同时固定初始化顺序为先建立空链表、再创建线程，避免 `hash_proc()` 与 `get_pid()` 访问未初始化的链表。

**最终结果：**

实现中 `idleproc` 不进入链表，init 线程由 `do_fork()` 正常登记，PID 分配结果为 1；静态核对与 `test_lab4_process_create()` 的断言一致。

##### 第三次迭代：失败路径回收与临界区顺序

**遇到的问题：**

初版描述只给出 `do_fork()` 的正常流程，没有说明资源分配失败时如何回滚，也没有说明 `wakeup_proc()` 是否应放在中断保护区内。

**原因分析：**

`do_fork()` 同时涉及控制块、内核栈、PID、散列桶和进程链表，任一环节失败都可能留下内存泄漏或半初始化线程；`wakeup_proc()` 会把线程置为可运行，如果放在临界区内，调度器可能在链表和计数尚未稳定时看到新线程。

**问题解决策略：**

补齐失败分支：`nr_process` 超限返回 `-E_NO_FREE_PROC`；控制块分配失败返回 `-E_NO_MEM`；内核栈或 `copy_mm()` 失败时按“内核栈 → 控制块”逆序释放，并且不插入任何链表。把 PID 分配、`hash_proc()`、`list_add()` 与 `nr_process++` 放入同一个 `local_intr_save/restore` 临界区，`wakeup_proc()` 放在临界区之后。

**最终结果：**

实现按上述顺序完成，失败路径不会留下已登记节点。在项目的 `code` 目录执行 `make -B obj/kern/process/proc.o`，输出 `+ cc kern/process/proc.c`，无编译错误与警告，生成 `obj/kern/process/proc.o`。由于模块三尚未实现，完整内核无法链接，运行阶段验证留待联调。

#### 最终提示词

以下是本模块最终使用的提示词：

````markdown
## [PROMPT]

**任务**：在当前 Lab4 项目中修改 `code/kern/process/proc.c`（相对于代码目录为 `kern/process/proc.c`），实现 `alloc_proc()`、`proc_init()`、`kernel_thread()`、`do_fork()`、`copy_thread()`、`copy_mm()`，完成进程控制块的初始化、idle 线程与 init 线程的建立，以及内核线程的创建，使新建线程具备内核栈、PID、首次执行现场和可运行状态。

**操作要求**：必须修改项目中的实际文件，不能只输出代码片段。先阅读现有实现和声明，只在 `//LAB4 function2 YOUR CODE` 对应区域补充上述函数及必要的内部辅助函数，并保留该标记，将其中的 `YOUR CODE` 替换为学号 `2410439`。不修改其他函数和模块：`do_exit()`、`init_main()`、`forkret()`、`setup_kstack()`、`put_kstack()`、`get_pid()`、`hash_proc()`、`find_proc()` 已由框架提供；`//LAB4 function3 YOUR CODE`、`kern/schedule/sched.c`、`kern/process/switch.S`、`kern/mm/pmm.c` 属于其他模块，不得改动。若编译时缺少 `SSTATUS_*` 或 `read_csr()` 的声明，可在文件头部按现有风格补充 `#include <riscv.h>`，不改动其他逻辑。

**输出要求**：以工程真实代码和下面三个部分为准完成修改，简要说明修改内容及验证结果。当前项目功能模块三尚未实现，完整内核无法链接和运行，因此只做本模块目标文件的编译检查，不补写 `schedule()`、`proc_run()`、`cpu_idle()`、`switch_to()`，也不声称线程创建测试已经在运行阶段通过。

## [RELY]

代码运行于 RV64/Sv39 环境，相关声明和类型位于 `kern/process/proc.h`、`kern/trap/trap.h`、`kern/mm/memlayout.h`、`kern/mm/pmm.h`、`kern/schedule/sched.h`、`kern/sync/sync.h`、`kern/mm/kmalloc.h`、`libs/error.h`、`libs/riscv.h` 等现有头文件。使用前以实际代码核对，不重新定义这些接口。

**类型与状态：**

- `struct proc_struct` 为进程控制块，字段包括 `state`、`pid`、`runs`、`kstack`、`need_resched`、`parent`、`mm`、`context`、`tf`、`pgdir`、`flags`、`name`、`list_link`、`hash_link`。
- `struct context` 保存上下文切换所需的 `ra`、`sp` 和 `s0`～`s11`，其中只有 `ra`、`sp` 需要在本模块设置，其余字段保持 `alloc_proc()` 中的清零状态。
- `struct trapframe` 由 36 个寄存器宽度组成，布局与 `trapentry.S` 的 `SAVE_ALL`／`RESTORE_ALL` 一致；本模块使用 `gpr.s0`、`gpr.s1`、`gpr.a0`、`gpr.sp`、`status`、`epc`。
- `KSTACKPAGE` 为内核栈页数（2 页），`KSTACKSIZE` 为内核栈字节大小；`bootstack` 为启动栈的起始地址，也是 `idleproc` 的内核栈。
- `proc_list` 为进程链表，`hash_list[HASH_LIST_SIZE]` 为按 PID 散列的进程桶；`nr_process`、`current`、`idleproc`、`initproc` 为本文件内的现有全局变量。
- `boot_pgdir_va` 为启动页表的内核虚拟地址，`boot_pgdir_pa` 为其物理地址；后续 `proc_run()` 会直接以 `proc->pgdir` 写入 `satp`，因此 `pgdir` 字段应保存物理基址 `boot_pgdir_pa`。
- `E_NO_MEM`、`E_NO_FREE_PROC` 由 `error.h` 提供，`MAX_PROCESS` 为进程数上限。

**可用接口：**

```c
static int get_pid(void);                       /* 分配唯一 PID */
static void hash_proc(struct proc_struct *proc);/* 插入 PID 散列桶 */
struct proc_struct *find_proc(int pid);         /* 按 PID 查找 */
static int setup_kstack(struct proc_struct *proc);   /* 分配 KSTACKPAGE 页内核栈 */
static void put_kstack(struct proc_struct *proc);    /* 释放内核栈 */
static void forkret(void);                      /* 新线程首次执行的 C 入口，内部调用 forkrets(current->tf) */
void kernel_thread_entry(void);                 /* entry.S：move a0, s1; jalr s0; jal do_exit */
void forkrets(struct trapframe *tf);            /* trapentry.S：恢复陷阱帧并 sret */
void wakeup_proc(struct proc_struct *proc);     /* 将线程置为 PROC_RUNNABLE */
char *set_proc_name(struct proc_struct *proc, const char *name);
void *kmalloc(size_t n);
void kfree(void *objp);
```

- `alloc_proc()` 使用 `kmalloc(sizeof(struct proc_struct))` 申请控制块，`kmalloc_init()` 已在 `pmm_init()` 中调用；`do_fork()` 失败时用 `kfree()` 回收控制块。
- `setup_kstack()` 成功返回 0 并设置 `proc->kstack`，失败返回 `-E_NO_MEM`；`put_kstack()` 与它配对。
- `wakeup_proc()` 内部断言线程不是 `PROC_ZOMBIE` 或 `PROC_RUNNABLE`，因此只能对新建的 `PROC_UNINIT` 线程调用。
- `forkret()`、`kernel_thread_entry` 与 `forkrets` 共同完成新线程首次运行：`switch_to` 返回 `forkret` 后，经 `forkrets(current->tf)` 进入 `__trapret`，再按 `tf->epc` 跳到线程入口。
- `read_csr(sstatus)` 与 `SSTATUS_SPP`、`SSTATUS_SPIE`、`SSTATUS_SIE` 由 `libs/riscv.h` 提供；`local_intr_save(x)`、`local_intr_restore(x)` 由 `kern/sync/sync.h` 提供，`x` 记录进入临界区前中断是否开启。
- `schedule()`、`proc_run()`、`cpu_idle()`、`switch_to` 属于功能模块三，本模块不实现、不调用。

## [GUARANTEE]

必须实现以下公开接口，签名与 `proc.h` 一致：

```c
void proc_init(void);
int kernel_thread(int (*fn)(void *), void *arg, uint32_t clone_flags);
int do_fork(uint32_t clone_flags, uintptr_t stack, struct trapframe *tf);
```

同时实现本文件内部使用的辅助函数：

```c
static struct proc_struct *alloc_proc(void);
static void copy_thread(struct proc_struct *proc, uintptr_t esp, struct trapframe *tf);
static int copy_mm(uint32_t clone_flags, struct proc_struct *proc);
```

不增加新的公开接口，不修改 `proc.h`、`sched.h` 及其他头文件。

## [SPECIFICATION]

### alloc_proc

**Pre-Condition：**

- `kmalloc_init()` 已完成，内核堆可分配对象；`boot_pgdir_pa` 已由 `pmm_init()` 设置。
- 调用者还没有为进程分配 PID、内核栈，也没有把它挂入任何链表。

**Post-Condition：**

- 成功返回一个已清零并完成字段初始化的 `struct proc_struct`；失败返回 `NULL`，不产生其他副作用。
- 新控制块不进入 `proc_list` 和 `hash_list`，不改变 `nr_process`，不改变 `current`。

**Case 1：`kmalloc()` 返回 `NULL`。** 直接返回 `NULL`，不访问空指针。

**Case 2：分配成功。** 依次初始化：`state = PROC_UNINIT`、`pid = -1`、`runs = 0`、`kstack = 0`、`need_resched = 0`、`parent = NULL`、`mm = NULL`、`tf = NULL`、`pgdir = boot_pgdir_pa`、`flags = 0`；`context` 整体清零（包括 `ra`、`sp`），`name` 整体清零。

**Requirements：**

- 不得在 `alloc_proc()` 中分配内核栈、分配 PID、插入链表或调用 `wakeup_proc()`。
- `context.ra` 必须保持 0；首次执行现场由 `copy_thread()` 设置，`idleproc` 不会经由 `switch_to` 进入。
- `mm` 保持 `NULL`；本实验当前阶段没有用户地址空间。

### proc_init

**Pre-Condition：**

- `pmm_init()`、`vmm_init()` 已完成，`kmalloc` 可用；`current`、`idleproc`、`initproc` 初值为 `NULL`。
- `proc_list` 与 `hash_list` 尚未初始化，需要在本函数中建立空链表。

**Post-Condition：**

- `proc_list` 与 `hash_list` 的每个桶均为空链表。
- `idleproc` 存在：`pid == 0`、`state == PROC_RUNNABLE`、`kstack == (uintptr_t)bootstack`、`need_resched == 1`、`name == "idle"`、`parent == NULL`、`mm == NULL`、`tf == NULL`、`runs == 0`、`flags == 0`、`context.ra == 0`。
- `current == idleproc`，`nr_process` 已包含 idle 线程。
- `initproc` 存在且已登记：`pid == 1`、`state == PROC_RUNNABLE`、`parent == idleproc`、`mm == NULL`、`context.ra != 0`、`context.sp != 0`、`name == "init"`。

**Case 1：`alloc_proc()` 为 idleproc 失败。** 调用 `panic("cannot alloc idleproc.\n")`。

**Case 2：创建 init 线程失败（`kernel_thread()` 返回值 `<= 0`）。** 调用 `panic("create init_main failed.\n")`，不继续使用未初始化的 `initproc`。

**Case 3：正常路径。** 先用 `list_init()` 初始化 `proc_list`，并对 `hash_list` 的每个桶调用 `list_init()`；然后为 idleproc 设置上述字段、`nr_process++`、`current = idleproc`；再调用 `kernel_thread(init_main, "Hello world!!", 0)` 创建 init 线程，通过 `find_proc(pid)` 取回控制块并 `set_proc_name(initproc, "init")`。

**Requirements：**

- idleproc 只作为当前执行现场和无可用线程时的调度回退，不挂入 `proc_list` 和 `hash_list`；`initproc` 由 `do_fork()` 负责登记。
- 初始化顺序必须先建立空链表，再创建线程，否则 `hash_proc()`、`get_pid()` 会访问未初始化的链表。
- `init_main` 已由框架实现，本模块不修改其内容。

### kernel_thread

**Pre-Condition：**

- 在进程管理初始化之后的内核上下文调用，`fn` 为内核函数地址，`arg` 为传给它的参数；`clone_flags` 为调用者给出的克隆标志。
- 调用者不要求本函数分配内核栈或 PID。

**Post-Condition：**

- 成功返回新建线程的 PID（大于 0），该线程尚未运行，已处于 `PROC_RUNNABLE`，其首次执行现场为：进入 `kernel_thread_entry`，由 `s0` 调用 `fn`、由 `s1` 传入 `arg`。
- 失败返回 `do_fork()` 给出的负错误码，不残留线程控制块、内核栈或链表节点。

**Case 1：正常创建。** 在栈上定义并清零 `struct trapframe tf`，设置 `tf.gpr.s0 = (uintptr_t)fn`、`tf.gpr.s1 = (uintptr_t)arg`、`tf.epc = (uintptr_t)kernel_thread_entry`、`tf.status = (read_csr(sstatus) | SSTATUS_SPP | SSTATUS_SPIE) & ~SSTATUS_SIE`，然后返回 `do_fork(clone_flags | CLONE_VM, 0, &tf)`。

**Case 2：`do_fork()` 失败。** 直接把负错误码返回给调用者，不做额外的资源回收。

**Requirements：**

- 必须附加 `CLONE_VM`，表示新建的是共享内核地址空间的线程。
- `status` 的设置必须保证 `sret` 返回 S 模式并重新开启中断：置位 `SPP`、`SPIE`，清除 `SIE`。
- 陷阱帧是栈上临时对象，只在调用期间有效；其内容由 `do_fork()` 中的 `copy_thread()` 复制到新线程内核栈，不能把它的地址长期保存在新控制块中。

### do_fork

**Pre-Condition：**

- `current` 指向当前线程，`tf` 指向调用者准备好的陷阱帧；`proc_list`、`hash_list`、`get_pid()`、`hash_proc()` 已可用。
- `clone_flags` 可能包含 `CLONE_VM`，`stack` 在 `kernel_thread` 路径为 0。

**Post-Condition：**

- 成功返回新线程 PID；新线程的 `parent == current`，已分配内核栈、PID，完成 `copy_mm()` 与 `copy_thread()`，已插入 `proc_list` 和 `hash_list`，`nr_process` 增加 1，状态为 `PROC_RUNNABLE`。
- 失败返回负错误码，且不残留控制块、内核栈、PID 占用或链表节点。

**Case 1：`nr_process >= MAX_PROCESS`。** 返回 `-E_NO_FREE_PROC`，不分配任何资源。

**Case 2：`alloc_proc()` 返回 `NULL`。** 返回 `-E_NO_MEM`。

**Case 3：`setup_kstack()` 失败。** 释放刚分配的控制块（`kfree`），返回 `-E_NO_MEM`。

**Case 4：`copy_mm()` 失败。** 释放内核栈（`put_kstack`）和控制块（`kfree`），返回 `-E_NO_MEM`。

**Case 5：正常路径。** 先设置 `proc->parent = current`；按序完成内核栈分配、`copy_mm()`、`copy_thread()`；随后在 `local_intr_save(intr_flag)` 保护的临界区内完成 `proc->pid = get_pid()`、`hash_proc(proc)`、`list_add(&proc_list, &(proc->list_link))`、`nr_process++`；退出临界区后调用 `wakeup_proc(proc)`，返回 `proc->pid`。

**Requirements：**

- PID 分配、散列插入、链表插入和进程计数必须放在同一个中断保护临界区内，避免 PID 冲突或链表被中断处理破坏；`local_intr_restore(intr_flag)` 按进入前的状态恢复中断。
- `wakeup_proc()` 必须在临界区之后调用，且只能对 `PROC_UNINIT` 状态调用。
- 失败路径按“内核栈 → 控制块”的逆序释放，不能把尚未登记成功的线程留在 `proc_list`、`hash_list` 中，也不能调用 `put_kstack()` 处理尚未分配的内核栈。
- 本函数只负责创建和唤醒线程，不调用 `schedule()`，不切换 `current`；线程真正运行由功能模块三负责。

### copy_thread

**Pre-Condition：**

- `proc` 已由 `alloc_proc()` 初始化并已分配内核栈；`tf` 指向调用者的陷阱帧模板；`esp` 为调用者指定的用户栈指针，`kernel_thread` 路径为 0。

**Post-Condition：**

- `proc->tf` 指向新线程内核栈顶部的陷阱帧，内容为 `*tf` 的副本；`proc->tf->gpr.sp` 为新线程首次运行时的栈指针；`proc->tf->gpr.a0 == 0`；`proc->context.ra == (uintptr_t)forkret`；`proc->context.sp == (uintptr_t)proc->tf`。
- 新线程第一次被调度时，从 `forkret` 进入，经 `forkrets(proc->tf)` 恢复陷阱帧，按 `tf->epc` 执行线程入口；`kernel_thread` 创建的线程因此进入 `kernel_thread_entry`，由 `s0`、`s1` 调用目标函数。

**Case 1：`esp == 0`（`kernel_thread` 路径）。** 令 `proc->tf->gpr.sp = (uintptr_t)proc->tf`，新线程使用内核栈顶部作为栈指针。

**Case 2：`esp != 0`（为后续用户态 fork 预留）。** 令 `proc->tf->gpr.sp = esp`，不覆盖调用者提供的栈指针。

**Requirements：**

- 陷阱帧位置使用 `(struct trapframe *)(proc->kstack + KSTACKSIZE) - 1` 计算，不得越出内核栈，也不得覆盖内核栈中其他数据。
- `a0` 置 0 用于表达子线程的返回值；`fn` 与 `arg` 仍通过 `s0`、`s1` 传递，不能改为写入 `a0`。
- 只设置 `context.ra` 与 `context.sp`；`context` 的其他寄存器保持 `alloc_proc()` 清零后的值。
- 不修改调用者传入的 `tf` 模板本身。

### copy_mm

**Pre-Condition：**

- 本实验当前阶段没有用户进程，`current->mm == NULL`；`clone_flags` 可能包含 `CLONE_VM`。

**Post-Condition：**

- 返回 0；`proc->mm` 保持 `NULL`；不复制、不新建 `mm_struct`，不修改页表。
- 调用者（`do_fork()`）据此继续创建线程；本函数不会因内存不足失败。

**Case 1：所有调用。** 断言当前线程没有用户地址空间（`assert(current->mm == NULL)`），直接返回 0。

**Requirements：**

- 保留该接口作为后续实验地址空间复制的挂接点，但本阶段不实现真正的内存复制。
- 不调用 `mm_create()`、`mm_destroy()`，不改动 `proc->pgdir`。
- 不因为 `CLONE_VM` 去共享或替换页表；线程共享内核页表由 `alloc_proc()` 设置的 `pgdir` 体现。

### 验证要求

先做静态自检：`alloc_proc()` 的初始字段、`proc_init()` 中 idleproc 与 initproc 的状态、PID、父子关系和上下文，必须与现有模块测试 `test_lab4_process_create()` 的断言一致，尤其注意 `idleproc->pid == 0`、`idleproc->kstack == (uintptr_t)bootstack`、`idleproc->tf == NULL`、`idleproc->runs == 0`、`idleproc->flags == 0`、`idleproc->context.ra == 0`、`initproc->pid == 1`、`initproc->parent == idleproc`、`initproc->context.ra != 0`、`initproc->context.sp != 0`。

编译检查在项目的 `code` 目录执行：

```bash
make -B obj/kern/process/proc.o
```

该命令只编译本模块目标文件，不链接完整内核，因此不依赖尚未实现的功能模块三。要求命令正常结束并生成 `obj/kern/process/proc.o`，无编译错误；若出现警告，需结合内容判断并记录。

由于 `schedule()`、`proc_run()`、`cpu_idle()`、`switch_to` 尚未实现，完整内核链接和 `make grade` 运行会失败，这属于功能模块三的工作，不应通过补写其他模块或修改测试来绕过。运行阶段验证留待联调时执行，本模块报告只说明编译检查结果和静态核对结论，不声称线程创建测试已经运行通过。
````

---

### 功能模块三：进程调度与上下文切换

**负责人：** 2410826-马伊汀

#### 模块功能描述

**需要实现/修改的函数：**

涉及文件为 `kern/process/proc.c`、`kern/schedule/sched.c` 和 `kern/process/switch.S`，分别在 `//LAB4 function3 YOUR CODE` 对应位置实现以下内容：

```c
void cpu_idle(void);                                       // kern/process/proc.c
void schedule(void);                                       // kern/schedule/sched.c
void proc_run(struct proc_struct *proc);                   // kern/process/proc.c
void switch_to(struct context *from, struct context *to);  // kern/process/switch.S
```

**功能说明：**

本模块把功能模块二创建出来的内核线程真正交给 CPU 执行。`idleproc` 在 `kern_init()` 的最后进入 `cpu_idle()`，发现需要重调度后调用 `schedule()` 选择下一个可运行线程；`proc_run()` 在中断保护的临界区内更新当前线程、切换页表并调用 `switch_to`；`switch_to` 只负责被调用者保存寄存器的保存与恢复。模块完成后，系统应能完成从 `idleproc` 到 `initproc` 的第一次切换，并使 `initproc` 从预先准备好的陷阱帧进入 `init_main()` 执行。

`cpu_idle()` 是 `idleproc` 的执行主体。此时 `current` 指向 `idleproc`，且 `proc_init()` 已把它的 `need_resched` 置为 1，函数在无限循环中检查 `current->need_resched`：为真时调用 `schedule()` 让出 CPU，返回后继续检查；为假时原地等待。该函数不返回，作为系统没有其他可运行线程时的空闲循环。

`schedule()` 采用 FIFO 轮转策略从 `proc_list` 中选择下一个可运行线程。它先把 `current->need_resched` 清零，再确定遍历起点：当前线程是 `idleproc` 时从 `proc_list` 头节点开始，否则从 `current->list_link` 的下一个节点开始；沿环形链表遍历一圈，选择第一个 `state == PROC_RUNNABLE` 的线程。若一圈内没有可运行线程，则回退到 `idleproc`。选中后把 `next->runs` 加 1，并在 `next != current` 时调用 `proc_run(next)`。选择与切换整体位于 `local_intr_save/restore` 临界区中。

`proc_run()` 完成真正的切换。若目标线程就是当前线程则直接返回；否则在关中断的临界区内保存 `prev = current`、`next = proc`，先更新 `current = proc`，再把 `proc->pgdir` 按 Sv39 格式写入 `satp` 并调用 `flush_tlb()` 使旧翻译失效，最后调用 `switch_to(&prev->context, &next->context)`。新线程首次被选中时，`context.ra` 已被 `copy_thread()` 设置为 `forkret`，因此 `switch_to` 返回后进入 `forkret → forkrets → __trapret`，按陷阱帧恢复现场并跳到 `kernel_thread_entry`；旧线程再次被选中时，`switch_to` 返回到 `proc_run` 中的返回点，继续执行 `local_intr_restore` 并回到 `schedule()` 的调用者。

`switch_to()` 是 `switch.S` 中的汇编函数，只负责寄存器的保存与恢复。它以 `a0` 指向旧线程的 `context`、`a1` 指向新线程的 `context`，把 `ra`、`sp`、`s0`～`s11` 共 14 个被调用者保存寄存器写入 `from`，再从 `to` 读出并覆盖当前寄存器，最后通过 `ret` 返回到 `to->ra`。它不判断调度策略，也不访问 `proc_struct`。

**主要要求（spec）：**

- `cpu_idle()` 必须是不返回的空闲循环，只在 `current->need_resched != 0` 时调用 `schedule()`；不得在循环外擅自关闭中断，也不得在没有调度需求时反复调用调度器。
- `schedule()` 必须在选择前清零 `current->need_resched`；只选择 `PROC_RUNNABLE` 状态的线程，不能选中 `PROC_UNINIT`、`PROC_SLEEPING` 或 `PROC_ZOMBIE`。
- `schedule()` 的遍历起点必须区分当前线程是否为 `idleproc`：`idleproc` 不挂入 `proc_list`，其 `list_link` 未初始化，不能作为遍历起点；从 `&proc_list` 开始时必须跳过链表头节点本身。
- 遍历必须沿环形链表绕回起点并终止；一整圈没有可运行线程时回退到 `idleproc`；若回退结果仍是当前线程，则不调用 `proc_run()`。
- `schedule()` 中被选中线程的 `runs` 加 1；选择、`need_resched` 清零与 `proc_run()` 调用必须位于同一个 `local_intr_save/restore` 临界区内。
- `proc_run()` 在 `proc == current` 时直接返回；否则必须先保存 `prev`，再更新 `current`，最后调用 `switch_to`。顺序不能颠倒。
- `proc_run()` 不得使用 `lsatp()`。`lsatp()` 是 Sv32 遗留实现，写入的是 32 位的 `SATP32_MODE`，在 RV64/Sv39 下会把 `satp` 的 mode 字段写成 0（Bare），无法表达 Sv39 页表基址。正确写法参照 `pmm.c` 的 `enable_paging()`：`write_csr(satp, 0x8000000000000000 | (proc->pgdir >> RISCV_PGSHIFT));`，其中 `RISCV_PGSHIFT == 12`。
- `proc->pgdir` 保存的是页表物理基址（功能模块二约定为 `boot_pgdir_pa`），直接编码进 `satp` 即可，不能再套 `PADDR()`。
- 修改 `satp` 后必须调用 `flush_tlb()`；`current` 更新、`satp` 写入与 `switch_to` 必须在同一临界区内，不能在切换前提前开中断。
- `switch_to` 必须包含 `#include <riscv.h>`，使用 `STORE`、`LOAD`、`REGBYTES` 宏；保存时以 `a0` 为基址，恢复时以 `a1` 为基址，偏移必须与 `struct context` 的字段顺序一致（`ra=0`、`sp=1`、`s0=2`、…、`s11=13`）。
- `switch_to` 只保存/恢复 `ra`、`sp`、`s0`～`s11`；调用者保存寄存器由编译器在调用点处理。它不做调度判断，不修改 `current`，恢复完全部寄存器后再 `ret`。
- 复用现有的 `proc_list`、`current`、`idleproc`、`le2proc()`、`list_next()`、`local_intr_save/restore`、`read_csr/write_csr`、`flush_tlb()` 等接口，不修改 `proc.h`、`sched.h`、`sync.h`、`trapentry.S`、`entry.S` 及其他模块代码。

本模块的输入来自功能模块二：`idleproc` 的 `need_resched` 已置 1，`initproc` 已处于 `PROC_RUNNABLE` 且上下文由 `copy_thread()` 准备好；本模块的输出是让 `initproc` 真正获得 CPU。调度正确性还依赖功能模块一保证的页表映射和功能模块二的线程登记，因此联调时应沿 `proc_init → cpu_idle → schedule → proc_run → switch_to → forkret → forkrets → __trapret → kernel_thread_entry` 逐段检查。

#### 实现迭代过程

##### 第一次迭代：`proc_run()` 使用 `lsatp()` 导致切换后地址翻译错误

**遇到的问题：**

初版提示词参考了 `proc.h` 中 `pgdir` 的注释和常见 ucore 写法，要求 AI 在 `proc_run()` 中调用 `lsatp(proc->pgdir)` 切换页表，生成的代码能够编译，但第一次切换后系统取指异常，QEMU 无法继续运行。

**原因分析：**

`libs/riscv.h` 中的 `lsatp()` 是 Sv32 遗留实现：

```c
write_csr(satp, SATP32_MODE | (pgdir >> RISCV_PGSHIFT));
```

其中 `SATP32_MODE` 为 `0x80000000`。Rv64 的 `satp` 把 mode 放在第 63～60 位，`0x80000000` 只置位了第 31 位，mode 实际为 0（Bare），页表基址也没有落在 `satp` 的 PPN 字段中。本实验的内核链接在高地址 `KERNBASE = 0xFFFFFFFFC0200000`，关闭分页后 CPU 会把该地址当作物理地址取指，而该地址并不是有效 RAM，于是产生指令访问异常。`proc->pgdir` 本身已经是物理基址，也不需要再做 `PADDR()` 转换。

**问题解决策略：**

在提示词的 `[RELY]` 与 `proc_run()` 的 spec 中明确禁用 `lsatp()`，要求按 `kern/mm/pmm.c` 中 `enable_paging()` 的写法编码 Sv39 的 `satp`：

```c
write_csr(satp, 0x8000000000000000 | (proc->pgdir >> RISCV_PGSHIFT));
flush_tlb();
```

并说明 `pgdir` 已是物理基址、切换后必须刷新 TLB。

**最终结果：**

实现改为直接写 `satp` 的 Sv39 格式并在其后调用 `flush_tlb()`。本实验的内核线程共享 `boot_pgdir_pa`，因此切换前后页表内容相同，但 `satp` 的写入路径正确，为后续用户进程使用独立页表打下基础。

##### 第二次迭代：`current` 的更新必须早于 `switch_to`

**遇到的问题：**

初版生成的 `proc_run()` 把 `current = proc` 放在 `switch_to()` 之后，或者只切换上下文而没有更新 `current`，切换到新线程后，`forkret()` 中的 `current->tf` 仍指向旧线程。

**原因分析：**

新线程第一次被调度时，`switch_to` 会返回到 `copy_thread()` 预先设置的 `context.ra = forkret`，`forkret()` 的实现是 `forkrets(current->tf)`，它依赖 `current` 已经指向新线程。如果此时 `current` 仍是 `idleproc`，而 `idleproc->tf == NULL`，`forkrets(NULL)` 会把 `sp` 设为 0，随后 `__trapret` 从地址 0 恢复现场，直接触发异常；即使旧线程不是 `idleproc`，也会恢复错误的陷阱帧，导致新线程执行旧线程的入口或重复执行。`current` 同时也是 `schedule()` 下一次判断遍历起点的依据，必须在切换前稳定下来。

**问题解决策略：**

在 `proc_run()` 的 spec 中固定顺序：进入临界区后先用局部变量保存 `prev = current`、`next = proc`，然后更新 `current = proc`，再写 `satp`、刷新 TLB，最后调用 `switch_to(&prev->context, &next->context)`。同时明确 `proc == current` 时直接返回，避免无意义切换。

**最终结果：**

实现按“保存 prev → 更新 current → 切换 satp → 刷新 TLB → switch_to”的顺序完成。第一次调度时 `current` 在 `switch_to` 前已经指向 `initproc`，`forkret()` 能取到正确的 `initproc->tf`。

##### 第三次迭代：`schedule()` 的遍历起点与 `idleproc` 特殊处理

**遇到的问题：**

初版生成的 `schedule()` 无条件从 `&(current->list_link)` 开始遍历，或者使用 `while ((le = list_next(le)) != &proc_list)` 直接跳过链表头。系统第一次调度时 `current` 是 `idleproc`，遍历读到未初始化的 `list_link`，产生非法指针访问；另一版则因为没有正确处理链表头，始终选不到 `initproc`。

**原因分析：**

功能模块二已经确定 `idleproc` 不挂入 `proc_list` 和 `hash_list`，它的 `list_link` 没有被 `list_init()` 初始化，而 `alloc_proc()` 使用的 `kmalloc()` 也不保证清零。因此 `idleproc->list_link` 的 `prev/next` 是无效值，不能作为遍历起点。另一方面，`proc_list` 是带头节点的双向循环链表，从 `&proc_list` 开始时，第一次 `list_next()` 得到的是第一个线程节点，链表头本身不是线程，必须跳过。遍历还需要在一整圈后终止，并在没有可运行线程时回退到 `idleproc`。

**问题解决策略：**

在 `schedule()` 的 spec 中给出确定的遍历方式：

```c
last = (current == idleproc) ? &proc_list : &(current->list_link);
le = last;
do {
    le = list_next(le);
    if (le != &proc_list) {
        next = le2proc(le, list_link);
        if (next->state == PROC_RUNNABLE) {
            break;
        }
    }
} while (le != last);
if (next == NULL || next->state != PROC_RUNNABLE) {
    next = idleproc;
}
```

并补充：先清零 `need_resched`，选中后 `next->runs++`，只有 `next != current` 才调用 `proc_run()`。

**最终结果：**

实现按上述方式遍历。第一次调度时 `current == idleproc`，从 `&proc_list` 开始找到处于 `PROC_RUNNABLE` 的 `initproc` 并切换；后续没有其他可运行线程时回退到 `idleproc`，不会把链表头当作线程，也不会读取 `idleproc` 未初始化的 `list_link`。

##### 第四次迭代：`switch.S` 的宏与上下文偏移

**遇到的问题：**

初版生成的 `switch.S` 直接使用 `sd`/`ld`，或者只保存了部分 `s` 寄存器；另一个版本没有包含 `riscv.h`，汇编时 `STORE`、`LOAD`、`REGBYTES` 均未定义。还有版本把 `struct context` 的字段顺序记错，把 `ra`、`sp` 与 `s0` 的偏移写反。

**原因分析：**

`struct context` 在 `proc.h` 中的字段顺序是 `ra`、`sp`、`s0`～`s11`，共 14 个 `uintptr_t`；RV64 下 `REGBYTES == 8`，所以偏移必须严格是 `0*REGBYTES` 到 `13*REGBYTES`。`switch_to` 保存的是被调用者保存寄存器，恢复时任何一个偏移错位都会让 `ra` 或 `sp` 取到错误的值，表现为 `ret` 跳到非法地址或栈指针损坏。`STORE`、`LOAD`、`REGBYTES` 定义在 `libs/riscv.h`，汇编文件需要显式包含。

**问题解决策略：**

在提示词中给出 `switch.S` 的完整约束：文件首行包含 `#include <riscv.h>`，使用 `.text` 和 `.globl switch_to`；保存时以 `a0` 为基址写 `0*REGBYTES(a0)`～`13*REGBYTES(a0)`，恢复时以 `a1` 为基址读同样偏移；恢复完全部寄存器后再 `ret`，不提前覆盖作为基址的 `a1`；不加入任何调度逻辑。

**最终结果：**

实现按 `0`～`13` 的偏移保存和恢复 14 个寄存器，`switch.o` 可正常汇编，偏移与 `struct context` 一一对应。

#### 最终提示词

以下是本模块最终使用的提示词：

````markdown
## [PROMPT]

**任务**：在当前 Lab4 项目中实现内核线程的调度与上下文切换，使 `kern_init()` 结尾的 `cpu_idle()` 能够通过 `schedule()`、`proc_run()` 和 `switch_to` 把 CPU 从 `idleproc` 交给已经创建好的 `initproc`。需要修改三个文件：

- `code/kern/process/proc.c`（相对代码目录为 `kern/process/proc.c`），在 `//LAB4 function3 YOUR CODE` 处实现 `cpu_idle()`、`proc_run()`；
- `code/kern/schedule/sched.c`，在 `//LAB4 function3 YOUR CODE` 处实现 `schedule()`；
- `code/kern/process/switch.S`，在 `//LAB4 function3 YOUR CODE` 处实现 `switch_to`。

**操作要求**：必须修改项目中的实际文件，不能只输出代码片段。先阅读现有代码和声明，只在上面的标记处补充代码，保留标记并把 `YOUR CODE` 替换为学号 `2410826`。不修改 `proc.h`、`sched.h`、`sync.h`、`pmm.h`、`trapentry.S`、`entry.S`，不修改功能模块一、二的实现和测试代码，不增加新的公开接口。`switch.S` 需要自行补充 `#include <riscv.h>` 以使用其中的 `STORE`、`LOAD`、`REGBYTES` 宏；其他文件按现有包含关系即可获得所需声明。

**输出要求**：以工程真实代码和下面三个部分为准完成修改，简要说明修改内容及验证结果。完成后应能通过 `make` 链接出完整内核；若因其他模块未完成而失败，应说明实际阻塞原因，不补写其他模块，也不声称未执行的测试已通过。

## [RELY]

代码运行于 RV64 / Sv39 环境。相关声明位于 `kern/process/proc.h`、`kern/schedule/sched.h`、`kern/sync/sync.h`、`kern/mm/pmm.h`、`libs/riscv.h`、`libs/list.h` 等头文件，使用前以实际代码核对，不重新定义。

**类型与状态：**

- `struct proc_struct` 的 `state` 取值为 `PROC_UNINIT`、`PROC_SLEEPING`、`PROC_RUNNABLE`、`PROC_ZOMBIE`；只有 `PROC_RUNNABLE` 可被调度。
- `struct context` 依次包含 `ra`、`sp`、`s0`～`s11`，共 14 个 `uintptr_t`；RV64 下每个字段 8 字节，偏移分别为 `0*REGBYTES`～`13*REGBYTES`。
- `current` 为当前线程的 `proc_struct *`；`idleproc` 是第 0 个内核线程，`context.ra == 0`，且**不在** `proc_list`、`hash_list` 中，其 `list_link` 未被初始化；`initproc` 是第一个真正可调度的内核线程，已由 `do_fork()` 插入 `proc_list`。
- `proc_list` 是带头节点的双向循环链表；`le2proc(le, list_link)` 把链表节点转换为 `proc_struct *`。
- `proc->pgdir` 保存页表**物理基址**（功能模块二的 `alloc_proc()` 写入 `boot_pgdir_pa`），`proc_run()` 必须直接把它编码进 `satp`，不能再套 `PADDR()`。
- `proc->runs` 记录线程被选中的次数；`need_resched` 为 `volatile bool`，表示当前线程是否需要让出 CPU。

**可用接口：**

```c
#define local_intr_save(x)  do { x = __intr_save(); } while (0)
#define local_intr_restore(x) __intr_restore(x);
read_csr(sstatus)                        /* 宏，读取 sstatus，见 libs/riscv.h */
write_csr(satp, val)                     /* 宏，写入 satp */
void flush_tlb(void);                    /* sfence.vma，见 kern/mm/pmm.h */
list_entry_t *list_next(list_entry_t *listelm);
struct proc_struct *le2proc(list_entry_t *le, list_link);
void switch_to(struct context *from, struct context *to);
```

- `local_intr_save(x)` 把进入临界区前 `sstatus.SIE` 的状态保存到 `bool x` 并关闭中断；`local_intr_restore(x)` 仅在 `x` 为真时重新开中断，因此可嵌套。`x` 必须是调用者声明的 `bool` 局部变量。
- `flush_tlb()` 是 `sfence.vma` 的封装；修改 `satp` 后应调用它，避免继续使用旧的地址翻译。
- **不要使用 `lsatp()`**：它是 Sv32 遗留实现，写入的是 32 位的 `SATP32_MODE`（`0x80000000`），在 RV64/Sv39 下会使 `satp` 的 mode 字段为 0（Bare），无法表达 Sv39 页表基址。正确写法参照 `kern/mm/pmm.c` 的 `enable_paging()`：`write_csr(satp, 0x8000000000000000 | (pgdir >> RISCV_PGSHIFT));`，其中 `RISCV_PGSHIFT == 12`。
- `switch_to` 由 C 代码以 `switch_to(&from->context, &to->context)` 调用，RISC-V 调用约定下第一个参数在 `a0`、第二个在 `a1`。
- 新线程首次运行的链路已由功能模块二准备好：`copy_thread()` 设置 `context.ra = forkret`、`context.sp = proc->tf`；`switch_to` 恢复 `ra` 后返回到 `forkret()`，后者调用 `forkrets(current->tf)`，经 `__trapret` 按 `tf->epc` 跳到 `kernel_thread_entry`，再由 `s0`/`s1` 调用线程函数。

## [GUARANTEE]

必须实现以下函数，签名与头文件及现有声明一致：

```c
void cpu_idle(void);                                      /* proc.c，声明带 __attribute__((noreturn)) */
void schedule(void);                                      /* sched.c */
void proc_run(struct proc_struct *proc);                  /* proc.c */
void switch_to(struct context *from, struct context *to); /* switch.S */
```

不增加新的公开接口；`proc.c`、`sched.c` 内部可按需增加 `static` 辅助函数，但本任务不需要。

## [SPECIFICATION]

### cpu_idle

**Pre-Condition：**

- `proc_init()` 已完成，`current` 指向 `idleproc`，其 `need_resched` 为 1；中断已由 `intr_enable()` 打开。

**Post-Condition：**

- 函数不返回；在循环中当 `current->need_resched != 0` 时调用 `schedule()`，否则继续检查。

**Case 1：需要重调度。** 调用 `schedule()`；返回后继续循环。

**Case 2：不需要重调度。** 空转等待，不主动修改进程状态。

**Requirements：**

- 不得在循环外关闭中断；不得在 `need_resched == 0` 时调用 `schedule()`；函数必须保持 `noreturn` 语义。

### schedule

**Pre-Condition：**

- `current` 有效，`proc_list` 已初始化；可在任意内核上下文调用，调用者不要求中断已关闭。

**Post-Condition：**

- `current->need_resched` 被清零；若存在可运行的其它线程，则选中它并调用 `proc_run()`；否则回退到 `idleproc`。被选中线程的 `runs` 加 1。整个“选择 + 切换”过程在 `local_intr_save/restore` 临界区内完成。

**Case 1：`current != idleproc`。** 从 `current->list_link` 的下一个节点开始，沿 `proc_list` 环形遍历一圈，选择第一个 `state == PROC_RUNNABLE` 的线程，实现轮转。

**Case 2：`current == idleproc`。** 从 `proc_list` 头节点的下一个节点开始遍历，给所有线程平等机会；不能使用 `idleproc->list_link` 作为起点，因为它未挂入链表、字段未初始化。

**Case 3：一圈内没有可运行线程。** 令 `next = idleproc`；若 `next == current`（当前就是 idle），则不调用 `proc_run()`，直接返回。

**Requirements：**

- 遍历必须能绕回起点并终止；不能把 `&proc_list` 当作线程节点；不得选中 `PROC_UNINIT`、`PROC_SLEEPING`、`PROC_ZOMBIE` 线程。
- `need_resched` 必须在选择前清零；`next->runs` 在切换前递增；`local_intr_restore` 按进入前状态恢复中断。

### proc_run

**Pre-Condition：**

- `proc` 是已登记且可运行的线程；`proc->pgdir` 是页表物理基址；调用时 `current` 为旧线程。

**Post-Condition：**

- 若 `proc != current`，在中断关闭的临界区内完成：更新 `current = proc`、把 `proc->pgdir` 编码进 `satp` 并 `flush_tlb()`、调用 `switch_to(&prev->context, &next->context)`；退出临界区后恢复中断。若 `proc == current`，不做任何事。

**Case 1：`proc == current`。** 直接返回，不重复切换。

**Case 2：需要切换。** 先用局部变量保存 `prev = current`、`next = proc`；**必须先更新 `current = proc`，再调用 `switch_to`**，因为新线程会从 `forkret` 开始执行并读取 `current->tf`；随后写 `satp`、刷新 TLB、切换上下文。

**Requirements：**

- 不得使用 `lsatp()`；`satp` 写法与 `enable_paging()` 一致：`write_csr(satp, 0x8000000000000000 | (proc->pgdir >> RISCV_PGSHIFT));`。
- 写 `satp` 后调用 `flush_tlb()`。
- `current` 的更新、`satp` 的写入和 `switch_to` 必须在同一个临界区内，不能在切换前提前开中断。
- 不调用 `schedule()`，不修改 `proc_list`，不改变线程状态。

### switch_to

**Pre-Condition：**

- `from`、`to` 分别指向有效的 `struct context`；由 C 代码正常调用，`a0 = from`、`a1 = to`。

**Post-Condition：**

- `from` 保存当前 `ra`、`sp`、`s0`～`s11`；从 `to` 恢复同样的 14 个寄存器；最后通过 `ret` 返回到 `to->ra` 指向的位置（新线程为 `forkret`，旧线程为 `proc_run` 中 `switch_to` 的返回点）。

**Requirements：**

- 文件需包含 `#include <riscv.h>`，使用 `STORE`、`LOAD`、`REGBYTES` 宏；RV64 下 `REGBYTES == 8`。
- 保存时以 `a0` 为基址，恢复时以 `a1` 为基址；偏移必须与 `struct context` 顺序一致：`ra=0`、`sp=1`、`s0=2`、…、`s11=13`。
- 只保存/恢复 `ra`、`sp`、`s0`～`s11`；调用者保存寄存器由编译器在调用点处理，不需要保存。
- 不做调度判断，不读写 `proc_struct`，不修改 `current`。
- 恢复完全部寄存器后再 `ret`；不能提前覆盖作为基址的 `a1`。

### 验证要求

在 `code` 目录执行：

```bash
make -B obj/kern/process/proc.o obj/kern/schedule/sched.o obj/kern/process/switch.o
```

确认三个目标文件无编译错误；待功能模块一、二完成后执行 `make` 链接完整内核。运行 `make qemu` 时，预期先创建 `idleproc` 与 `initproc`，随后 `cpu_idle()` 触发第一次调度，`initproc` 输出自己的 pid、名字和 `"Hello world!!"`，证明 `schedule → proc_run → switch_to → forkret → forkrets → __trapret → kernel_thread_entry` 链路成立。若失败，优先检查 `satp` 编码、`current` 更新顺序、`schedule` 遍历起点和 `switch.S` 偏移，不通过修改测试或补写其他模块绕过。
````

---

### Challenge 1：`local_intr_save` / `local_intr_restore` 的开关中断实现

**负责人：** 2410826-马伊汀

#### 1. 宏展开与底层 CSR 操作链路

`local_intr_save` 和 `local_intr_restore` 定义在 `kern/sync/sync.h` 中，二者并不是真正的函数，而是对 `sstatus.SIE` 位进行读改写的一组宏：

```c
static inline bool __intr_save(void) {
    if (read_csr(sstatus) & SSTATUS_SIE) {
        intr_disable();
        return 1;
    }
    return 0;
}

static inline void __intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

#define local_intr_save(x) \
    do {                   \
        x = __intr_save(); \
    } while (0)
#define local_intr_restore(x) __intr_restore(x);
```

`local_intr_save(intr_flag)` 展开后把 `__intr_save()` 的返回值写入调用者提供的 `intr_flag`。`__intr_save()` 先用 `read_csr(sstatus)`（`csrr`）读取 `sstatus`，检查 `SSTATUS_SIE`（值为 `0x2`）是否置位：

- 若 `SIE == 1`，说明进入临界区前中断是开启的，于是调用 `intr_disable()` 关闭中断，并返回 `1`；
- 若 `SIE == 0`，说明中断本来就已经关闭，不做任何修改，返回 `0`。

`local_intr_restore(intr_flag)` 展开后调用 `__intr_restore(intr_flag)`：只有当保存的 `intr_flag` 为真时，才调用 `intr_enable()` 重新开启中断；若为假则保持关闭。

`intr_disable()` 和 `intr_enable()` 位于 `kern/driver/intr.c`，分别通过 `clear_csr(sstatus, SSTATUS_SIE)` 和 `set_csr(sstatus, SSTATUS_SIE)` 实现，对应 RISC-V 的 `csrrc`/`csrrs` 指令。`sstatus.SIE` 是 S 模式全局中断使能位：它为 1 时，S 模式可以响应 `sie`/`sip` 中已使能的中断；为 0 时，这些中断被屏蔽，但同步异常仍然会正常陷入。由于 ucore 运行在 S 模式，这一位就是内核代码能否被时钟中断打断的总开关。

#### 2. 为什么保存旧状态，而不是无条件开关中断

这组宏的关键在于 `intr_flag` 保存的是**进入临界区之前的中断状态**，而不是“当前中断已经关闭”这一事实，`local_intr_restore` 只在原来开启时才重新开启，因此这对宏可以正确嵌套：

- 外层 `local_intr_save` 时中断为开，返回 `1` 并关闭中断；
- 内层 `local_intr_save` 时中断已关，返回 `0`；
- 内层 `local_intr_restore(0)` 不做任何事，不会提前打开中断；
- 外层 `local_intr_restore(1)` 才把中断恢复为开启。

如果 `local_intr_restore` 无条件调用 `intr_enable()`，内层恢复就会在外层临界区尚未结束时重新开中断，临界区被破坏。同理，在启动早期尚未初始化中断、`panic()`、陷阱入口等本来就要求关中断的上下文中，无条件开中断也会造成错误，保存旧状态使这对宏在嵌套和“进入前已关中断”的场景下都保持语义正确。

`local_intr_save` 外层使用 `do { ... } while (0)`，是为了让宏在语法上表现得像一条语句，能够安全地放在没有花括号的 `if/else` 分支中；同时 `x` 由调用者声明，宏才能把保存的状态带回后续的 `local_intr_restore`，因此 `intr_flag` 必须是调用者作用域内的 `bool` 局部变量，不能省略，也不能用未初始化的值去恢复。`local_intr_restore` 的宏定义末尾带有分号，调用处再写一个分号只会多出一个空语句，不影响语义。

#### 3. 在调度临界区中的作用与边界

在功能模块三中，`schedule()` 需要遍历 `proc_list`、修改 `need_resched` 并调用 `proc_run()`；`proc_run()` 需要同时更新 `current`、写 `satp`、刷新 TLB 并切换内核栈，如果时钟中断在这些步骤中间到达，中断处理程序可能再次触发调度，造成 `current`、页表和实际执行现场不一致，甚至重入调度。用 `local_intr_save/restore` 包住“选择线程 + 切换现场”的整个过程，可以把这段代码相对本 hart 的中断变成原子操作，保证调度器看到的是稳定状态。

这里有一个容易忽略的细节：`proc_run()` 调用 `switch_to` 后，旧线程并不会立即执行自己的 `local_intr_restore`，而是被挂起在 `proc_run` 的调用栈中。新线程从 `forkret → forkrets → __trapret → sret` 开始运行，`sret` 会从陷阱帧的 `sstatus` 中恢复 `SIE`（具体是令 `SIE = SPIE`），`kernel_thread()` 正是为此把 `tf.status` 设置为“置位 `SPP`、`SPIE`，清除 `SIE`”，所以新线程第一次 `sret` 后会以中断开启的状态运行；旧线程以后再次被选中时，`switch_to` 返回到它自己的 `proc_run`，才执行 `local_intr_restore` 恢复它进入临界区前的中断状态。若 `kernel_thread()` 没有设置 `SPIE`，新线程会带着关中断状态运行，时钟中断无法到达，系统看起来就像卡死。

这组宏的边界也需要说明：

- 它只关闭当前 hart 的 S 模式可屏蔽中断，不关闭 M 模式中断，也不阻止同步异常；它不是通用的“原子”操作。
- 它不提供多核之间的互斥。若系统有多个 hart 同时访问 `proc_list` 或 `current`，仅靠关本 hart 的中断不够，还需要自旋锁或原子操作。
- `read_csr` 与随后的 `clear_csr` 是两条指令，并非原子的读改写。若中断恰好在两者之间到达，处理程序会先执行，`sret` 按 `SPIE` 恢复中断状态后再执行 `clear_csr`，临界区只是稍微推迟开始；而 `clear_csr` 之后到达的中断会被屏蔽。因此这段代码仍能建立一个语义明确的临界区，但不能理解为“从读取那一刻起就绝不会被中断”。
- `read_csr`/`write_csr` 使用 `volatile` 内联汇编，CSR 访问本身不会被编译器重排；但临界区内的普通内存访问没有自动的 `fence` 语义。单 hart 的 ucore 不涉及其他 hart 观察这些写入，因此本实验不需要额外的内存屏障。

---

## 五、测试与验证

<!-- 根据该实验的具体情况，提供完整的测试运行截图，应包含：
- 编译及运行成功的输出（make qemu），可能有多个
- 测试通过的结果（make grade），只有一个
-->

### 5.1 前置代码与功能模块一编译检查

**负责人：** 2412084-刘芳希

**测试目的：** 检查补充的 Best Fit 分配器、中断与异常处理、陷阱现场恢复代码，以及 `pmm.c` 中的 `page_init()`、`page_insert()` 和 `page_remove()` 能否与现有接口一起由 RISC-V 工具链编译，确认不存在阻止生成目标文件的语法、类型或汇编错误。

**执行方法：** 在已安装实验工具链的 Linux/WSL 环境中，进入项目的 `code` 目录，执行：

```bash
cd /mnt/d/OS_lab/OS_lab/code
make -B obj/kern/mm/default_pmm.o obj/kern/trap/trap.o obj/kern/trap/trapentry.o
make -B obj/kern/mm/pmm.o
```

其中 `-B` 强制重新编译，避免已有目标文件使本次检查直接跳过。该命令只生成对应目标文件，不链接完整内核，因此不依赖尚未完成的线程创建和调度函数。

**预期结果：** 两条编译命令正常结束，生成上述四个 `.o` 文件，无编译错误；若存在警告，需结合具体内容判断并记录。

**实际结果：** 第一条编译命令依次输出 `+ cc kern/mm/default_pmm.c`、`+ cc kern/trap/trap.c` 和 `+ cc kern/trap/trapentry.S`，随后正常返回命令提示符，前置代码编译检查通过。第二条编译命令输出 `+ cc kern/mm/pmm.c`，截图范围内未出现编译错误，但未完整显示该命令结束后的提示符，最终退出状态仍需确认。

**结果确认方法：** 第二条编译命令结束后立即执行 `echo $?`，输出 `0` 表示该命令成功结束。补充包含返回提示符和退出状态的截图后，即可确认两项编译检查均通过。

**测试截图：** 下图同时记录了前置代码与功能模块一的编译输出。

![A 部分前置代码与功能模块一编译检查](./images/a_compile_check.png)

**验证范围：** 上述检查独立于模块二、三进行，但只验证代码能否编译，不能证明映射、引用计数、权限和 TLB 的运行行为正确。新建映射、同页重复映射、替换物理页、共享映射、重复删除和分配失败等功能路径，仍需在后续运行测试中验证。

---

### 5.2 功能模块二编译检查

**负责人：** 2410439-吴佳璐

**测试目的：** 检查 `proc.c` 中新增的 `alloc_proc()`、`proc_init()`、`kernel_thread()`、`do_fork()`、`copy_thread()` 和 `copy_mm()` 能否与框架已有的 `get_pid()`、`hash_proc()`、`find_proc()`、`setup_kstack()`、`put_kstack()`、`forkret`、`wakeup_proc()`、`kmalloc()` 等接口一起通过 RISC-V 工具链编译，确认函数签名、结构体字段和宏定义没有冲突。

**执行方法：** 在已安装实验工具链的 WSL 环境中，进入项目的 `code` 目录，执行：

```bash
cd /mnt/d/OS_CS/OS_lab/lab4/OS_lab/code
make -B obj/kern/process/proc.o
```

其中 `-B` 强制重新编译，避免已有目标文件使检查被跳过。该命令只生成 `proc.o`，不链接完整内核，因此不依赖模块三尚未实现的 `schedule()`、`proc_run()`、`cpu_idle()` 和 `switch_to`。

**预期结果：** 命令输出 `+ cc kern/process/proc.c`，正常结束并生成 `obj/kern/process/proc.o`，无编译错误；若存在警告，需结合具体内容判断并记录。

**实际结果：** 命令输出为：

```text
+ cc kern/process/proc.c
```
![B部分测试](./images/b_test.png)

---

### 5.3 功能模块三：编译与首次调度检测

**负责人：** 2410826-马伊汀

**测试目的：** 检查 `cpu_idle()`、`schedule()`、`proc_run()` 以及汇编函数 `switch_to` 能否与框架已有的 `proc_list`、`struct context`、`local_intr_save/restore`、`write_csr(satp)`、`flush_tlb()` 等接口一起编译、链接，并验证 `idleproc` 第一次让出 CPU 后能够切换到 `initproc` 执行，即 `cpu_idle → schedule → proc_run → switch_to → forkret → forkrets → __trapret → kernel_thread_entry` 链路成立。

**执行方法：** 在 WSL Ubuntu 中进入项目的 `code` 目录，使用课程工具链与 QEMU 4.1.1：

```bash
export PATH=/mnt/d/OS_CS/riscv64-unknown-elf-toolchain-10.2.0-2020.12.8-x86_64-linux-ubuntu14/bin:$HOME/qemu-4.1.1/riscv64-softmmu:$PATH
cd "/mnt/d/1南开/课程/大三上学期/操作系统/lab/lab4/OS_lab/code"
make -B obj/kern/process/proc.o obj/kern/schedule/sched.o obj/kern/process/switch.o
make
make qemu
```

其中 `-B` 强制重新编译三个模块三目标文件；`make` 链接完整内核；`make qemu` 启动内核。由于本实验的 `do_exit()` 仍是框架提供的占位实现，`init_main()` 返回后 `kernel_thread_entry` 会调用它并触发 panic，这是预期行为，不属于模块三的失败。

**预期结果：** 三个目标文件分别输出 `+ cc kern/process/proc.c`、`+ cc kern/schedule/sched.c`、`+ cc kern/process/switch.S` 并以退出码 0 结束；完整 `make` 生成 `bin/kernel` 和 `bin/ucore.img`；QEMU 输出中出现 `this initproc, pid = 1, name = "init"` 与 `To U: "Hello world!!".`，说明第一次调度和首次运行链路成功。

**实际结果：** 三个目标文件编译输出为：

```text
+ cc kern/process/proc.c
+ cc kern/schedule/sched.c
+ cc kern/process/switch.S
```

命令退出码为 0。随后完整 `make` 正常链接并生成 `bin/kernel`（236096 字节）与 `bin/ucore.img`（36912 字节），最后一步输出：

```text
riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img
```

`make qemu` 的输出中，与模块三直接相关的部分为：

```text
++ setup timer interrupts
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
kernel panic at kern/process/proc.c:208:
    process exit!!.
```

**测试截图：** 下面两张图分别记录了三个目标文件的编译结果与 `make qemu` 的首次调度运行输出。

![C 部分模块三编译结果](./images/c_compile.png)

![C 部分模块三首次调度运行](./images/c_first_switch.png)

**结果分析：**

- 三个目标文件覆盖了 `proc.c`、`sched.c` 和 `switch.S` 三种源文件类型，能够通过编译与链接，说明函数签名、`struct context` 字段、`REGBYTES` 偏移和头文件包含关系一致。
- `initproc` 打印出自己的 `pid = 1`、名字 `"init"` 以及 `init_main()` 中的 `"Hello world!!"`，证明调度链路成立：`cpu_idle()` 检测到 `need_resched` → `schedule()` 选中 `initproc` → `proc_run()` 更新 `current`、切换 `satp` 并调用 `switch_to` → `switch_to` 恢复 `context.ra = forkret` → `forkret → forkrets → __trapret` → `kernel_thread_entry` → `init_main()`。
- 最后的 `kernel panic at kern/process/proc.c:208: process exit!!.` 来自框架原有的 `do_exit()` 占位实现。`init_main()` 正常返回后，`kernel_thread_entry` 按设计执行 `jal do_exit`，因此该 panic 反而说明线程入口和返回路径都是通的；按分工该函数属于实验后续内容，本实验不补写。
- 本小节的运行只覆盖“`idleproc` 第一次切换到 `initproc`”这一条路径；多线程轮转和重复切换在 5.4 的 `make grade` 调度用例中验证。

**验证范围：** 本节证明模块三可以编译、链接，并完成第一次上下文切换；不能单独证明多线程轮转、无就绪线程时的 `idleproc` 回退以及多次往返切换的稳定性，这些由 5.4 的完整运行与评分测试补充。

---

### 5.4 完整运行验证

环境：WSL Ubuntu，`riscv64-unknown-elf-gcc` 10.2，QEMU 4.1.1（`~/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64`）。本节在功能模块一、二、三均已合入的情况下运行，验证三人模块的协同。

**(1) `make qemu` 完整启动与运行**

下面的输出节选省略了 OpenSBI 启动横幅，保留从设备树初始化到内核 panic 的完整内核日志。

```text
DTB Init
HartID: 0
DTB Address: 0x82200000
Physical Memory from DTB:
  Base: 0x0000000080000000
  Size: 0x0000000008000000 (128 MB)
  End:  0x0000000087ffffff
DTB init completed
(THU.CST) os is loading ...

Special kernel symbols:
  entry  0xc020004a (virtual)
  etext  0xc0203e24 (virtual)
  edata  0xc0209030 (virtual)
  end    0xc020d4e4 (virtual)
Kernel executable memory footprint: 54KB
memory management: best_fit_pmm_manager
physical memory detected: [0x0000000080000000, 0x0000000088000000)
physical memory managed:  [0x0000000080000000, 0x0000000088000000), npage: 557056
reserved: [0x0000000080000000, 0x000000008040e000) (firmware+kernel+Page array)
free:     [0x000000008040e000, 0x0000000088000000), 31730 pages
pages: 0xffffffffc020e000 (physical 0x000000008020e000), 32768 descriptors, kernel end 0x000000008020d4e4
check_alloc_page() succeeded!
check_pgdir() succeeded!
check_boot_pgdir() succeeded!
use SLOB allocator
kmalloc_init() succeeded!
check_vma_struct() succeeded!
check_vmm() succeeded.
++ setup timer interrupts
this initproc, pid = 1, name = "init"
To U: "Hello world!!".
To U: "en.., Bye, Bye. :)"
kernel panic at kern/process/proc.c:208:
    process exit!!.

Welcome to the kernel debug monitor!!
Type 'help' for a list of commands.
```

![完整运行输出](./images/c_full_run.png)

对输出逐段说明：

- `DTB Init`、物理内存范围与 `npage`、`free pages` 说明设备树解析和物理内存初始化正常，Best Fit 分配器登记的可用页范围与内核保留区正确；
- `check_alloc_page() succeeded!` 验证物理页分配与释放，`check_pgdir() succeeded!`、`check_boot_pgdir() succeeded!` 验证功能模块一的 `page_insert()`、`page_remove()`、引用计数、权限与 TLB 处理；
- `use SLOB allocator`、`kmalloc_init() succeeded!`、`check_vma_struct() succeeded!`、`check_vmm() succeeded.` 说明 `kmalloc` 和虚拟内存管理结构初始化正常，为 `alloc_proc()` 申请进程控制块提供了条件；
- `++ setup timer interrupts` 说明实验三的时钟中断处理仍在正常工作；
- `this initproc, pid = 1, name = "init"`、`To U: "Hello world!!".`、`To U: "en.., Bye, Bye. :)"` 说明功能模块二创建的 `initproc` 已被功能模块三正确调度并执行；
- 最后的 panic 是 `do_exit()` 占位实现导致的预期行为，`init_main()` 返回后由 `kernel_thread_entry` 调用，不影响本次验证的结论。

**(2) `make grade` 评分测试**

在相同环境下执行：

```bash
make grade
```

结果为：

```text
  -check PMM:                                OK
  -check Process Create:                     OK
  -check Schedule:                           OK
Total Score: 30/30
```

![make grade 评分输出](./images/c_grade.png)

| 用例 | 分值 | 验证内容 | 关键证据 |
| --- | --- | --- | --- |
| check PMM | 10 | `page_insert()`/`page_remove()` 的新建映射、替换物理页、同页重复插入、权限更新、引用计数和幂等删除 | `[Test PMM] PASS` |
| check Process Create | 10 | `idleproc` 的 PID、状态、内核栈、父进程、上下文，以及 `initproc` 的 PID、父进程、可运行状态和首次执行现场 | `[Test Process Create] PASS` |
| check Schedule | 10 | 通过 `kernel_thread()` 创建两个测试线程，反复设置 `need_resched` 并调用 `schedule()`，验证两个线程都能被轮转执行 | `[Test Schedule] PASS`，评分输出 `OK` |

`check Schedule` 用例尤其重要：它不是只验证一次 `idleproc → initproc` 的切换，而是创建两个额外内核线程并多次调度，覆盖了 `schedule()` 的轮转选择、`idleproc` 回退、`proc_run()` 的重复切换以及 `switch_to` 的往返保存/恢复。两个线程都置位了各自的运行标志，说明轮转调度确实让两个线程都获得了 CPU。

**验证结论：** `make qemu` 的完整启动日志表明物理内存管理、页表映射、虚拟内存管理、时钟中断、线程创建与调度能够协同工作；`make grade` 的三个用例全部通过，总分 **30/30**。功能模块三的调度与上下文切换在单线程首次运行和多线程轮转两种场景下均得到验证。

---

## 六、实验总结与收获

### 对操作系统的理解

| 实验知识点 | 对应 OS 原理及联系 |
| --- | --- |
| 虚拟地址空间与 VMA | OS 用 `mm_struct`/`vma_struct` 描述进程的地址空间布局，把“程序看到的地址”与物理页解耦；本实验建立 `mm`、`vma` 框架并做自检，但内核线程共享内核地址空间，`copy_mm()` 仍是占位。差异是本实验采用预映射，不涉及按需分页和页面置换。 |
| Sv39 多级页表 | OS 通过多级页表把虚拟页号逐级翻译为物理页号；本实验按 `PDX1 → PDX0 → PTX` 三级查找，页表项 8 字节、每页 512 项。差异是本实验主要使用 4 KiB 页，未完整支持大页叶子和 Sv48。 |
| `get_pte()` 与页表页分配 | OS 在建立映射时按需分配页表页；本实验用 `create` 参数把“查找”和“必要时分配中间页表”合并，失败返回 `NULL`。差异是分配失败时不回滚已经挂入页表树的上级页表页。 |
| `page_insert()`/`page_remove()` | OS 建立或撤销虚拟地址到物理页的映射，并维护引用计数与 TLB；本实验处理同页重复映射、替换旧页、权限更新和幂等删除。差异是单核环境，没有并发保护。 |
| 物理页引用计数 | OS 用引用计数管理共享页的生命周期；本实验 `page_ref_inc/dec` 决定何时 `free_page()`，同一物理页可以被多个虚拟地址映射。差异是没有对象缓存、反向映射等机制。 |
| TLB 与 `sfence.vma` | OS 修改页表后必须使旧翻译失效；本实验在 `page_insert`/`page_remove` 和 `proc_run` 切换 `satp` 后调用 `tlb_invalidate()`/`flush_tlb()`。差异是未使用 ASID，切换地址空间时整体刷新。 |
| `satp` 与页表基址 | OS 用 `satp` 指向当前页表根；本实验 `proc->pgdir` 保存物理基址，按 Sv39 的 `mode=8` 写入。差异是 `lsatp()` 为 Sv32 遗留，不能用于 Sv39。 |
| 进程控制块 PCB | OS 用 PCB 保存进程状态、PID、页表、上下文、父子关系等；本实验用 `proc_struct`，并通过 `proc_list` 和 `hash_list` 管理。差异是字段简化，没有文件描述符、信号等资源。 |
| 进程与线程 | OS 中进程是资源分配单位，线程是调度单位；内核线程共享内核地址空间，属于同一个内核进程。本实验的 `kernel_thread()` 通过 `CLONE_VM` 创建共享地址空间的线程。差异是没有用户态和独立的用户地址空间。 |
| 进程状态机 | OS 进程有新建、就绪、运行、阻塞、终止等状态；本实验用 `PROC_UNINIT/RUNNABLE/SLEEPING/ZOMBIE`，调度器只选 `RUNNABLE`。差异是 `do_exit()` 尚未实现，`ZOMBIE` 和资源回收未完整使用。 |
| 内核栈与陷阱帧 | OS 为每个线程准备内核栈，陷阱帧保存被打断的执行现场；本实验每个线程 2 页内核栈，`tf` 放在栈顶。差异是内核栈固定大小，缺少溢出保护。 |
| 上下文切换 | OS 切换时保存和恢复当前执行现场；本实验只保存 `ra`、`sp`、`s0`～`s11` 这些被调用者保存寄存器，调用者保存寄存器由编译器处理。差异是不保存浮点/SIMD 状态，也没有地址空间 ASID 优化。 |
| 新线程首次运行 | OS 新进程通常从内核入口返回用户态；本实验通过 `forkret → forkrets → __trapret → kernel_thread_entry` 恢复预先构造的陷阱帧，再调用线程函数。差异是内核线程不经过用户态。 |
| 调度器与轮转 | OS 调度器按策略从就绪队列选择线程；本实验用 `proc_list` 做 FIFO 轮转，当前线程为 `idleproc` 时从链表头开始，找不到就绪线程时回退 `idleproc`。差异是没有优先级、时间片和公平性度量。 |
| 临界区与中断保护 | OS 用关中断、自旋锁等保护共享数据；本实验 `local_intr_save/restore` 保存并恢复 `sstatus.SIE`，支持嵌套。差异是只保护单核，不能替代多核互斥。 |
| PID 分配与哈希表 | OS 用唯一 PID 标识进程并支持快速查找；本实验 `get_pid()` 扫描 PID 空间，`hash_proc()` 按 PID 散列。差异是没有 PID 命名空间和回收策略。 |
| 预映射与按需分页 | OS 常用按需分页，在缺页异常时才分配物理页；本实验一次性建立所有映射，不涉及缺页处理。差异是本实验不能节省物理内存，也没有页面置换。 |
| `fork`/`clone` 语义 | OS 的 `fork` 复制地址空间，`clone` 可按标志共享资源；本实验 `do_fork()` 创建内核线程，`copy_mm()` 只保留接口。差异是尚未实现用户态 `fork/exec/wait`。 |


### AI 协作开发的经验

1. **平台约定必须写进提示词，不能依赖 AI 的默认习惯。** 本实验最容易出错的不是算法，而是地址形态和平台接口。AI 很容易沿用 x86 ucore 的 `lcr3`、`lsatp`、`boot_pgdir_va` 等写法；在 RV64/Sv39 下，`pgdir` 必须是物理基址，`satp` 必须按 `mode=8` 编码，`lsatp()` 是 Sv32 遗留、不可使用。A 的 `page_insert()` 要区分物理页号与内核虚拟地址，B 的 `alloc_proc()` 要写 `boot_pgdir_pa`，C 的 `proc_run()` 要直接写 `satp`，三者必须使用同一套约定。因此 `[RELY]` 部分要固定平台、地址形态和跨模块契约，否则每个模块单独看都“合理”，合起来却对不上。

2. **spec 不能只写正常流程，失败路径和边界才是 AI 最容易漏的部分。** `page_insert()` 的同页重复映射、替换旧页和分配失败，`page_remove()` 的未映射地址，`do_fork()` 的内核栈/控制块回滚，`schedule()` 的无就绪线程与 `idleproc` 特判，都属于正常流程之外的边界。AI 倾向于生成“happy path”代码，本实验的多次迭代（A 的引用计数与 TLB、B 的失败回收、C 的 `idleproc` 遍历起点）都说明：把这些边界写成 `Case 1/2/3` 和 `Requirements`，生成结果才稳定。

3. **顺序和状态不变量要写成硬约束。** `proc_run()` 中 `current` 必须先于 `switch_to` 更新，否则 `forkret()` 会读到 `idleproc->tf == NULL`；`do_fork()` 中 PID、哈希、链表和计数必须在同一临界区内，`wakeup_proc()` 放在临界区之后；`schedule()` 必须先清 `need_resched` 再选择线程。这些不是风格问题，而是正确性问题。AI 生成的代码可能在表面上完成切换，却在特定时序下崩溃；提示词要把顺序写成明确步骤，代码审查也要逐条核对。

4. **验证要分层，结论必须与证据匹配。** 单文件编译通过只能说明语法和声明一致，不能说明链接成功；链接成功不能说明第一次切换成功；`make qemu` 能输出 initproc 的信息，也不能说明多线程轮转正确。本实验按“目标文件编译 → 完整 `make` → `make qemu` → `make grade`”逐层验证：C 先用三个 `.o` 确认模块三接口，再用完整启动日志确认首次切换，最后用 `make grade` 的 Schedule 用例确认两个测试线程都能轮转。报告中每个“通过”都应附上对应命令和输出，不能用推断代替证据。

综上，Lab4 的提示词设计需要同时固定平台约定、跨模块契约、边界条件和执行顺序；验证则要覆盖编译、链接、运行和评分四个层次。通过本次实验，我们不仅理解了虚拟内存和内核线程的实现，也实践了如何把 OS 原理转化为可检查、可验证的 spec 与 prompt，并通过真实运行结果约束 AI 生成的代码。

---

