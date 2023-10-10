/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Arm Limited.
 * All rights reserved.
 */
#include <libcflat.h>

#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/rsi.h>

bool rsi_present;

int rsi_invoke(unsigned int function_id, unsigned long arg0,
	       unsigned long arg1, unsigned long arg2,
	       unsigned long arg3, unsigned long arg4,
	       unsigned long arg5, unsigned long arg6,
	       unsigned long arg7, unsigned long arg8,
	       unsigned long arg9, unsigned long arg10,
	       struct smccc_result *result)
{
	return arm_smccc_smc(function_id, arg0, arg1, arg2, arg3, arg4, arg5,
			     arg6, arg7, arg8, arg9, arg10, result);
}

struct rsi_realm_config __attribute__((aligned(RSI_GRANULE_SIZE))) config;

static unsigned long rsi_get_realm_config(struct rsi_realm_config *cfg)
{
	struct smccc_result res;

	rsi_invoke(SMC_RSI_REALM_CONFIG, __virt_to_phys((unsigned long)cfg),
		   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, &res);

	return res.r0;
}

int __rsi_get_version(unsigned long ver, struct smccc_result *res)
{
	if ((get_id_aa64pfr0_el1() & ID_AA64PFR0_EL1_EL3) == ID_AA64PFR0_EL1_EL3_NI)
		return -1;

	return rsi_invoke(SMC_RSI_ABI_VERSION, ver, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		          0, res);
}

int rsi_get_version(unsigned long ver)
{
	struct smccc_result res = {};
	int ret;


	ret = __rsi_get_version(ver, &res);
	if (ret == -1)
		return ret;

	return res.r0;
}

void arm_rsi_init(void)
{
	if (rsi_get_version(RSI_ABI_VERSION) != RSI_SUCCESS)
		return;

	if (rsi_get_realm_config(&config))
		return;

	rsi_present = true;

	phys_mask_shift = (config.ipa_width - 1);
	/* Set the upper bit of the IPA as the NS_SHARED pte attribute */
	prot_ns_shared = (1UL << phys_mask_shift);
}

static unsigned rsi_set_addr_range_state(unsigned long start, unsigned long end,
					 enum ripas_t state, unsigned int flags,
					 unsigned long *top)
{
	struct smccc_result res;

	rsi_invoke(SMC_RSI_IPA_STATE_SET, start, end, state, flags,
		   0, 0, 0, 0, 0, 0, 0, &res);
	*top = res.r1;
	return res.r0;
}

static int rsi_get_addr_range_state(unsigned long start, unsigned long end,
				    unsigned long *ripas, unsigned long *top)
{
	struct smccc_result res;

	rsi_invoke(SMC_RSI_IPA_STATE_GET, start, end, 0, 0,
		   0, 0, 0, 0, 0, 0, 0, &res);
	if (res.r0 == RSI_SUCCESS) {
		*top = res.r1;
		*ripas = res.r2;
	}
	return res.r0;
}

bool arm_is_protected_mmio(unsigned long pa, unsigned long size)
{
	unsigned long end = pa + size;
	unsigned long next = 0, ripas = RIPAS_EMPTY;

	if (!is_realm())
		return false;

	pa = ALIGN_DOWN(pa, RSI_GRANULE_SIZE);
	end = ALIGN(end, RSI_GRANULE_SIZE);

	while (pa < end) {
		if (rsi_get_addr_range_state(pa, end, &ripas, &next))
			break;
		assert(next > pa);
		if (ripas != RIPAS_IO)
			break;
		pa = next;
	}

	return (size && pa >= end);
}

static void arm_set_memory_state(unsigned long start,
				 unsigned long size,
				 unsigned int ripas,
				 unsigned int flags)
{
	int ret;
	unsigned long end, top;
	unsigned long old_start = start;

	if (!is_realm())
		return;

	start = ALIGN_DOWN(start, RSI_GRANULE_SIZE);
	if (start != old_start)
		size += old_start - start;
	end = ALIGN(start + size, RSI_GRANULE_SIZE);
	while (start != end) {
		ret = rsi_set_addr_range_state(start, end, ripas, flags, &top);
		assert(!ret);
		assert(top <= end);
		start = top;
	}
}

/*
 * Convert the IPA state of the given range to RIPAS_RAM, ignoring the
 * fact that the host could have destroyed the contents and we don't
 * rely on the previous state of the contents.
 */
void arm_set_memory_protected(unsigned long start, unsigned long size)
{
	arm_set_memory_state(start, size, RIPAS_RAM, RSI_CHANGE_DESTROYED);
}

/*
 * Convert the IPA state of the given range to RSI_RAM, ensuring that the
 * host has not destroyed any of the contents in the IPA range. Useful in
 * converting a range of addresses where some of the IPA may already be in
 * RSI_RAM state (e.g., images loaded at boot) and we want to make sure the
 * host hasn't modified (by destroying them) the contents.
 */
void arm_set_memory_protected_safe(unsigned long start, unsigned long size)
{
	arm_set_memory_state(start, size, RIPAS_RAM, RSI_NO_CHANGE_DESTROYED);
}

void arm_set_memory_shared(unsigned long start, unsigned long size)
{
	arm_set_memory_state(start, size, RIPAS_EMPTY, RSI_CHANGE_DESTROYED);
}

int rsi_attest_token_init(unsigned long *challenge, unsigned long *max_size)
{
	struct smccc_result res;

	rsi_invoke(SMC_RSI_ATTEST_TOKEN_INIT,
		   challenge[0], challenge[1], challenge[2],
		   challenge[3], challenge[4], challenge[5],
		   challenge[6], challenge[7], 0, 0, 0, &res);

	if (max_size)
		*max_size = res.r1;
	return res.r0;
}

int rsi_attest_token_continue(phys_addr_t addr,
			      unsigned long offset,
			      unsigned long size,
			      unsigned long *len)
{
	struct smccc_result res = { 0 };

	rsi_invoke(SMC_RSI_ATTEST_TOKEN_CONTINUE, addr, offset, size,
		   0, 0, 0, 0, 0, 0, 0, 0, &res);
	switch (res.r0) {
	case RSI_SUCCESS:
	case RSI_INCOMPLETE:
		if (len)
			*len = res.r1;
		/* Fall through */
	default:
		break;
	}
	return res.r0;
}

void rsi_extend_measurement(unsigned int index, unsigned long size,
			    unsigned long *measurement, struct smccc_result *res)
{
	rsi_invoke(SMC_RSI_MEASUREMENT_EXTEND, index, size,
		   measurement[0], measurement[1],
		   measurement[2], measurement[3],
		   measurement[4], measurement[5],
		   measurement[6], measurement[7],
		   0, res);
}

void rsi_read_measurement(unsigned int index, struct smccc_result *res)
{
	rsi_invoke(SMC_RSI_MEASUREMENT_READ, index, 0,
		   0, 0, 0, 0, 0, 0, 0, 0, 0, res);
}
