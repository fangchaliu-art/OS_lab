
# 分支任务 AI 提示词汇总

**负责人：2410826-马伊汀。**

## 提示词一览

| 编号 | 主题 | 使用阶段 | 对应报告行 | 使用结果 |
| --- | --- | --- | --- | --- |
| A | QEMU Sv39 地址翻译源码理解（静态分析） | 第四章静态分析 | 第④⑥⑩行 | 见 AI 辅助过程记录 3 |
| B | 条件断点一次不命中的排查（首次独立四段式） | 动态调试 | 第③⑤⑨⑩行 | 见 AI 辅助过程记录 6 |
| C | 双 GDB RSP 报文错乱（invalid hex digit 116）定位 | 动态调试 | 第②⑨⑩行 | 见 AI 辅助过程记录 7 |

---

## 提示词 A：理解 QEMU 地址翻译源码

用于静态分析阶段（操作指南第四章），对应报告表格第④行和第⑩行。原文全文：

```text
[PROMPT]

**任务：** 帮助我理解 QEMU 4.1.1 源码中 RISC-V Sv39 虚拟地址到物理地址翻译的完整实现。
我正在进行操作系统课程实验的调试任务，需要先用源码分析建立理解，再用 GDB 验证。

**操作要求：** 只读分析我本地 ~/qemu-4.1.1 下的源码，不修改任何文件。
回答必须基于源码实际内容，标注文件路径和行号；不确定的地方明确说明，不要编造函数名。

**输出要求：** 按调用顺序给出从访存指令到页表遍历的完整调用路径，
指出每个关键分支判断的条件和含义，并解释软件 TLB 查询发生的位置。

[RELY]

- 源码根目录：~/qemu-4.1.1（QEMU 4.1.1 版本）
- 已定位的关键位置：
  - target/riscv/cpu_helper.c:155 get_physical_address()（页表遍历入口）
  - target/riscv/cpu_helper.c:237 三级页表遍历 for 循环
  - target/riscv/cpu_helper.c:435 riscv_cpu_tlb_fill()
  - accel/tcg/cputlb.c:1026-1033 tlb_index + tlb_hit（TLB 查询）
  - accel/tcg/cputlb.c:868 tlb_fill()
- 背景：ucore 内核在 entry.S 中设置 satp 为 Sv39 模式，
  用 1GiB 大页将虚拟地址 0xffffffffc0000000 映射到物理地址 0x80000000。

[GUARANTEE]

请交付：
1. 从访存到完成翻译的完整函数调用路径（含文件名和行号）
2. get_physical_address 中各分支的触发条件（Bare 直通、Sv39 遍历、失败路径）
3. for 循环每一级在做什么：索引如何计算、PTE 如何读取、如何判断叶子节点
4. TLB 查询与页表遍历的先后关系及代码证据

[SPECIFICATION]

**重点解释：**
- cpu_helper.c:184 如何从 satp 寄存器提取页表基址
- cpu_helper.c:237 开始的循环：变量 i、ptshift、idx、pte_addr、pte 在每一级的含义
- cpu_helper.c:256-262 的分支判断：PTE_V、PTE_R/W/X 各种组合分别导致什么结果
- 为什么 ucore 的映射在第一级就结束（大页叶子），不会走完三级
- cputlb.c 中 TLB 命中和未命中分别走什么路径
```

**使用结果**：AI 给出了取指与数据两条调用路径、`get_physical_address` 各分支的行号证据、
"ucore 大页映射 i=0 即返回"的预期（idx=0x1ff、pte=0x200000cf、pa=0x80000000），
并修正了指南 4.2 中"TLB 查询本体"的行号归属（1026-1033 为取指路径，数据路径在 1271）。
详见 AI 辅助过程记录 3。

---

## 提示词 B：条件断点一次不命中的排查（首次独立四段式）

**情景**：条件断点 `break get_physical_address if addr == 0xffffffffc02000d8` 在
"先 `make qemu` 跑起来、后 attach"的场景下一次都不命中；改用无条件断点能命中，
但 addr 多为 0x0 / 0x1000 / 0x80200000 且调用栈显示来自 gdbstub。

```text
[PROMPT]

**任务：** 分析我在用 GDB 调试 QEMU 4.1.1 地址翻译时遇到的异常现象，帮我定位原因并给出解决方案。

**操作要求：** 基于我提供的现象描述和代码位置分析，不修改任何文件。
如果信息不足以定位，列出需要我补充的观察项（比如某个变量的值、某处的输出）。

**输出要求：** 给出最可能的原因排序、验证每个原因的方法、对应的解决操作。

[RELY]

- 环境：WSL2 Ubuntu，QEMU 4.1.1（~/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64，带 debug_info）
- 调试方式：x86 宿主 gdb 用 `sudo gdb -p <PID>` attach 到 QEMU 进程，断点设在 target/riscv/cpu_helper.c 的 get_physical_address（函数体从第 158 行开始）
- 被调试系统：ucore lab2 内核，RISC-V Sv39，entry.S 第 29 行 `csrw satp, t0` 开启分页；内核链接虚拟地址 0xffffffffc0200000，物理装入地址 0x80200000
- 另有一个 riscv64-unknown-elf-gdb 通过 QEMU 的 gdbstub（`-s`，端口 1234）调试客户机内核

[GUARANTEE]

请交付：原因列表（按可能性排序）+ 每项的验证命令/操作 + 解决步骤。

[SPECIFICATION]

**我遇到的现象是：**

1. 先正常运行过一次内核，确认它能完整启动到 `[slub_test] ALL CASES PASSED`，串口输出里有 `satp virtual address: 0xffffffffc0207000`、`satp physical address: 0x0000000080207000`。
2. 想观察 `kern_init` 第一次被取指时的页表翻译。用
   `riscv64-unknown-elf-nm bin/kernel | grep -w kern_init`
   查到 `ffffffffc02000d8 T kern_init`。
3. 用 `make qemu` 启动 QEMU（**没有 -S**），等内核已经跑起来之后，用 `sudo gdb -p <QEMU PID>` attach，设置
   `break get_physical_address if addr == 0xffffffffc02000d8`
   然后 `continue`。结果是断点**一次都没有命中**，内核一直跑到结束。
4. 把条件去掉，改成无条件的 `break get_physical_address` 后能命中，但 `print/x addr` 大多是 `0x0`、`0x1000`、`0x80200000` 这类地址，调用栈是：
   `get_physical_address ← riscv_cpu_get_phys_page_debug ← cpu_get_phys_page_attrs_debug ← cpu_memory_rw_debug ← gdbstub`
   看起来是另一个 gdb 读写客户机内存触发的翻译，不是内核取指。
5. 在 riscv 那个 gdb 里改用 `break *0xffffffffc02000d8` 直接断虚拟地址，同样不命中。
6. 反而在命中时 `print/x env->satp` 的值是 `0x8000000000080207`，说明 Sv39 确实已经开启、地址也确实在走页表翻译。

**我已经做过的尝试：**

- 用 `nm` 反复确认 `kern_init` 就是 `0xffffffffc02000d8`，地址没有写错。
- 确认 `access_type`、`mmu_idx` 等参数在命中时是正常值（`access_type=2` 取指、`mmu_idx=1` S 模式），说明断点本身工作正常。
- 试过在 QEMU 启动后尽可能早地 attach，但 QEMU 从启动到跑完内核只要零点几秒，等 attach 上去时内核已经执行完了。
- 试过 `break kern_entry`，不命中；后来意识到内核入口在物理地址 0x80200000。
- 没有修改任何 QEMU 源码或内核源码。

**我想知道：** 为什么这个条件断点一次都不命中？`0xffffffffc02000d8` 这个地址的翻译在内核生命周期里到底会发生几次？在不能修改 QEMU 源码、只能用 gdb 的前提下，怎样才能可靠地在**第一次翻译发生之前**把断点装好？
```

**使用结果**：AI 用 `-d mmu` 日志实证该地址作为真实取指地址只翻译 1 次，并给出
QEMU 4.1.1 关键细节——`write_satp()`（csr.c:729）仅在 ASID 改变时清 TLB（csr.c:746-749），
真正清 TLB 的是 `sfence.vma`（op_helper.c:144-155）；flush 后第一次取指恰为跳转 kern_init，
因此断点必须装在 CPU 冻结时。采纳方案一（`make debug` 冻结 → 先设断点再 `condition` 挂条件），
实测命中，首次命中预期值清单（addr/access_type=2/mmu_idx=1/idx=0x1ff/pte=0x200000cf 等）
与实测全部吻合。详见 AI 辅助过程记录 6。

---

## 提示词 C：双 GDB 报文错乱（invalid hex digit 116）的定位

**情景**：双 GDB 三终端调试中，终端 3 装/改客户机断点反复报
`Ignoring packet error` + `Reply contains invalid hex digit 116` + `Cannot insert breakpoint`，
断点时有时无；终端 2 同步抓到两条 gdbstub 调试翻译调用栈。本人独立撰写的四段式提示词，原文全文：

```text
[PROMPT]

**任务：** 分析我在用两个 GDB 同时调试 QEMU 4.1.1 时遇到的报文错乱问题，帮我定位原因并给出可靠的操作顺序。

**操作要求：** 基于我提供的现象描述和代码位置分析，不修改任何文件。
如果信息不足以定位，列出需要我补充的观察项（比如某个变量的值、某处的输出）。

**输出要求：** 给出最可能的原因排序、验证每个原因的方法、对应的解决操作。

[RELY]

- 环境：WSL2 Ubuntu，QEMU 4.1.1（~/qemu-4.1.1/riscv64-softmmu/qemu-system-riscv64，带 debug_info）
- 被调试系统：ucore lab2 内核，RISC-V Sv39，内核入口物理地址 0x80200000，kern_init 虚拟地址 0xffffffffc02000d8，entry.S 第 29 行 csrw satp 开启分页
- 终端 1：`make ... debug`，即 QEMU 带 `-s -S`，gdbstub 监听 1234，启动时 CPU 冻结
- 终端 2：x86 宿主 gdb 用 `sudo gdb -p <QEMU PID>` attach 到 QEMU 进程，断点设在 target/riscv/cpu_helper.c 的 get_physical_address（函数体从第 158 行开始）
- 终端 3：riscv64-unknown-elf-gdb 用 `target remote localhost:1234` 连 gdbstub，调试客户机内核

[GUARANTEE]

请交付：原因列表（按可能性排序）+ 每项的验证命令/操作 + 解决步骤。

[SPECIFICATION]

**我遇到的现象是：**

1. 终端 1 起 QEMU（`-s -S`，CPU 冻结）。终端 2 attach 后执行
   `break get_physical_address`
   `continue`
   没有输出、光标卡住，这是预期的。
2. 终端 3 执行 `make gdb`，再输入
   `break *0x80200000`
   此时终端 3 报：
Ignoring packet error, continuing...
Reply contains invalid hex digit 116接着 `info break` 显示没有任何断点，说明这条断点根本没装上。
3. 重试 `break *0x80200000`，这次显示 `Breakpoint 1 at 0x80200000`，但 `continue` 时又报：
Warning:
Cannot insert breakpoint 1.
Cannot access memory at address 0x80200000. 这时切到终端 2，发现它命中了 `get_physical_address`，调用栈是：
get_physical_address
← riscv_cpu_get_phys_page_debug
← cpu_get_phys_page_attrs_debug
← cpu_memory_rw_debug
← target_memory_rw_debug
← gdbstub
addr = 0x80200000, len = 64, is_write = false也就是终端 3 的 gdb 读客户机内存触发了终端 2 的断点。
5. 在终端 2 里 `bt`、`delete`、`continue` 之后再回终端 3 `continue`，有时能恢复；但终端 3 一旦要**再装一个断点**（例如 `break *0x80200034`），又会报同样的 `Reply contains invalid hex digit 116` + `Cannot insert breakpoint 2`。
6. 再看终端 2，这次的调用栈变成了：
get_physical_address
← riscv_cpu_get_phys_page_debug
← cpu_get_phys_page_attrs_debug
← breakpoint_invalidate
← cpu_breakpoint_insert
← gdb_breakpoint_insert
addr = 0x80200034


说明终端 3 装断点这个动作本身又触发了终端 2 的断点。
7. 终端 3 有时还会出现：
Program received signal SIGINT, Interrupt.
0x0000000080200000 in ?? ()


我没有主动给内核发信号，只是按过 Ctrl+C。

**我已经做过的尝试：**

- 单独只用终端 3（不 attach 终端 2）跑同样的 `break *0x80200000`、`continue`，完全正常：断点能装上，内核停在内核第一条指令，`pc=0x80200000`、`satp=0`。所以不是 QEMU 或内核本身的问题。
- 单独只用终端 2 调试 QEMU 也正常，能命中 `get_physical_address`。
- 试过在终端 3 装断点之前，先在终端 2 里 `delete` 并 `continue`；这样能成功一次，但后面终端 3 再装新断点时又复现。
- 试过先让终端 3 装完所有断点，再 attach 终端 2 装断点，能走通一段，但流程一改顺序又出问题。
- 试过在终端 3 里 `delete` 后重新 `break`，有时成功、有时仍报 `invalid hex digit 116`。
- 用 `info registers pc` 确认 gdbstub 连接本身是活的（能读到 `pc=0x1000`）。
- 用 `pgrep` 确认只有一个 QEMU 进程、1234 端口只有一个 gdbstub 客户端。
- 没有修改任何 QEMU 源码或内核源码。

**我想知道：**

- 为什么终端 2 的断点会让终端 3 的 gdbstub 报文变成 `Reply contains invalid hex digit 116`？116 对应什么？
- `cpu_memory_rw_debug` 和 `breakpoint_invalidate` 这两条路径分别在什么时机调用 `get_physical_address`，为什么它们都算"调试操作"而不是客户机执行？
- 两个 gdb 同时工作时，QEMU 的 vCPU 线程、gdbstub 和 ptrace 之间是什么关系？一个 gdb 停住进程时，另一个 gdb 的请求会发生什么？
- 有没有一种确定的操作顺序/规则，能保证终端 3 装断点时终端 2 不会把 QEMU 停住？（前提：两个断点都要用，且不能改 QEMU 源码）
```

**使用结果**：AI 定位出完整根因链——① 116 = ASCII 't'（0x74），是 GDB 的 fromhex() 在
停止应答 `T05thread:` 里遇到的第一个非十六进制字符（GDB bugzilla remote/1695），不是错误码；
② 根因是宿主断点落在 gdbstub 的同步调用链里：终端 3 的 m/Z0 包会走
`cpu_memory_rw_debug` / `breakpoint_invalidate` → `get_physical_address`，终端 2 断点命中后
ptrace all-stop 停住 QEMU 主循环线程，put_packet 永远发不出去，终端 3 默认 2 秒超时后
请求/应答错位；③ QEMU 每次响应 `?` 都会 `gdb_breakpoint_remove_all()`（gdbstub.c:2360），
解释了断点"凭空消失"。
**确定性规程**（已写入操作指南第八章）：终端 3 发任何包时终端 2 必须处于 continue 运行态；
宿主断点改打 `riscv_cpu_tlb_fill`（调试路径不经过它），或保留 `get_physical_address`
但把条件收紧到 `access_type == 2`；用 `set remotetimeout 60` 留足余量；
失步后终端 3 `disconnect` 重连并重设全部断点（`?` 会触发 `gdb_breakpoint_remove_all()`，
只补发单条断点无效）。详见 AI 辅助过程记录 7。


