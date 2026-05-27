# LinRTOS 统一队列（Unified Queue）—— IPC 的原点

> **版本**: LinRTOS v1.x  
> **作者**: bunnydeny  
> **定位**: 所有进程间通信（IPC）机制的单一底层实现。

---

## 1. 为什么叫 "统一队列"

在 LinRTOS 中，**消息队列、二进制信号量、计数信号量、互斥锁、递归互斥锁**……这些看似完全不同的 IPC 概念，底层都源于同一个数据结构：`struct rtos_queue`。

这是 FreeRTOS 经典设计思想的继承与简化：

| 上层概念 | `length` | `item_size` | 数据拷贝 | 额外标记 |
|---------|---------|------------|---------|---------|
| **消息队列** | N | `sizeof(T)` | 有（环形缓冲区 memcpy） | `QUEUE_TYPE_BASE` |
| **二进制信号量** | 1 | 0 | 无 | `QUEUE_TYPE_BINARY` |
| **计数信号量** | N | 0 | 无 | `QUEUE_TYPE_COUNTING` |
| **互斥锁** | 1 | 0 | 无 | `QUEUE_TYPE_MUTEX` |
| **递归互斥锁** | 1 | 0 | 无 | `QUEUE_TYPE_RECURSIVE` |

当 `item_size == 0` 时，队列退化为**纯计数器**：`send` 只把 `messages_waiting++`，`recv` 只把 `messages_waiting--`，**零数据拷贝、零缓冲区开销**。这就是信号量和互斥锁的本质。

因此，理解统一队列的 API，就等于理解了 LinRTOS 整个 IPC 体系的根基。

---

## 2. 核心概念

### 2.1 三种发送位置

LinRTOS 队列支持三种写入策略：

```c
typedef enum {
    RTOS_QUEUE_SEND_BACK = 0,   // 尾部追加（常规 FIFO）
    RTOS_QUEUE_SEND_FRONT,      // 头部插入（高优先级消息插队）
    RTOS_QUEUE_SEND_OVERWRITE,  // 覆盖写入（仅当 length == 1 时合法）
} rtos_queue_send_pos_t;
```

- **`SEND_BACK`**（默认）：常规队列行为，数据排到队尾。
- **`SEND_FRONT`**：紧急消息插队，写到队首，下次 `recv` 优先读到。
- **`OVERWRITE`**：覆盖队列中已有的唯一元素。**要求 `length == 1`**，否则触发断言。常用于单槽 Mailbox。

### 2.2 阻塞与超时

所有任务上下文的 `send` / `recv` 都支持阻塞：

```c
#define RTOS_DONT_WAIT      0U          // 不阻塞，立即返回
#define RTOS_WAIT_FOREVER   0xFFFFFFFFU // 永久阻塞，直到被唤醒
```

- **发送阻塞**：队列满时，任务挂到 `tasks_waiting_to_send` 链表，按优先级降序排列。
- **接收阻塞**：队列空时，任务挂到 `tasks_waiting_to_receive` 链表，同样按优先级排序。
- **唤醒规则**：当队列状态变化（空→有数据、满→有空位）时，**唤醒等待链表中优先级最高的任务**。
- **超时处理**：LinRTOS 使用**双列表延时队列**解决 32-bit tick 回绕问题。超时唤醒后，任务返回 `RTOS_ERR_TIMEOUT`。

> ⚠️ **重要**：LinRTOS 通过 `wakeup_reason`（1=正常唤醒，2=超时唤醒）区分唤醒原因，**不会**出现"正常唤醒误报 timeout"的 bug。

### 2.3 ISR 安全版本

中断中禁止阻塞，因此提供 `_from_isr` 变体：

- 不阻塞，队列满/空时直接返回 `RTOS_ERR_RESOURCE`。
- 通过 `pxHigherPrioTaskWoken` 输出指针告诉调用者：是否唤醒了比当前被中断任务更高优先级的任务。
- ISR 退出前，调用方应根据 `pxHigherPrioTaskWoken` 决定是否触发 `portYIELD_FROM_ISR()` 进行上下文切换。

---

## 3. 数据结构

```c
struct rtos_queue {
    uint8_t *buffer;            // 数据缓冲区起始（item_size==0 时指向自身）
    uint8_t *buffer_end;        // 缓冲区末尾（越界，不访问）
    uint8_t *write_to;          // 下一次尾部写入位置
    uint8_t *read_from;         // 逻辑上前一个元素的位置

    uint32_t length;            // 队列容量（最大元素个数）
    uint32_t item_size;         // 单个元素字节数；0=无数据队列
    uint32_t messages_waiting;  // 当前元素数 / 信号量 count

    struct rtos_list_node tasks_waiting_to_send;    // 发送阻塞链表
    struct rtos_list_node tasks_waiting_to_receive; // 接收阻塞链表

    uint8_t queue_type;         // 队列类型标记（预留）
};
```

- **零堆依赖**：用户静态分配 `struct rtos_queue` 和 `buffer`，不需要 `malloc`。
- **环形缓冲区**：采用 FreeRTOS 风格的指针回绕，配合分段 `memcpy`（kfifo 风格），**不依赖长度是 2 的幂**。

---

## 4. API 参考

### 4.1 生命周期

#### `rtos_queue_init`

```c
rtos_err_t rtos_queue_init(struct rtos_queue *queue, void *buffer,
                           uint32_t length, uint32_t item_size);
```

**功能**：初始化队列。

**参数**：
- `queue` —— 用户静态分配的队列结构体。
- `buffer` —— 数据缓冲区。`item_size == 0` 时可传 `NULL`。
- `length` —— 队列容量（最大元素个数），必须 `>= 1`。
- `item_size` —— 单个元素字节数。传 `0` 表示无数据队列（信号量模式）。

**返回值**：
- `RTOS_OK` —— 成功
- `RTOS_ERR_PARAM` —— 参数非法（`queue == NULL`、`length == 0`、或 `item_size > 0 且 buffer == NULL`）

**示例**：

```c
struct rtos_queue q;
uint8_t buf[10 * sizeof(uint32_t)];
rtos_queue_init(&q, buf, 10, sizeof(uint32_t));  // 消息队列

struct rtos_queue sem;
rtos_queue_init(&sem, NULL, 1, 0);               // 二进制信号量
```

---

#### `rtos_queue_delete`

```c
void rtos_queue_delete(struct rtos_queue *queue);
```

**功能**：删除队列。

**注意**：
- 用户自行释放 `buffer` 和 `queue` 结构体内存。
- 删除前必须确保**没有任务阻塞在该队列上**，否则触发断言。

---

### 4.2 任务上下文发送

#### `rtos_queue_generic_send`

```c
rtos_err_t rtos_queue_generic_send(struct rtos_queue *queue,
                                   const void *item,
                                   uint32_t timeout,
                                   rtos_queue_send_pos_t pos);
```

**功能**：统一发送入口，支持阻塞。

**参数**：
- `queue` —— 目标队列
- `item` —— 待发送数据指针。`item_size == 0` 时可传 `NULL`。
- `timeout` —— 阻塞超时（ticks）。`RTOS_DONT_WAIT` = 不阻塞；`RTOS_WAIT_FOREVER` = 永久阻塞。
- `pos` —— 发送位置：`RTOS_QUEUE_SEND_BACK` / `RTOS_QUEUE_SEND_FRONT` / `RTOS_QUEUE_SEND_OVERWRITE`

**返回值**：
- `RTOS_OK` —— 发送成功
- `RTOS_ERR_RESOURCE` —— 队列满且 `timeout == RTOS_DONT_WAIT`
- `RTOS_ERR_TIMEOUT` —— 阻塞超时

**行为**：
- 若队列有空间（或 `pos == OVERWRITE`），立即写入，然后唤醒 `tasks_waiting_to_receive` 中优先级最高的任务。
- 若队列满且允许阻塞，任务挂起到 `tasks_waiting_to_send`，等待被接收方唤醒或超时。

---

#### `rtos_queue_send` —— 尾部追加（便捷封装）

```c
static inline rtos_err_t rtos_queue_send(struct rtos_queue *queue,
                                         const void *item,
                                         uint32_t timeout)
{
    return rtos_queue_generic_send(queue, item, timeout, RTOS_QUEUE_SEND_BACK);
}
```

**用法**：最常见的 FIFO 队列发送。

```c
uint32_t data = 42;
rtos_err_t e = rtos_queue_send(&q, &data, RTOS_WAIT_FOREVER);
```

---

#### `rtos_queue_send_to_front` —— 头部插队

```c
static inline rtos_err_t rtos_queue_send_to_front(struct rtos_queue *queue,
                                                  const void *item,
                                                  uint32_t timeout)
{
    return rtos_queue_generic_send(queue, item, timeout, RTOS_QUEUE_SEND_FRONT);
}
```

**用法**：紧急消息优先被接收。

```c
uint32_t urgent = 0xFF;
rtos_queue_send_to_front(&q, &urgent, RTOS_WAIT_FOREVER);
```

---

#### `rtos_queue_overwrite` —— 覆盖写入

```c
static inline rtos_err_t rtos_queue_overwrite(struct rtos_queue *queue,
                                              const void *item)
{
    return rtos_queue_generic_send(queue, item, RTOS_DONT_WAIT,
                                   RTOS_QUEUE_SEND_OVERWRITE);
}
```

**用法**：单槽 Mailbox，总是覆盖旧值。**要求 `queue->length == 1`**，否则断言失败。

```c
struct rtos_queue mailbox;
uint8_t buf[sizeof(uint32_t)];
rtos_queue_init(&mailbox, buf, 1, sizeof(uint32_t));

uint32_t latest = 100;
rtos_queue_overwrite(&mailbox, &latest);  // 总是成功，不阻塞
```

---

### 4.3 任务上下文接收

#### `rtos_queue_generic_recv`

```c
rtos_err_t rtos_queue_generic_recv(struct rtos_queue *queue,
                                   void *buffer,
                                   uint32_t timeout,
                                   bool peek);
```

**功能**：统一接收入口，支持阻塞和 peek。

**参数**：
- `queue` —— 目标队列
- `buffer` —— 接收缓冲区。`item_size == 0` 时可传 `NULL`。
- `timeout` —— 阻塞超时
- `peek` —— `true` = 只看不取（不修改队列状态）；`false` = 正常取出

**返回值**：同 `rtos_queue_generic_send`。

**行为**：
- 若队列有数据，立即读取，然后唤醒 `tasks_waiting_to_send` 中优先级最高的任务。
- 若队列空且允许阻塞，任务挂起到 `tasks_waiting_to_receive`。

---

#### `rtos_queue_recv` —— 正常取出

```c
static inline rtos_err_t rtos_queue_recv(struct rtos_queue *queue,
                                         void *buffer,
                                         uint32_t timeout)
{
    return rtos_queue_generic_recv(queue, buffer, timeout, false);
}
```

**用法**：标准接收。

```c
uint32_t v;
rtos_err_t e = rtos_queue_recv(&q, &v, RTOS_WAIT_FOREVER);
if (e == RTOS_OK) {
    // 使用 v
}
```

---

#### `rtos_queue_peek` —— 只看不取

```c
static inline rtos_err_t rtos_queue_peek(struct rtos_queue *queue,
                                         void *buffer,
                                         uint32_t timeout)
{
    return rtos_queue_generic_recv(queue, buffer, timeout, true);
}
```

**用法**：预览队首元素，不移动 `read_from` 指针，队列状态不变。

```c
uint32_t preview;
if (rtos_queue_peek(&q, &preview, RTOS_DONT_WAIT) == RTOS_OK) {
    // preview 是队首值，但队列中该元素仍在
}
```

---

### 4.4 ISR 安全版本

#### `rtos_queue_generic_send_from_isr`

```c
rtos_err_t rtos_queue_generic_send_from_isr(struct rtos_queue *queue,
                                            const void *item,
                                            rtos_queue_send_pos_t pos,
                                            bool *pxHigherPrioTaskWoken);
```

**功能**：ISR 中发送，不阻塞。

**参数**：
- `pxHigherPrioTaskWoken` —— 出参。若 `*pxHigherPrioTaskWoken == true`，表示唤醒了比当前被中断任务更高优先级的任务，ISR 退出前应考虑触发 `portYIELD_FROM_ISR()`。

**返回值**：
- `RTOS_OK` —— 发送成功
- `RTOS_ERR_RESOURCE` —— 队列满（ISR 中不能阻塞）

**注意**：该函数内部会调用 `rtos_port_is_in_isr()` 断言，确保只能在 ISR 上下文中调用。

---

#### `rtos_queue_generic_recv_from_isr`

```c
rtos_err_t rtos_queue_generic_recv_from_isr(struct rtos_queue *queue,
                                            void *buffer,
                                            bool *pxHigherPrioTaskWoken);
```

**功能**：ISR 中接收，不阻塞。

**返回值**：
- `RTOS_OK` —— 接收成功
- `RTOS_ERR_RESOURCE` —— 队列空（ISR 中不能阻塞）

---

**ISR 使用示例**：

```c
void TIM2_IRQHandler(void)
{
    bool hpw = false;
    uint32_t adc_val = ADC1->DR;

    rtos_queue_generic_send_from_isr(&adc_queue, &adc_val,
                                     RTOS_QUEUE_SEND_BACK, &hpw);

    // 如果唤醒了更高优先级的任务，在 ISR 末尾触发调度
    portYIELD_FROM_ISR(hpw);
}
```

---

### 4.5 状态查询

```c
static inline uint32_t rtos_queue_messages_waiting(struct rtos_queue *queue)
{
    return queue->messages_waiting;
}

static inline uint32_t rtos_queue_spaces_available(struct rtos_queue *queue)
{
    return queue->length - queue->messages_waiting;
}

static inline bool rtos_queue_is_empty(struct rtos_queue *queue)
{
    return queue->messages_waiting == 0;
}

static inline bool rtos_queue_is_full(struct rtos_queue *queue)
{
    return queue->messages_waiting >= queue->length;
}
```

**注意**：这些查询函数**无锁、无临界区**，适合快速判断。但由于没有原子保护，在多任务/ISR 并发场景下，返回值仅代表"某一瞬间"的快照。

---

## 5. 阻塞与超时机制详解

### 5.1 双列表延时队列

LinRTOS 使用两个延时链表解决 32-bit tick 回绕问题：

- `px_delayed_task_list`：存放 `wake_tick >= current_tick` 的任务（无符号比较）
- `px_overflow_delayed_task_list`：存放 `wake_tick < current_tick` 的任务

当 `tick_count` 回绕到 0 时，两个列表指针 swap。这样 tick handler 中只需要遍历当前列表，遇到 `wake_tick > current_tick` 即可 `break`，**无需遍历全部任务**。

### 5.2 唤醒原因

TCB 中新增 `wakeup_reason` 字段：

- `0` —— 无
- `1` —— 正常唤醒（被 `prv_wake_highest_from_event_list` 唤醒）
- `2` —— 超时唤醒（被 tick handler 从 delay_list 移除）

`send` / `recv` 被唤醒后检查 `wakeup_reason == 2` 才返回 `RTOS_ERR_TIMEOUT`，**彻底杜绝了"正常唤醒被误报为超时"的 bug**。

---

## 6. 从队列到信号量/互斥锁——映射关系

虽然信号量和互斥锁有独立的包装 API（后续文档介绍），但它们的底层就是统一队列。

### 6.1 二进制信号量

```c
struct rtos_queue sem;
rtos_queue_init(&sem, NULL, 1, 0);

//  give（释放）
rtos_queue_send(&sem, NULL, RTOS_DONT_WAIT);

//  take（获取）
rtos_queue_recv(&sem, NULL, RTOS_WAIT_FOREVER);
```

`messages_waiting` 只能是 0 或 1，天然表示"可用/不可用"。

### 6.2 计数信号量

```c
struct rtos_queue sem;
rtos_queue_init(&sem, NULL, 10, 0);

//  give N 次
for (int i = 0; i < 5; i++)
    rtos_queue_send(&sem, NULL, RTOS_DONT_WAIT);

//  messages_waiting == 5，还可 take 5 次
```

### 6.3 互斥锁

互斥锁在二进制信号量的基础上，增加了**所有权记录**和**优先级继承**逻辑（后续实现）。但底层队列仍是 `length=1, item_size=0`。

### 6.4 递归互斥锁

在互斥锁的基础上，增加**同任务递归计数**（后续实现）。

---

## 7. 完整使用示例

### 7.1 消息队列——生产者-消费者

```c
#include "linRTOS.h"
#include "rtos_queue.h"

static struct rtos_queue s_q;
static uint8_t s_buf[10 * sizeof(uint32_t)];

static void producer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 100; i++) {
        rtos_queue_send(&s_q, &i, RTOS_WAIT_FOREVER);
    }
    rtos_task_delete(NULL);
}

static void consumer(void *p)
{
    (void)p;
    for (uint32_t i = 0; i < 100; i++) {
        uint32_t v;
        rtos_queue_recv(&s_q, &v, RTOS_WAIT_FOREVER);
        // 处理 v...
    }
    rtos_task_delete(NULL);
}

void app_entry_task(void *param)
{
    (void)param;
    rtos_queue_init(&s_q, s_buf, 10, sizeof(uint32_t));

    rtos_task_create(producer, "prod", s_pstk, 128, NULL, 2, NULL);
    rtos_task_create(consumer, "cons", s_cstk, 128, NULL, 5, NULL);

    rtos_task_delete(NULL);
}
```

### 7.2 单槽 Mailbox——覆盖写入

```c
static struct rtos_queue mailbox;
static uint8_t m_buf[sizeof(uint32_t)];

void sensor_task(void *p)
{
    (void)p;
    while (1) {
        uint32_t latest = read_sensor();
        rtos_queue_overwrite(&mailbox, &latest);  // 总是保留最新值
        rtos_task_delay(10);
    }
}

void display_task(void *p)
{
    (void)p;
    while (1) {
        uint32_t v;
        rtos_queue_recv(&mailbox, &v, RTOS_WAIT_FOREVER);
        show_value(v);
    }
}
```

### 7.3 紧急消息插队

```c
void normal_task(void *p)
{
    (void)p;
    uint32_t data = 1;
    rtos_queue_send(&q, &data, RTOS_WAIT_FOREVER);   // 排到队尾
}

void urgent_task(void *p)
{
    (void)p;
    uint32_t alert = 0xFF;
    rtos_queue_send_to_front(&q, &alert, RTOS_WAIT_FOREVER);  // 插队到队首
}
```

### 7.4 ISR 向任务发送数据

```c
static struct rtos_queue adc_queue;
static uint8_t adc_buf[8 * sizeof(uint16_t)];

void ADC_IRQHandler(void)
{
    bool hpw = false;
    uint16_t val = ADC1->DR;

    rtos_queue_generic_send_from_isr(&adc_queue, &val,
                                     RTOS_QUEUE_SEND_BACK, &hpw);

    portYIELD_FROM_ISR(hpw);
}

void processing_task(void *p)
{
    (void)p;
    rtos_queue_init(&adc_queue, adc_buf, 8, sizeof(uint16_t));

    while (1) {
        uint16_t sample;
        rtos_queue_recv(&adc_queue, &sample, RTOS_WAIT_FOREVER);
        process_sample(sample);
    }
}
```

---

## 8. 注意事项

1. **overwrite 限制**：`rtos_queue_overwrite()` 要求 `queue->length == 1`，否则触发断言。这是 FreeRTOS 兼容行为。

2. **零堆依赖**：`rtos_queue_init()` 不会分配内存，队列结构体和缓冲区必须由用户静态分配或自行管理。

3. **删除前清空阻塞任务**：`rtos_queue_delete()` 会断言检查两个阻塞链表必须为空。如果强行删除仍有任务等待的队列，会导致未定义行为。

4. **ISR 版本不阻塞**：`send_from_isr` / `recv_from_isr` 在队列满/空时返回 `RTOS_ERR_RESOURCE`，不会挂起任务。

5. **查询函数非原子**：`rtos_queue_messages_waiting()` 等查询函数无临界区保护，返回值仅为快照。若需精确判断，应使用带超时的 `send` / `recv`。

6. **item_size == 0 时 item/buffer 可传 NULL**：信号量模式下不搬数据，参数可省略。

---

## 9. 与 FreeRTOS 队列的差异速查

| 特性 | LinRTOS | FreeRTOS |
|------|---------|----------|
| 统一实现 | ✅ Queue = 队列+信号量+互斥锁 | ✅ `QueueDefinition` + `queueQUEUE_TYPE_*` |
| 阻塞链表排序 | 按优先级降序 | 按优先级降序 |
| tick 回绕处理 | 双列表 swap | 双列表 swap |
| overwrite 限制 | `length == 1` | `length == 1` |
| 唤醒原因区分 | `wakeup_reason` 字段 | `xTaskRemoveFromEventList` + `xTaskCheckForTimeOut` |
| 数据拷贝 | 分段 memcpy（kfifo 风格） | 分段 memcpy |

---

## 10. 相关文件

- `include/rtos_queue.h` —— 头文件与 inline 封装
- `src/queue.c` —— 核心实现
- `include/kernel.h` —— TCB、延时链表定义
- `include/types.h` —— 错误码与常量
