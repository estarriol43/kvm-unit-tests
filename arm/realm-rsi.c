/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Arm Limited.
 * All rights reserved.
 */

#include <libcflat.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <asm/psci.h>
#include <alloc_page.h>
#include <asm/rsi.h>
#include <asm/pgtable.h>
#include <asm/processor.h>

#define FID_SMCCC_VERSION	0x80000000
#define FID_INVALID		0xc5000041

#define SMCCC_VERSION_1_1	0x10001
#define SMCCC_SUCCESS		0
#define SMCCC_NOT_SUPPORTED	-1

static bool unknown_taken;

static void unknown_handler(struct pt_regs *regs, unsigned int esr)
{
	report_info("unknown_handler: esr=0x%x", esr);
	unknown_taken = true;
}

static void hvc_call(unsigned int fid)
{
	struct smccc_result res;

	unknown_taken = false;
	arm_smccc_hvc(fid, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, &res);

	if (unknown_taken) {
		report(true, "FID=0x%x caused Unknown exception", fid);
	} else {
		report(false, "FID=0x%x did not cause Unknown exception", fid);
		report_info("x0:  0x%lx", res.r0);
		report_info("x1:  0x%lx", res.r1);
		report_info("x2:  0x%lx", res.r2);
		report_info("x3:  0x%lx", res.r3);
		report_info("x4:  0x%lx", res.r4);
		report_info("x5:  0x%lx", res.r5);
		report_info("x6:  0x%lx", res.r6);
		report_info("x7:  0x%lx", res.r7);
	}
}

static void rsi_test_hvc(void)
{
	report_prefix_push("hvc");

	/* Test that HVC causes Undefined exception, regardless of FID */
	install_exception_handler(EL1H_SYNC, ESR_EL1_EC_UNKNOWN, unknown_handler);
	hvc_call(FID_SMCCC_VERSION);
	hvc_call(FID_INVALID);
	install_exception_handler(EL1H_SYNC, ESR_EL1_EC_UNKNOWN, NULL);

	report_prefix_pop();
}

static void host_call(unsigned int fid, unsigned long expected_x0)
{
	struct smccc_result res;
	struct rsi_host_call __attribute__((aligned(256))) host_call_data = { 0 };

	host_call_data.gprs[0] = fid;

	arm_smccc_smc(SMC_RSI_HOST_CALL, virt_to_phys(&host_call_data),
		       0, 0, 0, 0, 0, 0, 0, 0, 0, 0, &res);

	if (res.r0) {
		report(false, "RSI_HOST_CALL returned 0x%lx", res.r0);
	} else {
		if (host_call_data.gprs[0] == expected_x0) {
			report(true, "FID=0x%x x0=0x%lx",
				fid, host_call_data.gprs[0]);
		} else {
			report(false, "FID=0x%x x0=0x%lx expected=0x%lx",
				fid, host_call_data.gprs[0], expected_x0);
			report_info("x1:  0x%lx", host_call_data.gprs[1]);
			report_info("x2:  0x%lx", host_call_data.gprs[2]);
			report_info("x3:  0x%lx", host_call_data.gprs[3]);
			report_info("x4:  0x%lx", host_call_data.gprs[4]);
			report_info("x5:  0x%lx", host_call_data.gprs[5]);
			report_info("x6:  0x%lx", host_call_data.gprs[6]);
		}
	}
}

static void rsi_test_host_call(void)
{
	report_prefix_push("host_call");

	/* Test that host calls return expected values */
	host_call(FID_SMCCC_VERSION, SMCCC_VERSION_1_1);
	host_call(FID_INVALID, SMCCC_NOT_SUPPORTED);

	report_prefix_pop();
}

static void rsi_test_version(void)
{
	struct smccc_result res;
	int ret, version;

	report_prefix_push("version");

	ret = __rsi_get_version(RSI_ABI_VERSION, &res);
	if (ret < 0) {
		report(false, "SMC_RSI_ABI_VERSION failed (%d)", ret);
		return;
	}

	version = res.r1;
	report(res.r0 == RSI_SUCCESS, "RSI ABI version %u.%u (expected: %u.%u)",
	       RSI_ABI_VERSION_GET_MAJOR(version),
	       RSI_ABI_VERSION_GET_MINOR(version),
	       RSI_ABI_VERSION_GET_MAJOR(RSI_ABI_VERSION),
	       RSI_ABI_VERSION_GET_MINOR(RSI_ABI_VERSION));
	report_prefix_pop();
}

#define ALLOC_SIZE	(SZ_1M * 8)
#define HOLE_SIZE	(1UL << 12) + SZ_16K
#define HOLE_OFFSET	(1UL << 12)

static void rsi_test_get_ipa_state(void)
{
	unsigned long top, end, start, hole_start, hole_end;
	unsigned long ripas;
	void *mem = memalign(PAGE_SIZE, ALLOC_SIZE);

	report_prefix_push("get_ipa_state");

	if (!mem) {
		report_abort("Unable to allocate memory");
		goto out;
	}

	start = virt_to_phys(mem);
	end = start + ALLOC_SIZE;
	hole_start = start + HOLE_OFFSET;
	hole_end = hole_start + HOLE_SIZE;

	set_memory_decrypted(hole_start, HOLE_SIZE);

	report_info("Memory: 0x%lx - 0x%lx, Hole: 0x%lx-0x%lx\n",
		     start, end, hole_start, hole_end);
	while (start < end) {
		if (rsi_get_addr_range_state(start, end, &ripas, &top) != RSI_SUCCESS) {
			report_abort("Unexpected failure! %lx-%lx\n", start, end);
			return;
		}

		if (start < hole_start)
			report(top <= hole_start && ripas == RIPAS_RAM,
				"RIPAS Ram for region before the hole (0x%lx, 0x%lx)",
				start, top);
		else if (start < hole_end)
			report(top <= hole_end && ripas == RIPAS_EMPTY,
				"RIPAS Empty for hole region (0x%lx-0x%lx)",
				start, top);
		else
			report(top <= end && ripas == RIPAS_RAM,
				"RIPAS Ram for covering region after hole (0x%lx-0x%lx)",
				start, top);
		start = top;
	}
out:
	report_prefix_pop();
}

int main(int argc, char **argv)
{
	int i;

	report_prefix_push("rsi");

	if (!is_realm()) {
		report_skip("Not a realm, skipping tests");
		goto exit;
	}

	if (argc < 2) {
		rsi_test_version();
		rsi_test_host_call();
		rsi_test_hvc();
		rsi_test_get_ipa_state();
	} else {
		for (i = 1; i < argc; i++) {
			if (strcmp(argv[i], "version") == 0) {
				rsi_test_version();
			} else if (strcmp(argv[i], "hvc") == 0) {
				rsi_test_hvc();
			} else if (strcmp(argv[i], "host_call") == 0) {
				rsi_test_host_call();
			} else if (strcmp(argv[i], "get_ipa_state") == 0) {
				rsi_test_get_ipa_state();
			} else {
				report_abort("Unknown subtest '%s'", argv[1]);
			}
		}
	}
exit:
	return report_summary();
}
