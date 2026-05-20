# 🗺️ LinRTOS 源码阅读路线图

> 🚧 本指南正在逐步完善中...

---

## 📖 第一章：RTOS 调度离不开的三个"硬件配角" 🎭

在 LinRTOS 里，真正干苦力活的是三个 Cortex-M 内置的 **系统异常** 🎯（你可以把它们理解为"CPU 自带的三个特殊中断"）。

RTOS 的大部分魔法 ✨，都是通过对这三个异常的精心编排实现的！

---

### 🥇 1.1 先理解一个前提：Cortex-M 的"双栈设计"

在深入三个异常之前，你必须先理解一件事：**Cortex-M 有两根栈指针** 📌！

| 栈指针 | 名字 | 谁在用 | 什么时候用 |
|:------:|:----:|:------:|:---------:|
| **MSP** 🏢 | Main Stack Pointer | 操作系统/中断 | 复位后默认、所有异常/中断处理函数 |
| **PSP** 🏠 | Process Stack Pointer | 用户任务 | 线程模式（Thread Mode）下执行任务代码 |

**通俗理解** 🧠：
- **MSP** 🏢 是"内核的办公桌"。中断来了 ⚡、操作系统调度了 🔄，CPU 自动切到 MSP 上干活！
- **PSP** 🏠 是每个任务的"私人工作台"。任务自己的局部变量 📦、函数调用压栈，都在 PSP 上进行～

**为什么要分开** ❓

想象一下 💭：如果任务 A 的栈溢出了 💥，但它和操作系统共用同一个栈（MSP），那么 A 的溢出就会破坏操作系统的中断现场 😱！两栈分离后，任务的脏水 🚰 不会泼到内核身上 🛡️～

LinRTOS 的设计就是：**任务跑在 PSP 上 🏠，调度器和异常处理跑在 MSP 上 🏢**

---

### ⏱️ 1.2 SysTick —— 系统的"节拍器" 🥁

**作用** 🎯：提供一个固定频率的周期性中断，就像时钟的"滴答"声 🔔～

**在 LinRTOS 中的职责** 📋：
1. 每来一个 SysTick，`g_kernel.tick_count` ➕1️⃣
2. 检查延时队列 ⏰：有没有任务睡够了该醒了 😴➡️🏃？
3. 检查时间片 🍕：同优先级任务该换班了吗 🔄？
4. 如果有更高优先级的任务就绪了 🏆，触发一次调度！

**代码位置** 📍：
- 硬件中断入口：`src/tick.c` 中的 `SysTick_Handler()`
- 实际逻辑：`src/tick.c` 中的 `rtos_tick_handler()`
- 初始化：`src/port/cortex_m/port.c` 中的 `rtos_port_init_systick()`

**关键参数** 🔧：
- `RTOS_TICK_RATE_HZ = 1000` 🎚️ 表示 1 秒 1000 个 tick，即每个 tick 1ms
- SysTick 的优先级通常设为最低（15）🐢，因为它不需要抢占其他中断～

**一句话总结** 📝：SysTick 是 RTOS 的"心跳" 💓，时间靠它推进！

---

### 🐢 1.3 PendSV —— 专门负责"换班"的异常 🔄

**作用** 🎯：执行上下文切换（Context Switch），也就是保存当前任务的现场 💾、恢复下一个任务的现场 📂！

**为什么不用 SysTick 直接切换** ❓

因为 SysTick 里可能还要干很多事情 📝（扫描延时队列 ⏰、处理时间片 🍕），如果在 SysTick 里直接切换，代码会很乱 😵‍💫。更重要的是：**PendSV 可以被"挂起"（pend）** 📌，它不会立即执行，而是等到所有其他中断都处理完了再执行～

**PendSV 的核心设计 —— 最低优先级** 🥉：

```
优先级：   高 0 🔥 ───────────────────────────────> 低 15 ❄️
           │  外部中断  │  SysTick  │  PendSV  │
           │   (0~4)   │   (15)    │   (15)   │
```

PendSV 的优先级必须是最低的 🐢。这意味着：
- 即使代码里"请求"了上下文切换 📢，PendSV 也不会马上执行 ⏸️
- 它会等到当前所有中断（包括正在跑的 SysTick ⏱️）都返回了，才执行 ✅
- 这样可以保证：**PendSV 执行时，MSP 上是干净的 🧼，不会嵌套在其他中断里**

**代码位置** 📍：
- 触发：`src/port/cortex_m/port.c` 中的 `rtos_port_request_switch()`（设置挂起位 📌）
- 处理：`src/port/cortex_m/port_asm.S` 中的 `PendSV_Handler`

**一句话总结** 📝：PendSV 是 RTOS 的"换班铃" 🔔，铃响了就换任务，但它总是等教室安静了 🤫 才响～

---

### 🔑 1.4 SVC —— 从"内核模式"切换到"任务模式"的启动键 🚀

**作用** 🎯：触发一次同步异常，在异常处理函数里完成从 MSP 到 PSP 的切换，并启动第一个任务 🏁！

**为什么需要 SVC** ❓

复位后，CPU 跑在 **Handler 模式 + MSP** 🏢。但任务代码必须跑在 **Thread 模式 + PSP** 🏠 上。

问题来了 🤔：**普通的 C 函数无法改变 CPU 的工作模式** 🚫！只有异常处理函数可以 ✅。

所以启动流程是这样的 🎬：
1. `main()` 初始化完毕 ✅，调用 `rtos_scheduler_start()`
2. `rtos_scheduler_start()` 选出第一个要运行的任务 🏆
3. 执行 `svc 0` 触发 SVC 异常 🚨
4. CPU 跳转到 `SVC_Handler`（Handler 模式，MSP）🏢
5. 在 `SVC_Handler` 里 🔧：
   - 设置 `CONTROL.SPSEL = 1`（告诉 CPU：Thread 模式以后用 PSP 🏠）
   - 从 TCB 读出任务的初始 `stack_ptr`，设置到 PSP
   - 恢复任务的 R4-R11
   - 返回时使用 `EXC_RETURN = 0xFFFFFFFD`（告诉 CPU：返回 Thread 模式 + PSP）
6. 硬件自动从 PSP 指向的栈弹出 R0-R3, R12, LR, PC, xPSR
7. **第一个任务开始运行** 🎉🎉🎉！

**代码位置** 📍：
- 触发：`src/task.c` 中的 `rtos_scheduler_start()` → `rtos_port_start_first_task()`
- 处理：`src/port/cortex_m/port_asm.S` 中的 `SVC_Handler`

**一句话总结** 📝：SVC 是 RTOS 的"点火开关" 🔑，只按一次 ⏏️，按下后 CPU 就从"内核模式"切换到"任务模式"，第一个任务开始执行 🏎️💨！

---

### 🤝 1.5 三兄弟的分工协作

用一张图看清楚它们的关系 🗺️：

```
系统启动阶段
    │
    ▼
┌─────────────┐
│ main() 运行  │  ← Handler 模式 🏢，MSP
│ 初始化完成   │
└─────────────┘
    │
    ▼
 SVC #0        ← SVC 点火 🔑
    │
    ▼
┌─────────────┐
│ SVC_Handler  │  ← 设置 CONTROL.SPSEL=1 🏠
│ 加载首个任务 │    设置 PSP
└─────────────┘
    │
    ▼
  返回          ← 进入 Thread 模式 🏠 + PSP
    │
    ▼
┌─────────────┐
│ 任务 A 运行  │  ← Thread 模式 🏠，PSP
└─────────────┘
    │
    │        SysTick 每隔 1ms 来一次 ⏱️
    │        （检查延时队列 ⏰、时间片 🍕）
    ▼
┌─────────────┐
│ 任务 A 调用  │
│ rtos_task_   │
│ delay(500)   │
└─────────────┘
    │
    ▼
  A 进入阻塞   ← 从就绪队列移除 ❌，加入延时队列 ✅
    │
    ▼
 rtos_sched()  ← 选出任务 B 🏆
    │
    ▼
 PendSV 挂起   ← 设置挂起位 📌
    │
    ▼
  当前中断返回  ← 假设此时在 SysTick 末尾 ⏱️
    │
    ▼
┌─────────────┐
│ PendSV_      │  ← Handler 模式 🏢，MSP
│ Handler      │    保存 A 的现场 💾 到 A 的 PSP 栈
│ 执行切换     │    恢复 B 的现场 📂 从 B 的 PSP 栈
└─────────────┘
    │
    ▼
  返回          ← 进入 Thread 模式 🏠 + PSP（B 的 PSP）
    │
    ▼
┌─────────────┐
│ 任务 B 运行  │  ← Thread 模式 🏠，PSP
└─────────────┘
```

---

### 🎯 1.6 阅读前的小测验

在继续读源码之前，确保你能回答 🤔：

1. **MSP 🏢 和 PSP 🏠 的区别是什么？任务用哪个？中断用哪个？**
2. **SysTick ⏱️ 的职责是什么？为什么它的优先级要低 🐢？**
3. **PendSV 🔄 的职责是什么？为什么它的优先级必须是最低的 🥉？**
4. **SVC 🔑 的职责是什么？为什么启动第一个任务必须用 SVC，而不能直接在 C 代码里跳转 🚫？**
5. **上下文切换时，为什么 PendSV 用 MSP 执行 🏢，但保存/恢复的却是 PSP 指向的任务栈 🏠？**

如果这五个问题你都能在心里给出答案 ✅，就可以进入下一章了 🚀！


---

## 📖 第二章：移植层 —— 从 C 到汇编的桥梁 🌉

这一章要读两个文件：

| 文件 | 语言 | 内容 |
|:----:|:----:|:----:|
| `src/port/cortex_m/port.c` | 🇨 C | 栈帧伪造 💾、临界区 🛡️、SysTick 初始化 ⏱️ |
| `src/port/cortex_m/port_asm.S` | 🔧 汇编 | SVC_Handler 🔑（点火）、PendSV_Handler 🔄（换班） |

**阅读顺序**：先读 C，再读汇编。因为汇编里的 `ldmia`、`stmdb` 操作的数据，都是 `port.c` 里的 `rtos_port_init_stack()` 提前布置好的 🎯

---

### 🎨 2.1 `rtos_port_init_stack()` —— 伪造一个"假现场"

**位置**：`src/port/cortex_m/port.c` 第 97 行

**问题**：任务创建时，代码还没跑过，哪来的"现场"可以恢复？

**答案**：我们**伪造**一个！🎭 在任务栈顶上预先写好一组数据，让硬件以为这个任务之前被中断打断过，现在只是恢复它而已～

#### 逐行拆解

```c
uint32_t *rtos_port_init_stack(uint32_t *stack_top, rtos_task_func_t func, void *param)
{
```

- `stack_top` 📍：用户提供的栈数组的末尾地址（最高地址）
- `func` 🎯：任务的入口函数（比如 `task_high`）
- `param` 📦：传给任务的参数

```c
    stack_top = (uint32_t *)(((uint32_t)stack_top) & ~7U);
```

**8 字节对齐** 🎚️。ARM 的 AAPCS 要求栈在公共接口处 8 字节对齐。`& ~7U` 就是把低 3 位清零，确保地址是 8 的倍数。

```c
    stack_top--;                    /* xPSR */
    *stack_top = 0x01000000;        /* Thumb 位必须置 1 */
```

**向下增长栈**，先减指针再写入。

`xPSR` = 程序状态寄存器。`0x01000000` 把 **T 位（Thumb 位）** 置 1。ARM Cortex-M 只支持 Thumb 指令集，如果这一位是 0，CPU 会触发 `INVSTATE` HardFault 💀

```c
    stack_top--;                    /* PC */
    *stack_top = (uint32_t)func;
```

**PC** = 程序计数器。这里写入任务的入口函数地址。

当 SVC/PendSV 返回时，硬件会自动从栈上弹出 PC，然后**跳转到这个地址执行** 🚀。这就是任务能"自动开始运行"的秘密！

```c
    stack_top--;                    /* LR */
    *stack_top = (uint32_t)rtos_task_exit_trampoline;
```

**LR** = 链接寄存器。正常情况下，函数返回时 CPU 从 LR 读取返回地址。

这里放的是 `rtos_task_exit_trampoline()` 的地址——一个兜底函数。如果任务函数写错了，真的执行到了 `return`，就会跳到这里，然后调用 `rtos_task_delete(NULL)` 把自己删掉 🗑️。防止任务返回后 CPU 去执行非法地址！

```c
    stack_top--;                    /* R12 */
    *stack_top = 0;
    stack_top--;                    /* R3 */
    *stack_top = 0;
    stack_top--;                    /* R2 */
    *stack_top = 0;
    stack_top--;                    /* R1 */
    *stack_top = 0;
    stack_top--;                    /* R0 */
    *stack_top = (uint32_t)param;
```

**R0** 是函数的第一个参数。所以任务函数 `void task_high(void *param)` 收到的 `param`，就是这里写入的值！

R1、R2、R3、R12 暂时没用，填 0 占位。

```c
    stack_top--;                    /* R11 */
    *stack_top = 0;
    /* ... R10, R9, R8, R7, R6, R5, R4 全部填 0 ... */
    stack_top--;                    /* R4 */
    *stack_top = 0;

    return stack_top;
}
```

**R4-R11** 是"被调用者保存寄存器"。任务第一次运行时，这些寄存器本来就不需要恢复（因为任务从来没跑过），全部填 0。

**返回值** `stack_top` 就是 TCB 里的 `stack_ptr` 字段。这个值最终会被 `SVC_Handler` 读出来，设置到 PSP。

#### 伪造完成后的栈布局 📊

```
高地址（栈底 / 数组末尾）
┌─────────────────────────────┐
│  xPSR = 0x01000000          │  ← 最先写入，地址最高
│  PC   = func (任务入口)      │  ← 🔑 关键！返回后跳到这里
│  LR   = exit_trampoline     │  ← 兜底，防止任务真的 return
│  R12  = 0                   │
│  R3   = 0                   │
│  R2   = 0                   │
│  R1   = 0                   │
│  R0   = param               │  ← 任务收到的第一个参数
├─────────────────────────────┤
│  R11  = 0                   │
│  R10  = 0                   │
│  R9   = 0                   │
│  R8   = 0                   │
│  R7   = 0                   │
│  R6   = 0                   │
│  R5   = 0                   │
│  R4   = 0                   │  ← 最后写入，地址最低
└─────────────────────────────┘
低地址 ← stack_ptr（返回值，TCB 保存这个值）
```

**思考题** 🧠：为什么伪造帧里同时有"软件保存区"（R4-R11）和"硬件自动保存区"（R0-R3, R12, LR, PC, xPSR）？因为 Cortex-M 的异常返回机制就是这么设计的——硬件只自动弹出后 8 个，前 8 个（R4-R11）需要软件手动恢复！

---

### 🛡️ 2.2 临界区 —— 关中断的艺术

**位置**：`src/port/cortex_m/port.c` 第 51 行

```c
uint32_t rtos_port_enter_critical(void)
{
    uint32_t primask;
    __asm volatile (
        "mrs %0, primask\n"   /* 把 PRIMASK 的当前值读入 primask 变量 */
        "cpsid i\n"           /* 关中断！（Disable IRQ） */
        : "=r" (primask)      /* 输出：primask = 原来的 PRIMASK */
        :
        : "memory"            /* 告诉编译器：这段代码可能访问内存 */
    );
    return primask;
}
```

**人话翻译** 🗣️：
1. `mrs %0, primask` —— "看看 PRIMASK 现在是几？记下来 📋"
2. `cpsid i` —— "把中断关掉！🚫"
3. 返回记下来的旧值

为什么要记旧值？为了**支持嵌套**！🪆

```c
void rtos_port_exit_critical(uint32_t state)
{
    __asm volatile (
        "msr primask, %0\n"   /* 直接把原来的 PRIMASK 值写回去 */
        :: "r" (state)
        : "memory"
    );
}
```

**人话翻译** 🗣️：把进入临界区前保存的 PRIMASK 旧值，原样写回寄存器。进入前是开的就是开的，是关的就是关的——**精确恢复**，不瞎猜！

**使用方式**（宏包装）：

```c
#define RTOS_ENTER_CRITICAL()   \
    uint32_t _critical_state = rtos_port_enter_critical()

#define RTOS_EXIT_CRITICAL()    \
    rtos_port_exit_critical(_critical_state)
```

典型用法：
```c
RTOS_ENTER_CRITICAL();   /* 中断关闭 🚫 */
/* 操作链表... */
RTOS_EXIT_CRITICAL();    /* 恢复之前的中断状态 */
```

---

### ⏱️ 2.3 SysTick 初始化

**位置**：`src/port/cortex_m/port.c` 第 22 行

```c
void rtos_port_init_systick(uint32_t tick_hz)
{
    if (s_core_clock == 0) {
        s_core_clock = (SystemCoreClock) ? SystemCoreClock : 8000000U;
    }

    uint32_t reload = s_core_clock / tick_hz - 1;

    volatile uint32_t *ctrl  = (volatile uint32_t *)0xE000E010;
    volatile uint32_t *load  = (volatile uint32_t *)0xE000E014;
    volatile uint32_t *val   = (volatile uint32_t *)0xE000E018;

    *load = reload;
    *val  = 0;
    /* ENABLE(1) | TICKINT(2) | CLKSOURCE(4) */
    *ctrl = 0x07;
}
```

**寄存器地址速查** 📍：

| 地址 | 寄存器 | 作用 |
|:----:|:------:|:----:|
| `0xE000E010` | CTRL | 控制寄存器：使能、中断使能、时钟源 |
| `0xE000E014` | LOAD | 重装载值：计数到 0 后自动装回这个值 |
| `0xE000E018` | VAL | 当前值：写 0 会清零计数器 |

**`0x07` 的含义** 🔧：`ENABLE(1) | TICKINT(2) | CLKSOURCE(4)` = `0b111` = 7
- bit 0 = 1：使能 SysTick ⏱️
- bit 1 = 1：使能 SysTick 中断（计数到 0 时触发）🔔
- bit 2 = 1：使用处理器时钟（HCLK），而不是外部时钟

**reload 的计算** 🧮：如果主频 170 MHz，tick 频率 1000 Hz，那么 `reload = 170000000 / 1000 - 1 = 169999`。计数器从 169999 减到 0，正好 1ms。

---

### 🔑 2.4 `SVC_Handler` —— 点火启动第一个任务 🚀

**位置**：`src/port/cortex_m/port_asm.S` 第 29 行

这是你最值得逐行读懂的汇编代码！建议打开源码文件对照阅读 📖

```asm
    .thumb_func
SVC_Handler:
```

`.thumb_func` 告诉汇编器这是一个 Thumb 函数，需要正确设置 LSB 位。

#### 第 1 步：切换线程模式到 PSP 🏠

```asm
    movs    r0, #0x02       /* r0 = 2，对应 CONTROL.SPSEL 位 */
    msr     control, r0     /* 写 CONTROL 寄存器：线程模式以后用 PSP！ */
    isb                     /* 指令同步屏障：确保上面的写操作生效 */
```

**人话** 🗣️：告诉 CPU——"以后线程模式别用 MSP 了，改用 PSP！"

`isb` 很重要！没有它，CPU 可能还在用旧模式执行下一条指令 😵

#### 第 2 步：加载第一个任务的栈指针 📂

```asm
    ldr     r3, =rtos_current_tcb   /* r3 = &rtos_current_tcb（变量的地址） */
    ldr     r1, [r3]                /* r1 = rtos_current_tcb（TCB 指针） */
    ldr     r0, [r1]                /* r0 = current_task->stack_ptr */
```

**人话** 🗣️：
1. `r3` 拿到 `rtos_current_tcb` 这个全局变量的地址
2. `r1` 从那个地址里读出值——也就是第一个任务的 TCB 指针
3. `r0` 从 TCB 的偏移 0 处读取 `stack_ptr`（就是 `rtos_port_init_stack()` 返回的那个值！）

回忆一下 `rtos_port_init_stack()` 伪造的栈布局：
```
高地址
│  R11 = 0      │
│  ...          │
│  R4  = 0      │
├───────────────┤ ◄── r0（stack_ptr）
│  xPSR         │
│  PC = func    │
│  LR           │
│  R12          │
│  R3-R0        │
└───────────────┘
```

所以此时 `r0` 指向的是 **R4 的位置**（R4-R11 区域的顶部）。

#### 第 3 步：恢复 R4-R11

```asm
    ldmia   r0!, {r4-r11}   /* 从 r0 指向的地址，弹出 R4 到 R11 */
                            /* r0 自动递增（! 后缀） */
```

**人话** 🗣️：把伪造栈里的 R4-R11 值（全是 0）加载到寄存器里。每弹出一个 32 位值，`r0` 自动 +4。

弹完 8 个寄存器后，`r0` 增加了 32 字节，现在指向 **xPSR** 的位置！

#### 第 4 步：设置 PSP

```asm
    msr     psp, r0         /* PSP = r0，现在 PSP 指向硬件异常帧 */
    isb
```

**人话** 🗣️：把更新后的 `r0` 写入 PSP。现在 PSP 指向 xPSR/PC/LR/R12/R3-R0 那一片区域——也就是硬件异常返回时会自动弹出的区域！

#### 第 5 步：异常返回 🎉

```asm
    ldr     r0, =0xFFFFFFFD /* r0 = EXC_RETURN：返回线程模式 + PSP */
    bx      r0              /* 跳转！CPU 看到 0xFFFFFFFD 就知道是异常返回 */
```

**人话** 🗣️：`0xFFFFFFFD` 是一个特殊的魔法值 ✨，叫做 **EXC_RETURN**。当 CPU 执行 `BX 0xFFFFFFFD` 时：

1. 从 **PSP** 指向的栈弹出 R0, R1, R2, R3, R12, LR, PC, xPSR（8 个寄存器，32 字节）
2. **PC** 被赋值为 `func`（任务的入口地址）
3. **xPSR** 被赋值为 `0x01000000`（Thumb 模式）
4. **R0** 被赋值为 `param`
5. 切换到 **Thread 模式**
6. 使用 **PSP** 作为栈指针
7. 跳转到 PC 指向的地址执行

**第一个任务开始运行了** 🎊🎊🎊！

---

### 🔄 2.5 `PendSV_Handler` —— 日常上下文切换

**位置**：`src/port/cortex_m/port_asm.S` 第 58 行

PendSV 比 SVC 多了一步：**保存当前任务**，然后再恢复下一个任务。

#### 第 1 步：保存当前任务的 R4-R11 💾

```asm
    mrs     r0, psp             /* r0 = 当前任务的 PSP */
    stmdb   r0!, {r4-r11}       /* 把 R4-R11 压入当前任务的 PSP 栈 */
                                /* r0 自动递减（! 后缀，db = Decrement Before） */
```

**人话** 🗣️：
1. `mrs r0, psp` —— 看看当前任务跑到哪了（读取 PSP）
2. `stmdb r0!, {r4-r11}` —— **在任务的私有栈上保存现场**！

`stmdb` = Store Multiple, Decrement Before。意思是：先减地址，再存数据。压入 8 个寄存器（32 字节），`r0` 减少了 32。

**为什么只保存 R4-R11，不保存 R0-R3 那些？**

因为 R0-R3, R12, LR, PC, xPSR 这 8 个寄存器，在进入 PendSV 时已经被**硬件自动保存**到 PSP 栈上了（异常发生时的自动压栈）。PendSV 只需要保存软件负责的 R4-R11。

#### 第 2 步：把新的栈顶写回 TCB 📝

```asm
    ldr     r3, =rtos_current_tcb   /* r3 = &rtos_current_tcb */
    ldr     r2, [r3]                /* r2 = rtos_current_tcb（旧 TCB 指针） */
    str     r0, [r2]                /* old_tcb->stack_ptr = r0（更新栈顶） */
```

**人话** 🗣️：当前任务被切换出去了，把它的"书签位置"（最新的 PSP）记回 TCB，下次切回来才知道从哪恢复！

#### 第 3 步：切换到下一个任务 🏆

```asm
    ldr     r2, =rtos_next_tcb      /* r2 = &rtos_next_tcb */
    ldr     r2, [r2]                /* r2 = rtos_next_tcb（新 TCB 指针） */
    str     r2, [r3]                /* rtos_current_tcb = rtos_next_tcb */
```

**人话** 🗣️：把全局变量 `rtos_current_tcb` 更新为下一个任务。

```asm
    ldr     r3, =g_kernel           /* r3 = &g_kernel */
    str     r2, [r3, #260]          /* g_kernel.current_task = 新任务 */
```

**人话** 🗣️：同时更新 C 代码里的 `g_kernel.current_task`，让 `rtos_task_delay()` 等函数知道现在谁正在跑。

`#260` 是结构体偏移量。`g_kernel` 里 `current_task` 字段前面有 `ready_list[32]`（32 × 8 = 256 字节）+ `ready_map`（4 字节）+ padding，所以 `current_task` 在偏移 260 处。

#### 第 4 步：恢复下一个任务的 R4-R11 📂

```asm
    ldr     r0, [r2]                /* r0 = new_tcb->stack_ptr */
    ldmia   r0!, {r4-r11}           /* 从新任务的栈弹出 R4-R11 */
                                    /* r0 自动递增 */
```

**人话** 🗣️：从新任务的 TCB 读出它上次被切出去时保存的栈顶，然后把 R4-R11 弹出来。

弹完 8 个寄存器后，`r0` 增加了 32，现在指向 **xPSR** 的位置（和 SVC 一样）。

#### 第 5 步：更新 PSP 并返回

```asm
    msr     psp, r0                 /* PSP = r0 */
    isb

    ldr     r0, =0xFFFFFFFD         /* EXC_RETURN：线程模式 + PSP */
    bx      r0
```

**人话** 🗣️：和 SVC 的最后一模一样！更新 PSP，然后异常返回。硬件自动从 PSP 弹出 R0-R3, R12, LR, PC, xPSR，新任务开始执行～

---

### 🎯 2.6 SVC vs PendSV 对比表

| 对比项 | SVC_Handler 🔑 | PendSV_Handler 🔄 |
|:------:|:-------------:|:----------------:|
| **触发时机** | 启动调度器时，只执行一次 | 每次需要换任务时 |
| **是否有保存操作** | ❌ 没有（第一次运行，没现场可存） | ✅ 有（先保存当前任务） |
| **是否有切换 TCB** | ❌ 没有（current_tcb 已提前设好） | ✅ 有（rtos_current_tcb = rtos_next_tcb） |
| **最后一步** | 恢复 R4-R11 → 设置 PSP → EXC_RETURN | 恢复 R4-R11 → 设置 PSP → EXC_RETURN |
| **返回值** | `0xFFFFFFFD`（线程模式 + PSP） | `0xFFFFFFFD`（线程模式 + PSP） |

---

### 📝 2.7 阅读后的小测验

1. **`rtos_port_init_stack()` 为什么要在栈上伪造 xPSR、PC、LR 这些寄存器？xPSR 的 `0x01000000` 是什么意思？**
2. **`stack_top--` 为什么是先减指针再写数据？这说明了栈是向哪个方向增长的？**
3. **SVC_Handler 里的 `ldmia r0!, {r4-r11}` 执行完后，`r0` 指向哪里？为什么紧接着 `msr psp, r0` 就能让 PSP 指向正确的位置？**
4. **PendSV_Handler 里的 `stmdb r0!, {r4-r11}` 和 `ldmia r0!, {r4-r11}` 有什么对称关系？它们和 SVC_Handler 里的 `ldmia` 有什么共同点？**
5. **`0xFFFFFFFD` 是什么？为什么异常返回时 CPU 看到它就知道要"从 PSP 弹栈、切到线程模式"？**

能答对 5 题，移植层你就彻底通关了 🏆！
