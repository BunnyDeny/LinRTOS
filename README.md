# LinRTOS

LinRTOS 是一款面向 **ARM Cortex-M3/M4/M7** 的极简抢占式实时操作系统，采用纯 C 编写，零外部依赖，源码级集成。

**核心特性**
- 抢占式优先级调度 + 可选时间片轮转
- 任务延时 / 绝对周期延时 / 主动让出
- 任务挂起、恢复、自删除
- 动态优先级调整
- 调度器嵌套锁
- 极轻量：内核核心约 2~3 KB Flash

---

## 目录结构

```
LinRTOS/
├── include/          # 对外 API 头文件
│   ├── linRTOS.h     # 总入口（含 RTOS_ENTER_CRITICAL 等宏）
│   ├── task.h        # 任务管理 API
│   ├── kernel.h      # 内核内部数据结构
│   ├── list.h        # 双向链表
│   ├── port.h        # 移植层接口
│   └── types.h       # 基础类型与错误码
├── src/              # 内核源码
│   ├── task.c        # 任务生命周期 + TCB 静态池
│   ├── sched.c       # 调度器 + O(1) 位图就绪队列
│   ├── tick.c        # SysTick + 延时队列
│   └── port/cortex_m/# Cortex-M 移植层
├── tests/            # 测试用例（条件编译，一次只启用一个）
├── examples/stm32g431/   # STM32G431CBUx 真实硬件示例
├── configs/          # 默认 defconfig
├── tools/            # Kconfig 脚本
└── Makefile          # 顶层构建入口
```

---

## 移植指南

### 1. 集成源码

将本仓库作为子目录加入你的工程，把 `include/` 加入头文件搜索路径，`src/` 下的 `.c` 文件加入编译：

```
-I../../include
../../src/task.c
../../src/sched.c
../../src/tick.c
../../src/port/cortex_m/port.c
../../src/port/cortex_m/port_asm.S
```

### 2. 实现中断向量表绑定

在你的启动文件中，确保以下三个 Handler 指向 LinRTOS：

| Handler | 说明 |
|---------|------|
| `SysTick_Handler` | 系统节拍，直接调用 `rtos_tick_handler()` |
| `PendSV_Handler`  | 上下文切换，已在 `port_asm.S` 中实现 |
| `SVC_Handler`     | 首次任务启动，已在 `port_asm.S` 中实现 |

若 HAL 框架已接管 `SysTick_Handler`（如 STM32 HAL），可在 HAL 的 SysTick ISR 末尾调用 `rtos_tick_handler()`：

```c
void SysTick_Handler(void)
{
    HAL_IncTick();
    rtos_tick_handler();   // 追加 LinRTOS 节拍处理
}
```

### 3. 实现 debug_puts

**LinRTOS 不自带串口驱动。** 测试用例依赖 `debug_printf` 输出日志，而 `debug_printf` 内部调用 `debug_puts` 完成实际发送。你需要根据目标板实现：

```c
void debug_puts(const char *str)
{
    // 示例：使用 HAL UART 阻塞发送
    HAL_UART_Transmit(&huart3, (uint8_t *)str, strlen(str), 100);
}
```

> ⚠️ `debug_printf` 内部已包含临界区保护，你的 `debug_puts` 实现**不需要**额外关中断。

### 4. 启动调度器

```c
#include "linRTOS.h"

static uint32_t my_stack[128];

static void my_task(void *param)
{
    (void)param;
    for (;;) {
        /* 业务逻辑 */
        rtos_task_delay(1000);
    }
}

int main(void)
{
    HAL_Init();            // 你的硬件初始化
    SystemClock_Config();

    rtos_task_create(my_task, "my_task",
                     my_stack, sizeof(my_stack) / sizeof(uint32_t),
                     NULL, 1, NULL);

    rtos_scheduler_start();   // 永不返回
}
```

### 5. 配置系统（Kconfig）

LinRTOS 使用 Kconfig 管理编译时配置：

```bash
make menuconfig       # 交互式图形配置
make defconfig        # 加载默认配置
make savedefconfig    # 将当前配置中与默认值不同的项保存为最小 defconfig（根目录 defconfig），不覆盖默认模板
make mrproper         # 清除配置和生成文件
```

配置结果生成 `include/linrtos_kconfig.h`，源码通过 `#include "config.h"` 引入。

---

## 测试用例

`tests/` 目录提供一组验证内核功能的独立测试文件。通过 Kconfig 的 `TEST_CASE` 选项**一次只启用一个**：

```bash
make menuconfig
# → Example Test Cases → Select test case to build
```

| 测试用例 | 验证内容 |
|---------|---------|
| **TEST_FPU** | 多浮点任务抢占时 FPU 寄存器上下文正确保存/恢复 |
| **TEST_SELFDELETE** | 任务自删除后空闲任务回收 TCB，回收的 TCB 可重用 |
| **TEST_BASIC_TASKS** | 多任务创建、抢占调度、时间片轮转、`delay_until` 绝对周期精度 |
| **TEST_SUSPEND_RESUME** | `suspend` / `resume` 挂起与恢复其他任务，自挂起后由外部恢复 |
| **TEST_SCHED_LOCK** | `sched_lock` / `sched_unlock` 禁止/恢复抢占，嵌套锁计数 |
| **TEST_STATE** | `get_state` / `get_priority` / `get_current` 状态查询 |
| **TEST_PRIORITY** | `set_priority` 动态升降优先级，验证抢占行为 |
| **TEST_YIELD** | `rtos_task_yield` 同优先级主动让出 CPU |
| **TEST_STACK_FREE** | `rtos_task_get_stack_free` 查询剩余栈空间 |
| **TEST_ABORT_DELAY** | `rtos_task_abort_delay` 强制唤醒阻塞中的任务 |

每个测试文件实现 `app_entry_task(void *param)` 作为统一入口，由示例工程的 `main()` 调用创建。

---

## 示例：STM32G431 编译与烧录

> ⚠️ 以下命令仅适用于项目自带的 **STM32G431CBUx + CMSIS-DAP** 示例工程。你的目标板型号和调试器可能不同，需自行修改 `examples/stm32g431/Makefile` 和 `flash-g431` 中的 OpenOCD 参数。

```bash
# 1. 选择测试用例
make menuconfig

# 2. 编译示例工程（会自动补全 defconfig 和头文件）
make build-g431

# 3. 编译并烧录（OpenOCD + CMSIS-DAP）
make flash-g431
```

`make build-g431` 会自动检测 `include/linrtos_kconfig.h` 是否存在，若缺失则先执行 `make defconfig`。`make flash-g431` 依赖 `build-g431`，编译完成后调用 OpenOCD 烧录并复位。如果你使用 ST-Link 或其他调试器，请修改 `flash-g431` 中的 OpenOCD 接口配置。

---

## 许可证

MIT License — 详见各源文件头。
