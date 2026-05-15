/*
 * LinRTOS - Lightweight preemptive RTOS for ARM Cortex-M.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_LIST_H
#define RTOS_LIST_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * 📌 侵入式双向链表节点
 * ============================================================ */

struct rtos_list_node {
    struct rtos_list_node *next;
    struct rtos_list_node *prev;
};

/* ============================================================
 * 🔧 链表操作宏
 * ============================================================ */

#define RTOS_LIST_NODE_INIT(name)   { &(name), &(name) }

static inline void rtos_list_init(struct rtos_list_node *list)
{
    list->next = list;
    list->prev = list;
}

static inline bool rtos_list_is_empty(const struct rtos_list_node *list)
{
    return list->next == list;
}

static inline void rtos_list_insert_after(struct rtos_list_node *node,
                                          struct rtos_list_node *new_node)
{
    new_node->next = node->next;
    new_node->prev = node;
    node->next->prev = new_node;
    node->next = new_node;
}

static inline void rtos_list_insert_before(struct rtos_list_node *node,
                                           struct rtos_list_node *new_node)
{
    new_node->prev = node->prev;
    new_node->next = node;
    node->prev->next = new_node;
    node->prev = new_node;
}

static inline void rtos_list_remove(struct rtos_list_node *node)
{
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->next = node;
    node->prev = node;
}

#define rtos_list_for_each(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

#define rtos_list_for_each_safe(pos, n, head) \
    for ((pos) = (head)->next, (n) = (pos)->next; \
         (pos) != (head); \
         (pos) = (n), (n) = (pos)->next)

#define rtos_list_entry(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

#ifdef __cplusplus
}
#endif

#endif /* RTOS_LIST_H */
