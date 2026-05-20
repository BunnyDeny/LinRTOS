# 🎯 LinRTOS

LinRTOS 是一款面向 **ARM Cortex-M3/M4/M7** 的极简抢占式实时操作系统（RTOS），采用纯 C 语言编写，零外部依赖，可直接嵌入裸机固件。

> ⚡ **核心特性**
> - ✅ 抢占式优先级调度 + 可选时间片轮转
> - ✅ 任务延时与绝对周期延时
> - ✅ 任务挂起与恢复
> - ✅ 动态优先级调整
> - 🪶 极轻量：内核核心约 2~3 KB Flash

---

## 📂 目录结构

```
LinRTOS/
├── include/            # 📌 对外 API 头文件
│   ├── rtos.h          # 总入口
│   ├── rtos_config.h   # 裁剪配置
│   ├── rtos_task.h     # 任务管理
│   ├── rtos_kernel.h   # 内核内部定义
│   ├── rtos_list.h     # 双向链表
│   ├── rtos_port.h     # 移植层接口
│   ├── rtos_types.h    # 基础类型
│   └── rtos_config.h   # 配置宏
├── src/
│   ├── rtos_sched.c    # 调度器 + 就绪队列（O(1) 位图）
│   ├── rtos_task.c     # 任务生命周期 + TCB 静态池
│   ├── rtos_tick.c     # SysTick + 延时队列
│   └── port/
│       └── cortex_m/
│           ├── rtos_port.c     # SysTick 初始化 + 临界区 + 栈帧
│           └── rtos_port_asm.S # PendSV + SVC 上下文切换
├── examples/
│   ├── stm32g431/      # STM32G431CBUx 真实硬件示例
│   └── ...
├── docs/               # 架构与使用文档
└── CMakeLists.txt
```

---

## 🏗️ 系统架构

```
┌─────────────────────────────────────┐
│  应用层（用户任务 / 中断服务程序）     │
├─────────────────────────────────────┤
│  API 层                              │
│  rtos_task_create / rtos_task_delay │
├─────────────────────────────────────┤
│  内核核心                            │
│  调度器 (O(1) 位图)  │  延时队列      │
│  就绪队列            │  时间片轮转    │
├─────────────────────────────────────┤
│  硬件抽象层 (Port)                    │
│  SysTick │ PendSV │ SVC │ 临界区     │
└─────────────────────────────────────┘
```

### 调度器

- 采用 **32-bit 位图** 实现 O(1) 优先级查找。
- 数值越大优先级越高（`0` = 最低，`RTOS_MAX_PRIORITIES-1` = 最高）。
- 同优先级任务可选 **时间片轮转**（默认 1 tick）。

### 上下文切换

- **首次启动**：`SVC #0` 从 Handler 模式切换到 Thread 模式 + PSP。
- **日常切换**：`PendSV`（最低优先级异常），确保在所有其他 ISR 完成后执行。
- **栈对齐**：严格遵循 AAPCS 8 字节对齐；异常帧包含 `xPSR/PC/LR/R12/R3-R0`，软件保存 `R11-R4`。

更详细的架构说明请参阅 [`docs/architecture.md`](docs/architecture.md)。

---

## 🚀 快速开始

### 1️⃣ 添加到你的 MCU 工程

LinRTOS 采用**源码级集成**，推荐将本仓库作为子目录加入你的 CMake 工程：

```cmake
add_subdirectory(LinRTOS)
target_link_libraries(your_firmware PRIVATE linrtos)
```

在你的链接脚本中确保保留 `.text` 段中的异常向量表，并导出以下三个 Handler 的符号：

| 向量 | 说明 |
|------|------|
| `SysTick_Handler` | 系统节拍（位于 `src/rtos_tick.c`） |
| `PendSV_Handler`  | 上下文切换（位于 `src/port/cortex_m/rtos_port_asm.S`） |
| `SVC_Handler`     | 首次任务启动（位于 `src/port/cortex_m/rtos_port_asm.S`） |

### 2️⃣ 编写任务

```c
#include "rtos.h"

static uint32_t my_stack[128];

static void my_task(void *param)
{
    (void)param;
    for (;;) {
        /* 你的业务逻辑 */
        rtos_task_delay(100);
    }
}

int main(void)
{
    rtos_task_create(my_task, "my_task",
                     my_stack, sizeof(my_stack)/sizeof(uint32_t),
                     NULL, 1, NULL);
    rtos_scheduler_start();   /* 永不返回 */
}
```

### 3️⃣ 编译与烧录

```bash
mkdir build && cd build
cmake ..
make
```

---

## 🛠️ 移植指南

LinRTOS 的移植层集中在 `src/port/cortex_m/`。若你的芯片已包含 Cortex-M3/M4/M7 内核，通常**无需修改**即可运行。

若需要移植到其他架构，需实现以下接口：

| 函数 | 说明 |
|------|------|
| `rtos_port_init_stack()` | 构造任务初始栈帧（含异常帧与 R4-R11 占位） |
| `rtos_port_enter_critical()` / `rtos_port_exit_critical()` | 临界区进入/退出 |
| `rtos_port_request_switch()` | 触发 PendSV（或等价低优先级异常） |
| `rtos_port_init_systick()` | 初始化系统 tick 定时器 |
| `rtos_port_start_first_task()` | 触发 SVC 或等效机制启动第一个任务 |

已在 `src/port/cortex_m/rtos_port.c` 与 `rtos_port_asm.S` 中提供参考实现。

> 📖 **实战参考**：完整的 STM32G431CBUx 真实硬件移植记录（含 Makefile 集成、HAL 兼容、HardFault 排查与修复）请参阅 [`docs/porting_stm32g431.md`](docs/porting_stm32g431.md)。  
> 🚨 **常见陷阱**：QEMU 正常但真机 HardFault？可能是 PendSV 优先级未设对！请参阅 [`docs/pendsv_priority_trap.md`](docs/pendsv_priority_trap.md)。

---

## 📋 API 速查

### 任务管理

```c
rtos_err_t rtos_task_create(func, name, stack_buffer, stack_depth, param, priority, &handle);
void       rtos_task_delete(task);          /* NULL = 删除自身 */
void       rtos_task_delay(ticks);
void       rtos_task_delay_until(&prev, interval);
void       rtos_task_yield(void);
void       rtos_task_suspend(task);
void       rtos_task_resume(task);
void       rtos_task_set_priority(task, priority);
uint32_t   rtos_task_get_priority(task);
rtos_task_state_t rtos_task_get_state(task);
uint32_t   rtos_task_get_stack_free(task);
rtos_task_handle_t rtos_task_get_current(void);
uint32_t   rtos_get_tick_count(void);
int        rtos_scheduler_is_running(void);
```

---

## ⚙️ 配置裁剪

编辑 `include/rtos_config.h` 或在编译时通过 `-D` 覆盖：

| 宏 | 默认值 | 说明 |
|----|:------:|------|
| `RTOS_MAX_PRIORITIES` | 32 | 最大优先级数 |
| `RTOS_TICK_RATE_HZ` | 1000 | 系统节拍频率 |
| `RTOS_ENABLE_TIME_SLICING` | 1 | 同优先级时间片轮转 |
| `RTOS_ENABLE_IDLE_HOOK` | 0 | 空闲任务钩子 |

---

## ⚠️ 已知限制

- 📝 **FPU 支持**：尚未实现 Cortex-M4F/M7 的浮点上下文保存（`S16-S31`）。若任务使用 `float`/`double`，需关闭硬件 FPU 或后续添加 Lazy Stacking 支持。
- 📝 **内存分配**：当前采用静态对象池（TCB 等），不支持运行时 `malloc`。如需动态创建大量任务，可增大 `RTOS_MAX_TASKS` 宏。

---

## 📄 许可证

MIT License — 详见各源文件头。
