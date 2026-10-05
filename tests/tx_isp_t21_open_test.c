/* Host test for the T21 first-open activate walk and its rollback
 * (driver/t21/tx_isp_t21_open.h). */
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include "../driver/t21/tx_isp_t21_open.h"

struct mod {
	int32_t act_ret;	/* activate return value */
	int has_act, has_slake;
	int active, act_calls, slake_calls, slake_order;
};

static int order;

static int32_t act(void *m)
{
	struct mod *x = m;

	x->act_calls++;
	if (x->act_ret == 0)
		x->active = 1;
	return x->act_ret;
}

static int32_t slake(void *m)
{
	struct mod *x = m;

	x->slake_calls++;
	x->active = 0;
	x->slake_order = ++order;
	return 0;
}

static t21_open_module_fn lookup(void *m, unsigned int which)
{
	struct mod *x = m;

	if (which == T21_OPEN_ACTIVATE)
		return x->has_act ? act : NULL;
	return x->has_slake ? slake : NULL;
}

static int fails;
#define CHECK(c) do { if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

int main(void)
{
	struct mod m[5];
	void *v[16] = { 0 };
	int32_t r;
	int i;

	/* 1: all succeed, one -515 and one NULL slot: success, nothing slaked */
	for (i = 0; i < 5; i++)
		m[i] = (struct mod){ 0, 1, 1, 0, 0, 0, 0 };
	m[2].act_ret = T21_OPEN_NOIOCTLCMD;
	v[0] = &m[0]; v[1] = &m[1]; v[3] = &m[2]; v[5] = &m[3]; v[7] = &m[4];
	r = t21_open_activate_modules(v, 16, lookup);
	CHECK(r == 0);
	for (i = 0; i < 5; i++)
		CHECK(m[i].slake_calls == 0);
	CHECK(m[0].active && m[1].active && m[3].active && m[4].active);

	/* 2: module 3 (slot 5) fails: 0,1 rolled back newest first, the
	 * -515 module and the failing one not slaked, later not touched */
	order = 0;
	for (i = 0; i < 5; i++)
		m[i] = (struct mod){ 0, 1, 1, 0, 0, 0, 0 };
	m[2].act_ret = T21_OPEN_NOIOCTLCMD;
	m[3].act_ret = -22;
	r = t21_open_activate_modules(v, 16, lookup);
	CHECK(r == -22);
	CHECK(!m[0].active && !m[1].active);
	CHECK(m[1].slake_order == 1 && m[0].slake_order == 2);
	CHECK(m[2].slake_calls == 0 && m[3].slake_calls == 0);
	CHECK(m[4].act_calls == 0 && m[4].slake_calls == 0);

	/* 3: last module returns -515 -> success (stock semantics) */
	for (i = 0; i < 5; i++)
		m[i] = (struct mod){ 0, 1, 1, 0, 0, 0, 0 };
	m[4].act_ret = T21_OPEN_NOIOCTLCMD;
	CHECK(t21_open_activate_modules(v, 16, lookup) == 0);

	/* 4: no activate op on the last module -> success; on failure a
	 * module without slake op is skipped */
	for (i = 0; i < 5; i++)
		m[i] = (struct mod){ 0, 1, 1, 0, 0, 0, 0 };
	m[4].has_act = 0;
	CHECK(t21_open_activate_modules(v, 16, lookup) == 0);
	for (i = 0; i < 5; i++)
		m[i] = (struct mod){ 0, 1, 1, 0, 0, 0, 0 };
	m[1].has_slake = 0;
	m[4].act_ret = -16;
	CHECK(t21_open_activate_modules(v, 16, lookup) == -16);
	CHECK(m[1].active && m[1].slake_calls == 0);
	CHECK(!m[0].active && !m[2].active && !m[3].active);

	/* 5: first module fails: nothing to roll back */
	for (i = 0; i < 5; i++)
		m[i] = (struct mod){ 0, 1, 1, 0, 0, 0, 0 };
	m[0].act_ret = -19;
	CHECK(t21_open_activate_modules(v, 16, lookup) == -19);
	for (i = 0; i < 5; i++)
		CHECK(m[i].slake_calls == 0 && !m[i].active);

	if (fails)
		return 1;
	printf("tx_isp_t21_open_test: ok\n");
	return 0;
}
