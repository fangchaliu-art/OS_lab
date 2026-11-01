# Lab3 练习二提示词：异常处理与集成验证

[PROMPT]

**任务**：
完成 Lab3 练习二的功能模块二，即异常处理与集成验证。目标文件为工程根目录
`lab3/OS_lab/code` 下的 `kern/trap/trap.c`。

实现 `void exception_handler(struct trapframe *tf);`，使内核在收到非法指令异常和
断点异常时能够打印异常信息、按指令长度更新返回地址 `epc`，从而跳过触发指令并继续
执行；对其他未处理的异常打印完整 trapframe 便于调试。该函数的合入还将解除
`trap_dispatch()` 因调用未定义函数而导致的全工程编译阻塞。

**操作要求**：
必须修改项目中的实际文件，而不是仅展示代码。
在 `kern/trap/trap.c` 中 `interrupt_handler()` 之后、`trap_dispatch()` 之前实现
`exception_handler()`，并在函数上方添加注释 `//lab3 function3 2410439`。

`interrupt_handler()` 由另一位成员负责。本任务不得修改它，也不得修改
`print_ticks()`、`trap_dispatch()`、`trap()`、`idt_init()`、`trapentry.S`、
`struct trapframe` 或其他文件。

`exception_handler()` 必须是非 `static` 的外部链接函数：评分测试在另一个编译单元中
用 `extern` 引用它。函数定义必须位于 `trap_dispatch()` 之前，避免隐式声明错误。

**输出要求**：
以工程真实代码为准，使用下列依赖、接口和行为规格完成实现。
完成后说明修改内容、关键约束的满足情况及实际验证结果。
区分单文件编译、完整构建和运行测试；本函数合入后，`trap.c` 应能通过单文件编译，
完整构建应能生成 `bin/ucore.img`。

[RELY]

## 1. 已有文件与接口

- `kern/trap/trap.c`：已包含 `idt_init`、`trap_in_kernel`、`print_trapframe`、
  `print_regs`、`print_ticks`、`interrupt_handler`、`trap_dispatch`、`trap`。
  `trap_dispatch()` 在 `tf->cause` 最高位为 0 时调用 `exception_handler(tf)`，当前
  该函数未定义，导致全工程编译失败。
- `kern/trap/trap.h`：定义 `struct pushregs` 和 `struct trapframe`；`uintptr_t`、
  `intptr_t` 实际定义在 `libs/defs.h`。
- `kern/trap/trapentry.S`：`SAVE_ALL` 将 `sepc`、`scause`、`stval`、`sstatus` 保存
  到 trapframe，`RESTORE_ALL` 从 trapframe 恢复 `sepc` 和 `sstatus` 后执行 `sret`。
  因此处理函数对 `tf->epc` 的修改会在返回时生效。
- `libs/riscv.h`：提供 `CAUSE_*` 异常原因码、`IRQ_*` 中断原因码、`SSTATUS_*` 等定义。
- `tools/grade.sh` 与 `tools/test_lab3.c`：评分测试在另一个编译单元中
  `extern void exception_handler(struct trapframe *tf);`，并检查
  `Illegal instruction at`、`Breakpoint at`、`cause    0x00000014` 等字符串。

## 2. 已有常量和函数语义

`struct trapframe` 中 `epc` 是发生异常时的 `sepc`，即触发异常的指令地址。
`cause` 是 `scause` 的快照，异常时最高位为 0，低 63 位为异常原因码。

`print_trapframe(tf)` 已实现，会输出：

```text
trapframe at %p
  zero ...
  ... (所有通用寄存器)
  status   0x%08x
  epc      0x%08x
  badvaddr 0x%08x
  cause    0x%08x
```

对于未知异常，直接调用 `print_trapframe(tf)` 即可输出 `cause    0x00000014` 等信息。

`trap_dispatch()` 用 `(intptr_t)tf->cause < 0` 判断中断（最高位为 1），否则为异常。
本函数只处理异常路径，不需要处理中断。

`make qemu` 不编译 `tools/test_lab3.c`；该测试只在 `make grade` 时由 `tools/inject.py`
注入到临时树。普通运行看不到 `[Test 1]`、`[Test 2]` 等输出。

## 3. 需要识别的异常原因

`libs/riscv.h` 中已定义：

| 原因码 | 宏 | 处理方式 |
|---|---|---|
| 0x0 | `CAUSE_MISALIGNED_FETCH` | 打印 trapframe |
| 0x1 | `CAUSE_FAULT_FETCH` | 打印 trapframe |
| 0x2 | `CAUSE_ILLEGAL_INSTRUCTION` | 打印 `Illegal instruction at ...`，epc 前进指令长度 |
| 0x3 | `CAUSE_BREAKPOINT` | 打印 `Breakpoint at ...`，epc 前进指令长度 |
| 0x4 | `CAUSE_MISALIGNED_LOAD` | 打印 trapframe |
| 0x5 | `CAUSE_FAULT_LOAD` | 打印 trapframe |
| 0x6 | `CAUSE_MISALIGNED_STORE` | 打印 trapframe |
| 0x7 | `CAUSE_FAULT_STORE` | 打印 trapframe |
| 0x8 | `CAUSE_USER_ECALL` | 打印 trapframe |
| 0x9 | `CAUSE_SUPERVISOR_ECALL` | 打印 trapframe |
| 0xa | `CAUSE_HYPERVISOR_ECALL` | 打印 trapframe |
| 0xb | `CAUSE_MACHINE_ECALL` | 打印 trapframe |
| ≥0xc | 未定义 | 打印 trapframe |

`CAUSE_ILLEGAL_INSTRUCTION` 是非法指令异常，需跳过触发指令。
`CAUSE_BREAKPOINT` 是断点异常（`ebreak` 或 `c.ebreak`），需跳过触发指令。
其他异常和未知异常调用 `print_trapframe(tf)` 并正常返回。

## 4. 指令长度判断与 epc 更新

RISC-V 指令长度由指令低两位决定：

- 低两位为 `11`：4 字节指令（或更长，本实验只考虑 4 字节）；
- 低两位不为 `11`：2 字节压缩指令。

判断方式：读取 `tf->epc` 处的 32 位指令字，检查 `insn & 0x3`：

```c
uint32_t insn = *(uint32_t *)tf->epc;
size_t insn_len = (insn & 0x3) == 0x3 ? 4 : 2;
tf->epc += insn_len;
```

本实验运行在内核态（S 模式），`tf->epc` 指向内核代码段，可安全解引用。

评分测试中：
- 非法指令为 `.word 0x0000007b`，低两位为 `11`，长度 4 字节；
- 断点为 `ebreak`（`0x00100073`），低两位为 `11`，长度 4 字节。

两者都通过 `tf->epc += 4` 跳过。使用上述通用判断可同时覆盖压缩指令场景。

## 5. 函数调用约定和上下文

`exception_handler` 由 `trap_dispatch` 直接调用，正常返回即可；不要执行 `sret`。
本函数通过修改 `tf->epc` 改变返回地址，`RESTORE_ALL` 会把新值写回 `sepc`，
`sret` 从更新后的地址继续执行。

不得修改 `tf->status`、`tf->cause`、`tf->badvaddr` 以及任何通用寄存器槽位。
通用寄存器的保存和恢复由汇编入口完成，C 处理函数不得破坏已保存的现场。

评分测试可能在时钟初始化之前用 `memset` 清零的 trapframe 直接调用本函数
（注入点在第二次 `idt_init()` 之后、`clock_init()` 之前）。因此：

- 不得依赖时钟、`ticks` 或中断状态；
- 不得做与异常类型无关的全局副作用。

本任务不需要动态内存分配，也不新增其他公开接口，但 `exception_handler` 本身必须是
非 `static` 的外部符号。

[GUARANTEE]

必须实现以下 C 接口：

```c
void exception_handler(struct trapframe *tf);
```

交付结果必须满足：

- 函数签名不变，定义在 `kern/trap/trap.c` 中 `interrupt_handler()` 之后、
  `trap_dispatch()` 之前，函数上方有 `//lab3 function3 2410439` 注释；
- 不修改 `interrupt_handler()`、`print_ticks()`、`trap_dispatch()`、`trap()`、
  `idt_init()`、`trapentry.S`、`struct trapframe` 及其他文件；
- 对非法指令异常输出包含 `Illegal instruction at` 的信息，并按指令长度更新 `epc`；
- 对断点异常输出包含 `Breakpoint at` 的信息，并按指令长度更新 `epc`；
- 对其他异常和未知异常调用 `print_trapframe(tf)`；
- 函数为非 `static` 定义，满足外部编译单元的 `extern` 引用；
- 合入后 `trap.c` 单文件编译通过，完整构建生成 `bin/ucore.img`。

[SPECIFICATION]

## exception_handler

**Pre-Condition**：

- `tf` 指向有效 trapframe，`tf->cause` 为 scause 快照，最高位为 0 表示异常，
  低 63 位为原因码。
- `tf->epc` 为触发异常的指令地址，指向内核代码段，可安全读取。
- `print_trapframe(tf)`、`cprintf` 等接口已存在。
- 通用寄存器已由 `SAVE_ALL` 保存在 trapframe 中，本函数不得修改这些槽位。

**Post-Condition**：

- 非法指令异常：输出异常信息，`tf->epc` 增加触发指令的长度，跳过该指令。
- 断点异常：输出异常信息，`tf->epc` 增加触发指令的长度，跳过该指令。
- 其他异常：打印完整 trapframe，正常返回。
- 不修改 `tf->status`、`tf->cause`、`tf->badvaddr` 和通用寄存器槽位。
- `sret` 返回后从更新后的 `epc` 继续执行，不重复触发同一异常。

**Case 1：非法指令异常**

`tf->cause == CAUSE_ILLEGAL_INSTRUCTION`，`tf->epc` 指向 `.word 0x0000007b`。
输出包含 `Illegal instruction at` 的信息，读取该指令并判断长度为 4 字节，
`tf->epc += 4`。返回后 `sret` 跳过非法指令，继续执行后续代码。

**Case 2：断点异常**

`tf->cause == CAUSE_BREAKPOINT`，`tf->epc` 指向 `ebreak` 指令。
输出包含 `Breakpoint at` 的信息，读取该指令并判断长度为 4 字节，
`tf->epc += 4`。返回后 `sret` 跳过断点指令，继续执行后续代码，
通用寄存器保持断点前的值不变。

**Case 3：未知异常**

`tf->cause == 20`（0x14）时，直接调用 `print_trapframe(tf)`，
输出包含 `cause    0x00000014` 的完整现场信息，正常返回。

**Case 4：其他已定义异常**

`CAUSE_FAULT_LOAD`、`CAUSE_USER_ECALL` 等已定义但本实验不专门处理的异常，
调用 `print_trapframe(tf)` 并正常返回，不 panic、不关机。

**Requirements**：

- 必须用 `switch (tf->cause)` 分类，`cause` 是异常原因码（最高位已为 0），
  不需要再掩码。
- 非法指令和断点异常必须按指令长度更新 `tf->epc`，不能固定加 4，也不能不加。
- 指令长度通过读取 `*(uint32_t *)tf->epc` 并检查低两位判断：
  `(insn & 0x3) == 0x3` 时为 4 字节，否则为 2 字节。
- 输出字符串必须包含 `Illegal instruction at` 和 `Breakpoint at`，
  以匹配评分脚本的 `grep -F` 检查；地址格式不限，但建议使用 `0x%08x`。
- 不得修改 `tf` 中除 `epc` 以外的任何字段。
- 不得执行 `sret`，不得修改中断使能状态。
- 未知异常和其他异常统一调用 `print_trapframe(tf)`，不得自行拼凑输出。
- 函数为非 `static`，定义在 `trap_dispatch()` 之前。

## 验证要求

- 单独编译 `trap.c`，确认语法、符号引用和告警情况；合入后不再有
  `exception_handler` 隐式声明错误。
- 完整构建 `make`，确认生成 `bin/ucore.img`，无告警无错误。
- 运行 `make qemu`：确认内核启动后时钟正常工作，约每秒输出一次 `100 ticks`，
  第 3 次后关机（此验证依赖中断处理模块已完成）。
- 运行 `make grade`：
  - `GRADE_LAB3_CONTEXT` 用例输出 `Breakpoint at` 与
    `[Test 1] Trap entry and breakpoint return: PASS (35/35)`；
  - `GRADE_LAB3_ILLEGAL` 用例输出 `Illegal instruction at`、
    `Supervisor software interrupt`、`Unknown interrupt: cause 15`、
    `cause    0x00000014`，以及 `[Test 2] Illegal instruction return: PASS (25/25)`；
  - `GRADE_LAB3_TIMER` 用例输出 `100 ticks` 与 `ticks=200`，以及
    `[Test 3] Sustained supervisor timer: PASS (40/40)`。
- 检查 `epc` 更新逻辑、异常信息输出、未知异常 trapframe 打印，以及返回后不重复
  触发同一异常。
- 报告实际输出，不伪造；没有工具链或 QEMU 时，只报告源码检查和编译结果。
