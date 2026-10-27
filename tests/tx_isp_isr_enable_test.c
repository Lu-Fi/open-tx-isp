/* Host test for driver/common/tx_isp_isr_enable.h (issue 108): the run-time
 * t23/t31_isr_enable parameter must never switch the core interrupt sources
 * back on after the stream path cleared register 0xb0. */
#include <stdint.h>
#include <stdio.h>
#include "tx_isp_isr_enable.h"

static int failures;

#define CHECK(cond) do { if (!(cond)) { \
	fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
	failures++; } } while (0)

static uint32_t reg_b0;		/* the hardware register */
static uint32_t param;		/* the module parameter */
static struct tx_isp_isr_word word;

/* the two driver paths, each one critical section under the spinlock */
static void stream(int on) { reg_b0 = tx_isp_isr_word_switch(&word, on, param); }
static void param_write(uint32_t v)
{
	uint32_t reg;

	param = v;		/* param_set_uint, outside the lock */
	if (tx_isp_isr_word_update(&word, param, &reg))
		reg_b0 = reg;
}

int main(void)
{
	const uint32_t def = 0xffffffffU & ~((1U << 12) | (1U << 13) | (1U << 16));

	param = def;
	/* before the first stream on: the parameter must not touch 0xb0 */
	param_write(0xffffffffU);
	CHECK(reg_b0 == 0);
	stream(1);
	CHECK(reg_b0 == 0xffffffffU);	/* the value set while off is applied on */

	/* while streaming the parameter takes effect at once */
	param_write(def);
	CHECK(reg_b0 == def);

	/* the issue 108 interleaving: stream disable, then a late parameter
	 * write that had sampled "on" before -> 0xb0 must stay 0 */
	stream(0);
	param_write(0xffffffffU);
	CHECK(reg_b0 == 0);
	CHECK(param == 0xffffffffU);

	/* parameter written in the other order: value first, disable after */
	stream(1);
	CHECK(reg_b0 == 0xffffffffU);
	param_write(def);
	stream(0);
	CHECK(reg_b0 == 0);
	stream(1);
	CHECK(reg_b0 == def);

	if (failures) {
		fprintf(stderr, "tx_isp_isr_enable_test: %d failure(s)\n", failures);
		return 1;
	}
	printf("tx_isp_isr_enable_test: all checks passed\n");
	return 0;
}
