# Lab3 练习二提示词：中断与时钟处理

[PROMPT]

**任务**：
完成 Lab3 练习二的功能模块二，即中断与时钟处理。目标文件为工程根目录
`lab3/OS_lab/code` 下的 `kern/trap/trap.c`。

实现 `void interrupt_handler(struct trapframe *tf);`，使内核在收到 S 模式时钟中断时能够重设下一次时钟事件、累计 `ticks`、周期性输出统计信息，并在输出次数达到 `SHUTDOWN_AFTER` 后关机；对软件中断和其他中断完成类型区分与提示；对未知中断打印 trapframe 便于调试。

**操作要求**：
必须修改项目中的实际文件，而不是仅展示代码。
只补全 `kern/trap/trap.c` 中 `//lab3 function2` 注释下属于 `interrupt_handler()` 的代码。
保留 `//lab3 function2` 注释，把其中的 `YOUR CODE` 替换为学号 `2410826`。

`exception_handler()` 由另一位成员负责。本任务不得实现它，也不得添加空函数桩或前置声明来绕过编译错误；合并时由该成员在 `trap_dispatch()` 之前提供其定义或原型。

不得修改函数签名、`print_ticks()`、`trap_dispatch()`、`trap()`、`idt_init()`、`trapentry.S`、`struct trapframe` 或其他文件。
若需要 `sbi_shutdown()`，可以增加 `#include <sbi.h>`。
工程中未定义 `SHUTDOWN_AFTER`，在 `TICK_NUM` 附近定义 `#define SHUTDOWN_AFTER 3`，并说明其含义。

`interrupt_handler()` 必须是非 `static` 的外部链接函数：评分测试在另一个编译单元中用 `extern` 引用它。函数定义必须位于 `trap_dispatch()` 之前，或自带可见声明，避免隐式声明错误。

**输出要求**：
以工程真实代码为准，使用下列依赖、接口和行为规格完成实现。
完成后说明修改内容、关键约束的满足情况及实际验证结果。
区分单文件编译、完整构建和运行测试；由于 `exception_handler()` 尚未实现，`trap.c` 的单文件编译会在 `trap_dispatch()` 调用处因隐式声明失败（编译期错误），这不是链接期 `undefined reference`。应如实记录为受其他模块阻塞，不得添加空桩绕过。

[RELY]

## 1. 已有文件与接口

- `kern/trap/trap.c`：已包含 `idt_init`、`trap_in_kernel`、`print_trapframe`、`print_regs`、`print_ticks`、`trap_dispatch`、`trap`。
- `kern/trap/trap.h`：定义 `struct pushregs` 和 `struct trapframe`；`uintptr_t`、`intptr_t` 实际定义在 `libs/defs.h`。
- `kern/driver/clock.h`：声明时钟计数与时钟事件接口：

```c
extern volatile size_t ticks;
void clock_init(void);
void clock_set_next_event(void);
```

- `kern/driver/clock.c`：`ticks` 定义为 `volatile size_t`，初值 0；`timebase = 100000`；`clock_set_next_event()` 调用 `sbi_set_timer(get_cycles() + timebase)`。QEMU 时钟频率 10MHz，因此约每 10ms 一次时钟中断，每秒 100 次。
- `libs/sbi.h`：`void sbi_shutdown(void);`，通过 legacy SBI EID 8 请求 M 模式固件关机；声明为普通 `void`，不是 `noreturn`；在 QEMU + 默认 OpenSBI 下会关机。
- `libs/riscv.h`：提供 `IRQ_*`、`CAUSE_*`、`SSTATUS_*` 等定义。
- `tools/grade.sh` 与 `tools/test_lab3.c`：评分测试在另一个编译单元中 `extern void interrupt_handler(struct trapframe *tf);`，并检查 `Supervisor software interrupt`、`Unknown interrupt: cause 15` 等字符串。

## 2. 已有常量和函数语义

`#define TICK_NUM 100`。`print_ticks()` 输出 `"%d ticks\n"`，参数是常量 `TICK_NUM`，所以每次都输出 `100 ticks`，不是当前 `ticks` 值；`DEBUG_GRADE` 下还会输出 `End of Test.` 并 panic。不得修改。

`ticks` 初值为 0，每收到一次时钟中断必须加 1。

`trap_dispatch()` 用 `(intptr_t)tf->cause < 0` 判断中断；因此中断发生时 `tf->cause` 的最高位是 1，低 63 位是原因码。

`clock_set_next_event()` 必须在每次时钟中断都调用，否则系统只能收到一次时钟中断。

`SHUTDOWN_AFTER` 在工程中未定义，本任务取 3：第 3 次调用 `print_ticks()` 后调用 `sbi_shutdown()`。这发生在 `ticks = 300`，不会与评分测试等待的 `ticks = 200` 竞争。

`make qemu` 不编译 `tools/test_lab3.c`；该测试只在 `make grade` 时由 `tools/inject.py` 注入到临时树。普通运行看不到 `[Test 2]` 等输出。

## 3. 需要识别的中断原因

`libs/riscv.h` 中已定义：

| 原因码 | 宏 | 建议输出 |
|---|---|---|
| 0 | `IRQ_U_SOFT` | `User software interrupt` |
| 1 | `IRQ_S_SOFT` | `Supervisor software interrupt`（评分精确检查） |
| 2 | `IRQ_H_SOFT` | `Hypervisor software interrupt` |
| 3 | `IRQ_M_SOFT` | `Machine software interrupt` |
| 4 | `IRQ_U_TIMER` | `User timer interrupt` |
| 5 | `IRQ_S_TIMER` | 按第 2 节处理 |
| 6 | `IRQ_H_TIMER` | `Hypervisor timer interrupt` |
| 7 | `IRQ_M_TIMER` | `Machine timer interrupt` |
| 8 | `IRQ_U_EXT` | `User external interrupt` |
| 9 | `IRQ_S_EXT` | `Supervisor external interrupt` |
| 10 | `IRQ_H_EXT` | `Hypervisor external interrupt` |
| 11 | `IRQ_M_EXT` | `Machine external interrupt` |
| 12 | `IRQ_COP` | `Coprocessor interrupt` |
| 13 | `IRQ_HOST` | `Host interrupt` |
| ≥14 | 未定义 | `Unknown interrupt: cause %d` + `print_trapframe(tf)` |

`IRQ_S_TIMER` 是时钟中断，按第 2 节处理。
`IRQ_S_SOFT` 是软件中断，输出 `Supervisor software interrupt`。
其他已定义的 `IRQ_*` 输出与类型对应的提示，不要 panic。
未知原因输出 `Unknown interrupt: cause %d`，其中 `%d` 为去掉最高位后的原因码，必须转成 `(int)` 以匹配 `%d`，并调用 `print_trapframe(tf)`。

## 4. 函数调用约定和上下文

`interrupt_handler` 由 `trap_dispatch` 直接调用，正常返回即可；不要执行 `sret`，不要修改 `tf->epc` 或 `tf->status`，时钟中断不应改变返回地址。

`tf` 指向栈上的有效 trapframe；未知中断时 `print_trapframe(tf)` 可安全使用。

评分测试可能在 `clock_init()` 之前用 `memset` 清零的 trapframe 直接调用本函数（注入点在第二次 `idt_init()` 之后、`clock_init()` 之前）。因此：

- 不得在非时钟中断分支依赖 `ticks` 或时钟状态；
- 不得在本函数中做与中断类型无关的全局副作用；
- `IRQ_S_TIMER` 分支可以假定时钟已初始化，因为它只在真实时钟中断时进入。

本任务不需要动态内存分配，也不新增其他公开接口，但 `interrupt_handler` 本身必须是非 `static` 的外部符号。

[GUARANTEE]

必须实现以下 C 接口：

```c
void interrupt_handler(struct trapframe *tf);
```

交付结果必须满足：

- 函数签名不变，定义在 `kern/trap/trap.c` 中 `//lab3 function2` 注释下，且位于 `trap_dispatch()` 之前；
- 不实现、不声明空的 `exception_handler`；
- 对时钟中断完成重设下一次事件、`ticks` 递增、每 `TICK_NUM` 次输出、达到 `SHUTDOWN_AFTER` 次输出后关机；
- 对其他中断和未知中断按上述格式输出；
- 不修改其他函数和文件；
- 函数为非 `static` 定义，满足外部编译单元的 `extern` 引用。

[SPECIFICATION]

## interrupt_handler

**Pre-Condition**：

- `tf` 指向有效 trapframe，`tf->cause` 为 scause 快照，最高位为 1 表示中断，低 63 位为原因码。
- 时钟中断路径上，时钟已由 `clock_init()` 初始化、`sie.STIP` 已使能；其他路径可能在时钟初始化之前被测试直接调用。
- `ticks`、`TICK_NUM`、`print_ticks()`、`clock_set_next_event()`、`sbi_shutdown()` 等接口已存在。

**Post-Condition**：

- 时钟中断：下一次时钟事件已设置；`ticks` 恰好加 1；若 `ticks` 是 `TICK_NUM` 的整数倍，输出一行统计；若累计输出次数达到 `SHUTDOWN_AFTER`，调用 `sbi_shutdown()`。
- 软件中断和其他已定义中断：输出对应提示并正常返回。
- 未知中断：输出原因码并打印 trapframe，正常返回。
- 不修改 `tf` 中保存的返回现场，不改变 Trap 返回地址。

**Case 1：连续时钟中断**

`ticks` 为 100、200、300 时各输出一次 `100 ticks`；第 3 次输出后关机。每次时钟中断都必须重设下一次时钟事件，否则只会收到第一次中断。

**Case 2：软件中断**

`tf->cause` 最高位为 1、原因码为 1 时，输出 `Supervisor software interrupt`，`ticks` 不变。

**Case 3：未知中断**

原因码为 15 时（即使最高位为 0、直接调用 `interrupt_handler`），输出 `Unknown interrupt: cause 15`，并调用 `print_trapframe(tf)`。

**Case 4：其他已定义中断**

`IRQ_U_SOFT`、`IRQ_M_TIMER`、`IRQ_S_EXT` 等已定义原因输出对应类型提示，不关机、不 panic，正常返回。

**Requirements**：

- 必须先从 `tf->cause` 去掉最高位再 `switch`，不能直接 `switch (tf->cause)`；否则 `0x8000000000000005` 不会匹配 `IRQ_S_TIMER`。
- `ticks` 每发生一次时钟中断加 1，非时钟中断不能改动它。
- 每次时钟中断都要调用 `clock_set_next_event()`，且在返回前完成；不能只在 `ticks % TICK_NUM == 0` 时重设。
- 输出次数统计只统计时钟中断的 `print_ticks()` 调用，不能把其他中断计入。
- `print_ticks()` 打印常量 `TICK_NUM`，不得改写或“修正”为打印当前 `ticks`。
- 不修改 `exception_handler`。
- 关机阈值使用 `SHUTDOWN_AFTER = 3`；若框架已有定义则使用框架值。
- `cprintf("Unknown interrupt: cause %d\n", ...)` 的参数要转换为 `(int)`，避免 `-Wformat` 在 `-Werror` 下报错。

## 验证要求

- 单独编译 `trap.c`，确认语法、符号引用和告警情况。
- 若因 `exception_handler` 未实现而失败，应记录为“受其他模块阻塞”，不得添加空桩绕过。该失败发生在编译期（`trap_dispatch()` 中的隐式函数声明被 `-Werror` 升级为错误），不是链接期。
- 合并 `exception_handler` 后，运行 `make qemu`：期望约每秒输出一次 `100 ticks`，第 3 次后关机。
- 运行 `make grade`：期望 timer 用例在 30 秒内看到 `100 ticks` 与 `ticks=200`，并打印 `[Test 3] Sustained supervisor timer: PASS`。
- 检查 `tf->cause` 最高位处理、`ticks` 计数、下一次事件重设、关机次数和未知中断文案。
- 报告实际输出，不伪造；没有工具链或 QEMU 时，只报告源码检查和编译阻塞原因。
