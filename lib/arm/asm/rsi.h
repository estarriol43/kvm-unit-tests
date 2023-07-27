/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Arm Limited.
 * All rights reserved.
 */
#ifndef __ASMARM_RSI_H_
#define __ASMARM_RSI_H_

#include <stdbool.h>

static inline bool is_realm(void)
{
	return false;
}

static inline void arm_rsi_init(void) {}
static inline void arm_set_memory_protected(unsigned long va, size_t size) {}
static inline void arm_set_memory_protected_safe(unsigned long va, size_t size) {}
static inline void arm_set_memory_shared(unsigned long va, size_t size) {}

#endif /* __ASMARM_RSI_H_ */
