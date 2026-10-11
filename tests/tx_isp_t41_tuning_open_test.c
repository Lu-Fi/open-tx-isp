#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include "../driver/t41/tx_isp_t41_tuning_open.h"

/* Simulated core: state + tisp_tattr allocation. */
static uint32_t state = 2;
static int tattr_live;
static unsigned int count;

static int do_open(void)
{
	int act = t41_tuning_open_decide(state, count);

	if (act < 0)
		return act;
	if (act == T41_TUNING_OPEN_ENABLE) {
		tattr_live = 1;
		state = 3;
	}
	t41_tuning_open_done(&count);
	return 0;
}

static void do_close(void)
{
	if (t41_tuning_release_decide(state, &count)) {
		tattr_live = 0;
		state = 2;
	}
}

int main(void)
{
	/* streamer opens, a tool opens and closes: tuning must survive */
	assert(do_open() == 0 && tattr_live && state == 3 && count == 1);
	assert(do_open() == 0 && count == 2);
	do_close();
	assert(tattr_live && state == 3 && count == 1);
	do_close();
	assert(!tattr_live && state == 2 && count == 0);

	/* core not initialised (state 1): refused like the vendor */
	state = 1;
	assert(do_open() == -EPERM && count == 0);

	/* state 3 without our handles (cannot share): refused */
	state = 3;
	assert(do_open() == -EPERM && count == 0);

	/* slake disabled tuning (state 2) while a handle was open */
	state = 2;
	assert(do_open() == 0 && count == 1);
	state = 2;		/* ispcore_slake_module -> tisp_disable_tuning */
	tattr_live = 0;
	assert(do_open() == 0 && tattr_live && state == 3 && count == 2);
	do_close();
	assert(tattr_live && count == 1);
	do_close();
	assert(!tattr_live && count == 0);

	/* unbalanced close never underflows */
	do_close();
	assert(count == 0);

	puts("tx_isp_t41_tuning_open_test: ok");
	return 0;
}
