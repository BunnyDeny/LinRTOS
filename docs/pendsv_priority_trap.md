# 🎯 PendSV 优先级陷阱：RTOS 上下文切换的隐形地雷 💣

> *"我的 RTOS 在 QEMU 上跑得好好的，怎么一到真机上就 HardFault 了？"* 😭  
> 如果你也有同样的困惑，恭喜你找到了这篇文档！本文将带你拆解这个让无数嵌入式开发者**抓狂**的经典陷阱 🕳️

---

## 😱 症状：一切看起来都很正常，直到...

你的 RTOS 在 **QEMU** 模拟器里运行丝滑流畅 ✅：

```
[Task-A] Running...
[Task-B] Running...
[Task-A] Running...
```

但当你信心满满地把固件烧录到 **真实 STM32** 上时...

```
💥 BANG! 💥
HardFault_Handler()  ← 死循环
```

打开调试器一看 👀：

| 寄存器 | 值 | 含义 |
|--------|:--:|------|
| `CFSR` | `0x00040000` | 😵 **INVPC** — 无效的 PC 加载 |
| `HFSR` | `0x40000000` | 🔥 **FORCED** — 升级为 HardFault |
| `CONTROL` | `0x00` | 🚫 线程模式居然还在用 MSP！ |
| `MSP` | `0x20007fa0` | 📍 主栈指针位置异常 |

**你的第一反应**：
> *"肯定是栈溢出了！"* → 加大栈空间 → **仍然崩溃** 😤  
> *"肯定是启动文件有问题！"* → 检查向量表 → **一切正常** 🤔  
> *"肯定是编译器优化搞坏了！"* → 关闭 `-O2` → **毫无改善** 😫

别急，真正的问题藏在一个**只有 8 位**的小寄存器里... 🔬

---

## 🧠 背景：PendSV 是做什么的？

在 Cortex-M 架构上，RTOS 使用 **PendSV**（可挂起系统调用）来完成**上下文切换**：

```
🧵 线程模式 (Thread)          ⚙️ 处理模式 (Handler)
    ↓                              ↑
  任务主动让出 / Tick 到期  →  触发 PendSV  →  切换栈指针 →  运行新任务
```

PendSV 的核心设计哲学 🎨：

> **"我虽然是异常，但我脾气最好 —— 等所有高优先级兄弟忙完了，我再来收拾残局"** 🐢

这是 RT-Thread、FreeRTOS、LinRTOS 等所有成熟 RTOS 的共同选择 ✨

---

## 💥 陷阱：PendSV 的默认优先级 = 0（最高）

### 🔍 来，看看你的 NVIC 寄存器

Cortex-M 的系统异常优先级寄存器在 `0xE000ED00` 附近：

| 寄存器 | 地址 | 控制对象 | 默认值 |
|--------|:----:|----------|:------:|
| `SHPR2` | `0xE000ED1C` | SVC 优先级 | `0x00` |
| `SHPR3` | `0xE000ED20` | SysTick 优先级 | `0x00` |
| `SHPR3` | `0xE000ED22` | **PendSV 优先级** | **`0x00`** ⚠️ |

**问题就在这里！** 🚨

PendSV 复位后的默认值是 **`0x00`**，在 Cortex-M 的优先级体系中：**数值越小 = 优先级越高** 📈

而 HAL 库通常会把 **SysTick 设为 15（最低）**：

```
🥇 PendSV  prio = 0  （霸道总裁，谁来都抢）
🐌 SysTick prio = 15 （打工人，谁都能欺负）
```

### 🔥 灾难现场：SysTick 里触发 PendSV

当 `rtos_tick_handler()` 在 SysTick 中决定需要调度时，会调用：

```c
void rtos_port_request_switch(void)
{
    SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;  // 📌 挂起 PendSV
}
```

**噩梦开始** 😰：

```
⏱️ SysTick_Handler 正在运行（优先级 15）
   ↓
   调用 rtos_tick_handler()
   ↓
   调用 rtos_sched()
   ↓
   触发 PendSV（优先级 0）
   ↓
   💥 PendSV 立即抢占 SysTick！
```

此时 CPU 的处理模式栈（MSP）上的布局变成这样 📊：

```
高地址
┌──────────────────────────────┐ ◄── MSP 初始值（如 0x20008000）
│                              │
│   SysTick 硬件异常帧          │ 32 bytes ← 硬件自动保存 R0-R3, R12, LR, PC, xPSR
│   (0x20007fe0 ~ 0x20007fff) │
├──────────────────────────────┤
│   SysTick_Handler push       │ 8 bytes  ← push {r3, lr}
│   (0x20007fd8 ~ 0x20007fdf) │
├──────────────────────────────┤
│   rtos_tick_handler push     │ 16 bytes ← push {r4, r5, r6, lr}
│   (0x20007fc8 ~ 0x20007fd7) │
├──────────────────────────────┤
│   rtos_sched push            │ 8 bytes  ← push {r4, lr}
│   (0x20007fc0 ~ 0x20007fc7) │
├──────────────────────────────┤
│ ⚠️ PendSV 硬件异常帧         │ 32 bytes ← 💥💥💥 覆盖了上面的栈帧！！！
│   (0x20007fa0 ~ 0x20007fbf) │
└──────────────────────────────┘
低地址
```

**PendSV 的异常帧（32 字节）** 正好覆盖了 `rtos_sched`、`rtos_tick_handler`、`SysTick_Handler` 的栈帧！🎯

### 😵 返回时的致命一击

PendSV 执行完上下文切换后返回，CPU 继续执行 SysTick。当 `SysTick_Handler` 执行 `pop {r3, pc}` 时：

```asm
pop  {r3, pc}    ; 从 0x20007fb8 加载 PC
```

但 `0x20007fb8` 此时是 **PendSV 异常帧的 PC 字段**，值为 `0x00000000` 🚫

CPU 尝试加载 PC = `0x00000000`，bit[0] = 0，立即触发 **INVPC** HardFault！💀

---

## ✅ 修复方案：把 PendSV 变成"老好人"

### 🛠️ 一行代码搞定

在 HAL 初始化之后、启动调度器之前，设置 PendSV 优先级：

```c
#include <stdint.h>

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    /* 🐢 关键：把 PendSV 设为最低优先级（15），让它乖乖排队 */
    *(volatile uint8_t *)0xE000ED22 = 0xF0;

    /* 初始化外设... */
    MX_GPIO_Init();
    MX_USART3_UART_Init();

    /* 创建任务... */
    rtos_task_create(task_high, "high", stack_high, 128, NULL, 2, NULL);
    rtos_task_create(task_low,  "low",  stack_low,  128, NULL, 1, NULL);

    rtos_scheduler_start();  /* 🚀 起飞！ */
}
```

> 💡 **为什么写 `0xF0` 而不是 `0x0F`？**  
> Cortex-M 的 SHPR 寄存器是 **8 位优先级字段**，但 STM32G4 只实现了高 4 位。`0xF0` = 优先级 15（最低）。

### 🎉 修复后的效果

```
🐌 SysTick prio = 15
🐌 PendSV prio = 15
```

现在它们**平起平坐**，PendSV 不会再抢占 SysTick：

```
⏱️ SysTick_Handler 运行完毕
   ↓ 返回线程模式
   ↓
   PendSV 执行（MSP 上只有它自己的帧）
   ↓ 切换任务上下文
   ↓ 返回线程模式
   ↓
🧵 新任务开始运行 —— 丝滑！✨
```

MSP 栈上永远只有 **一层** PendSV 异常帧，上下文切换安全无虞 🛡️

---

## 📋 RTOS 工程优先级配置最佳实践

参考 RT-Thread、FreeRTOS 的工程经验，Cortex-M 系统异常的推荐优先级如下：

| 异常 | 推荐优先级 | 说明 |
|------|:----------:|------|
| 🥇 外部硬件中断 | 0 ~ 4 | 最高实时性要求（如定时器捕获、外部触发） |
| 📞 SVC | 15（最低） | 用户态系统调用入口 |
| ⏱️ SysTick | 15（最低） | RTOS 心跳节拍 |
| 🐢 **PendSV** | **15（最低）** | **上下文切换专用** |

> 🎯 **核心原则**：PendSV 必须是整个系统中**最低优先级**的异常之一，确保它只在所有中断和异常处理完毕后执行。

### 🔧 FreeRTOS 的做法

```c
#define configKERNEL_INTERRUPT_PRIORITY         255  /* 最低优先级 */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    191  /* 可屏蔽的最高优先级 */
```

### 🔧 RT-Thread 的做法

在 `libcpu/arm/cortex-m4/context_gcc.S` 配套初始化代码中：

```c
/* 设置 PendSV 和 SysTick 为最低优先级 */
NVIC_SetPriority(PendSV_IRQn, 0xFF);
NVIC_SetPriority(SysTick_IRQn, 0xFF);
```

### 🔧 LinRTOS 的做法

直接在 `main()` 中写寄存器（零依赖）：

```c
*(volatile uint8_t *)0xE000ED22 = 0xF0;  /* PendSV = prio 15 */
```

---

## 🧪 快速自查清单

如果你的 RTOS 在真实硬件上出现以下症状，请立即检查 PendSV 优先级：

- [ ] 🩸 **HardFault** 发生在首次或随后的上下文切换时
- [ ] 📊 **CFSR** 的 INVPC 位（bit 18）被置位
- [ ] 🎛️ **CONTROL** 寄存器 = `0x00`（线程模式未切换到 PSP）
- [ ] 💻 QEMU 正常，**真机崩溃**
- [ ] 🔍 GDB 显示 MSP 栈帧被破坏，LR 或 PC 值异常

**诊断命令**（OpenOCD + GDB）：

```bash
(gdb) x/1xb 0xE000ED22   # 读取 PendSV 优先级
0xe000ed22: 0x00         # 🚨 危险了！默认最高优先级
0xe000ed22: 0xf0         # ✅ 安全，已设为最低
```

---

## 🎓 深入原理：为什么 QEMU 不会触发？

QEMU 模拟的是 Cortex-M3 核心行为，但它**不会**精确模拟中断嵌套和异常抢占的时序细节：

| 方面 | QEMU | 真实硬件 |
|------|:----:|:--------:|
| 指令执行 | 顺序执行，无流水线延迟 | ⚡ 有 3 级流水线、分支预测 |
| 中断延迟 | 固定周期 | ⚡ 受总线竞争、Flash 等待周期影响 |
| 异常嵌套 | 简化模型 | ⚡ **严格按 NVIC 优先级抢占** |
| 写缓冲 | 通常忽略 | ⚡ `DSB`/`ISB` 影响可见 |

因此，**QEMU 是一个优秀的功能验证工具，但不是时序和并发问题的可靠检测器** 🔬

> 💡 **建议**：任何涉及中断优先级、上下文切换、临界区的代码，都必须在**真实硬件**上验证！

---

## 📚 参考与延伸阅读

- 📖 [ARMv7-M Architecture Reference Manual — Exception Model](https://developer.arm.com/documentation/ddi0403)
- 📖 [Cortex-M3/M4 权威指南 — Joseph Yiu](https://www.sciencedirect.com/book/9780124080829/the-definitive-guide-to-arm-cortex-m3-and-cortex-m4-processors)
- 🔗 [FreeRTOS Cortex-M Port — PendSV 实现](https://www.freertos.org/RTOS-Cortex-M3-M4.html)
- 🔗 [RT-Thread Cortex-M 上下文切换源码](https://github.com/RT-Thread/rt-thread/tree/master/libcpu/arm/cortex-m4)
- 🔗 [Memfault Blog — Cortex-M RTOS Context Switching](https://interrupt.memfault.com/blog/cortex-m-rtos-context-switching)

---

## 🏆 总结

```
┌─────────────────────────────────────────┐
│                                         │
│   🐢 PendSV 必须是系统最低优先级！       │
│                                         │
│   一行代码：*(uint8_t*)0xE000ED22=0xF0  │
│                                         │
│   省去三天调试，远离 HardFault 😎        │
│                                         │
└─────────────────────────────────────────┘
```

**记住这个地址**：`0xE000ED22` —— 它是你的 RTOS 在真实硬件上稳定运行的**护身符** 🧿

---

> 📝 *本文档基于 LinRTOS 在 STM32G431CBUx 上的真实移植经验撰写。如有疑问，欢迎在 Issues 区讨论！* 🙌
