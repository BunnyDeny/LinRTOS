# LinRTOS 移植到 STM32G431CBUx 实战记录

本文档记录将 LinRTOS 从 QEMU 模拟环境移植到 **STM32G431CBUx**（Cortex-M4, 170 MHz, 128K Flash / 32K RAM）真实硬件的完整过程，包含 Makefile 集成、HAL 兼容、上下文切换符号冲突以及一个关键 HardFault 的根因分析与修复。

> **目标平台**：STM32G431CBUx  
> **开发环境**：STM32CubeMX + Makefile + arm-none-eabi-gcc  
> **调试器**：CMSIS-DAP (DAPLink) + OpenOCD  
> **串口**：USART3 (PB10/PB11) @ 115200

---

## 1. 工程搭建

使用 STM32CubeMX 生成 Makefile 工程，开启以下外设：

- **RCC**：HSE 24 MHz + PLL 170 MHz
- **USART3**：PB10 (TX) / PB11 (RX)，用于日志输出
- **SysTick**：由 HAL 默认配置（后续由 LinRTOS 接管）

生成的目录结构如下（已省略 Drivers 细节）：

```
examples/stm32g431/
├── Core/Src/
│   ├── main.c              # 应用入口
│   ├── stm32g4xx_it.c      # CubeMX 中断桩（需修改）
│   ├── stm32g4xx_hal_msp.c # HAL MSP
│   └── usart.c / gpio.c / dma.c
├── Core/Inc/
│   └── main.h / usart.h / stm32g4xx_it.h
├── Drivers/                # HAL + CMSIS
├── Makefile
├── STM32G431XX_FLASH.ld
└── startup_stm32g431xx.s   # 启动文件（含弱别名向量表）
```

---

## 2. Makefile 集成 LinRTOS 源码

将 LinRTOS 作为相对路径引入，避免复制源码导致维护困难。

### 2.1 添加 C 源文件与头文件路径

编辑 `examples/stm32g431/Makefile`：

```makefile
# 添加 LinRTOS 内核源文件
C_SOURCES += \
Core/Src/linrtos_sys.c \
../../src/rtos_sched.c \
../../src/rtos_task.c \
../../src/rtos_tick.c \
../../src/rtos_sem.c \
../../src/rtos_mutex.c \
../../src/rtos_queue.c \
../../src/rtos_event.c \
../../src/rtos_timer.c \
../../src/port/cortex_m/rtos_port.c

# 添加 LinRTOS 汇编文件（上下文切换）
ASMM_SOURCES += \
../../src/port/cortex_m/rtos_port_asm.S

# 添加头文件包含路径
C_INCLUDES += -I../../include
```

### 2.2 关键编译标志

确保 Makefile 已包含 Cortex-M4F 的编译选项：

```makefile
CPU = -mcpu=cortex-m4 -mthumb
FPU = -mfpu=fpv4-sp-d16
FLOAT-ABI = -mfloat-abi=hard
MCU = $(CPU) $(FPU) $(FLOAT-ABI)
```

---

## 3. 移植层适配：创建 `linrtos_sys.c`

新建 `Core/Src/linrtos_sys.c`，负责桥接 HAL 与 LinRTOS：

```c
#include "rtos.h"
#include "usart.h"
#include "stm32g4xx_hal.h"

extern UART_HandleTypeDef huart3;
static uint32_t s_core_clock = 170000000;  /* G431 @ 170 MHz */

/* HAL 的 SysTick 仍会先使能，我们在此基础上叠加 LinRTOS tick */
void SysTick_Handler(void)
{
    HAL_IncTick();
    rtos_tick_handler();
}

/* 内核日志输出：阻塞式 UART 发送 */
void debug_puts(const char *str)
{
    if (!str) return;
    HAL_UART_Transmit(&huart3, (uint8_t *)str, strlen(str), 100);
}
```

### 3.1 内核时钟校准

`rtos_port_init_systick()` 会根据 `s_core_clock` 计算 SysTick 重装载值。STM32G431 主频 170 MHz，必须准确设置，否则 tick 频率会偏差。

在 `linrtos_sys.c` 中添加：

```c
void rtos_port_set_core_clock(uint32_t clock_hz)
{
    s_core_clock = clock_hz;
}
```

---

## 4. SVC / PendSV / SysTick 符号冲突处理

### 4.1 问题背景

`startup_stm32g431xx.s` 中的向量表将 `SVC_Handler`、`PendSV_Handler`、`SysTick_Handler` 声明为**弱别名**（weak alias），默认指向 `Default_Handler`：

```asm
.weak   SVC_Handler
.thumb_set SVC_Handler, Default_Handler
.weak   PendSV_Handler
.thumb_set PendSV_Handler, Default_Handler
```

LinRTOS 的 `rtos_port_asm.S` 中定义了**强符号**（strong symbol）`SVC_Handler` 和 `PendSV_Handler`，用于首次任务启动和上下文切换。

然而，CubeMX 生成的 `stm32g4xx_it.c` 中也定义了**强符号**的 `SVC_Handler`、`PendSV_Handler` 和 `SysTick_Handler`：

```c
void SVC_Handler(void) { /* empty */ }
void PendSV_Handler(void) { /* empty */ }
void SysTick_Handler(void) { HAL_IncTick(); }
```

如果保留这些定义，链接器会报**多重定义错误**，或者更糟糕的是——根据链接顺序**静默选择错误实现**。

### 4.2 解决方案

从 `stm32g4xx_it.c` 中**删除**以下三个函数的定义：

- `SVC_Handler`
- `PendSV_Handler`
- `SysTick_Handler`

保留其他外设中断（如 `USART3_IRQHandler`、`DMA1_Channel1_IRQHandler` 等）不变。

`SysTick_Handler` 的定义已迁移到 `linrtos_sys.c`；`SVC_Handler` 和 `PendSV_Handler` 由 `rtos_port_asm.S` 的强符号接管。

---

## 5. 关键修复：PendSV 优先级设置

### 5.1 故障现象

系统烧录后，在首次上下文切换时触发 **HardFault**：

- `CFSR = 0x00040000`（INVPC，无效 PC 加载）
- `HFSR = 0x40000000`（FORCED，升级为 HardFault）
- `control = 0x00`（线程模式仍在使用 MSP）

### 5.2 根因分析

Cortex-M 的 NVIC 中，**PendSV 默认优先级为 0（最高）**，而 HAL 将 **SysTick 设为 15（最低）**。

当 `rtos_sched()` 在 `SysTick_Handler` 中触发 PendSV 时，PendSV 会**立即抢占** SysTick。硬件在 MSP 上保存 PendSV 异常帧，恰好覆盖了 SysTick Handler 调用链的栈帧：

```
MSP 初始: 0x20008000
  SysTick 异常帧          (0x20007fe0 - 0x20007fff)
  SysTick_Handler push     (0x20007fd8 - 0x20007fdf)
  rtos_tick_handler push   (0x20007fc8 - 0x20007fd7)
  rtos_sched push          (0x20007fc0 - 0x20007fc7)
  ⚠️ PendSV 异常帧抢占保存 (0x20007fa0 - 0x20007fbf) ← 覆盖了上面所有栈帧！
```

PendSV 返回后，`SysTick_Handler` 继续执行 `pop {r3, pc}`，从被覆盖的栈帧中加载 PC。由于 PendSV 异常帧的 PC 字段恰好是 `0x00000000`（bit[0]=0），触发 **INVPC** HardFault。

### 5.3 工程修复

在 `main()` 的 HAL 初始化之后，将 PendSV 优先级显式设为最低（15），与 SysTick 同级：

```c
int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* 关键：将 PendSV 设为最低优先级，防止在 SysTick 中抢占导致 MSP 栈帧被破坏 */
    *(volatile uint8_t *)0xE000ED22 = 0xF0;

    MX_GPIO_Init();
    MX_DMA_Init();
    MX_USART3_UART_Init();

    rtos_task_create(task_high, "high", task_high_stack, 128, NULL, 2, NULL);
    rtos_task_create(task_low,  "low",  task_low_stack,  128, NULL, 1, NULL);

    rtos_scheduler_start();   /* 永不返回 */
}
```

这与 **RT-Thread**、**FreeRTOS** 的工程实践完全一致：**PendSV 必须是系统最低优先级异常**，确保上下文切换在所有中断返回后安全执行。

---

## 6. 内核 pre-init 保护

HAL 的 `HAL_Init()` 会提前使能 SysTick。此时 LinRTOS 内核尚未初始化（`g_kernel.ready_list` 为空），如果 `rtos_tick_handler()` 被调用，会访问空指针。

在 `src/rtos_tick.c` 中添加守卫：

```c
void rtos_tick_handler(void)
{
    /* 若内核尚未初始化（如 HAL_Init 阶段 SysTick 已使能），直接返回 */
    if (g_kernel.ready_list[0].next == NULL) {
        return;
    }
    /* ... 正常 tick 处理 ... */
}
```

---

## 7. 验证结果

烧录后通过串口观察输出：

```
[HIGH] tick=3514 count=7
[LOW ] tick=4018 count=4
[HIGH] tick=4518 count=9
[HIGH] tick=5020 count=10
[LOW ] tick=5022 count=5
...
```

- `task_high`（优先级 2）和 `task_low`（优先级 1）正常交替执行
- tick 计数稳定增长，无 HardFault
- 任务延时、信号量、互斥锁均工作正常

---

## 8. 移植检查清单

| 步骤 | 内容 | 状态 |
|------|------|:----:|
| Makefile 集成 | 添加 LinRTOS C 源、汇编源、头文件路径 | ✅ |
| 移除 CubeMX 空桩 | 删除 `stm32g4xx_it.c` 中的 `SVC/PendSV/SysTick` | ✅ |
| 创建 `linrtos_sys.c` | SysTick 转发 + UART 日志输出 | ✅ |
| 内核时钟校准 | 设置 `s_core_clock = 170000000` | ✅ |
| PendSV 优先级 | 设为 15（最低），防止 MSP 栈帧覆盖 | ✅ |
| Pre-init 保护 | `rtos_tick_handler()` 空列表守卫 | ✅ |
| 烧录验证 | 串口观察任务正常调度 | ✅ |

---

## 9. 参考

- [ARMv7-M Architecture Reference Manual — SVCall & PendSV](https://developer.arm.com/documentation/ddi0403)
- RT-Thread `context_gcc.S` / `port.c`（PendSV 最低优先级策略）
- FreeRTOS `port.c` for Cortex-M4F（`configKERNEL_INTERRUPT_PRIORITY`）
