/* Host test of driver/t31/tx_isp_t31_framedrop.h (T31 frame drop). */
#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "../driver/t31/tx_isp_t31_framedrop.h"

int main(void)
{
	u32 a, b;

	assert(sizeof(struct tisp_frame_drop_user) == 12);
	assert(TX_ISP_SET_FRAME_DROP_CMD == 0xc00456e6U);
	assert(TX_ISP_GET_FRAME_DROP_CMD == 0xc00456e7U);
	assert(tisp_frame_drop_base(0) == 0x9800 && tisp_frame_drop_base(2) == 0x9a00);

	/* registers: enabled writes lsize/fmark, disabled the reset state */
	assert(tisp_frame_drop_regs(1, 3, 0x5, &a, &b) == 0 && a == 3 && b == 5);
	assert(tisp_frame_drop_regs(0, 3, 0x5, &a, &b) == 0 && a == 0 && b == 1);
	assert(tisp_frame_drop_regs(1, 31, 0xffffffff, &a, &b) == 0 && a == 31);
	assert(tisp_frame_drop_regs(1, 32, 1, &a, &b) == -EINVAL);
	assert(tisp_frame_drop_regs(0, 32, 1, &a, &b) == -EINVAL);

	/* reset state is what a disabled channel is written with */
	assert(tisp_frame_drop_regs(0, 0, 0, &a, &b) == 0 && a == 0 && b == 1);
	/* the user record is three of 12 bytes: enable, lsize, fmark */
	assert(offsetof(struct tisp_frame_drop_user, lsize) == 4);
	assert(offsetof(struct tisp_frame_drop_user, fmark) == 8);
	puts("tx_isp_t31_framedrop_test: ok");
	return 0;
}
