# Challenge 1：Copy-on-Write 机制设计报告

**负责人：2410826-马伊汀**

## 0. 修改清单

| 文件 | 位置 | 修改内容 |
|---|---|---|
| `kern/mm/mmu.h` | PTE 定义区 | 新增软件位 `PTE_COW = 0x200`（bit 9，硬件不可见） |
| `kern/mm/vmm.h` | `dup_mmap` 声明之后 | 新增 `do_pgfault()` 声明 |
| `kern/mm/vmm.c` | `dup_mmap()` | `copy_range(..., 0)` 改为 `share = 1` |
| `kern/mm/vmm.c` | `dup_mmap()` 之后 | 新增 `do_pgfault()` |
| `kern/mm/vmm.c` | `user_mem_check()` | `write = 1` 时先把范围内的 COW 页解开，避免内核写用户页触发内核态故障 |
| `kern/mm/pmm.c` | `copy_range()` 的 `share == true` 分支 | 父子共享物理页并写保护，设置 `PTE_COW` |
| `kern/trap/trap.c` | `exception_handler()` | 新增 `CAUSE_STORE_PAGE_FAULT` 分支 |
| `user/cowtest.c` | 新增 | COW 正确性功能测试（5 个显式场景 + `init check memory pass.` 的内存回收检查） |
| `user/cowbug.c` | 新增 | Dirty COW 后果模拟测试，配合缺陷开关 |

所有新增代码均带 `//challenge1 2410826` 注释；未删除或改写其他成员的 `lab5 function1/2/3/4` 注释；未修改 `do_exit()`、`do_yield()`、`do_kill()`、`do_wait()`、`mm_count` 语义及 `tools/grade.sh`。

## 1. 设计目标与边界

- 普通 `fork` 不再整页复制用户空间：可写页父子共享并写保护，首次写入时才分配私有副本。
- 只读代码页保持普通只读共享，不设置 COW 位；对代码段的写操作被拒绝并使进程终止。
- 只处理“页已映射、PTE 被写保护且带 `PTE_COW`”的写故障；本实验没有按需调页、swap、mmap、文件系统或 page cache，因此缺页、未映射页一律返回 `-E_INVAL`。
- 嵌套 `fork` 必须保留 COW 状态，不能把 COW 页当成普通只读页。
- 引用计数完全交给 `page_insert()` / `page_remove_pte()` 维护，`do_pgfault()` 不手工增减。
- 不改变 `do_exit()` 中 `mm_count` 的递减与释放规则；COW 只影响 `page->ref`，不影响 `mm_count`。

## 2. 页状态与转换

### 2.1 四种页状态

| 状态 | PTE | page->ref | 含义 |
|---|---|---|---|
| **P-W** | `W=1, COW=0` | 1（通常） | 进程私有可写页 |
| **S-RO** | `W=0, COW=1` | ≥ 1（共享时通常 ≥ 2） | 带 COW 位的只读映射；共享者退出后可能只剩 1 个引用 |
| **RO** | `W=0, COW=0` | ≥ 1 | 普通只读页（代码段），共享但不 COW |
| **UNMAPPED** | `V=0` | — | 未建立映射（含栈底保护页） |

`PTE_COW` 是软件位，RISC-V 硬件忽略它；判断“已经是 COW 页”必须直接看原始 `*ptep`，因为 `perm = *ptep & PTE_USER` 会丢掉该位。

### 2.2 状态转换

| 当前状态 | 事件 | 条件 | 结果状态 | 动作 |
|---|---|---|---|---|
| P-W | `fork` | 父 PTE 可写 | 父、子均为 S-RO | 子先 `page_insert`，父再 `page_insert`；`page->ref +1`；两边 `W=0, COW=1` |
| S-RO | `fork`（嵌套） | 父 PTE 带 COW | 父、子仍为 S-RO | 用原始 `*ptep` 检测 COW，保留 COW 位；`ref +1` |
| RO | `fork` | 代码页 | 父、子仍为 RO | 不设 COW，仅共享；`ref +1` |
| S-RO | 读 | — | S-RO | 无动作（COW 页本身可读） |
| S-RO | 用户写故障 | `page->ref == 1` | P-W | 原地改 PTE 为可写、清 COW、刷新 TLB，不分配新页 |
| S-RO | 用户写故障 | `page->ref > 1` | 当前进程 P-W，其他共享者仍 S-RO | 分配新页、整页复制、`page_insert` 新映射；旧页引用由 `page_insert` 递减 |
| S-RO | 内核写用户页（如 `do_wait` 写 `code_store`） | `user_mem_check(..., write=1)` 发现 PTE 带 COW | P-W | 写前由 `user_mem_check` 调用 `do_pgfault` 解除写保护或复制 |
| RO | 写 | 代码段 VMA 无 `VM_WRITE` | 进程终止 | `do_pgfault` 返回 `-E_INVAL`，置 `PF_EXITING` |
| UNMAPPED | 写 | 无 PTE / 无 VMA | 进程终止 | 返回 `-E_INVAL`，不做按需调页 |
| P-W / S-RO / RO | `exit` / `unmap` | — | UNMAPPED | `page->ref -1`，归零时释放物理页 |

```
                 fork(可写页)                写故障 ref>1
   P-W  ───────────────────────▶  S-RO  ───────────────────────▶  P-W(新页)
    ▲                               │  │
    │        写故障 ref==1          │  │ fork(嵌套)
    └───────────────────────────────┘  ▼
                                    S-RO(父/子)

   RO(代码) ──fork──▶ RO(共享,ref+1) ──写──▶ 进程被终止(-E_KILLED)
```

### 2.3 实现中的关键点

- `copy_range()` 只对“可写或已 COW”的页设置 `shared_perm = (perm & ~PTE_W) | PTE_COW`；对纯只读代码页使用原 `perm`，保持普通只读共享。
- 父子都要改 PTE：先写子进程页表，再写父进程页表，并使用 `page_insert()` 完成 TLB 失效。父进程对同一页重新 `page_insert()` 时，内部“同页则撤销临时引用”的分支保证引用计数净变化为 0。
- `do_pgfault()` 成功返回 0 后 `tf->epc` 不前进，`trap()` 恢复现场后重新执行触发故障的指令。

## 3. 引用计数

### 3.1 `page->ref`

| 操作 | `page->ref` 变化 |
|---|---|
| 可写页 `fork` | 父页 +0（同页重映射），子映射 +1，最终 1 → 2 |
| 只读代码页 `fork` | 同上，最终 +1 |
| 写故障且 `ref == 1` | 不变 |
| 写故障且 `ref > 1` | 新页 0 → 1；旧页 2 → 1（`page_insert` 内部 `page_remove_pte`） |
| 进程退出 `exit_mmap` | 每个映射 -1，最后一个映射撤销时释放 |

`do_pgfault()` 中没有手工 `page_ref_inc/dec`；复制分支失败时只 `free_page(npage)` 释放本次刚分配、尚未插入的新页，旧页引用由 `page_insert()` 负责。

### 3.2 `mm_count`

- 普通 `fork`：子进程新建 `mm`，`dup_mmap()` 成功后才 `mm_count_inc(mm)`，即子进程持有 1 个引用；父进程的 `mm_count` 不变。
- `CLONE_VM`：`mm_count_inc(oldmm)`，与 COW 无关。
- 进程退出：`do_exit()` 仍是“每进程递减一次，归零才 `exit_mmap/put_pgdir/mm_destroy`”。
- COW 只把多个进程的虚拟地址映射到同一个物理页，`mm_count` 的语义（地址空间引用数）没有改变，因此不需要修改 `do_exit()`。

## 4. 关键实现

### 4.1 `copy_range()` 的共享分支

```c
if ((perm & PTE_W) || (*ptep & PTE_COW))
{
    uint32_t shared_perm = (perm & ~PTE_W) | PTE_COW;

    if (page_insert(to, page, start, shared_perm) != 0)   /* 子进程先 */
        return -E_NO_MEM;
    if (page_insert(from, page, start, shared_perm) != 0) /* 父进程再 */
        return -E_NO_MEM;
}
else
{
    if (page_insert(to, page, start, perm) != 0)          /* 只读代码页 */
        return -E_NO_MEM;
}
```

`perm` 来自 `*ptep & PTE_USER`，不含 `PTE_COW`；因此嵌套 `fork` 判断 COW 必须使用原始 `*ptep`。

### 4.2 `dup_mmap()`

只把每个 VMA 的 `copy_range(..., 0)` 改为 `copy_range(..., 1)`，VMA 的创建顺序、失败清理约定不变。

### 4.3 `do_pgfault()`

```c
la = ROUNDDOWN(addr, PGSIZE);
vma = find_vma(mm, la);
if (vma == NULL || vma->vm_start > la)   return -E_INVAL;
if (!(vma->vm_flags & VM_WRITE))         return -E_INVAL;
ptep = get_pte(mm->pgdir, la, 0);
if (ptep == NULL || !(*ptep & PTE_V))    return -E_INVAL;
if (!(*ptep & PTE_COW))                  return -E_INVAL;

page = pte2page(*ptep);
perm = (*ptep & (PTE_U | PTE_R | PTE_X)) | PTE_W;
if (page_ref(page) == 1) {               /* 原地解除写保护 */
    *ptep = pte_create(page2ppn(page), perm);
    tlb_invalidate(mm->pgdir, la);
    return 0;
}
npage = alloc_page();                    /* 复制私有副本 */
if (npage == NULL) return -E_NO_MEM;
memcpy(page2kva(npage), page2kva(page), PGSIZE);
if (page_insert(mm->pgdir, npage, la, perm) != 0) {
    free_page(npage);
    return -E_NO_MEM;
}
return 0;
```

### 4.4 `exception_handler()`

- `CAUSE_STORE_PAGE_FAULT` 且用户态：调用 `do_pgfault(current->mm, tf->cause, tf->tval)`，成功则直接返回并重试原指令；失败打印现场并置 `PF_EXITING`，由 `trap()` 统一调用 `do_exit(-E_KILLED)`。
- 不增加 `tf->epc += 4`。
- 不处理 `CAUSE_LOAD_PAGE_FAULT`。
- 内核态写故障一律打印现场并 `panic`，不按 COW 处理。

### 4.5 内核写用户 COW 页的衔接（`user_mem_check`）

`do_wait()` 通过用户指针 `code_store` 写退出码，此时父进程的用户栈页可能仍是 COW 只读页；若直接写会触发内核态 store page fault。为保持“内核态写故障不按 COW 处理”的规则，`user_mem_check(mm, addr, len, write = 1)` 在通过 VMA 权限检查后，逐页检查该范围内的 PTE：只对“已映射且带 `PTE_COW`”的页调用 `do_pgfault()` 解除写保护或复制。这样后续内核写用户内存不会产生 COW 故障；`user_mem_check()` 的原有权限/范围语义不变，也不会为普通未映射页做按需调页。

## 5. 测试

### 5.1 环境

- WSL Ubuntu，`riscv64-unknown-elf-gcc 10.2.0`。
- QEMU：`qemu-system-riscv64`。
- 运行方式：`make build-<prog>`，再 `make qemu`；内核在测试完成后进入 idle 循环，故命令由 `timeout` 结束，退出码 124 表示超时而非失败，结果以串口输出判定。

### 5.2 单文件编译

```
$ make -B -k obj/kern/mm/vmm.o obj/kern/mm/pmm.o obj/kern/trap/trap.o obj/user/cowtest.o obj/user/cowbug.o
+ cc kern/mm/vmm.c
+ cc kern/mm/pmm.c
+ cc kern/trap/trap.c
+ cc user/cowtest.c
+ cc user/cowbug.c
```

无警告、无错误。

### 5.3 完整构建

```
$ make
...
+ ld bin/kernel
riscv64-unknown-elf-objcopy bin/kernel --strip-all -O binary bin/ucore.img
```

### 5.4 COW 功能测试（重复 3 次）

```
$ make build-cowtest
$ make qemu
```

三次运行的关键输出完全一致（`run 1/2/3` 均为 `cowtest pass.` 且随后 `init check memory pass.`）：

```
kernel_execve: pid = 2, name = "cowtest".
cowtest: starting COW tests.
cowtest: child-write isolation ok.
cowtest: parent-write isolation ok.
cowtest: 4-children isolation ok.
cowtest: nested-fork isolation ok.
cowtest: read-only code write killed child (code -9).
cowtest pass.
init check memory pass.
```

覆盖：子写父读、父写子读、4 个子进程并发写、嵌套 fork（子进程再 fork 一个孙进程，深度 2）、代码段写保护（子进程退出码 `-9`）、全部子进程回收后 `init check memory pass.`。三次运行的子进程数量与嵌套深度相同，未受调度顺序影响。

### 5.5 基础 lab5 回归

单程序运行（均以 `init check memory pass.` 收尾）：

| 程序 | 关键输出 |
|---|---|
| `hello` | `Hello world!!`、`I am process 2.`、`hello pass.` |
| `exit` | `exit pass.` |
| `forktest` | `forktest pass.`（无 `fork claimed to work ... times!`、`wait stopped early`、`wait got too many`） |
| `spin` | `spin may pass.` |
| `badarg` | `badarg pass.` |
| `waitkill` | `wait child 1.` 等输出正常，`init check memory pass.` |
| `forktree` | 树形输出正常，`init check memory pass.` |

`make grade`（临时副本 + 注入测试，未修改评分脚本）：

```
$ make grade
```

在最终源码上完整运行，13 个评分程序全部 `-check result: OK`、`-check output: OK`：

```
badsegment:              OK      faultread:       OK      pgdir:      OK
divzero:                 OK      faultreadkernel: OK      yield:      OK
softint:                 OK      hello:           OK      badarg:     OK
testbss:                 OK      exit:            OK      spin:       OK
forktest:                OK
Total Score: 130/130
```

其中 `exit`、`badarg`、`spin`、`forktest` 覆盖 fork/wait 路径；`forktest` 未出现 `fork claimed to work ... times!`、`wait stopped early`、`wait got too many`，测试结束后均出现 `init check memory pass.`。

## 6. Dirty COW 分析

### 6.1 CVE-2016-5195 的成因

Linux 的私有只读文件映射在写入时本应“解除 COW 并生成私有副本”。漏洞利用 `madvise(MADV_DONTNEED)` 丢弃私有副本、再用 `/proc/self/mem` 写只读映射，在“解除 COW”和“回收私有副本”之间制造竞争窗口，使写入落到 page cache 的共享页，从而修改只读文件，并可能用于提权。

### 6.2 ucore 为什么不能原样复现

ucore 没有文件系统、page cache、文件映射（`mmap` 文件）、`/proc/self/mem`、`madvise`，也没有多线程和同一地址空间内的并发写者；单核上 trap 处理期间中断关闭，检查和复制是原子的。缺少这些前置条件，无法复现 Linux 的竞争过程。

### 6.3 缺陷版本模拟的后果

本任务模拟的是**后果类别**——“写到共享页，使其他进程看到本不该看到的修改”，而不是复现 Linux 的 race。缺陷版 `do_pgfault()` 在 `page_ref(page) > 1` 时没有复制，而是原地把共享物理页在当前进程 PTE 中改成可写并清除 COW；父进程的 PTE 仍指向同一物理页，因此父进程能读到子进程的写入。这是后果级模拟。

### 6.4 正确实现如何阻止

1. `do_pgfault()` 要求 VMA 可写、PTE 有效且带 `PTE_COW`，否则拒绝；
2. `page->ref > 1` 时强制分配新页并整页复制，绝不原地写共享页；
3. `page_insert()` 把当前进程映射切换到私有新页，旧页引用递减；
4. 只读代码页不设 COW，写入被 VMA 权限检查拒绝，进程终止，代码内容不变；
5. 嵌套 `fork` 通过原始 `*ptep` 检测 COW 位并继续传递，避免把 COW 页误当成普通只读页共享。

## 7. 缺陷版本

- 开关：`COW_SIMULATE_DIRTYCOW`，默认不定义，默认构建始终是正确实现。
- 构建与运行（直接指定 DEFS）：

```
$ make -B \
    "DEFS+=-DTEST=cowbug -DTESTSTART=_binary_obj___user_cowbug_out_start \
     -DTESTSIZE=_binary_obj___user_cowbug_out_size -DCOW_SIMULATE_DIRTYCOW=1"
$ make qemu
```

- 正确内核下的 `cowbug`：

```
cowbug: parent still reads 0x11223344, COW isolated the page.
cowbug pass.
init check memory pass.
```

- 缺陷内核下的 `cowbug`：

```
cowbug: parent reads 0x55667788, the child's write reached the shared page!
cowbug: Dirty COW consequence simulated by the defect kernel.
init check memory pass.
```

两者对比说明：正确实现下父子隔离；缺陷实现下子进程的写穿透到共享物理页，模拟了 Dirty COW 的“写共享页”后果。该程序仅用于报告演示，不作为默认回归测试，`tools/grade.sh` 未包含它。

## 8. 实现过程中遇到的 bug 与修复

1. **内核态写用户 COW 页导致 panic（实际运行发现）**
   - 现象：`cowtest` 第一次运行时在 `do_wait()` 的 `*code_store = proc->exit_code` 处触发内核态 store page fault，`panic("store page fault in kernel mode")`。
   - 原因：父进程用户栈页在 `fork` 后是 COW 只读页；`waitpid()` 由内核直接写用户指针，而用户态代码在 `fork` 返回后并未必然先写该页（`-O2` 下可能没有中间栈写），因此内核成为第一个写者。
   - 修复：在 `user_mem_check(..., write = 1)` 中，通过 VMA 检查后对范围内的 COW 页预先调用 `do_pgfault()` 解除写保护/复制。`do_wait()` 在写 `code_store` 前已经调用该检查，因此内核写用户页时不再触发内核态故障；`exception_handler()` 对内核态写故障仍然严格 panic，不把内核故障当作 COW。
   - 验证：修复后 `cowtest`、`exit`、`waitkill`、`forktest` 全部通过，且 `init check memory pass.` 出现。

2. **未发现引用计数或 TLB 错误**
   - 所有共享页映射都通过 `page_insert()` 建立，写故障复制通过 `page_insert()` 替换旧映射；没有手工增减引用计数。嵌套 fork、代码页共享、进程退出回收均通过运行时检查（`init check memory pass.`）。
