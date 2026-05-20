# 🎯 LinRTOS

LinRTOS 是一款面向 **ARM Cortex-M3/M4/M7** 的极简抢占式实时操作系统（RTOS），采用纯 C 语言编写，零外部依赖，可直接嵌入裸机固件。

> ⚡ **核心特性**
> - ✅ 抢占式优先级调度 + 可选时间片轮转
> - ✅ 二值/计数信号量
> - ✅ 递归互斥锁（带优先级继承，防止优先级翻转）
> - ✅ 任务延时与绝对周期延时
> - ✅ 软件定时器（tick 级精度，回调运行于中断上下文）
> - 🔹 消息队列与事件标志组（骨架已就绪，待完善）
> - 🪶 极轻量：内核核心约 3~4 KB Flash

---

## 📂 目录结构

```
LinRTOS/
├── include/            # 📌 对外 API 头文件
│   ├── rtos.h          # 总入口
│   ├── rtos_config.h   # 裁剪配置
│   ├── rtos_task.h     # 任务管理
│   ├── rtos_sem.h      # 信号量
│   ├── rtos_mutex.h    # 互斥锁
│   ├── rtos_queue.h    # 消息队列
│   ├── rtos_event.h    # 事件标志
│   ├── rtos_timer.h    # 软件定时器
│   └── rtos_port.h     # 移植层接口
├── src/
│   ├── rtos_sched.c    # 调度器 + 就绪队列
│   ├── rtos_task.c     # 任务生命周期 + TCB 池
│   ├── rtos_tick.c     # SysTick + 延时队列
│   ├── rtos_sem.c      # 信号量实现
│   ├── rtos_mutex.c    # 互斥锁 + 优先级继承
│   ├── rtos_queue.c    # 队列（待完善 buffer 管理）
│   ├── rtos_event.c    # 事件标志（待完善 wait_bits）
│   ├── rtos_timer.c    # 软件定时器
│   └── port/
│       └── cortex_m/
│           ├── rtos_port.c     # SysTick 初始化 + 临界区 + 栈帧
│           └── rtos_port_asm.S # PendSV + SVC 上下文切换
├── tests/
│   ├── qemu/           # QEMU 集成测试（semihosting）
│   │   ├── qemu_startup.s
│   │   ├── qemu_linker.ld
│   │   └── test_main.c
│   └── qemu_suite/     # 独立 QEMU 测试用例集合
│       ├── test_preemption.c
│       ├── test_priority_inheritance.c
│       ├── test_task_delete.c
│       ├── test_timeout.c
│       └── test_timer.c
├── examples/
│   └── simple_demo.c   # 双任务 + 信号量 + 互斥锁示例
├── cmake/
│   └── toolchain-arm-none-eabi.cmake
├── docs/               # 架构与使用文档
└── CMakeLists.txt
```

---

## 🏗️ 系统架构

LinRTOS 采用经典的分层微内核架构：

```
┌─────────────────────────────────────┐
│  应用层（用户任务 / 中断服务程序）     │
├─────────────────────────────────────┤
│  API 层                              │
│  rtos_task / rtos_sem / rtos_mutex  │
│  rtos_queue / rtos_event / rtos_timer│
├─────────────────────────────────────┤
│  内核核心                            │
│  调度器 (O(1) 位图)  │  延时队列      │
│  就绪队列            │  软件定时器    │
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
- **任务退出保护**：每个任务的 LR 指向 `rtos_task_exit_trampoline`，防止任务函数返回后触发 HardFault。

### 临界区

默认使用 `PRIMASK`（`cpsid i` / `cpsie i`）实现全局中断开关，兼容 Cortex-M0~M7。如需使用 `BASEPRI` 保留高优先级中断，可修改 `src/port/cortex_m/rtos_port.c`。

更详细的架构说明请参阅 [`docs/architecture.md`](docs/architecture.md)。

---

## 🚀 快速开始

### 1️⃣ 添加到你的 MCU 工程

LinRTOS 采用**源码级集成**，推荐将本仓库作为子目录加入你的 CMake 工程：

```cmake
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/toolchain-arm-none-eabi.cmake)
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
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm-none-eabi.cmake
make
```

---

## 🛠️ 移植指南

LinRTOS 的移植层集中在 `src/port/cortex_m/`。若你的芯片已包含 Cortex-M3/M4/M7 内核，通常**无需修改**即可运行。

> 📖 **实战参考**：完整的 STM32G431CBUx 真实硬件移植记录（含 Makefile 集成、HAL 兼容、HardFault 排查与修复）请参阅 [`docs/porting_stm32g431.md`](docs/porting_stm32g431.md)。  
> 🚨 **常见陷阱**：QEMU 正常但真机 HardFault？可能是 PendSV 优先级未设对！请参阅 [`docs/pendsv_priority_trap.md`](docs/pendsv_priority_trap.md)。

若需要移植到其他架构，需实现以下接口：

| 函数 | 说明 |
|------|------|
| `rtos_port_init_stack()` | 构造任务初始栈帧（含异常帧与 R4-R11 占位） |
| `rtos_port_enter_critical()` / `rtos_port_exit_critical()` | 临界区进入/退出 |
| `rtos_port_request_switch()` | 触发 PendSV（或等价低优先级异常） |
| `rtos_port_init_systick()` | 初始化系统 tick 定时器 |
| `rtos_port_start_first_task()` | 触发 SVC 或等效机制启动第一个任务 |

已在 `src/port/cortex_m/rtos_port.c` 与 `rtos_port_asm.S` 中提供参考实现。

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
rtos_task_handle_t rtos_task_get_current(void);
```

### 信号量

```c
rtos_err_t rtos_sem_create(&sem, initial_count);
rtos_err_t rtos_sem_take(sem, timeout_ticks);   /* RTOS_WAIT_FOREVER / RTOS_DONT_WAIT */
rtos_err_t rtos_sem_give(sem);
rtos_err_t rtos_sem_give_isr(sem, &needs_switch);
void       rtos_sem_delete(sem);
```

### 互斥锁

```c
rtos_err_t rtos_mutex_create(&mutex);
rtos_err_t rtos_mutex_take(mutex, timeout_ticks);
rtos_err_t rtos_mutex_give(mutex);
void       rtos_mutex_delete(mutex);
```

### 软件定时器

```c
rtos_err_t rtos_timer_create(&tm, name, period_ticks, mode, arg, callback);
rtos_err_t rtos_timer_start(tm);
rtos_err_t rtos_timer_stop(tm);
rtos_err_t rtos_timer_reset(tm);
rtos_err_t rtos_timer_change_period(tm, new_period);
void       rtos_timer_delete(tm);
```

### 事件标志组（骨架）

```c
rtos_err_t rtos_event_group_create(&group);
rtos_err_t rtos_event_group_set_bits(group, bits);
rtos_err_t rtos_event_group_clear_bits(group, bits);
rtos_err_t rtos_event_group_wait_bits(group, bits, clear, wait_all, timeout);
```

### 队列（骨架）

```c
rtos_err_t rtos_queue_create(&queue, item_size, max_items);
rtos_err_t rtos_queue_send(queue, item, timeout);
rtos_err_t rtos_queue_receive(queue, item, timeout);
```

---

## ⚙️ 配置裁剪

编辑 `include/rtos_config.h` 或在编译时通过 `-D` 覆盖：

| 宏 | 默认值 | 说明 |
|----|:------:|------|
| `RTOS_MAX_PRIORITIES` | 32 | 最大优先级数 |
| `RTOS_TICK_RATE_HZ` | 1000 | 系统节拍频率 |
| `RTOS_ENABLE_TIME_SLICING` | 1 | 同优先级时间片轮转 |
| `RTOS_ENABLE_PRIORITY_INHERITANCE` | 1 | 互斥锁优先级继承 |
| `RTOS_ENABLE_SOFT_TIMER` | 1 | 软件定时器 |
| `RTOS_ENABLE_QUEUES` | 1 | 消息队列 |
| `RTOS_ENABLE_EVENT_GROUPS` | 1 | 事件标志组 |

---

## 🐧 在本地 Linux 环境运行测试

LinRTOS 使用 **QEMU** (`qemu-system-arm`) 模拟 **Cortex-M3** 裸机环境，无需真实硬件即可在 Linux 主机上运行全部测试用例。

### 前置依赖

```bash
# Ubuntu / Debian
sudo apt-get install gcc-arm-none-eabi qemu-system-arm

# 验证安装
arm-none-eabi-gcc --version   # 建议 >= 10.x
qemu-system-arm --version     # 建议 >= 6.x
```

### 编译内核静态库

```bash
cd LinRTOS
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../cmake/toolchain-arm-none-eabi.cmake
make
```

### 运行 QEMU 测试套件

`tests/qemu_suite/` 包含一组独立的裸机测试，每个测试都链接了 `qemu_startup.s`（向量表 + Reset_Handler）和 `qemu_linker.ld`（ bare-metal 链接脚本），并通过 **semihosting** 输出结果到终端。

```bash
cd tests/qemu_suite

# 编译并运行全部测试
for f in test_*.c; do
    name="${f%.c}"
    arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 -g -I../../include \
        -T ../qemu/qemu_linker.ld -nostartfiles \
        ../qemu/qemu_startup.s "$f" ../../build/liblinrtos.a \
        -o "${name}.elf"

    echo -n "Running $name ... "
    timeout 10 qemu-system-arm -M mps2-an385 -semihosting -nographic \
        -kernel "${name}.elf" 2>&1 | grep -q "PASS" && echo "PASS" || echo "FAIL"
done
```

当前套件包含以下测试：

| 测试文件 | 验证内容 |
|----------|----------|
| `test_preemption.c` | 高优先级任务抢占低优先级任务 |
| `test_priority_inheritance.c` | 互斥锁优先级继承，防止中优先级任务抢占 |
| `test_task_delete.c` | 任务自删与调度器正确处理 |
| `test_timeout.c` | 信号量 / 互斥锁带超时阻塞 |
| `test_timer.c` | 软件定时器周期性回调 |

### 运行集成测试

```bash
cd tests/qemu
arm-none-eabi-gcc -mthumb -mcpu=cortex-m3 -O2 -g -I../../include \
    -T qemu_linker.ld -nostartfiles \
    qemu_startup.s test_main.c ../../build/liblinrtos.a \
    -o test_main.elf

qemu-system-arm -M mps2-an385 -semihosting -nographic -kernel test_main.elf
```

### 调试单个测试

若某个测试失败，可用 `timeout` 防止 QEMU 无限挂起，并直接查看输出：

```bash
timeout 10 qemu-system-arm -M mps2-an385 -semihosting -nographic \
    -kernel test_timeout.elf
```

> 💡 **提示**：QEMU 退出码 `1` 是 semihosting `SYS_EXIT` 的正常表现；退出码 `124` 表示 `timeout` 触发，即测试挂死。

---

## ⚠️ 已知限制

- 📝 **队列 / 事件标志组**：API 骨架已创建，部分功能（如 `rtos_queue_create` 的内部 buffer 分配、`rtos_event_group_wait_bits` 的精确位匹配）需要进一步完善。
- 📝 **FPU 支持**：尚未实现 Cortex-M4F/M7 的浮点上下文保存（`S16-S31`）。若任务使用 `float`/`double`，需关闭硬件 FPU 或后续添加 Lazy Stacking 支持。
- 📝 **内存分配**：当前采用静态对象池（TCB、信号量、互斥锁等），不支持运行时 `malloc`。如需动态创建大量对象，可增大各模块的 `RTOS_MAX_XXX` 宏。

---

## 📄 许可证

MIT License — 详见各源文件头。
