# 🏗️ LinRTOS 统一队列 —— 全部 IPC 机制的根基

> 📦 **版本**: LinRTOS v1.x
> 🎯 **定位**: 消息队列、信号量、互斥锁的单一底层实现。

---

## 1. 💡 设计理念

在 LinRTOS 中，所有进程间通信（IPC）机制共享同一个数据结构 `struct rtos_queue`。这不是普通的"消息队列"，而是一个通用同步原语：

```
                ┌──────────────────────┐
                │   struct rtos_queue   │
                │    🌀 环形缓冲区 (可选)  │
                │    📊 messages_waiting │
                │    📤 发送阻塞链表       │
                │    📥 接收阻塞链表       │
                │    🏷️  queue_type 标记  │
                └──────┬───────────────┘
                       │
        ┌──────────────┼──────────────┐
        │              │              │
   ┌────▼────┐   ┌────▼────┐   ┌────▼────┐
   │ 📨 消息队列│   │ 🚦 信号量 │   │ 🔐 互斥锁 │
   │ copy data│   │仅操作计数│   │所有权+继承│
   └─────────┘   └─────────┘   └─────────┘
```

> 💡 **核心思想**：当 `item_size == 0` 时，队列退化为纯计数器，不拷贝数据，不消耗缓冲区。这就是信号量和互斥锁的本质。

| 🏷️ 上层概念 | 📏 `length` | 📐 `item_size` | 📦 数据拷贝 | 🏷️ `queue_type` | ⚡ 额外行为 |
|------------|------------|---------------|-----------|----------------|-----------|
| 📨 消息队列    | N | sizeof(T) | ✅ 有 | `BASE`      | — |
| 🚦 二进制信号量 | 1 | 0 | ❌ 无 | `BINARY`    | give 时 count≤1 |
| 🔢 计数信号量  | N | 0 | ❌ 无 | `COUNTING`  | give 时 count≤N |
| 🔐 互斥锁      | 1 | 0 | ❌ 无 | `MUTEX`     | 所有权 + 优先级继承 |
| 🔁 递归互斥锁  | 1 | 0 | ❌ 无 | `RECURSIVE` | 所有权 + 递归计数 + 优先级继承 |

---

## 2. 🧬 数据结构

```c
struct rtos_queue {
    /* 🌀 环形缓冲区（item_size > 0 时使用） */
    uint8_t *buffer;            // 📍 缓冲区起始地址
    uint8_t *buffer_end;        // 🚫 缓冲区末尾（越界，不访问）
    uint8_t *write_to;          // ✍️ 下一次尾部写入位置
    uint8_t *read_from;         // 👁️ 逻辑上前一个元素的位置

    /* 📊 状态 */
    uint32_t length;            // 📏 队列容量（最大元素个数）
    uint32_t item_size;         // 📐 单个元素字节数；0 = 无数据
    uint32_t messages_waiting;  // 🔢 当前元素数 / 信号量 count

    /* 🚦 阻塞链表（按优先级降序排列） */
    struct rtos_list_node tasks_waiting_to_send;     // 📤 队列满时阻塞的发送者
    struct rtos_list_node tasks_waiting_to_receive;  // 📥 队列空时阻塞的接收者

    /* 🏷️ 语义标记 */
    uint8_t queue_type;

    /* 🔐 互斥锁专用字段 */
    struct rtos_tcb *mutex_holder;   // 👤 当前持有者；NULL = 🆓 无持有者
    uint32_t recursive_count;        // 🔢 递归计数（仅 RECURSIVE 类型）
    uint32_t original_priority;      // ⏮️ 被提升前的优先级；0xFFFFFFFF = 未被提升
};
```

### ✨ 关键特性

- 🪨 **零堆依赖** — 所有内存由用户静态分配，无需 `malloc`
- 🔄 **环形缓冲区** — 采用指针回绕而非 2 的幂取模，长度任意
- ✂️ **分段 memcpy** — 参考 kfifo 风格，正确处理跨缓冲区边界的拷贝
- ⚡ **阻塞链表按优先级排序** — 唤醒时直接取链表头即可得到最高优先级等待者

---

## 3. 📨 消息队列 API

### 3.1 🏗️ 创建与删除

```c
rtos_err_t rtos_queue_init(struct rtos_queue *queue,
                           void *buffer,
                           uint32_t length,
                           uint32_t item_size);

void rtos_queue_delete(struct rtos_queue *queue);
```

| 参数 | 说明 |
|------|------|
| `queue` | 🎯 用户静态分配的 `struct rtos_queue` |
| `buffer` | 🗂️ 数据缓冲区，大小 ≥ `length × item_size`；`item_size == 0` 时可传 `NULL` |
| `length` | 📏 队列容量（最大元素个数），必须 ≥ 1 |
| `item_size` | 📐 单个元素字节数；传 `0` 表示无数据队列 |

📤 **返回值**：`RTOS_OK` ✅ / `RTOS_ERR_PARAM` ❌

📝 **示例**：
```c
struct rtos_queue q;
uint8_t buf[10 * sizeof(uint32_t)];
rtos_queue_init(&q, buf, 10, sizeof(uint32_t));  // 📨 10 个 uint32_t 的消息队列
```

> ⚠️ **注意**：`rtos_queue_delete` 会断言检查两个阻塞链表必须为空。删除前请确保没有任务阻塞在队列上。

### 3.2 ✍️ 三种发送方式

```c
typedef enum {
    RTOS_QUEUE_SEND_BACK      = 0,  // 📨 尾部追加（FIFO）
    RTOS_QUEUE_SEND_FRONT     = 1,  // ⏩ 头部插入（插队）
    RTOS_QUEUE_SEND_OVERWRITE = 2,  // ♻️ 覆盖写入（仅 length == 1 时合法）
} rtos_queue_send_pos_t;
```

| ✍️ 方式 | 📍 写入位置 | ⏸️ 阻塞行为 | 🎯 适用场景 |
|---------|-----------|-----------|-----------|
| `SEND_BACK` | 📨 队尾 | 队列满时阻塞 | 常规 FIFO 队列 |
| `SEND_FRONT` | ⏩ 队首 | 队列满时阻塞 | 🔴 紧急消息插队 |
| `SEND_OVERWRITE` | ♻️ 覆盖唯一元素 | 永不阻塞 | 📬 单槽 Mailbox（只保留最新值） |

🔧 **底层函数**：
```c
rtos_err_t rtos_queue_generic_send(struct rtos_queue *queue,
                                   const void *item,
                                   uint32_t timeout,
                                   rtos_queue_send_pos_t pos);
```

🍬 **便捷封装**（零成本 inline）：
```c
rtos_queue_send(&q, &data, RTOS_WAIT_FOREVER);         // 📨 FIFO 发送
rtos_queue_send_to_front(&q, &data, RTOS_WAIT_FOREVER); // ⏩ 插队发送
rtos_queue_overwrite(&q, &data);                        // ♻️ 覆盖写入 (不阻塞)
```

### 3.3 📥 接收

```c
rtos_err_t rtos_queue_generic_recv(struct rtos_queue *queue,
                                   void *buffer,
                                   uint32_t timeout,
                                   bool peek);

// 🍬 便捷封装
rtos_queue_recv(&q, &data, RTOS_WAIT_FOREVER);  // 📥 正常接收
rtos_queue_peek(&q, &data, RTOS_DONT_WAIT);      // 👁️ 只看不取
```

- 👁️ `peek == true` — 读取队首元素但**不移除**，队列状态不变
- 📥 `peek == false` — 正常取出，`messages_waiting--`，`read_from` 前移

### 3.4 📊 状态查询

```c
rtos_queue_messages_waiting(&q)   // 🔢 当前元素数
rtos_queue_spaces_available(&q)   // 📏 剩余空间
rtos_queue_is_empty(&q)           // 🕳️ 是否为空
rtos_queue_is_full(&q)            // 🈵 是否已满
```

> ⚠️ 这些函数**无锁、无临界区**。多任务并发时仅为快照 📸，不应依赖其精确性做决策。

### 3.5 ⚡ ISR 安全版本

```c
rtos_err_t rtos_queue_generic_send_from_isr(struct rtos_queue *queue,
                                            const void *item,
                                            rtos_queue_send_pos_t pos,
                                            bool *pxHigherPrioTaskWoken);

rtos_err_t rtos_queue_generic_recv_from_isr(struct rtos_queue *queue,
                                            void *buffer,
                                            bool *pxHigherPrioTaskWoken);
```

- 🚫 不阻塞，队列满/空时直接返回 `RTOS_ERR_RESOURCE`
- 🔔 `pxHigherPrioTaskWoken` — 出参，若为 `true` 表示唤醒了更高优先级的任务，ISR 退出前需触发一次调度
- 🛡️ 函数内部断言 `rtos_port_is_in_isr()`，**只能从 ISR 中调用**

⚡ **ISR 完整示例**：
```c
void TIM2_IRQHandler(void)
{
    bool hpw = false;
    uint32_t adc_val = ADC1->DR;
    rtos_queue_generic_send_from_isr(&adc_queue, &adc_val,
                                     RTOS_QUEUE_SEND_BACK, &hpw);
    portYIELD_FROM_ISR(hpw);  // 🔄 如有更高优先级任务被唤醒，触发调度
}
```

---

## 4. 🚦 信号量 API

> 🍬 信号量是无数据队列的语法糖。`item_size == 0`，不拷贝任何数据，仅操作 `messages_waiting`。

### 4.1 🚦 二进制信号量

```c
rtos_err_t rtos_semaphore_init_binary(struct rtos_queue *sem);
```

🔧 内部等价于 `rtos_queue_init(sem, NULL, 1, 0)`，初始 count = **0**（🕳️ 空状态，需要 give 后才能 take）。

```c
rtos_semaphore_take(&sem, RTOS_WAIT_FOREVER);   // 🔻 P 操作，-1，可阻塞
rtos_semaphore_give(&sem);                      // 🔺 V 操作，+1（上限为 1）
```

> 💡 **典型场景**：任务间同步 —— 一个任务等待另一个任务的信号。

### 4.2 🔢 计数信号量

```c
rtos_err_t rtos_semaphore_init_counting(struct rtos_queue *sem,
                                        uint32_t max_count,
                                        uint32_t initial);
```

🔑 管理有限资源的访问，`give` 时 count ≤ max_count，超过上限返回 `RTOS_ERR_RESOURCE`。

```c
struct rtos_queue sem;
rtos_semaphore_init_counting(&sem, 10, 5);  // 🎫 最多 10 个资源，初始 5 个可用
```

> 💡 **典型场景**：资源池管理 —— 如 10 个 DMA 通道，初始 5 个空闲。

### 4.3 ⚡ ISR 安全版本

```c
rtos_semaphore_give_from_isr(&sem, &hpw);   // 🔺 ISR 中释放
rtos_semaphore_take_from_isr(&sem, &hpw);   // 🔻 ISR 中获取
```

---

## 5. 🔐 互斥锁 API

互斥锁在二进制信号量的基础上增加了三个关键特性：

- 👤 **所有权** — `mutex_holder` 记录持有者 TCB，非持有者不能释放
- 📈 **优先级继承** — 防止优先级反转 🌀
- 🔁 **递归计数** — 同一任务可多次获取（仅递归互斥锁）

### 5.1 🔐 普通互斥锁

```c
rtos_err_t rtos_mutex_init(struct rtos_queue *mutex);
rtos_err_t rtos_mutex_take(struct rtos_queue *mutex, uint32_t timeout);
rtos_err_t rtos_mutex_give(struct rtos_queue *mutex);
```

#### 👤 所有权规则

- 🔒 只有持有者才能 `give`，否则返回 `RTOS_ERR_STATE` ❌
- ✅ `take` 成功后当前任务成为持有者
- 🔓 `give` 后若没有等待者 → `mutex_holder = NULL`，`messages_waiting = 1`（🆓 可用状态）

#### 📈 优先级继承机制

```
  ┌──────────┐                  ┌──────────┐
  │ High Task│ (prio=5)         │ Low Task │ (prio=2)
  │ 尝试 take │ ───阻塞────────→ │ 持有锁    │
  │ 等待锁   │                  │ 被提升到 5 │  ← 内核自动提升
  └──────────┘                  └──────────┘
```

- 🔍 当高优先级任务阻塞在互斥锁上时，内核**自动将持有者的优先级提升**到与阻塞者相同
- 🔓 `give` 时**自动恢复**原始优先级
- ⚠️ 只在等待者优先级 **>** 持有者优先级时才触发继承

### 5.2 🔁 递归互斥锁

```c
rtos_err_t rtos_mutex_init_recursive(struct rtos_queue *mutex);
rtos_err_t rtos_mutex_take_recursive(struct rtos_queue *mutex, uint32_t timeout);
rtos_err_t rtos_mutex_give_recursive(struct rtos_queue *mutex);
```

#### 🆚 与普通互斥锁的区别

| 🔐 普通互斥锁 | 🔁 递归互斥锁 |
|-------------|-------------|
| 同一任务 take 第二次 → 💀 **死锁** | 同一任务 take 第二次 → ✅ **recursive_count++** |
| give 即释放 | 只有 `recursive_count` 归零才真正释放 |
| — | 非持有者 give → `RTOS_ERR_STATE` ❌ |

> 💡 **典型场景**：递归函数中需要获取同一把锁，或者多个嵌套函数各自需要锁定同一资源。

### 5.3 🔍 查询

```c
rtos_task_handle_t rtos_mutex_get_holder(struct rtos_queue *mutex);
```

👤 返回当前持有者句柄；无持有者或类型不匹配时返回 `NULL`。

---

## 6. ⏱️ 阻塞与超时机制

### 6.1 ⏱️ 超时常量

```c
#define RTOS_DONT_WAIT      0U            // ⚡ 不阻塞，立即返回
#define RTOS_WAIT_FOREVER   0xFFFFFFFFU   // ♾️ 永久阻塞，直到被唤醒
```

### 6.2 🔄 阻塞流程

```
  send() 队列满                    recv() 队列空
       │                               │
       ▼                               ▼
  ┌─────────────┐               ┌─────────────┐
  │ 📤 挂入发送   │               │ 📥 挂入接收   │
  │  等待链表    │               │  等待链表    │
  │ (按优先级↓)  │               │ (按优先级↓)  │
  └──────┬──────┘               └──────┬──────┘
         │                             │
         │    队列状态变化时唤醒          │
         │    (满→有空位 / 空→有数据)    │
         │                             │
         └──────────┬──────────────────┘
                    ▼
         ┌─────────────────┐
         │ 🔔 唤醒链表头部    │
         │   (最高优先级任务) │
         └─────────────────┘
```

1. 📤 `send` 操作 → 队列满 → 任务挂入 `tasks_waiting_to_send`（按优先级降序 🔽）
2. 📥 `recv` 操作 → 队列空 → 任务挂入 `tasks_waiting_to_receive`（按优先级降序 🔽）
3. 🔔 队列状态变化时 → 唤醒对应链表**头部**（最高优先级）任务
4. ⏰ 若设置了超时 → 任务**同时**挂入 tick 延时队列

### 6.3 🏷️ 唤醒原因

TCB 中 `wakeup_reason` 字段精确区分唤醒原因：

| 值 | 🏷️ 含义 | 📍 来源 |
|---|--------|--------|
| 0 | 🆕 初始状态 | — |
| 1 | ✅ 正常唤醒 | 被 `prv_wake_highest_from_event_list` 唤醒 |
| 2 | ⏰ 超时唤醒 | 被 tick handler 从 delay_list 移除 |

> 🎯 此机制**彻底解决**了"正常唤醒被误报为超时"的经典 bug：任务被唤醒后检查 `wakeup_reason == 2` 才返回 `RTOS_ERR_TIMEOUT`。

### 6.4 🔄 双列表延时队列

LinRTOS 使用两个延时链表解决 **32-bit tick 溢出回绕** 🌀：

```
  tick_count 递增 →
  ┌──────────────────────────────────────────┐
  │  px_delayed_task_list (主队列)             │
  │  wake_tick >= current_tick               │
  └──────────────────────────┬───────────────┘
                             │
              tick_count 溢出归零 (0xFFFFFFFF → 0)
                             │
                             ▼
  ┌──────────────────────────┴───────────────┐
  │  px_overflow_delayed_task_list (溢出队列)  │
  │  wake_tick < current_tick (跨回绕边界)     │
  └──────────────────────────────────────────┘
```

💡 当 `tick_count` 溢出归零时，两个链表指针 swap 🔄，tick handler 只需遍历当前活跃列表，无需遍历全部任务。

---

## 7. 📚 使用示例

### 7.1 📨 生产者-消费者（消息队列）

```c
static struct rtos_queue s_q;
static uint8_t s_buf[10 * sizeof(uint32_t)];  // 🗂️ 10 个元素的缓冲区
static uint32_t s_pstk[128];                   // 📤 生产者栈
static uint32_t s_cstk[128];                   // 📥 消费者栈

static void producer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 100; i++) {
        rtos_queue_send(&s_q, &i, RTOS_WAIT_FOREVER);  // 📨 阻塞发送
    }
    rtos_task_delete(NULL);  // 🗑️ 生产完毕，自删
}

static void consumer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t v;
        rtos_queue_recv(&s_q, &v, RTOS_WAIT_FOREVER);  // 📥 阻塞接收
        process(v);  // 🔧 处理数据
    }
    rtos_task_delete(NULL);  // 🗑️ 消费完毕，自删
}

void app_entry_task(void *param)
{
    (void)param;
    rtos_queue_init(&s_q, s_buf, 10, sizeof(uint32_t));  // 🏗️ 初始化
    rtos_task_create(producer, "prod", s_pstk, 128, NULL, 2, NULL);  // 📤 prio=2
    rtos_task_create(consumer, "cons", s_cstk, 128, NULL, 5, NULL);  // 📥 prio=5
    rtos_task_delete(NULL);
}
```

### 7.2 📬 单槽 Mailbox（覆盖写入）

```c
static struct rtos_queue mailbox;
static uint8_t m_buf[sizeof(uint32_t)];

void init(void) {
    rtos_queue_init(&mailbox, m_buf, 1, sizeof(uint32_t));  // 🏗️ length=1
}

void sensor_task(void *p) {
    (void)p;
    while (1) {
        uint32_t latest = read_sensor();        // 📡 读取传感器
        rtos_queue_overwrite(&mailbox, &latest); // ♻️ 总是保存最新值，永不阻塞
        rtos_task_delay(10);
    }
}

void display_task(void *p) {
    (void)p;
    while (1) {
        uint32_t v;
        rtos_queue_recv(&mailbox, &v, RTOS_WAIT_FOREVER);  // 📥 等待新数据
        show_value(v);  // 🖥️ 显示
    }
}
```

### 7.3 🤝 任务同步（二进制信号量）

```c
static struct rtos_queue s_sync;  // 🚦 同步信号量

void init(void) {
    rtos_semaphore_init_binary(&s_sync);  // 🏗️ 初始 count=0（空）
}

void slow_task(void *p) {
    (void)p;
    do_heavy_work();                        // ⏳ 耗时操作
    rtos_semaphore_give(&s_sync);           // 🔔 通知：工作完成！
    rtos_task_delete(NULL);
}

void waiting_task(void *p) {
    (void)p;
    rtos_semaphore_take(&s_sync, RTOS_WAIT_FOREVER);  // ⏸️ 等待通知
    process_result();  // 🎉 工作已完成，开始处理结果
    rtos_task_delete(NULL);
}
```

### 7.4 🔒 共享资源保护（互斥锁 + 优先级继承）

```c
static struct rtos_queue s_mutex;
static volatile uint32_t s_shared;  // 🔒 受保护的共享变量

void init(void) {
    rtos_mutex_init(&s_mutex);  // 🏗️ 初始为可用状态
}

void high_prio_task(void *p) {      // 🔺 优先级 = 5
    (void)p;
    rtos_mutex_take(&s_mutex, RTOS_WAIT_FOREVER);  // 🔒 获取锁
    s_shared++;                                     // ✍️ 临界区
    rtos_mutex_give(&s_mutex);                      // 🔓 释放锁
}

void low_prio_task(void *p) {       // 🔻 优先级 = 2
    (void)p;
    rtos_mutex_take(&s_mutex, RTOS_WAIT_FOREVER);  // 🔒 获取锁
    rtos_task_delay(50);                            // ⏳ 模拟耗时临界区
    s_shared++;                                     // ✍️
    rtos_mutex_give(&s_mutex);                      // 🔓 释放锁
    // 💡 若 high_prio 在此期间阻塞等待，low 的优先级会自动提升到 5
    // 🔓 give 时自动恢复为 2
}
```

### 7.5 ⚡ ISR 向任务发送数据

```c
static struct rtos_queue adc_queue;
static uint8_t adc_buf[8 * sizeof(uint16_t)];

void ADC_IRQHandler(void)
{
    bool hpw = false;
    uint16_t val = ADC1->DR;                          // 📡 读取 ADC
    rtos_queue_generic_send_from_isr(&adc_queue, &val,
                                     RTOS_QUEUE_SEND_BACK, &hpw);  // 📤 ISR 中发送
    portYIELD_FROM_ISR(hpw);  // 🔄 如有高优先级任务被唤醒，立即调度
}

void processing_task(void *p)
{
    (void)p;
    while (1) {
        uint16_t sample;
        rtos_queue_recv(&adc_queue, &sample, RTOS_WAIT_FOREVER);  // 📥 阻塞等待
        process_sample(sample);  // 🔬 处理采样数据
    }
}
```

---

## 8. ⚠️ 注意事项

| # | ⚠️ 注意点 | 💡 说明 |
|---|---------|--------|
| 1 | ♻️ **overwrite 限制** | `rtos_queue_overwrite()` 要求 `length == 1`，否则触发断言 💥 |
| 2 | 🪨 **零堆依赖** | 所有内存由用户静态分配，`rtos_queue_init` 不调用 `malloc` |
| 3 | 🧹 **删除前清空阻塞任务** | `rtos_queue_delete` 断言阻塞链表为空，强制删除 → 💀 未定义行为 |
| 4 | ⚡ **ISR 版本不阻塞** | `send_from_isr` / `recv_from_isr` 满/空时返回 `RTOS_ERR_RESOURCE` |
| 5 | 📸 **查询函数非原子** | `messages_waiting` 等查询无临界区保护，仅为瞬时快照 |
| 6 | 🆓 **item_size == 0 时参数可省** | 信号量/互斥锁模式下 `item` 和 `buffer` 可传 `NULL` |
| 7 | 🔄 **互斥锁 holder 立即转移** | `give` 中若有待唤醒任务，`holder` 直接设为被唤醒者，而非先置 `NULL` |
| 8 | 📈 **继承方向** | 优先级继承只在等待者优先级 **>** 持有者优先级时触发，低等高等不继承 |

---

## 9. 🗺️ 相关文件

| 📁 文件 | 📄 内容 |
|---------|--------|
| `include/rtos_queue.h` | 🧬 队列结构体 + 全部 inline 封装 |
| `include/rtos_semaphore.h` | 🚦 信号量 API |
| `include/rtos_mutex.h` | 🔐 互斥锁 API |
| `include/types.h` | 🏷️ 错误码与超时常量 |
| `src/queue.c` | ⚙️ 队列核心实现 |
| `src/mutex.c` | ⚙️ 互斥锁实现 |
| `src/semaphore.c` | ⚙️ 信号量实现 |
