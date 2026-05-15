/*
 * LinRTOS - Event group / flags API.
 * Copyright (C) 2026  bunnydeny <guoy55448@gmail.com>
 *
 * MIT License
 */

#ifndef RTOS_EVENT_H
#define RTOS_EVENT_H

#include "rtos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RTOS_EVENT_WAIT_ALL       0x01
#define RTOS_EVENT_WAIT_ANY       0x00
#define RTOS_EVENT_CLEAR_ON_EXIT  0x02

rtos_err_t rtos_event_group_create(rtos_event_group_handle_t *group);
void rtos_event_group_delete(rtos_event_group_handle_t group);
uint32_t rtos_event_group_set_bits(rtos_event_group_handle_t group,
                                    uint32_t bits);
uint32_t rtos_event_group_set_bits_isr(rtos_event_group_handle_t group,
                                        uint32_t bits,
                                        int *needs_switch);
uint32_t rtos_event_group_clear_bits(rtos_event_group_handle_t group,
                                      uint32_t bits);
rtos_err_t rtos_event_group_wait_bits(rtos_event_group_handle_t group,
                                       uint32_t bits_to_wait,
                                       uint32_t options,
                                       uint32_t timeout_ticks,
                                       uint32_t *out_bits);

#ifdef __cplusplus
}
#endif

#endif /* RTOS_EVENT_H */
