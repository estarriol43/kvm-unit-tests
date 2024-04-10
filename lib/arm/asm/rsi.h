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

#endif /* __ASMARM_RSI_H_ */
