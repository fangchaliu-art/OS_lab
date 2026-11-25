# Challenge 1：Copy-on-Write 机制提示词

[PROMPT]

**任务：**

在当前 RISC-V ucore Lab5 工程中实现 Copy-on-Write（COW）机制，把普通 `fork` 的“复制全部用户页”改成“父子共享只读页，写时再复制”，并完成测试用例和设计报告。

需要修改或新增的实际文件：

- `code/kern/mm/mmu.h`：增加 COW 的软件 PTE 位。
- `code/kern/mm/vmm.h`：增加 `do_pgfault()` 声明。
- `code/kern/mm/vmm.c`：新增 `do_pgfault()`，并把 `dup_mmap()` 的 `copy_range(..., share = 0)` 改成 `share = 1`。
- `code/kern/mm/pmm.c`：修改 `copy_range()` 的 `share == true` 分支，实现父子共享和写保护。
- `code/kern/trap/trap.c`：在 `exception_handler()` 中增加 `CAUSE_STORE_PAGE_FAULT` 分支，把用户态写故障交给 `do_pgfault()`。
- `code/user/cowtest.c`：新增 COW 正确性测试。
- `code/user/cowbug.c`：新增 Dirty COW 模拟测试，用于配合可选的缺陷版本。
- `report/Challenge1_COW_设计报告.md`：新增 Challenge 1 的设计报告，包含状态转换、测试记录和 Dirty COW 分析。

**操作要求：**

- 必须修改项目中的实际文件，不能只展示代码。
- 先阅读当前工程代码，确认 `do_fork()`、`copy_mm()`、`dup_mmap()`、`copy_range()`、`do_exit()`、`trap()` 和 `exception_handler()` 的真实实现，再动手修改。
- 新增代码使用 `//challenge1 2410826` 注释说明用途；不要删除或改写其他成员已有的 `lab5 function1/2/3/4` 注释。
- 不修改功能模块四的 `do_exit()`、`do_yield()`、`do_kill()`，不改变 `mm_count` 的语义，不修改 `do_wait()` 的回收责任。
- 不实现按需调页、swap、mmap、文件系统或 page cache；本任务只实现 COW，不为普通未映射页或缺页做通用处理。
- 不把内核态的写故障当成 COW 处理；内核态写故障仍然打印现场并终止。
- 不修改 Makefile、`tools/grade.sh`、`tools/test_lab5.c` 来伪造测试结果；新增用户程序由现有 Makefile 自动发现即可。
- 缺陷版本必须是可选的编译开关，默认构建保持正确行为；缺陷版本只用于报告和演示，不能成为默认内核。
- 不提交或推送 Git。

**输出要求：**

完成后说明：

1. 每个文件的修改位置和关键代码。
2. COW 的页状态、引用计数和状态转换。
3. 测试用例、实际运行命令和真实输出。
4. Dirty COW 的成因、ucore 为什么不能原样复现、缺陷版本模拟了什么后果、正确实现如何阻止。
5. 实现过程中遇到的实际 bug 和修复方式；没有 bug 也要如实说明。
6. 区分单文件编译、完整链接、基础 lab5 回归和 COW 运行测试；没有实际执行的测试不能写成通过。

[RELY]

## 1. 当前框架与模块边界

以实际代码为准核对：

- `code/kern/process/proc.c`：`do_fork()`、`copy_mm()`、`do_exit()`、`do_wait()`。
- `code/kern/mm/vmm.c`、`vmm.h`：`dup_mmap()`、`exit_mmap()`、`find_vma()`、`mm_count`、`vma_struct`。
- `code/kern/mm/pmm.c`、`pmm.h`：`copy_range()`、`get_pte()`、`page_insert()`、`page_remove_pte()`、`alloc_page()`、`page2kva()`、`pte2page()`。
- `code/kern/mm/mmu.h`、`memlayout.h`：`PTE_V/R/W/X/U`、`PTE_SOFT`、`PGSIZE`、`PTSIZE`、`USERBASE/USERTOP`、`USER_ACCESS`。
- `code/kern/trap/trap.c`：`exception_handler()`、`trap()`、`trap_in_kernel()`。
- `code/kern/process/proc.h`：`do_fork()`、`do_exit()`、`proc_struct`。
- `code/libs/error.h`、`libs/riscv.h`、`libs/list.h`。
- `code/user/` 下现有用户程序和 `user/libs/` 用户库。

当前已经具备、**不要重做**的部分：

- `do_fork()` 已创建 PCB、内核栈、地址空间和 trapframe，并把子进程接入进程树、PID 散列表和调度器。
- `copy_mm()` 已在普通 `fork` 路径调用 `setup_pgdir()` 和 `dup_mmap()`，并在成功后执行 `mm_count_inc()`。
- `dup_mmap()` 已按 VMA 顺序用 `vma_create()` + `insert_vma_struct()` 复制 VMA，并对每个 VMA 调用 `copy_range()`；当前传入 `share = 0`。
- `copy_range()` 已实现 `share == false` 的整页复制路径；`share == true` 目前只是把同一物理页按原权限映射到子进程，没有写保护，也没有 COW 位。
- `do_exit()` 已实现：先把 `current->pgdir` 切回 `boot_pgdir_pa`，再 `mm_count_dec()`，只有计数归零才 `exit_mmap()`、`put_pgdir()`、`mm_destroy()`；`do_wait()` 只回收内核栈和 PCB。
- `page_insert()` 会对新页 `page_ref_inc()`，替换旧映射时通过 `page_remove_pte()` 递减旧页引用并在归零时释放；`page_remove_pte()` 已经正确处理共享页，不需要为 COW 另写一套回收。
- `exception_handler()` 目前只处理 `CAUSE_USER_ECALL`，其他异常打印 trapframe 后 `panic`。
- `trap()` 在 `trap_dispatch()` 返回后，对用户态 trap 检查 `PF_EXITING` 并调用 `do_exit(-E_KILLED)`，再检查 `need_resched` 并调度。

## 2. 地址空间与页表语义

- Sv39：`PDX1`（1 GiB）、`PDX0`（2 MiB）、`PTX`（4 KiB）；`PGSIZE = 4096`、`PTSIZE = 2 MiB`、`PDSIZE = 1 GiB`。
- `mm->pgdir` 是页表内核虚拟地址；`proc->pgdir` 是页表物理地址。
- `get_pte(pgdir, la, 0)` 在任一级页表不存在时返回 `NULL`；`create = 1` 时分配缺失的页表页。
- `page_insert(pgdir, page, la, perm)` 先 `page_ref_inc(page)`；若该地址已有映射，则通过 `page_remove_pte()` 递减旧页引用并在归零时释放，然后写入 `pte_create(page2ppn(page), perm)` 并刷新 TLB。
- `pte_create()` 会自行设置 `PTE_V`；`PTE_USER` 是 `R | W | X | U | V`，不包含软件位。
- `load_icode()` 建立的 VMA：代码段 `VM_READ | VM_EXEC`，数据段 `VM_WRITE | VM_READ`，用户栈 `VM_READ | VM_WRITE | VM_STACK`；栈底保护页在 VMA 内但没有建立页映射。
- 这个 lab5 没有按需调页，`load_icode()` 已经把所有用户页预先映射；COW 只处理“页已存在、但 PTE 被写保护”的写故障，不处理“页不存在”的缺页。

## 3. 异常与返回约定

- `CAUSE_STORE_PAGE_FAULT` 的值是 `0xf`；故障虚拟地址在 `tf->tval`，不是 `tf->epc`。
- 页面故障不能像 `ecall` 那样把 `tf->epc` 加 4；处理成功后要重新执行触发故障的那条指令。
- `trap_in_kernel(tf)` 通过 `tf->status & SSTATUS_SPP` 判断故障来自内核态还是用户态。
- 用户态写故障处理失败时，可以设置 `current->flags |= PF_EXITING`，让 `trap()` 在返回前调用 `do_exit(-E_KILLED)`；不要在 `exception_handler()` 里直接 `schedule()`。
- 内核态写故障不属于 COW，打印现场并 `panic`。
- `CAUSE_LOAD_PAGE_FAULT` 不需要按 COW 处理；COW 页本身是可读的，读不会触发写保护。若出现未映射页的读故障，保持现有的终止行为即可。

## 4. 引用计数与回收责任

- 每个进程的 `mm` 通过 `mm_count` 计引用；`do_exit()` 负责递减，归零时释放整个地址空间。
- 每个物理页的 `page->ref` 通过 `page_insert()` / `page_remove_pte()` 维护；不要在新的故障处理里手工 `page_ref_dec()`，否则容易重复释放。
- `fork` 共享一个物理页时，父进程原有的映射保留一个引用，子进程的 `page_insert()` 再增加一个引用。
- 写故障复制时，新页通过 `page_insert()` 建立映射；旧页的引用由 `page_insert()` 内部的 `page_remove_pte()` 递减。
- 进程退出时，`exit_mmap()` 逐个 `unmap_range()`，共享页只有在最后一个映射撤销、`page->ref` 归零时才真正释放。

## 5. 测试与运行环境

- 用户程序放在 `code/user/` 下，现有 Makefile 会自动把它编译成 `obj/__user_<name>.out`。
- 运行用户程序使用当前工程已有的 QEMU 方式：优先 `make run-nox-<prog>`；如果该目标的 QEMU 参数在当前环境不可用，就使用你们平时运行 `hello`/`forktest` 的同一条 `qemu-system-riscv64` 命令，只把 `TEST` 的程序名换成 `cowtest`，本实验不使用 spike。
- 基础 lab5 的 `make grade` 必须继续通过；`tools/grade.sh` 不会自动包含 `cowtest`，COW 测试需要单独运行并记录输出。
- 运行测试至少重复 3 次，避免把调度顺序偶然性当成通过。
- 如果当前环境没有可用的 QEMU，必须明确说明哪些测试没有执行，不能写“预计通过”。

## 6. Dirty COW 背景

- Dirty COW 是 Linux 的 CVE-2016-5195，根因是私有只读文件映射在“解除 COW”和“回收私有副本”之间存在竞争，最终把写操作落到 page cache，导致只读文件被修改，并可能被用于提权。
- ucore 没有文件系统、page cache、文件映射、`/proc/self/mem` 和用户线程，因此不能原样复现 Linux 的竞争过程。
- 本任务要求的是“在 ucore 的 COW 实现中模拟这个错误和解决方案”，合理做法是模拟它的**后果类别**：写只读映射、写共享页、嵌套 fork 丢失 COW 位；并在报告中明确说明这是后果级模拟，不是原样复现 Linux 的 race。
- 不要下载或运行 `https://dirtycow.ninja/` 上的 Linux 漏洞利用程序；该网站只作为背景参考。

[GUARANTEE]

必须实现或修改以下接口，保持已有签名不变：

```c
/* kern/mm/mmu.h */
#define PTE_COW 0x200

/* kern/mm/vmm.h */
int do_pgfault(struct mm_struct *mm, uint32_t error_code, uintptr_t addr);

/* 已有接口，修改行为 */
int copy_range(pde_t *to, pde_t *from, uintptr_t start,
               uintptr_t end, bool share);
int dup_mmap(struct mm_struct *to, struct mm_struct *from);
void exception_handler(struct trapframe *tf);
```

新增文件：

```c
/* code/user/cowtest.c：正确 COW 的功能测试 */
/* code/user/cowbug.c：Dirty COW 后果模拟测试 */
/* report/Challenge1_COW_设计报告.md：设计报告 */
```

交付结果必须满足：

- 普通 `fork` 后父子进程共享用户页，任一方写入时各自获得独立副本，另一方看不到修改。
- 只读代码段的写操作被拒绝，进程被终止，代码内容不被修改。
- 嵌套 `fork` 后 COW 状态仍然正确，不会把 COW 页误当成普通只读页。
- 共享页的引用计数正确，进程退出后没有页泄漏，`init check memory pass.` 仍然出现。
- 基础 lab5 的 `forktest`、`forktree`、`exit`、`spin`、`badarg` 等行为不被破坏。
- 缺陷版本只有在显式定义 `COW_SIMULATE_DIRTYCOW` 时启用，默认构建不受影响。
- 不新增与 COW 无关的公开接口，不修改 `do_exit()` / `do_yield()` / `do_kill()` 的语义。

[SPECIFICATION]

## 1. PTE_COW 与 copy_range 的共享分支

**Pre-Condition：**

- `copy_range()` 由 `dup_mmap()` 在 `fork` 路径调用，`from` 是父进程页表，`to` 是子进程页表。
- `from` 和 `to` 的页表页已经建立；`start`、`end` 按 `PGSIZE` 对齐并满足 `USER_ACCESS`。
- `share == true`。

**Post-Condition：**

- 对每个有效用户页：
  - 可写页（`PTE_W`）或已经是 COW 页（`PTE_COW`）：父子 PTE 都改成 `W = 0, COW = 1`，映射同一物理页，物理页引用计数增加 1。
  - 只读页（代码段）：父子 PTE 都保持原只读权限，不设置 COW 位，映射同一物理页，引用计数增加 1。
- 父进程原来的可写 PTE 被改写为只读/COW，父进程 TLB 中对应项失效。
- 失败时返回 `-E_NO_MEM`，不手工调整引用计数，由 `copy_mm()` 的 `exit_mmap()` 清理已经建立的子进程映射。

**Case 1：可写数据页。** `perm = *ptep & PTE_USER` 含有 `PTE_W`；`shared_perm = (perm & ~PTE_W) | PTE_COW`；子进程先 `page_insert()`，父进程再 `page_insert()`。

**Case 2：已经是 COW 页。** `perm` 可能不含 `PTE_W`，但 `*ptep & PTE_COW` 为真；仍使用 `shared_perm = (perm & ~PTE_W) | PTE_COW`，保证嵌套 `fork` 不丢 COW 位。

**Case 3：只读代码页。** `perm` 不含 `PTE_W`，`*ptep` 不含 `PTE_COW`；`shared_perm = perm`，父子只读共享，不设置 COW。

**Case 4：页表不存在或 PTE 无效。** 沿用现有 `copy_range()` 的 `PTSIZE` / `PGSIZE` 前进逻辑，不分配数据页。

**Case 5：目标映射失败。** 返回 `-E_NO_MEM`；不释放已经由 `page_insert()` 建立的映射，交给 `copy_mm()` 的 `exit_mmap()`。

**Requirements：**

- `PTE_COW` 定义为 `0x200`（`PTE_SOFT` 的 bit 9），不能与 `PTE_V/R/W/X/U` 冲突。
- `perm = *ptep & PTE_USER` 会丢掉 `PTE_COW`，因此判断“已经是 COW 页”必须直接看原始 `*ptep`。
- 父进程和子进程都要改 PTE；只改子进程会让父进程继续可写，COW 失效。
- 建议先 `page_insert(to, ...)` 再 `page_insert(from, ...)`：子进程失败时父进程保持原状；父进程失败时子进程映射由 `exit_mmap()` 清理。
- 不手工 `page_ref_inc()` / `page_ref_dec()`；`page_insert()` 已经处理。
- 不修改只读代码页的权限，不给它设置 COW 位。
- 不修改 `share == false` 的复制路径。
- 不在 `copy_range()` 内加 `mm_lock`；锁由 `copy_mm()` 持有。

## 2. dup_mmap 切换到共享模式

**Pre-Condition：** `to` 和 `from` 有效，`to` 的根页表已由 `setup_pgdir()` 建立。

**Post-Condition：** 每个 VMA 的 `copy_range()` 使用 `share = 1`，父子共享页面并按 COW 规则写保护；VMA 的起止地址、`vm_flags`、顺序和 `map_count` 与父进程一致。

**Requirements：**

- 只把 `copy_range(..., 0)` 改成 `copy_range(..., 1)`，不改变 `vma_create()` + `insert_vma_struct()` 的顺序和失败清理约定。
- 失败时仍然返回 `-E_NO_MEM`，由 `copy_mm()` 统一清理。

## 3. do_pgfault

**Pre-Condition：**

- `mm` 是当前用户进程的地址空间，`addr` 是触发写故障的用户虚拟地址。
- 故障来自用户态，且 `mm->pgdir` 已经建立。
- 该页在 `load_icode()` 时已经预先映射，不是按需调页。

**Post-Condition：**

- 成功返回 `0`；触发故障的 PTE 变成可写，COW 位清除，物理页内容对该进程可见且独立。
- 失败返回负错误码；不修改其他进程的 PTE，不泄漏新分配的页。

**Case 1：地址不在任何 VMA 内。** `find_vma()` 返回 `NULL` 或 `vma->vm_start > la`，返回 `-E_INVAL`。

**Case 2：VMA 不可写。** `vma->vm_flags` 没有 `VM_WRITE`（例如代码段），返回 `-E_INVAL`，上层终止进程，不能修改代码页。

**Case 3：页表或 PTE 不存在。** `get_pte(mm->pgdir, la, 0)` 返回 `NULL` 或 `!(*ptep & PTE_V)`，返回 `-E_INVAL`；本实验没有按需调页。

**Case 4：PTE 不是 COW 页。** `!(*ptep & PTE_COW)`，返回 `-E_INVAL`；不能把普通只读页当成 COW。

**Case 5：引用计数为 1。** `page_ref(page) == 1`，说明只剩当前进程映射；原地把 PTE 改成可写、清 COW，执行 `tlb_invalidate()`，不分配新页。

**Case 6：引用计数大于 1。** 分配新页，`memcpy(page2kva(npage), page2kva(page), PGSIZE)`，用 `page_insert(mm->pgdir, npage, la, perm)` 建立新映射；旧页引用由 `page_insert()` 递减。分配或映射失败时释放新页并返回 `-E_NO_MEM`。

**Requirements：**

- `la = ROUNDDOWN(addr, PGSIZE)`；`get_pte()`、`page_insert()` 和 `tlb_invalidate()` 都使用页对齐地址。
- 可写权限取 `perm = (*ptep & (PTE_U | PTE_R | PTE_X)) | PTE_W`，清除 `PTE_COW`；不要丢失 `PTE_U`，不要在 Sv39 下建立只有 `W` 没有 `R` 的叶子。
- `page_ref(page) == 1` 时不要分配新页；`page_ref(page) > 1` 时必须复制，不能原地改成可写。
- 不手工 `page_ref_dec()`；旧页引用由 `page_insert()` / `page_remove_pte()` 处理。
- 不在 `do_pgfault()` 内加 `mm_lock`；单核、trap 处理期间中断关闭，检查、复制和更新是原子的。
- 不处理未映射页、swap 或文件映射；这些不属于本任务。
- 用户态错误返回负错误码，不要 `panic`；内核态错误由 `exception_handler()` 处理。
- 不修改 `do_exit()` 的 `mm_count` 逻辑。

## 4. exception_handler 的写故障分支

**Pre-Condition：** `tf` 指向当前 trapframe，`tf->cause` 为 `CAUSE_STORE_PAGE_FAULT`，`tf->tval` 是故障地址。

**Post-Condition：**

- 用户态写故障交给 `do_pgfault(current->mm, tf->cause, tf->tval)`。
- 成功时返回，`trap()` 恢复现场后重新执行触发故障的指令。
- 失败时打印 trapframe，设置 `current->flags |= PF_EXITING`，由 `trap()` 在返回前调用 `do_exit(-E_KILLED)`。
- 内核态写故障打印现场并 `panic`，不按 COW 处理。

**Requirements：**

- 保留现有的 `CAUSE_USER_ECALL` 分支，不要修改 `syscall()` 的行为。
- 不增加 `tf->epc += 4`；页面故障要重试原指令。
- 不处理 `CAUSE_LOAD_PAGE_FAULT` 为 COW；COW 页可读，读故障不是 COW 的正常路径。
- 不在 `exception_handler()` 内直接 `schedule()`；让 `trap()` 的统一返回路径处理 `PF_EXITING` 和 `need_resched`。
- 需要 `do_pgfault()` 的声明，放在 `kern/mm/vmm.h`，不要在每个文件里重复写 `extern`。

## 5. 测试用例

新增 `code/user/cowtest.c`，至少覆盖：

1. **子写父读。** 父进程 `fork()`，子进程修改一个全局变量并 `exit(0)`；父进程 `waitpid()` 后断言自己的值仍是原值。
2. **父写子读。** 父进程 `fork()` 后修改全局变量，子进程读取自己的副本并断言仍是原值；父子用 `waitpid()` 收尾，不依赖谁先运行。
3. **多子进程。** 连续 `fork()` 多个子进程，每个子进程写入不同的值并断言自己读到的值；父进程等待全部子进程后断言自己的值不变。
4. **嵌套 fork。** 子进程再 `fork()` 一个孙进程，子和孙分别写入并各自断言；父进程最后断言自己的值不变。
5. **代码段写保护。** 子进程尝试写自己的代码段（例如通过 `volatile` 指针写入一个函数地址），正确实现下子进程被终止，父进程 `waitpid()` 得到非零退出码，代码内容不被修改。
6. **退出回收。** 所有子进程退出后，父进程正常返回，最终出现 `cowtest pass.` 和 `init check memory pass.`。

要求：

- 使用 `waitpid()`、`assert()` 和返回值判断，不依赖 `cprintf` 的输出顺序。
- 不靠固定次数的 `yield()` 同步。
- 每个子进程的断言失败会走用户态 `panic`，父进程应能通过退出码发现。
- 测试通过时打印 `cowtest pass.`，不要只打印“看起来成功”。

新增 `code/user/cowbug.c`，用于配合缺陷版本：

- 在正确内核下，父子隔离，父进程读到的值不变。
- 在定义 `COW_SIMULATE_DIRTYCOW` 的缺陷内核下，缺陷路径让共享页被原地改写，父进程能看到子进程的写入，用来模拟 Dirty COW 的“写共享页”后果。
- 该程序只用于报告和演示，不作为默认回归测试。

## 6. Dirty COW 模拟与设计报告

`report/Challenge1_COW_设计报告.md` 至少包含：

1. **设计目标与边界。** COW 只处理已映射页的写保护；不做按需调页、swap、文件映射。
2. **状态转换。** 用表格和 Mermaid/ASCII 图说明 `P-W`、`S-RO`、`RO`、`UNMAPPED` 四种状态，以及 `fork`、读、写故障（引用计数 1 和大于 1）、写只读页、`exit/unmap`、嵌套 `fork` 的转换。
3. **引用计数。** 说明 `page->ref`、`mm_count` 在 `fork`、写故障、`exit` 中的变化，以及为什么不需要修改 `do_exit()`。
4. **关键实现。** `copy_range()` 共享分支、`dup_mmap()`、`do_pgfault()`、`exception_handler()` 的核心逻辑。
5. **测试结果。** 真实命令、真实输出、重复运行结果；区分基础 lab5 回归和 COW 测试。
6. **Dirty COW 分析。** 说明 CVE-2016-5195 的竞争、ucore 缺少的前置条件、缺陷版本模拟的是哪一类后果、正确实现通过哪些检查阻止它；明确这是后果级模拟，不是原样复现。
7. **缺陷版本。** 说明 `COW_SIMULATE_DIRTYCOW` 开关、如何构建、观察到的现象、默认构建如何保持正确。
8. **bug 与迭代。** 实际遇到的编译错误、引用计数错误、TLB 未刷新、嵌套 fork 丢 COW 位等问题及修复；没有遇到也要如实说明。

## 7. 验证要求

按以下顺序验证并记录真实结果：

1. 单文件编译：

```bash
make -B -k obj/kern/mm/vmm.o obj/kern/mm/pmm.o obj/kern/trap/trap.o
```

2. 完整构建：

```bash
make
```

3. 基础 lab5 回归（使用 QEMU）：

```bash
make grade
```

至少确认 `forktest`、`forktree`、`exit`、`spin`、`badarg` 仍然通过，`init check memory pass.` 仍然出现。

4. COW 测试：

```bash
make run-nox-cowtest    # QEMU
# 如果该目标的 QEMU 参数不可用，使用你们平时跑 forktest 的同一条 QEMU 命令
```

至少运行 3 次，记录 `cowtest pass.`、`init check memory pass.` 和每次的子进程数量、嵌套深度。

5. 缺陷版本（可选演示）：

```bash
make DEFS+=-DCOW_SIMULATE_DIRTYCOW=1 run-cowbug
```

记录缺陷版本与正确版本的差异，并在报告中说明这是模拟，不是真实 Linux 漏洞复现。

6. 如果完整链接或运行因环境缺少可用的 QEMU 而无法执行，明确说明阻塞点，不把单文件编译通过写成运行通过，不修改 `tools/grade.sh` 伪造结果。
