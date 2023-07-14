#ifndef __ARM_SVE_VL_TEST_H_
#define __ARM_SVE_VL_TEST_H_

#include <asm/processor.h>
#include <asm/rsi.h>
#include <asm/sysreg.h>

static bool check_arm_sve_vl(long val)
{
	unsigned long vl;

	if (!system_supports_sve()) {
		report_skip("SVE is not supported\n");
	} else {
		/* Enable the maxium SVE vector length */
		write_sysreg(ZCR_EL1_LEN, ZCR_EL1);
		vl = sve_vl();
		/* Realms are configured with a SVE VL */
		if (is_realm()) {
			report(vl == val,
				"SVE VL expected (%ld), detected (%ld)",
				val, vl);
		} else {
			report(true, "Detected SVE VL %ld\n", vl);
		}
	}
	return true;
}
#endif
