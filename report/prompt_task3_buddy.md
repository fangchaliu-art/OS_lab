# 模块二任务三 Buddy System 实现提示词

**负责人：2410439-吴佳璐。**

**版本：** v1，2026-10-17。

**状态：** v1 已于 2026-10-17 执行：Buddy 实现、初步结构检查和已有 SLUB 回归通过，Best-Fit 回归与评分通过。完整 Buddy 行为测试尚未生成或执行，留待另一份提示词。本页保留原始输入正文。

[PROMPT]

**任务：** 请参考当前 RISC-V ucore 物理内存管理框架，从零实现 Buddy System 页分配模块并接入内核。请修改实际工程文件。所有分配块的页数必须为二的幂，支持分配时逐级分裂和释放时逐级伙伴合并；算法时空复杂度不作要求。

**操作范围：** 新建 `code/kern/mm/buddy_pmm.c`、`buddy_pmm.h`，在 `pmm.c` 接入 `buddy_pmm_manager`。可以增加必要的内部元数据和最小构建配置，保留已有物理内存初始化、First-Fit、Best-Fit、SLUB、公共接口、`struct Page` 和原有测试，不修改评分工具，不提交或推送 Git。本轮生成分配模块和有实际断言的初步结构检查，不提前生成下一份提示词负责的完整行为测试。

[RELY]

- 先阅读 `report/prompt_task2_best_fit.md` 及实际 Best-Fit 完成状态，再阅读 `code/kern/mm/pmm.h`、`pmm.c`、`memlayout.h`、`mmu.h`、`default_pmm.c`、`slub.c`、`code/libs/list.h`、`code/libs/defs.h`、`code/Makefile` 和启动流程。若前置任务尚未完成，先报告依赖缺口，不把补做 Best-Fit 算作本轮 Buddy 实现。
- `pmm_manager` 要求 `init`、`init_memmap`、`alloc_pages`、`free_pages`、`nr_free_pages` 和 `check`。`pmm_init()` 在 `page_init()` 后调用当前管理器的 `check`，此时 SLUB 尚未初始化。
- `page_init()` 提供的空闲起点可能没有按大块对齐，总页数也不一定是二的幂。不得把整段容量向下取整后丢弃尾页，或把保留区补入可分配范围。
- 已有 SLUB 使用 `alloc_pages(n)` 获取连续页，随后按同一个原始 `n` 调用 `free_pages(base, n)`，因此 Buddy 必须支持这种配对方式。
- 使用内核已有断言、链表、输出和地址转换工具。不能依赖宿主 `malloc/free`，不能依赖尚未初始化的 SLUB，也不能递归调用被实现的页分配接口获取自身必要元数据。
- 参考资料：[伙伴分配器的一个极简实现](https://coolshell.cn/articles/10427.html)。可以借鉴思想，具体实现须适配本工程。以下向上取整、返回失败和管理器选择规则属于本实验主动选择的设计约定。

[GUARANTEE]

定义 `const struct pmm_manager buddy_pmm_manager`，在头文件声明，完整实现六个回调，内部辅助函数使用 `static`。明确记录最大阶数、各阶块大小、伙伴定位基准，以及使用 `property`、标志或额外元数据的具体含义。

在 `init_pmm_manager()` 中使用编译宏 `PMM_USE_BUDDY` 选择管理器：定义该宏时启用 Buddy，否则保持 Best-Fit。优先通过 Makefile 现有 `DEFS` 传递宏；若已有等价的选择机制则沿用并说明。每次切换配置先清理旧构建产物，确保 Best-Fit 的评分方式仍可复现。

[SPECIFICATION]

**Pre-Condition：** 按单核、串行调用实现。正常释放只接受本分配器返回的、尚未释放的原始块首地址及原始申请页数，不要求支持内部偏移释放、部分释放、重复释放或非法指针。不能直接套用 Best-Fit 原测试的部分释放语义。

**管理结构：** 自行选择按阶空闲链表或树等结构，并解释选择。尽量将管理状态集中组织，便于后续检查和受控区域测试。元数据存储需有明确容量与边界，初始化不能依赖未建立的分配服务。

**初始化：** `init()` 建立空状态。`init_memmap(base, n)` 对合法、互不重叠的保留页区间进行初始化，清除交付页的保留标志、将引用计数置零，按本实现约定初始化首页标志和元数据，再将整个区间拆成满足大小和对齐要求的二次幂块。正确处理不规则起点、非二次幂长度和多段区域；登记后空闲页总数增加恰好 `n`。分裂与合并均不得跨过未登记区域或保留区。

**分配：**

- 对 `n > 0`，取最小的 `2^k >= n` 作为实际分配页数。寻找可用的目标阶或更高阶空闲块；必要时逐级对半分裂，把未使用的伙伴放回管理结构。
- 返回完整、连续且符合本实现对齐规则的块首 `struct Page *`。存活分配不能重叠；分配不应破坏其他存活块数据，不承诺新内存清零。
- 空闲页计数扣除实际分配的 `2^k` 页，记录足够的释放信息。例如申请 3 页实际占 4 页，计数减少 4。
- `n == 0`、取整可能溢出、超过最大支持阶或没有可用块时返回 `NULL`。先做范围检查，再移位或计算地址；失败不修改管理状态，也不破坏已分配数据。

**释放：** 使用原始申请页数调用 `free_pages(base, n)` 时，必须完整回收当时分配的 `2^k` 页。例如 `alloc_pages(3)` 配对 `free_pages(base, 3)`，计数恢复 4 页。只在伙伴位于可管理范围内、同阶且确实空闲时合并，合并后继续向高阶检查；相邻但不是伙伴的块不能合并。计算伙伴时先验证范围，再访问描述符或元数据。

**统计与初步检查：** `nr_free_pages()` 返回可分配的实际物理页总数。`.check` 先提供有实际断言的结构一致性检查，至少检查块大小、对齐、范围、空闲块不重叠，以及各块页数之和等于空闲页计数；不得用空函数、固定成功输出代替检查。明确这只是第一阶段检查，完整行为测试尚待另一份提示词生成。

**编译与运行：**

1. 核实本机工具链与 QEMU，记录版本和真实命令；不硬编码他人的路径。
2. 干净构建 Buddy 配置，实际传入 `PMM_USE_BUDDY`，在 QEMU 启动输出中确认 `buddy_pmm_manager`。记录初步结构检查与已有 SLUB 回归测试的实际结果。
3. 干净构建并回归 Best-Fit 配置，确认原有测试和评分入口仍可用。现有 `make grade` 要求 Best-Fit，不能用它宣称 Buddy 已验收。
4. 分开报告编译、启动、结构检查和 SLUB 回归情况。完整 Buddy 行为测试未生成时明确标记为未完成；工具不可用或运行失败时如实记录，不编造通过日志。

**回复与报告：** 说明内部结构、伙伴定位公式及其对齐基准、分裂与合并、非二次幂请求的释放约定、边界处理和配置切换方法。将本轮实际回复、修改文件与执行记录追加到本文件 `[ACTUAL RESPONSE]` 后，并据实补充 `report/report.md` 的 4.2.3 实现部分及对应验证项。保留实际使用的 prompt，后续修订另记版本；不预填下一阶段测试结果。

[END PROMPT]

[ACTUAL RESPONSE]

### 第 1 轮实际回复与执行记录（2026-10-17）

**实际输入：** 上方 v1 正文，由用户在 Best-Fit 完成后发送。本轮没有修订提示词，也没有执行下一份 Buddy 测试提示词。先核对了上一轮 Best-Fit 的代码、真实运行记录和报告，再读取 PMM、SLUB、页描述符、链表、类型定义、Makefile 及启动顺序；前置任务不存在阻塞。

**本轮回复：**

已完成 Buddy 实现与接入：按阶链表管理，以 `pages[0]` 为基准异或定位伙伴，支持逐级分裂与合并；申请 3 页实际占 4 页，按原参数释放可完整回收。

实际验证：

- Buddy：干净编译、启动、结构断言及 SLUB 五组回归通过。
- Best-Fit：切换后原测试 6/6，SLUB 通过，评分 20/20（仅临时副本适配脚本换行）。

默认仍为 Best-Fit；清理构建后传入 `DEFS=-DPMM_USE_BUDDY` 启用 Buddy。**完整 Buddy 行为测试尚未生成**，留待下一份提示词。本版 prompt、实际回复和执行记录已写入本文件与[实验报告 4.2.3](report.md#423-任务三buddy-system-伙伴分配算法)。

**修改文件：**

- `code/kern/mm/buddy_pmm.c`：新增集中状态、逐页元数据、六个管理回调、伙伴分裂合并和第一阶段结构检查。
- `code/kern/mm/buddy_pmm.h`：声明管理器，定义最大阶 15、描述符容量 32768。
- `code/kern/mm/pmm.c`：增加头文件和 `PMM_USE_BUDDY` 条件选择；未定义宏时维持 Best-Fit。
- `report/report.md`：补充 4.2.3 的实现部分、真实验证与待完成项，更新第五节验证汇总。
- 本提示词文件及 `report/logs/task3_*`：保留本轮输入、实际输出、环境、构建、运行、评分及完整性证据。

没有修改 Best-Fit、First-Fit、SLUB、原测试、公共接口、`struct Page`、页初始化、Makefile 或评分工具。没有创建完整 Buddy 行为测试文件，没有提交或推送 Git。

**实现说明：**

`static struct buddy_state buddy` 集中保存 0 至 15 阶空闲链表、`nr_free`、`nr_managed`、32768 项状态/阶数元数据和结构检查覆盖图。当前 ELF 中该状态占 98576 字节；无需依赖 SLUB、宿主分配器或递归申请物理页。`init` 在 `page_init()` 之前运行，只初始化静态状态。`init_memmap` 清理交付页，按当前下标对齐和剩余页数拆出最大合法二次幂块，登记恰好全部 `n` 页；支持非二次幂长度、不规则起点及多段区域的实现路径，后两者的专门行为用例仍待下一阶段。

索引 `i = page - pages` 相对 `DRAM_BASE=0x80000000`，阶 `k` 的伙伴为 `i ^ ((size_t)1 << k)`，块长 `2^k`。先核对 `npage - nbase` 与容量，再验证伙伴整个块范围；仅已登记、同阶、确实空闲的首页参与合并。因此相邻非伙伴或跨未登记区域不能合并。空闲首页带 `PageProperty`、`property=实际块长`；分配首页不带该标志、`property=原始 n`，实际阶数保存在额外元数据中，内部页的块长和阶数字段清零。

`alloc_pages` 在修改状态前拒绝 0、超容量、空闲总数不足及无合适阶块的请求；取整有上界检查，不会进行超范围移位。找到高阶块后逐级对半拆分，将右半放回链表，保留左半继续处理，按实际块长扣减计数。`free_pages` 核对原始首地址、原始 `n` 和记录阶数，完整回收实际块，逐级寻找并摘除空闲伙伴、更新合并后的首页。`nr_free_pages` 返回实际空闲页数。所有内部辅助函数为 `static`，管理器完整提供六个回调。

`check` 有真实断言：有界遍历链表、验证块长/对齐/范围/双向链接/首页标志/尾页状态，使用覆盖图检查重叠，扫描登记与分配元数据核对页数总计，还检查没有遗留可合并伙伴。它没有分配释放场景用例；当前启动检查发生在 SLUB 初始化之前，因此不能将未实际进入的存活分配检查分支声称为已测试。

**实际环境：** WSL Ubuntu；`riscv64-unknown-elf-gcc (SiFive GCC 10.1.0-2020.08.2) 10.1.0`；QEMU 4.1.1；GNU Make 4.3；启动显示 OpenSBI v0.4 和 128 MiB RAM。[版本原始输出](logs/task3_buddy_environment.txt)。本轮复核如下本机路径，没有将其硬编码进 Makefile：

```powershell
wsl -d Ubuntu --exec /home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf-gcc --version
wsl -d Ubuntu --exec /opt/qemu-4.1.1/bin/qemu-system-riscv64 --version
wsl -d Ubuntu --exec make --version
```

**实际构建与运行命令：** 以下命令的工作目录均为 `/mnt/d/OS_CS/OS_lab/code`，通过 `wsl -d Ubuntu --cd /mnt/d/OS_CS/OS_lab/code --exec ...` 直接传参，输出分别保存至日志：

```bash
make clean
make -j2 GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- \
  DEFS=-DPMM_USE_BUDDY
timeout --signal=TERM 30s make \
  GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- \
  QEMU=/opt/qemu-4.1.1/bin/qemu-system-riscv64 DEFS=-DPMM_USE_BUDDY qemu
make GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- \
  DEFS=-DPMM_USE_BUDDY print-CFLAGS
/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf-nm -S bin/kernel

make clean
timeout --signal=TERM 30s make V= \
  GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- \
  QEMU=/opt/qemu-4.1.1/bin/qemu-system-riscv64 test
/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf-nm -S bin/kernel
```

Buddy 的构建退出码为 0，[构建日志](logs/task3_buddy_build.txt)包含新模块编译和链接。[编译宏与符号记录](logs/task3_buddy_linkage.txt)显示 `-DPMM_USE_BUDDY`、`buddy_pmm_manager` 和大小 `0x18110` 的状态符号。Best-Fit 的 `V=` 运行日志显示实际编译选项带 `-D ucore_test` 且无 Buddy 宏，运行确认管理器切换成功；[默认 ELF 符号](logs/task3_best_fit_symbols.txt)没有 Buddy 状态、回调或管理器，现有链接垃圾回收已裁掉未用模块。

**评分回归命令与换行适配：** 上一轮已确认工作区 `tools/grade.sh` 的 CRLF 会导致 WSL `/bin/sh` 语法错误。本轮沿用已验证的临时副本方式，原评分工具保持不变。在 PowerShell 中执行的核心准备步骤如下：

```powershell
$source = 'D:\OS_CS\OS_lab\report\logs\.task3-grade-lf-source'
Copy-Item -LiteralPath 'D:\OS_CS\OS_lab\code' -Destination $source -Recurse
$gradeScript = Join-Path $source 'tools\grade.sh'
$scriptText = [IO.File]::ReadAllText($gradeScript).Replace("`r`n", "`n")
[IO.File]::WriteAllText($gradeScript, $scriptText, [Text.UTF8Encoding]::new($false))
```

核对副本中 Buddy、Best-Fit、`pmm.c`、Makefile 及注入工具逐字节一致，评分脚本除换行外一致后，实际执行：

```powershell
wsl -d Ubuntu --cd /mnt/d/OS_CS/OS_lab/report/logs/.task3-grade-lf-source --exec make GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- QEMU=/opt/qemu-4.1.1/bin/qemu-system-riscv64 GRADE_TMP=/mnt/d/OS_CS/OS_lab/report/logs/.task3-grade-lf-build grade
```

评分命令退出码为 0，得到 `Total Score: 20/20`。该结果只属于 Best-Fit 回归。评分的临时构建目录由原 Makefile 清理，临时源码副本在验证绝对路径位于 `report/logs` 内且无重解析点后清理。[评分日志](logs/task3_best_fit_grade.txt)、[副本一致性记录](logs/task3_best_fit_grade_copy.txt)。

**Buddy 关键原始输出：**

```text
memory management: buddy_pmm_manager
free:     [0x0000000080393000, 0x0000000088000000), 31853 pages
buddy: structural check passed, 31853 free pages in 10 blocks, 31853 managed pages, max order 15 (full behavior tests pending)
check_alloc_page() succeeded!
[slub_test] start, baseline free pages = 31853
[slub_test] case1 (basic & size boundaries): PASS
[slub_test] case2 (object independence): PASS
[slub_test] case3 (in-page reuse & cross-page): PASS
[slub_test] case4 (reclaim): PASS
[slub_test] case5: exhaustion reached after 995 big objects
[slub_test] case5 (failure & recovery): PASS
[slub_test] ALL CASES PASSED
```

**Best-Fit 回归关键原始输出：**

```text
memory management: best_fit_pmm_manager
grading: 6 / 6 points
check_alloc_page() succeeded!
[slub_test] start, baseline free pages = 31878
[slub_test] case1 (basic & size boundaries): PASS
[slub_test] case2 (object independence): PASS
[slub_test] case3 (in-page reuse & cross-page): PASS
[slub_test] case4 (reclaim): PASS
[slub_test] case5: exhaustion reached after 1875 big objects
[slub_test] case5 (failure & recovery): PASS
[slub_test] ALL CASES PASSED
```

完整输出见 [Buddy QEMU 日志](logs/task3_buddy_qemu.txt)和 [Best-Fit QEMU 日志](logs/task3_best_fit_qemu.txt)。两个内核在成功输出后保持原有无限循环，30 秒超时包装命令均返回 124；依据成功标记及无 panic/明确 FAIL 判定结果，没有将超时当成测试通过。SLUB 五组断言包括释放后空闲计数恢复；耗尽输出确认真实资源不足，而不是只达到测试记录上限。[运行标记复核](logs/task3_runtime_evidence.txt)。

Buddy 元数据和代码扩大了内核保留区，实际可用页数比 Best-Fit 少 25 页。SLUB 大对象的 16 页用户区加 64 字节头后请求 17 页，Buddy 实际占 32 页，因此耗尽对象数为本次观测到的 995，不应沿用 Best-Fit 的 1875。当前 Buddy 自由区起始索引为 915，长度 31853，初步结构检查核对了本次不规则区域登记后的总量。

**完整性与未完成项：** [修改前摘要](logs/task3_buddy_protected_before.json)与[修改后核对](logs/task3_buddy_integrity.txt)确认原版 prompt 正文、已有分配器、原测试、SLUB、公共头文件、页初始化及评分工具保持不变。代码首版完成本轮编译与运行，未发生需要降低断言或改动已有测试的失败。本轮未重新运行上一轮 Best-Fit 的宿主随机测试；其源码未改，已执行原内核测试及评分回归。

完整 Buddy 行为测试依用户要求尚未生成、尚未执行。尤其是受控多段区域、所有阶数的分裂/合并、非伙伴不合并、取整与溢出失败、碎片状态不变等，需要下一份提示词独立验证。结构检查和已有 SLUB 回归不能代替这些专项测试，本记录没有预填其结果。
