# 模块一任务一：物理内存初始化提示词

**负责人：2412084-刘芳希。**

[PROMPT]

**任务：** 为当前 RISC-V ucore 实验二实现物理内存探测与页描述符初始化功能。在仓库根目录的 `code/kern/mm/pmm.c` 中补全 `page_init()`，使内核能够根据设备树识别内存，建立正确的地址转换和页面管理状态，将排除固件、内核镜像和描述符数组后的完整空闲页注册到当前 `pmm_manager`。

**操作要求：** 在我明确要求执行本提示词时，必须修改项目中的实际文件，而不只是展示代码。先核对以下依赖与现有实现，仅在 `code/kern/mm/pmm.c` 的 `//lab2 function1 YOUR CODE` 位置补全该功能，将此处的 `YOUR CODE` 替换为 `2412084`，保留 `lab2 function1` 标记及其他已有逻辑。不得修改其他任务、分配器、设备树解析器、启动页表、公共接口或测试。不要切换到 Best-Fit，不要提交或推送 Git。

**输出要求：** 依据下列真实工程依赖与行为规格实现，避免引入无关功能。完成后说明修改位置、地址和页号关系、保留区与空闲区边界，以及逐项验证结果。无法运行的检查要明确注明，不得将预计结果写成测试通过。

[RELY]

以 `code/kern/mm/pmm.c`、`pmm.h`、`memlayout.h`、`mmu.h`、`code/kern/driver/dtb.h` 和 `code/libs/defs.h` 的实际定义为准：

- **接口：** `get_memory_base()`、`get_memory_size()` 获取设备树内存信息；可使用 `panic()`、`cprintf()`、`PADDR()`、`pa2page()`、`page2pa()`、`set_page_ref()` 和 `init_memmap()`。后者要求页数大于零，且待注册页原先处于保留状态。
- **全局状态：** `pages` 是 `struct Page` 数组的虚拟地址；`pages[0]` 对应页号 `nbase = DRAM_BASE / PGSIZE`；`npage` 是物理页号上界，描述符数量为 `npage - nbase`，不能与空闲页数混淆。
- **地址关系：** `PADDR(kva) = kva - va_pa_offset`；`pa2page(pa) = &pages[PPN(pa) - nbase]`。偏移应符合启动映射的 `PHYSICAL_MEMORY_OFFSET`，不是 `KERNBASE - 内存基址`；`end` 是内核结束虚拟地址。当前 `KADDR` 被注释掉，不可使用。
- **宏与字段：** 使用 `PGSIZE`、`PPN`、`ROUNDUP`、`ROUNDDOWN`、`PHYSICAL_MEMORY_END` 及页面状态宏；`struct Page` 包含 `ref`、`flags`、`property`、`page_link`。初始化时不能假定描述符内存已经清零。

[GUARANTEE]

仅补全以下接口；必要的辅助函数限于本模块内的 `static` 函数，不改变公共接口：

```c
static void page_init(void);
```

[SPECIFICATION]

### page_init

**Pre-Condition：** 启动映射已建立，设备树初始化路径已执行，当前 `pmm_manager` 已初始化但尚未注册空闲区。本函数在启动时调用一次，不依赖尚未建立的页分配服务。

**Post-Condition：**

- 正确设置 `va_pa_offset`、`npage`、`pages`，输出探测范围和实际管理范围；所有地址、页号及描述符转换一致且不越界。
- `pages` 位于 `end` 之后并按页对齐，整个描述符数组落在有效内存内；页面状态初始化明确，未释放的页保持保留。
- 当前管理器仅接管排除 OpenSBI、内核和描述符数组后的完整空闲页，不遗漏可管理完整页，也不包含边缘不足一页的空间。

**Case 1：正常情况。** 内存信息和布局有效且存在完整空闲页时，满足上述后置条件并注册空闲区，注册前满足分配器的保留状态要求。

**Case 2：范围与对齐边界。** 内存末端超过 `PHYSICAL_MEMORY_END` 时截断到管理上界；描述符和空闲区起点向上按页对齐，管理末端向下对齐，所有大小与地址运算避免溢出。

**Case 3：无完整空闲页。** 描述符布局有效，但剩余空间不足一页或为零时，不调用 `init_memmap()`。

**Case 4：信息或布局无效。** 基址或大小无效、范围运算溢出、布局与固定映射不兼容，或无法容纳描述符数组时，通过 `panic()` 终止；必须在越界访问前发现问题。

**Requirements：** 验证构建及正常配置下的全局状态、保留区、空闲页和地址转换，并说明边界检查依据。保持测试不变；`test_page_init()` 假定设备树末端未超过管理上界且存在空闲页，不能直接用于判定截断和零空闲页场景。其他未完成任务导致的评分失败应单独说明，不扩展本任务修改范围。



