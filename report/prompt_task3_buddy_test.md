# 模块二任务三 Buddy System 测试提示词

**负责人：2410439-吴佳璐。**

**版本：** v1，2026-10-17。

**状态：** v1 已于 2026-10-17 执行。八组独立测试、后续 SLUB 回归及 Best-Fit 回归通过；阶 15 正向成功仅在模拟描述符中验证，真实 128 MiB 整块物理分配未覆盖。以下原始 prompt 正文保持不变。

[PROMPT]

**任务：** 请根据第一份 Buddy 实现提示词的行为规格和实际生成的模块，独立设计、实现并运行 Buddy System 测试，检验分配器是否正确。请生成实际测试文件、接入当前管理器的检查流程，并解释各项测试覆盖什么行为、能够发现什么错误。时空复杂度不作要求。

**操作范围：** 新建 `code/kern/mm/buddy_test.c`、`buddy_test.h`，声明 `void buddy_test(void)`，从 Buddy 管理器的 `.check` 路径接入。允许为接入测试作最小修改，辅助用例使用 `static`。保留原有 Best-Fit 两个测试、First-Fit、SLUB、物理内存初始化和评分工具，不提交或推送 Git。若发现分配算法缺陷，保留可复现失败用例并报告证据，记录为待修复；不要降低断言、隐藏失败或顺手修改分配算法来让测试通过。

[RELY]

- 先阅读 `report/prompt_task3_buddy.md`、其实际执行记录、实际 `code/kern/mm/buddy_pmm.c` 和 `buddy_pmm.h`，再核对 `pmm.h`、`pmm.c`、页描述符、地址转换和现有 SLUB 测试。若实现尚不存在，应说明前置依赖未满足，不能凭空写成测试通过。
- 测试预期以第一份提示词的接口契约为准。阅读实现是为了明确阶数、布局及接入方式，不能将实现的错误行为当成新的规格。
- 已选契约：`alloc_pages(n)` 对正数向上取整为二次幂页数；`free_pages(base, 原始 n)` 归还实际整块；空闲页计数按实际块大小变化。零页、超大或溢出请求返回 `NULL`，失败保持状态；不要求支持非法指针、重复释放或部分释放。
- `Page *` 是描述符地址，不能把它当作可写物理页的内核地址。数据读写应按工程真实映射转换；当前 `KADDR` 不可用。
- 记录数组和辅助元数据采用容量明确的静态存储，不依赖被测分配器或尚未初始化的 SLUB 保存必要测试记录。压力测试必须有上限和可恢复的清理路径。

[GUARANTEE]

测试通过当前公开分配、释放、统计接口验证行为，必要时辅以结构检查。每个用例有可定位的断言及输出，只有所有必需用例实际执行并通过后，才能打印整体通过。未执行或受环境限制的用例明确标为未覆盖，不能计入全部通过。

[SPECIFICATION]

**Pre-Condition：** 当前管理器确实为 Buddy，物理页初始化已完成，测试串行运行。入口记录空闲页基线；不调用初始化函数重置正在使用的全局分配器。测试不能递归调用当前 `.check`。

**受控测试区域：** 多级合并、非伙伴不合并及不规则初始化需要确定性场景。自行设计安全的隔离方法，优先复用内部管理结构形成独立实例，或从真实空闲内存中获取足够大的独占区域，保存并正确恢复相关管理状态。若公开接口不足以构造场景，允许在 Buddy 模块中加入最小的条件编译测试入口或只读观测接口，但不得改变生产分配、释放算法或暴露可任意改写全局状态的接口。不能只备份一个链表头就假定完整状态已保存。任何实际内存读写都必须位于独占区域内；未注册页不能被伙伴查找或合并纳入可分配范围。

**Case 1 基本分配与大小边界：** 覆盖 1、2、3、4、5 页，以及实际可测的 `2^k-1`、`2^k`、`2^k+1`。验证返回块位于合法范围、符合声明的对齐规则、引用计数等页面状态符合接口约定，空闲页数按实际二次幂块大小变化；使用原始 `n` 释放后恢复。预期取整结果使用明确样例或独立计算，不直接调用被测取整辅助函数充当判据。

**Case 2 逐级分裂：** 在受控的大块中申请较小块，验证分裂后的可用容量、地址范围及存活块互不重叠。继续申请剩余容量以检查余下伙伴是否正确登记，释放后恢复原容量。不能只用一次非空返回断言宣称逐级分裂已覆盖。

**Case 3 伙伴与多级合并：** 从一个确定的大块构造多个小块，分别按不同顺序释放真正的伙伴，验证逐级合并后能再次申请原来的大块，且计数准确。用受控区域避免其他空闲大块掩盖合并失败；不能只凭全局一次大块申请成功判断目标区域完成合并。

**Case 4 不应合并的情况：** 构造同阶、地址相邻但不是伙伴的块，以及伙伴仍被占用的情况，验证不能错误拼成大块。检查仍存活块的内容保持不变。对于多段初始区域，验证不能跨过未登记间隙或保留区合并；应按实现声明的伙伴基准判定关系。

**Case 5 对象独立与数据完整性：** 同时持有不同大小的块，写入不同模式，在请求范围内检查各页及首尾内容。交错分配释放后核对存活块的数据，结合区间不重叠检查发现重复分配、错误合并和越界覆盖。不要修改 `Page` 描述符充当数据写入测试。

**Case 6 失败与恢复：** 测试 `n == 0`、超过可管理容量、接近 `SIZE_MAX` 的请求；验证返回 `NULL`，计数和存活数据不变。构造真实耗尽及空闲总量足够但无所需阶块的场景，验证失败无副作用；释放后再次申请成功。达到记录数组上限但未耗尽时标记该耗尽用例未覆盖，不能把停止循环当成分配失败。

**Case 7 初始区域与最大块边界：** 在隔离状态测试起点相对于高阶块不对齐、非二次幂长度、单页区域、多段区域及最大支持阶边界。验证全部合法页可用且恰好计入一次，不浪费尾页、不越界，不把区域外页面纳入分裂或合并。最大阶无法在真实 RAM 中构造时，明确区分可运行的结构测试与未覆盖的实际页分配场景。

**Case 8 重复分配与回收：** 进行有明确轮数的混合分配和不同顺序释放。每轮清理后检查空闲页数恢复，并在受控区域验证原有大块重新可用。若使用随机序列，使用固定种子并记录，保证失败可复现。

**Post-Condition：** 所有测试申请均被正确归还，必要的隔离状态和页描述符恢复，空闲页数回到入口基线；不得遗留影响后续 SLUB 的分配、链表或元数据修改。

**执行与回归：** 干净构建 Buddy 配置，记录真实工具版本、命令、管理器名称和各用例结果，然后运行已有 SLUB 回归测试。切回 Best-Fit 后清理重建并确认原有测试及评分仍可运行。现有 `make grade` 不包含 Buddy 验收，不能替代上述测试。运行条件不足时记录未运行项；测试失败时保留诊断，不写预期成功日志。

**回复与报告：** 说明每个用例的输入、判定条件、实际结果，以及它能发现的具体错误；区分黑盒行为测试、结构检查和覆盖限制。将本轮实际回复、测试代码位置、命令及关键输出追加到本文件 `[ACTUAL RESPONSE]` 后，并据实补充 `report/report.md` 的 4.2.3 测试部分及对应验证项。保留实际使用的 prompt 和失败迭代记录，不将测试通过描述为对所有输入的形式化证明。

[END PROMPT]

[ACTUAL RESPONSE]

### 第 1 轮实际回复与执行记录（2026-10-17）

**实际输入：** 上方 v1 正文，由用户在第一阶段 Buddy 实现完成后发送。本轮未修改输入规格；先读取了实现提示词与上一轮真实记录、分配器、页描述符和映射、公开接口、SLUB 测试及启动流程，前置依赖已满足。第一轮实现提示词及其历史回复原样保留，没有用本轮结果改写它们。

**本轮实际回复：**

已生成 `buddy_test.c`、`buddy_test.h` 并接入 Buddy `.check`。八组测试全部通过，涵盖大小边界、分裂、三种顺序的多级合并、非伙伴与间隙、数据完整性、失败恢复、不规则初始化和固定种子的 4096 步对照测试。测试后空闲页恢复到入口基线 31503 页，后续 SLUB 五组也通过。

Best-Fit 清理重建后原测试 6/6，SLUB 通过，评分 20/20；评分仅临时副本适配脚本换行。原分配算法的 13 个函数体保持不变，本轮未发现可复现的分配算法缺陷。

覆盖限制已写入日志和报告：最大阶 15 的正向成功只使用模拟描述符检验，真实 128 MiB 整块分配未覆盖；有限场景通过不等于对所有输入的形式化证明。实现说明、各用例判据和真实执行记录已补充至[实验报告 4.2.3](report.md)。未提交或推送 Git。

**新增与修改位置：**

- [buddy_test.c](../code/kern/mm/buddy_test.c)：新增 `buddy_test()`、八组独立行为测试、静态记录和独立位图判据。辅助函数均为 `static`。
- [buddy_test.h](../code/kern/mm/buddy_test.h)：声明测试入口和受限隔离/只读检查接口。
- [buddy_pmm.c](../code/kern/mm/buddy_pmm.c)：仅增加活动实例选择、隔离入口、只读状态摘要及 `.check` 包装接入；原结构断言保留，原 13 个生产管理函数及辅助函数体未改。
- [report.md](report.md)：4.2.3 增加第二阶段测试、覆盖表、实际结果及限制；第五节更新当前验证状态。
- 本文件及 `report/logs/task3_tests_*`：记录原版输入、实际回复、代码核对、运行环境、编译、QEMU 与评分输出。

`pmm.c` 的管理器选择、`buddy_pmm.h`、First-Fit、Best-Fit 原两个测试、SLUB、公共接口、`struct Page`、物理初始化、Makefile 和评分工具均未改动。测试与隔离入口仅在 `PMM_USE_BUDDY` 配置下启用，默认仍为 Best-Fit。

**隔离与恢复方法：**

真实场景先经公开接口取得 64 页独占块，保存全部 64 项完整页描述符，用第二个静态 Buddy 状态登记受控子区域。生产实例原地保留，只切换活动指针；不复制链表头、不重新初始化生产分配器。隔离回调返回后恢复完整描述符和生产实例，使用原始 64 页参数归还整块并检查计数。成功路径还要求隔离实例所有登记页均已空闲，并运行结构检查；用例失败返回时也恢复隔离上下文。不可安全恢复的畸形返回或 allocator panic 保留诊断并停止，不输出成功。

真实数据使用 `page2pa(page) + va_pa_offset`，在数值范围、注册、对齐、页状态和不重叠验证后才写入原始请求页范围。高阶全局测试依据当前 `page_init()` 登记连续空闲尾区的事实，用入口基线计算原始空闲区下界，避免错误返回被拿去写内核保留区。未登记间隙的页标志、引用和块长也在前后核对。

最大阶另用静态 `Page[32768]`，保存并临时切换 `pages/npage`，结束后恢复。此回调只读写描述符与管理元数据，不调用数据地址转换或写入模拟物理地址。辅助存储不使用 SLUB 或被测分配器。当前 ELF 中模拟描述符数组为 1310720 字节、64 项快照为 2560 字节、独立 Buddy 状态为 98576 字节；[符号记录](logs/task3_tests_buddy_symbols.txt)。

**独立判据与覆盖：**

| 用例 | 本轮实际输入及判定 | 结果 |
| --- | --- | --- |
| 1 基本/边界 | 16 个小请求 `1,2,3,4,5,7,8,9,15,16,17,31,32,33,63,64`；真实高阶 `k=6..14` 的三边界，共 26 次成功和 `16385` 页的一次预期失败；独立乘二取整、完整描述符/计数/原参数回收 | PASS |
| 2 逐级分裂 | 64 个唯一单页覆盖全位图，额外一页真实返回 `NULL`，释放后同区域重新可分配 64 页 | PASS |
| 3 多级合并 | 地址升序、降序及乘 17 模 64 三种顺序释放单页；各轮必须恢复原区域的 64 页块 | PASS |
| 4 不应合并 | 四页中释放偏移 1/2，验证相邻非伙伴不能组成两页；释放 0 后 0/1 可以合并；另测试 `{0,1},{2,1}` 的未登记间隙 | PASS |
| 5 数据独立性 | 同时申请 `{1,3,5,2,7,1}` 页，用记录、页号和字节位置生成模式，交错释放复用后全请求区逐字节核对 | PASS |
| 6 失败恢复 | `0,32769,SIZE_MAX,SIZE_MAX-1,2^63,2^63-1`；64 页真实耗尽；32 个空闲单页但无 2 页块；核对计数、摘要、存活数据及恢复 | PASS |
| 7 初始化/最大阶 | `{3,29}`、`{17,1}`、乱序分段 `{41,7},{2,9},{20,13}`、乱序邻接 `{32,32},{0,16},{16,16}`；逐页恰好一次。模拟描述符另覆盖阶 0..15 的 46 个有效边界请求 | PASS；阶 15 实际整块物理分配未覆盖 |
| 8 有界混合序列 | 种子 `0x02410439`、32×128 步；独立页位图判断是否存在对齐空段，不强求某一返回顺序；每轮回收后原 64 页块可用 | PASS |

这些用例分别能够发现取整和扣页错误、余块丢失/重复、合并层数不足、误合并非伙伴或存活块、数据覆盖、失败路径破坏、初始化丢尾页/越界以及长序列泄漏。详细说明见报告的八组覆盖表。

黑盒检查使用公开分配、释放、统计接口及真实内存数据；结构断言和只读摘要作为补充。摘要逐字段计算，不读取填充字节，忽略检查临时覆盖图；64 位摘要存在理论碰撞，不是状态严格等价的证明。随机序列每步核对存活页首尾，每 16 步及每次释放前全字节核对；确定性数据用例完整检查请求区域。

**真实工具版本及命令：**

本轮实际复核 `riscv64-unknown-elf-gcc (SiFive GCC 10.1.0-2020.08.2) 10.1.0`、QEMU 4.1.1 和 GNU Make 4.3。运行显示 OpenSBI v0.4、128 MiB RAM，见[环境原始输出](logs/task3_tests_environment.txt)。版本命令为：

```powershell
wsl -d Ubuntu --exec /home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf-gcc --version
wsl -d Ubuntu --exec /opt/qemu-4.1.1/bin/qemu-system-riscv64 --version
wsl -d Ubuntu --exec make --version
```

以下命令在 `/mnt/d/OS_CS/OS_lab/code` 中，通过 `wsl -d Ubuntu --cd /mnt/d/OS_CS/OS_lab/code --exec ...` 执行。工具路径仅作为本机命令参数，没有写入 Makefile：

```bash
make clean
make -j2 V= GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- \
  DEFS=-DPMM_USE_BUDDY
timeout --signal=TERM 60s make \
  GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- \
  QEMU=/opt/qemu-4.1.1/bin/qemu-system-riscv64 DEFS=-DPMM_USE_BUDDY qemu
/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf-nm -S bin/kernel

make clean
timeout --signal=TERM 30s make V= \
  GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- \
  QEMU=/opt/qemu-4.1.1/bin/qemu-system-riscv64 test
/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf-nm -S bin/kernel
```

Buddy 首次编译退出码为 0，实际编译选项含 `-DPMM_USE_BUDDY`；[构建日志](logs/task3_tests_buddy_build.txt)。两个运行包装命令分别在 60 秒、30 秒后返回 124，因为原内核测试完后继续空转。实际通过依据是超时前出现全部成功输出和正确管理器、没有 panic 或明确 FAIL，而不是把超时码当成通过。

**Buddy 原始关键输出：**

```text
memory management: buddy_pmm_manager
[buddy_test] start, baseline free pages = 31503
[buddy_test] case1 real boundaries 2^k +/- 1, k=6..14: PASS
[buddy_test] case1 (basic, rounding and real size boundaries): PASS
[buddy_test] case2 (all split remainders and true exhaustion): PASS
[buddy_test] case3 (six-level merge in three release orders): PASS
[buddy_test] case4 (non-buddies, occupied buddies and unregistered holes): PASS
[buddy_test] case5 (full requested-page data and interleaved reuse): PASS
[buddy_test] case6: genuine exhaustion at 64/64 pages (extra request returned NULL)
[buddy_test] case6 (invalid, overflow, exhausted and fragmented failures): PASS
[buddy_test] case7: orders 0..15 descriptor-only boundaries: PASS
[buddy_test] case7 (irregular/multiple regions and max-order descriptors): PASS
[buddy_test] NOT COVERED: actual order-15 (32768-page) physical allocation; firmware/kernel reserve part of the 128 MiB RAM
[buddy_test] case8: seed=0x02410439, rounds=32, operations/round=128
[buddy_test] case8 (fixed-seed bitmap-oracle allocation/recovery): PASS
[buddy_test] 8/8 required groups passed (coverage exclusions listed above); baseline restored to 31503
check_alloc_page() succeeded!
[slub_test] start, baseline free pages = 31503
[slub_test] case1 (basic & size boundaries): PASS
[slub_test] case2 (object independence): PASS
[slub_test] case3 (in-page reuse & cross-page): PASS
[slub_test] case4 (reclaim): PASS
[slub_test] case5: exhaustion reached after 984 big objects
[slub_test] case5 (failure & recovery): PASS
[slub_test] ALL CASES PASSED
```

[Buddy 完整运行日志](logs/task3_tests_buddy_qemu.txt)还记录了每个隔离场景的结构检查。开始和结束的全局空闲页都为 31503，后续 SLUB 通过。与第一阶段 31853 相比少 350 页，是本轮新增测试静态存储及代码扩大保留区所致；本轮实际自由区为 `[0x804f1000,0x88000000)`，不是把上一轮日志当成本轮基线。

**Best-Fit 回归和评分：** 清理重建后实际编译带 `-D ucore_test`、无 Buddy 宏；运行名称为 `best_fit_pmm_manager`，原测试 `grading: 6 / 6 points`，SLUB 五组 PASS，1875 个大对象后真实耗尽并恢复基线 31878。见[本轮运行日志](logs/task3_tests_best_fit_qemu.txt)；[默认 ELF 符号](logs/task3_tests_best_fit_symbols.txt)没有 Buddy 状态或测试代码。

沿用前两轮已确认的 CRLF 环境适配：仅在独立临时源码副本中转换评分脚本换行，原脚本保持不变。实际核心准备命令为：

```powershell
$testGradeSource = 'D:\OS_CS\OS_lab\report\logs\.task3-tests-grade-source'
Copy-Item -LiteralPath 'D:\OS_CS\OS_lab\code' -Destination $testGradeSource -Recurse
$testGradeScript = Join-Path $testGradeSource 'tools\grade.sh'
$testGradeText = [IO.File]::ReadAllText($testGradeScript).Replace("`r`n", "`n")
[IO.File]::WriteAllText($testGradeScript,$testGradeText,[Text.UTF8Encoding]::new($false))
wsl -d Ubuntu --cd /mnt/d/OS_CS/OS_lab/report/logs/.task3-tests-grade-source --exec make GCCPREFIX=/home/luoisa/riscv-toolchain/bin/riscv64-unknown-elf- QEMU=/opt/qemu-4.1.1/bin/qemu-system-riscv64 GRADE_TMP=/mnt/d/OS_CS/OS_lab/report/logs/.task3-tests-grade-build grade
```

执行前核对副本中的 Buddy、测试、Best-Fit、`pmm.c`、Makefile 和注入工具与工作区逐字节一致，评分脚本除换行外相同；[一致性记录](logs/task3_tests_grade_copy.txt)。评分退出码为 0，[实际评分日志](logs/task3_tests_best_fit_grade.txt)为：

```text
  -check page init:                          OK
  -check best fit:                           OK
Total Score: 20/20
```

评分临时构建目录由原流程清理；源码副本在确认绝对路径位于 `report/logs` 内且无重解析点后清理。评分只证明 Best-Fit 回归，不能替代本轮 Buddy 八组测试。

**审查、迭代与未覆盖项：**

编译前只读审查发现测试自身 `case1_global` 在成功申请后、检查失败时可能遗漏全局分配的释放。测试实现改为保留原指针/页数、统一清理后传播失败；畸形或非独占返回则先诊断并停止，避免危险读写。随后补充隔离间隙描述符前后检查。这些修改发生在第一次构建前，没有编造失败运行日志，没有修改分配算法来使测试通过，也没有降低断言。首次实际构建及本轮所有运行均通过，未发现可复现的算法缺陷，无算法待修复项。

完整性证据包括[修改前文件摘要](logs/task3_tests_protected_before.json)、[修改前 Buddy 源码](logs/task3_tests_allocator_before.c.txt)、[生产函数对照](logs/task3_tests_algorithm_integrity.txt)和[最终完整性核对](logs/task3_tests_integrity.txt)。[运行证据核对](logs/task3_tests_runtime_evidence.txt)验证八组 Buddy、五组 SLUB、原 Best-Fit 及评分标记。

最大阶 15 的实际 32768 页物理分配未覆盖：128 MiB RAM 中包含固件、内核、页描述符和测试存储，不能将这些保留区当作独占可用页；仅用模拟描述符做正向边界检查，并验证真实 16385 页请求取整后失败。高阶真实成功请求检查全部页描述符及请求首尾字节，未全字节扫描所有高阶内存；64 页受控请求则完整检查。随机部分只使用一个固定种子和有限 4096 步；非法指针、重复/部分释放及并发不属于约定输入。测试通过不是对所有输入的形式化证明。
