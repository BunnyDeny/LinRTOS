# LinRTOS 源码阅读路线图

> 本指南为**熟悉 C 语言、但不熟悉 ARM 汇编**的读者设计，按从易到难、从概念到实现的顺序编排。建议按步骤阅读，不要跳跃。

---

## 📋 阅读前的最小知识储备

在开始之前，你只需要确认自己理解以下概念。如果不确定，先花 10 分钟搜索补一下：

| 概念 | 为什么需要 | 快速自查 |
|------|-----------|---------|
| **函数指针** | 任务入口以函数指针形式保存 | `void (*fp)(void *) = my_task;` 能看懂吗？ |
| **结构体 + 指针** | TCB、链表节点全是结构体嵌套指针 | `struct node *p = head->next;` 能看懂吗？ |
| **双向链表** | 就绪队列、延时队列都是双向链表 | 知道 `prev`/`next` 如何插入删除吗？ |
| **位运算** | 就绪位图用位掩码实现 O(1) 查找 | `(1u << 3)` 知道是第 3 位置 1 吗？ |
| **中断基本概念** | RTOS 靠中断驱动调度 | 知道"中断打断当前代码、执行 ISR、然后返回"吗？ |

**不需要提前掌握**（文档中会逐步解释）：
- ARM 汇编指令（`push`/`pop`/`ldr`/`str` 等）
- Cortex-M 异常模型（SVC/PendSV/SysTick 区别）
- 栈帧布局（R0-R15、xPSR、LR、PC）
- `PRIMASK` / `CONTROL` 等特殊寄存器

---

## 第一步：建立整体认知（10 分钟）

**目标**：不看任何源码，先知道 RTOS "应该做什么"。

### 1.1 读 `README.md`

只看"系统架构"和"API 速查"两节。了解：

- LinRTOS 有几层？每层做什么？
- 任务有哪些状态？状态之间怎么转换？
- 有哪些 API？（先记名字，不用看实现）

### 1.2 画一张草图

拿纸笔或在脑中构建这张图：

```
[你的任务代码] --调用--> [任务 API：create/delay/yield]
                                |
                                v
                      [调度器：谁该运行？]
                                |
                                v
                      [滴答中断：时间到了吗？]
                                |
                                v
                      [上下文切换：换栈、换寄存器]
```

**检验标准**：能用自己的话解释"任务延时 500ms 期间，CPU 在干什么"。

---

## 第二步：数据结构与类型（20 分钟）

**目标**：搞清楚 RTOS 用哪些"数据结构"描述任务和系统状态。

### 2.1 `include/types.h`

这是最简单的文件，没有逻辑，只有定义：

- `rtos_task_handle_t` 是什么类型？（`void *`，也就是指针）
- `rtos_err_t` 有哪些错误码？
- `rtos_task_state_t` 有哪些状态？

**思考问题**：为什么任务句柄设计成 `void *` 而不是具体的 `struct rtos_tcb *`？

### 2.2 `include/config.h`

全是宏开关和常量：

- `RTOS_MAX_PRIORITIES = 32` —— 最多支持多少优先级
- `RTOS_TICK_RATE_HZ = 1000` —— 1 秒多少个 tick
- `RTOS_ENABLE_TIME_SLICING` —— 是否允许同优先级任务轮流执行

**思考问题**：如果把 `RTOS_MAX_PRIORITIES` 改成 64，哪些地方会受影响？（提示：看 `kernel.h` 中的位图类型）

### 2.3 `include/list.h`

LinRTOS 自己实现的最小双向链表。这是你遇到的**第一个有逻辑的文件**，但非常简单：

```c
struct rtos_list_node {
    struct rtos_list_node *prev;
    struct rtos_list_node *next;
};
```

重点读这三个函数（其他都是宏或内联）：
- `rtos_list_init()` —— 初始化成"自己指向自己"
- `rtos_list_insert_before()` —— 在指定节点前插入
- `rtos_list_remove()` —— 从链表中摘除自己

**画图辅助**：拿纸笔画 3 个节点，手动走一遍 `insert_before` 和 `remove` 的指针操作。

**检验标准**：能独立画出"向空链表插入第一个节点"后，头节点和新节点的 `prev`/`next` 指向。

---

## 第三步：内核核心数据结构（15 分钟）

**目标**：理解"任务控制块"和"内核全局状态"长什么样。

### 3.1 `include/kernel.h`

这是整个 RTOS 的"心脏蓝图"。分两部分读：

#### TCB（任务控制块）`struct rtos_tcb`

逐字段理解：

| 字段 | 含义 | 类比 |
|------|------|------|
| `stack_ptr` | 当前栈顶位置（任务被切换出去时的 PSP） | 书签，标记读到哪一页 |
| `stack_base` / `stack_top` / `stack_size` | 栈的边界信息 | 书的封面和封底 |
| `priority` | 优先级，数字越大越高 | VIP 等级 |
| `time_slice` | 还剩多少时间片 | 游戏角色的行动点 |
| `state` | 当前状态（就绪/运行/阻塞/挂起/删除） | 人物状态 |
| `ready_node` / `delay_node` | 链表节点，用于插入就绪队列 / 延时队列 | 排队号码牌 |
| `wake_tick` | 阻塞到何时唤醒（绝对 tick 数） | 闹钟设定时间 |

**关键理解**：一个任务同时只会出现在**一个**队列中：要么在就绪队列（`ready_node` 被使用），要么在延时队列（`delay_node` 被使用），要么都不在（挂起/删除）。

#### 内核全局状态 `struct rtos_kernel`

重点字段：

- `ready_list[32]` —— 32 条链表，每条对应一个优先级
- `ready_map` —— 32 位位图，第 `i` 位为 1 表示 `ready_list[i]` 非空
- `current_task` —— 谁正在 CPU 上跑
- `delay_list` —— 所有在睡大觉的任务，按醒来时间排序
- `tick_count` —— 系统从启动到现在过了多少个 tick

**思考问题**：
- 为什么 `ready_map` 用位图而不用链表遍历？（答案：O(1) vs O(n)）
- `delay_list` 为什么要"按 wake_tick 升序排列"？（答案：tick 中断只需检查队列头部，到期就继续，没到期就退出）

---

## 第四步：任务管理（C 语言核心，40 分钟）

**目标**：理解任务从"出生"到"死亡"的完整生命周期。

### 4.1 `include/task.h`

先过一遍 API 声明，建立"任务能做什么"的概念：

- 创建 / 删除
- 延时 / 周期性延时 / 主动让出
- 挂起 / 恢复
- 查询（优先级、状态、剩余栈空间）

### 4.2 `src/task.c` —— 逐函数阅读

建议按以下顺序，**每读完一个函数，在纸上画一下 TCB 和链表的变化**。

#### ① `rtos_tcb_alloc()` / `rtos_tcb_free()`

静态对象池管理。理解：
- `s_tcb_pool[RTOS_MAX_TASKS]` 是预分配的数组
- `s_tcb_used_mask` 用位掩码记录哪个槽位被占用
- 分配时 `memset(tcb, 0, sizeof(*tcb))` 清零所有字段

#### ② `rtos_task_create()`

任务的"出生证明"。流程：
1. 参数检查（函数指针非空、栈大小足够、优先级合法）
2. 分配 TCB
3. 填充 TCB 字段（名字、栈边界、优先级、状态）
4. **调用 `rtos_port_init_stack()` 构造初始栈帧** ← 这里会跳转到汇编世界，先记一笔，后面再回来
5. 插入就绪队列
6. 如果调度器已运行且新任务优先级更高，触发调度

**重点**：`rtos_port_init_stack()` 的返回值是 `stack_ptr`，也就是任务第一次运行时的 PSP。这个值很关键，后面看汇编时会解释它是怎么算出来的。

#### ③ `rtos_task_delay()`

任务说"我要睡 500ms"之后发生了什么：
1. 计算唤醒时间 `wake_tick = 当前tick + ticks`
2. 从就绪队列摘除自己（`rtos_task_unready`）
3. 按 `wake_tick` 升序插入延时队列
4. **触发调度**（`rtos_sched()`）

**思考问题**：为什么 `rtos_task_delay(0)` 不阻塞，而是调用 `rtos_task_yield()`？

#### ④ `rtos_task_suspend()` / `rtos_task_resume()`

- 挂起 = 从就绪队列摘除，状态改成 `SUSPENDED`
- 恢复 = 插回就绪队列，如果优先级更高则触发调度

#### ⑤ `rtos_scheduler_start()`

调度器启动的"点火"流程：
1. 初始化内核（`rtos_kernel_init`）
2. 创建空闲任务（`rtos_create_idle_task`）
3. 初始化 SysTick（`rtos_port_init_systick`）
4. 选出最高优先级就绪任务作为第一个运行的任务
5. **调用 `rtos_port_start_first_task()` 触发 SVC** ← 第二次跳转到汇编世界

### 4.3 `src/sched.c` —— 调度器核心

#### ① `rtos_kernel_init()`

初始化 `ready_list[]` 数组和 `delay_list`，全部设成空循环链表。

#### ② `rtos_task_ready()` / `rtos_task_unready()`

- `ready`：把任务的 `ready_node` 插入对应优先级的链表头，并置位 `ready_map`
- `unready`：从链表摘除，如果该优先级链表空了则清 `ready_map` 位

#### ③ `rtos_pick_highest_ready()`

**本文件最精华的一行代码**：

```c
int prio = 31 - __builtin_clz(g_kernel.ready_map);
```

- `__builtin_clz` = Count Leading Zeros，数一个 32 位整数前面有多少个 0
- 例如 `ready_map = 0b0000_0000_0000_0000_0000_0000_0101_0000`
  - 前面有 25 个 0，所以 `clz = 25`
  - `31 - 25 = 6`，最高优先级是 6

**检验标准**：能手动算出任意 `ready_map` 值对应的优先级。

#### ④ `rtos_sched()`

调度器的"决策中枢"：
1. 如果调度器没启动，或调度锁大于 0，直接返回
2. 用 `rtos_pick_highest_ready()` 选出下一个该运行的任务
3. 如果下一个任务和当前任务不同，调用 `rtos_port_request_switch()`

**关键问题**：`rtos_port_request_switch()` 里做了什么？（答案：设置了 PendSV 挂起位，等当前中断退出后自动执行上下文切换）

#### ⑤ `rtos_sched_time_slice()`（可选）

如果时间片耗尽，把当前任务移到同优先级链表尾部，让下一个同优先级任务有机会运行。

---

## 第五步：节拍处理（15 分钟）

**目标**：理解"时间是怎么推进的"。

### 5.1 `src/tick.c`

`SysTick_Handler` 是硬件中断入口，它调用 `rtos_tick_handler()`。

`rtos_tick_handler()` 的逻辑：
1. `tick_count++` —— 全局时间 +1
2. **扫描延时队列**：从头到尾检查，如果 `tick_count >= wake_tick`，说明任务该醒了
   - 从延时队列摘除
   - 插入就绪队列
3. **时间片递减**：如果开启时间片轮转，当前运行任务的时间片 -1
4. **尝试调度**：如果调度器已运行且没上锁，调用 `rtos_sched()`

**思考问题**：
- 为什么 `delay_list` 按 `wake_tick` 升序排列？（答案：一旦遇到没到期的，后面的肯定也没到期，直接 `break`）
- `tick_count` 会溢出吗？溢出后 `(tick_count - wake_tick)` 的计算还正确吗？（答案：使用 `int32_t` 有符号减法，溢出后对结果没有影响，这是 RTOS 中经典的 tick 溢出安全技巧）

---

## 第六步：移植层 C 部分（20 分钟）

**目标**：理解"C 代码如何与硬件打交道"。

### 6.1 `include/port.h`

先过一遍接口声明。重点关注：
- `rtos_port_init_stack()` —— 给新任务"伪造"一个栈帧，让它第一次运行时像是从中断返回一样
- `rtos_port_enter_critical()` / `rtos_port_exit_critical()` —— 关中断 / 开中断
- `rtos_port_request_switch()` —— 触发 PendSV
- `rtos_port_start_first_task()` —— 触发 SVC

### 6.2 `src/port/cortex_m/port.c`

#### ① `rtos_port_init_systick()`

直接写寄存器配置 SysTick。地址 `0xE000E010` 是 Cortex-M 的 SysTick 控制寄存器基地址。

#### ② 临界区 `rtos_port_enter_critical()` / `rtos_port_exit_critical()`

用内联汇编读写 `PRIMASK` 寄存器：
- `cpsid i` —— 关中断（设置 PRIMASK = 1）
- `cpsie i` —— 开中断（设置 PRIMASK = 0）

**注意**：`enter` 返回的是**进入前的 PRIMASK 值**，`exit` 时如果原来就是 1（中断本来就没开），就不会错误地开中断。这支持临界区嵌套。

#### ③ `rtos_port_init_stack()` —— 本文件最重要的函数

这个函数在栈顶构造一个"假"的异常帧，让任务第一次运行时，硬件会以为这是从中断返回：

```
高地址
┌─────────────────────────────┐ ◄── stack_top（传入参数，已 8 字节对齐）
│          ...                │
├─────────────────────────────┤
│  R11  = 0  （软件保存）      │
│  R10  = 0                   │
│  R9   = 0                   │
│  R8   = 0                   │
│  R7   = 0                   │
│  R6   = 0                   │
│  R5   = 0                   │
│  R4   = 0                   │
├─────────────────────────────┤ ◄── 返回值 stack_ptr（存到 TCB）
│  xPSR = 0x01000000          │     Thumb 位必须置 1
│  PC   = 任务入口函数地址     │
│  LR   = rtos_task_exit_trampoline │
│  R12  = 0                   │
│  R3   = 0                   │
│  R2   = 0                   │
│  R1   = 0                   │
│  R0   = param               │
└─────────────────────────────┘
低地址
```

**为什么这样设计？**

Cortex-M 的异常返回机制：当中断处理程序执行 `BX LR`（LR = `0xFFFFFFFD`）时，硬件会自动从 PSP 指向的栈上弹出 R0-R3、R12、LR、PC、xPSR，然后跳转到 PC 指向的地址执行。

所以我们提前在栈上放好这些值，让硬件"以为"这个任务之前被中断打断过，现在只是恢复它。

---

## 第七步：ARM 汇编入门 + 上下文切换（60 分钟）

> ⚠️ **如果你是汇编新手，不要慌**。这一节你只需要理解"每一行在做什么"，不需要记住指令编码。建议打开 [ARM Cortex-M 指令集速查](https://developer.arm.com/documentation/dui0473/m/arm-and-thumb-instructions) 对照阅读。

### 7.1 最小必要汇编知识（5 分钟）

在读代码前，先记住这几条指令的"人话翻译"：

| 指令 | 人话 | 示例 |
|------|------|------|
| `push {r4, r5, lr}` | 把 r4、r5、lr 压入当前栈，同时 sp 自减 | 保存寄存器，准备调用函数 |
| `pop {r4, r5, pc}` | 从栈弹出到 r4、r5、pc，同时 sp 自增 | 恢复寄存器，并跳转返回 |
| `ldr r0, =0x1234` | 把 0x1234 装入 r0 | 加载常数 |
| `ldr r0, [r1]` | 把 r1 指向的内存值读入 r0 | 读内存 |
| `str r0, [r1]` | 把 r0 写入 r1 指向的内存 | 写内存 |
| `mrs r0, psp` | 把 PSP 寄存器的值读入 r0 | 读特殊寄存器 |
| `msr psp, r0` | 把 r0 写入 PSP 寄存器 | 写特殊寄存器 |
| `bx lr` | 跳转到 lr 指向的地址 | 函数返回 |
| `isb` | 指令同步屏障，确保前面的指令都执行完 | 类似内存栅栏 |
| `svc #0` | 触发 SVC 异常（软件中断） | 系统调用 |

### 7.2 `src/port/cortex_m/port_asm.S`

这是整个 RTOS 最硬核的部分，也是你最值得花时间理解的文件。**建议一行一行读，配合注释。**

#### ① SVC_Handler —— 首次启动任务

场景：`rtos_scheduler_start()` 里调用了 `svc 0`，CPU 进入 Handler 模式执行 SVC_Handler。

代码逻辑：
1. `mrs r0, psp` —— 读 PSP（此时 PSP 还没初始化，值可能无意义，先不管）
2. `movs r0, #0x02` + `msr control, r0` —— **设置 CONTROL.SPSEL = 1**，告诉 CPU：以后 Thread 模式用 PSP，Handler 模式继续用 MSP
3. `isb` —— 确保 CONTROL 写操作生效
4. `ldr r3, =rtos_current_tcb` —— r3 = `rtos_current_tcb` 变量的地址
5. `ldr r1, [r3]` —— r1 = `rtos_current_tcb` 的值（即第一个任务的 TCB 指针）
6. `ldr r0, [r1]` —— r0 = `tcb->stack_ptr`（我们在 `port.c` 里构造的初始栈顶）
7. `ldmia r0!, {r4-r11}` —— 从栈上弹出 R4-R11（我们当初填的都是 0），同时 r0 自增
8. `msr psp, r0` —— **设置 PSP = r0**（此时 PSP 指向硬件异常帧的上方）
9. `ldr r0, =0xFFFFFFFD` —— r0 = EXC_RETURN，告诉 CPU：返回 Thread 模式，使用 PSP
10. `bx r0` —— 异常返回！

**异常返回时硬件自动做什么？**

CPU 看到 `0xFFFFFFFD`，知道要从 PSP 指向的栈上弹出 8 个寄存器（R0-R3, R12, LR, PC, xPSR），然后跳转到 PC 指向的地址（即任务的入口函数），并使用 PSP 作为 Thread 模式的栈指针。

**恭喜你，第一个任务开始运行了！**

#### ② PendSV_Handler —— 日常上下文切换

场景：某个 tick 到期，高优先级任务就绪，`rtos_sched()` 设置了 PendSV 挂起位。当前中断（SysTick）返回后，PendSV 执行。

代码逻辑分两段：**保存当前任务** + **恢复下一个任务**。

**保存当前任务：**
1. `mrs r0, psp` —— r0 = 当前任务的 PSP（指向硬件异常帧上方）
2. `stmdb r0!, {r4-r11}` —— 把 R4-R11 压入当前任务的栈（硬件只自动保存了 R0-R3, R12, LR, PC, xPSR，软件负责保存剩下的），r0 自减
3. `ldr r3, =rtos_current_tcb` —— r3 = `rtos_current_tcb` 变量地址
4. `ldr r2, [r3]` —— r2 = 当前 TCB 指针
5. `str r0, [r2]` —— **把新的栈顶写回 `tcb->stack_ptr`**

**切换任务指针：**
6. `ldr r2, =rtos_next_tcb` —— r2 = `rtos_next_tcb` 变量地址
7. `ldr r2, [r2]` —— r2 = 下一个任务的 TCB 指针
8. `str r2, [r3]` —— `rtos_current_tcb = rtos_next_tcb`
9. `ldr r3, =g_kernel` —— r3 = `g_kernel` 地址
10. `str r2, [r3, #260]` —— `g_kernel.current_task = rtos_next_tcb`（260 是 `current_task` 在结构体中的偏移）

**恢复下一个任务：**
11. `ldr r0, [r2]` —— r0 = 新任务的 `stack_ptr`
12. `ldmia r0!, {r4-r11}` —— 从新任务的栈弹出 R4-R11，r0 自增
13. `msr psp, r0` —— **PSP 指向新任务的硬件异常帧上方**
14. `isb`
15. `ldr r0, =0xFFFFFFFD`
16. `bx r0` —— 异常返回，硬件从新任务的栈弹出 R0-R3, R12, LR, PC, xPSR，新任务开始执行

**检验标准**：能不看代码，用自己的话描述" PendSV 保存了什么、恢复了什么、为什么需要软件保存 R4-R11 "。

### 7.3 为什么硬件只保存 R0-R3, R12, LR, PC, xPSR？

这是 ARM 的 AAPCS（ARM Architecture Procedure Call Standard）规定的：
- **调用者保存**：R0-R3, R12（这些寄存器在函数调用时不保证保留）
- **被调用者保存**：R4-R11（如果函数要用，必须自己 push/pop 保护）

中断发生时，硬件自动保存"调用者保存"的寄存器（因为中断打断了正常代码执行，相当于一次强制函数调用）。而 R4-R11 由被打断的任务自己负责保存——但任务代码不会预知中断何时到来，所以由 RTOS 的 PendSV_Handler 代劳。

---

## 第八步：回头看，融会贯通（20 分钟）

现在你已经读完了所有代码。做下面三件事，把碎片拼成完整图景：

### 8.1 画一张"任务延时 500ms 的完整时序图"

标注以下时刻：
1. 任务 A 调用 `rtos_task_delay(500)`
2. `rtos_task_delay` 内部：A 进入延时队列，触发 `rtos_sched()`
3. `rtos_sched` 选出任务 B，触发 PendSV
4. PendSV 保存 A 的上下文，恢复 B 的上下文
5. B 开始运行
6. 过了 500 个 tick，SysTick 中断发生
7. `rtos_tick_handler` 扫描延时队列，发现 A 到期
8. A 被移回就绪队列，`rtos_sched` 发现 A 优先级高于 B
9. PendSV 再次切换，A 恢复运行

### 8.2 回答以下问题

1. 如果只有一个任务（除了空闲任务），还会发生上下文切换吗？（答案：不会，`rtos_sched` 发现 next == curr，不触发 PendSV）
2. 任务函数返回会发生什么？（答案：`rtos_port_init_stack` 把 LR 设成了 `rtos_task_exit_trampoline`，返回后会调用 `rtos_task_delete(NULL)` 自删）
3. 为什么 `rtos_task_delay` 里要先 `RTOS_ENTER_CRITICAL()` 再操作队列？（答案：防止中断打断导致链表断裂）

### 8.3 尝试修改

建议动手改一行代码，观察效果：

**实验 1**：在 `config.h` 里把 `RTOS_ENABLE_TIME_SLICING` 改成 0，重新编译烧录。观察同优先级任务是否还轮流执行。

**实验 2**：在 `tick.c` 的 `rtos_tick_handler` 里，把延时到期后的 `rtos_task_ready(tcb)` 注释掉。观察任务是否永远睡不醒。

---

## 📚 附录：推荐阅读顺序速查表

| 顺序 | 文件 | 预计时间 | 难度 |
|:----:|------|:--------:|:----:|
| 1 | `README.md`（架构 + API） | 10 min | ⭐ |
| 2 | `include/types.h` | 5 min | ⭐ |
| 3 | `include/config.h` | 5 min | ⭐ |
| 4 | `include/list.h` | 10 min | ⭐⭐ |
| 5 | `include/kernel.h` | 15 min | ⭐⭐ |
| 6 | `include/task.h` + `src/task.c` | 40 min | ⭐⭐⭐ |
| 7 | `src/sched.c` | 25 min | ⭐⭐⭐ |
| 8 | `src/tick.c` | 15 min | ⭐⭐ |
| 9 | `include/port.h` + `src/port/cortex_m/port.c` | 20 min | ⭐⭐⭐ |
| 10 | `src/port/cortex_m/port_asm.S` | 60 min | ⭐⭐⭐⭐ |
| 11 | 回头看，画时序图 + 做实验 | 20 min | ⭐⭐⭐ |

**总计**：约 3.5 小时，建议分 2~3 次完成。

---

> 💡 **最后的话**：RTOS 的本质其实不复杂——就是"多个函数轮流用 CPU，通过栈保存/恢复现场，通过链表管理排队，通过中断推进时间"。LinRTOS 被精简后代码量很小，正是理解这些本质的最佳教材。祝你阅读愉快！
