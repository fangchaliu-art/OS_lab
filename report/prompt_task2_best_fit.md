# 模块二任务二 Best-Fit 实现与验证提示词

**负责人：2410439-吴佳璐。**

**版本：** v1，2026-10-17。

**状态：** v1 已于 2026-10-17 执行，完成 Best-Fit 实现及本轮验证。下方提示词正文保留原始版本，真实回复、命令和结果记录在文末 `[ACTUAL RESPONSE]`。

[PROMPT]

**任务：** 请在当前 RISC-V ucore 实验二工程中，参考完整 First-Fit 实现，补全 Best-Fit 连续物理内存管理模块，接入内核并实际验证。请修改项目文件，不要只展示代码。算法时空复杂度不作要求，优先保证正确性和可解释性。

**操作范围：** 在 `code/kern/mm/best_fit_pmm.c` 的 `//lab2 function2-2 YOUR CODE` 位置补充实现，保留 `lab2 function2-2` 标记，将该处 `YOUR CODE` 替换为 `2410439`。可以补充必要的前置声明；必须完整保留原有 `basic_check()` 和 `best_fit_check()`，不得修改其函数体、断言或编译条件。在 `code/kern/mm/pmm.c` 的 `init_pmm_manager()` 中启用 `best_fit_pmm_manager`。仅在必要时调整相关头文件或构建接入，保留已有物理内存初始化、First-Fit、SLUB、公共接口和评分工具，不提交或推送 Git。

[RELY]

- 先阅读 `code/kern/mm/default_pmm.c`、`best_fit_pmm.c`、`best_fit_pmm.h`、`pmm.h`、`pmm.c`、`memlayout.h`、`mmu.h` 和 `code/libs/list.h`，再检查 `code/Makefile`、`code/tools/test_lab2.c` 与实际启动流程。
- 根据 `struct pmm_manager` 自行设计内部状态、管理函数及注册结构。页分配返回 `struct Page *` 描述符；`nr_free_pages()` 返回空闲页数，不是空闲块数。
- 原测试直接保存、清空和恢复 `free_list` 与 `nr_free`，两者必须是兼容该操作的可写状态。可以沿用 `free_area_t` 和宏别名。不要增加无法随测试状态操作保持一致的隐藏缓存或索引。
- `best_fit_check()` 会先清空链表、后清零计数；在链表为空但计数尚未清零时，分配仍须失败。测试还会释放一个已分配区域中的子区间，并将相邻已分配页面作为整体释放。
- 可使用现有链表、断言、输出和内存工具；不依赖宿主环境的 `malloc/free`，不改动 `struct Page`。

[GUARANTEE]

定义 `const struct pmm_manager best_fit_pmm_manager`，提供正确的名称和 `init`、`init_memmap`、`alloc_pages`、`free_pages`、`nr_free_pages`、`check` 六个回调；`check` 指向原有 `best_fit_check()`。内部辅助函数使用 `static`。

[SPECIFICATION]

**Pre-Condition：** 按当前内核的单核、串行调用环境实现。初始化时由 `page_init()` 提供合法的保留页区间；正常分配和释放的页数大于零，释放范围内的页面当前均已分配。

**初始化：** `init()` 建立空管理状态；`init_memmap(base, n)` 正确初始化区间内每个页描述符的标志、引用计数和管理信息，将完整区间登记为空闲内存。只有空闲块首页设置 `PageProperty`，首页 `property` 保存连续页数，空闲页总数准确。

**分配：** 在所有不少于 `n` 页的空闲块中选择最小者，分配恰好 `n` 页；同样大小时选择地址较低的块，作为本实现的确定性约定。只有保证按地址升序遍历，或能够确定不存在更低地址的精确匹配时，遇到恰好匹配的块才可以提前结束查找。较大块需要拆分并正确登记余块；已分配页面不得保留空闲块首页标志。没有合适连续块时返回 `NULL`，失败不改变管理状态，不能仅凭空闲页总数判断分配成功。

**释放：** `free_pages(base, n)` 归还指定的连续页面，重置必要的页状态和引用计数，准确增加空闲页数，并合并前后物理相邻的空闲块。既要支持部分区间释放，也要兼容原测试对相邻已分配页面的整体释放；不得强制要求每次释放与某次原始申请完全一一对应。

**一致性：** 空闲链表中各块互不重叠、大小为正、标志正确，各块页数之和等于 `nr_free_pages()`。拆分、合并不能留下重复节点或失效首页；测试结束后资源必须恢复。

**验证：**

1. 记录实际编译器、QEMU 版本及命令。参考报告中已有运行环境，核实本机工具位置，通过命令参数指定 QEMU，不硬编码他人的路径。
2. 先检查两个原测试的函数体与修改前一致，再进行干净构建。在 QEMU 中确认管理器名称为 `best_fit_pmm_manager`，原有测试实际执行，观察 `check_alloc_page() succeeded!`；如通过 `make test` 启用评分输出，先清理旧产物并核对实际编译选项。
3. 在 Best-Fit 配置运行 `make grade`，并回归启动流程中已有的 SLUB 测试。评分工具中的管理器检查不能替代原有分配行为测试；通用成功信息也不能替代管理器名称确认。
4. 若补充测试，使用独立测试函数，不能改动两个原测试。补充用例可以检查不同大小候选块、前后双向合并，以及空闲总量足够但无足够连续块时分配失败。
5. 无法编译或运行时，明确记录阻塞原因和未验证项；不将静态检查、预期输出或旧配置日志写成实际通过。

**回复与报告：** 完成后说明内部结构、各回调职责、选块与合并过程、修改文件及真实验证结果，并回答“本实现是否还有改进空间”，结合查找开销、索引维护成本和小碎片讨论，不必实现全部优化。将本轮实际回复与执行记录追加到本文件 `[ACTUAL RESPONSE]` 后，并据实补充 `report/report.md` 的 4.2.2 和对应验证项。保留实际使用的本版 prompt；后续修订另记版本和原因，不用后来的提示词冒充本轮输入，不预填其他任务的结果。

[END PROMPT]

[ACTUAL RESPONSE]

**第一轮：2026-10-17，执行 v1。** 用户提交了本文件的任务、依赖、接口保证、规格及验证要求。本轮没有修订 `[PROMPT]` 至 `[END PROMPT]` 的原始正文。

**本轮实际回复：**

已实现并启用 [Best-Fit](D:/OS_CS/OS_lab/code/kern/mm/best_fit_pmm.c:6)：按地址维护空闲链表，选择最小合适块，支持拆分及双向合并。两个原有测试完整保留。

验证通过：原测试 **6/6**、SLUB **五组测试**、独立补充测试及四个固定种子的随机对照；框架评分 **20/20**。评分在临时副本中仅转换脚本换行后完成，原评分工具未修改。

实际回复、命令、日志和改进分析已补入[提示词记录](D:/OS_CS/OS_lab/report/prompt_task2_best_fit.md)与[实验报告](D:/OS_CS/OS_lab/report/report.md:334)。

进一步可用大小索引减少线性扫描，但会增加维护成本，并需兼容原测试对状态的直接操作；Best-Fit 仍可能产生小碎片。

**实现与修改记录：**

- `code/kern/mm/best_fit_pmm.c`：在指定标记处补充 `free_area`、插入合并辅助函数、五个管理函数和注册结构，`.check` 指向原有 `best_fit_check()`。
- `code/kern/mm/pmm.c`：仅将当前管理器从 `default_pmm_manager` 切为 `best_fit_pmm_manager`。
- `code/tools/test_best_fit_host.py`：新增独立补充测试，包含真实分配器源文件，使用页位图对照模型验证行为。
- `report/report.md`：填写 4.2.2 的设计、测试、问题处理和改进分析，更新第五部分对应验证项；Buddy 保持待执行。
- `report/logs/task2_best_fit_*`：保存环境、构建、运行、评分、原测试完整性与宿主测试证据。没有修改原测试、First-Fit、SLUB、Makefile 或评分工具，没有提交或推送 Git。

**实际环境：** WSL Ubuntu 24.04；`riscv64-unknown-elf-gcc (SiFive GCC 10.1.0-2020.08.2) 10.1.0`；QEMU 4.1.1；OpenSBI v0.4；GNU Make 4.3；QEMU 内存为 128 MiB。宿主补充测试使用 MinGW GCC 13.2.0。[完整环境输出](logs/task2_best_fit_environment.txt)。

**实际执行命令：** 以下 Linux 命令通过 Windows 端的 `wsl -d Ubuntu --cd ... --exec ...` 执行，输出保存到对应日志。

```bash
cd /mnt/d/OS_CS/OS_lab/code
make clean
make -j2 GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf-
make clean
timeout --signal=TERM 30s make \
  GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- \
  QEMU=/opt/qemu-4.1.1/bin/qemu-system-riscv64 test
```

Windows 宿主补充测试的实际命令为：

```powershell
python code/tools/test_best_fit_host.py --cc E:/Apps/MinGW/bin/gcc.exe
```

首次在原源码目录调用的评分命令如下，因 CRLF 换行返回 2：

```bash
make GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- \
  QEMU=/opt/qemu-4.1.1/bin/qemu-system-riscv64 \
  GRADE_TMP=/mnt/d/OS_CS/OS_lab/report/logs/.task2-best-fit-grade-20261017 grade
```

随后在 PowerShell 中复制源码并仅转换副本评分脚本的换行，核心准备命令为：

```powershell
$gradeSource = 'D:\OS_CS\OS_lab\report\logs\.task2-grade-lf-source'
Copy-Item -LiteralPath code -Destination $gradeSource -Recurse
$stageScript = Join-Path $gradeSource 'tools\grade.sh'
$gradeScriptText = [System.IO.File]::ReadAllText($stageScript)
[System.IO.File]::WriteAllText($stageScript, $gradeScriptText.Replace("`r`n", "`n"), [System.Text.UTF8Encoding]::new($false))
```

在该副本内执行的原评分命令为：

```bash
cd /mnt/d/OS_CS/OS_lab/report/logs/.task2-grade-lf-source
make GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- \
  QEMU=/opt/qemu-4.1.1/bin/qemu-system-riscv64 \
  GRADE_TMP=/mnt/d/OS_CS/OS_lab/report/logs/.task2-grade-lf-build grade
```

第二次评分返回 0。副本中的分配器及 `pmm.c` 与当前源码逐字节相同，评分脚本只差换行。运行结束后清理了本轮创建的临时评分目录；原工作区脚本未改变，直接在相同环境运行仍需要处理该换行问题。

**真实结果与证据：**

| 检查 | 结果 | 原始记录 |
| --- | --- | --- |
| 完整干净构建 | 成功，编译了 Best-Fit、First-Fit 和 SLUB | [build](logs/task2_best_fit_build.txt) |
| 两个原测试保留 | 测试区域完全一致，统一换行后 SHA256 为 `b6c6f8cd0608f21e2710f19ce23847f74c36b68d67dea90e8eef9faacdc9022a` | [integrity](logs/task2_best_fit_integrity.txt) |
| QEMU 原测试 | 确认为 Best-Fit；6/6；`check_alloc_page() succeeded!` | [qemu](logs/task2_best_fit_qemu.txt) |
| SLUB 回归 | 五组 PASS；1875 个大对象后耗尽并恢复 | [qemu](logs/task2_best_fit_qemu.txt) |
| 宿主补充测试 | 确定性用例及 4 个种子各 5000 次随机循环通过 | [host](logs/task2_best_fit_host.txt) |
| 首次评分 | CRLF 导致 shell 语法错误，退出 2 | [grade CRLF](logs/task2_best_fit_grade_crlf.txt) |
| 临时副本评分 | 两项 OK，`Total Score: 20/20`，退出 0 | [grade](logs/task2_best_fit_grade.txt) |
| 副本等价检查 | 分配器和接入源码一致；评分脚本只转换换行 | [grade copy](logs/task2_best_fit_grade_copy.txt) |

QEMU 日志中的 `Runner exit code: 124` 来自 30 秒限时结束原内核的无限空转，此前所有成功标记已经输出。日志检查曾用过宽的 `FAIL` 匹配规则误命中 `failure & recovery`，随后收窄为明确的失败标记并重新检查同一日志，见[检查说明](logs/task2_best_fit_qemu_evidence.txt)。这不是内核测试失败，没有为此修改代码或原有测试。

独立测试的四个种子为 `1`、`2410439`、`324508639`、`12648430`，每个种子执行 5000 次循环；每次运行最后恢复 215 页。原始日志中的 `5000 operations` 随后仅在输出文字上修正为 `5000 iterations`，因为某些循环可能跳过释放；断言和算法未变，原始日志保留当时文字。原测试自身的 `p2` 未使用警告也保留在宿主日志中。宿主测试不涉及真实地址映射，不能替代已单独完成的 QEMU 验证。

**改进与边界：** 分配查找为 `O(F)`，初始化和释放为 `O(n + F)`，统计为 `O(1)`，分配器额外管理空间为常量级。可考虑按大小的平衡树或分级索引，同时处理地址合并和原测试状态恢复；这些优化会增加维护成本，且不能消除所有碎片。本轮没有实现额外优化，也未把非法释放、并发调用或所有输入组合列为已覆盖。完整设计与分析见实验报告 4.2.2。
