/* Host test of driver/t23/tx_isp_t23_clk.h (openimp #813): the vendor
 * ispcore_activate_module clock step (ko_all.c:53276-53293): parents
 * cgu_isp <- clk_name, cgu_vpu <- clka_name from sclk_name {mpll, sclka}
 * (ispcore_set_clk_parent, ko_all.c:52213-52265), then clks[0] = isp_clk,
 * clks[1] = isp_clka, in that order.  Simulated Ingenic CGU: rate =
 * parent / divider (integer divider, round down like the cgu set_rate). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../driver/t23/tx_isp_t23_clk.h"

struct sclk { const char *name; unsigned long rate; struct sclk *parent; int refs; };
static struct sclk mpll = { "mpll", 1200000000UL, NULL, 0 };
static struct sclk sclka = { "sclka", 1188000000UL, NULL, 0 };
static struct sclk cgu_isp = { "cgu_isp", 150000000UL, &mpll, 0 };
static struct sclk cgu_vpu = { "cgu_vpu", 400000000UL, &mpll, 0 };
static struct sclk *all[] = { &mpll, &sclka, &cgu_isp, &cgu_vpu };
static char trace[512];
static int logs;

static void *get(const char *n)
{
	unsigned int i;

	for (i = 0; i < sizeof(all) / sizeof(all[0]); i++)
		if (!strcmp(all[i]->name, n)) {
			all[i]->refs++;
			return all[i];
		}
	return NULL;
}
static void put(void *c) { ((struct sclk *)c)->refs--; }
static int set_parent(void *c, void *p)
{
	struct sclk *k = c;

	snprintf(trace + strlen(trace), sizeof(trace) - strlen(trace), "P %s<-%s;", k->name, ((struct sclk *)p)->name);
	k->parent = p;
	return 0;
}
static int set_rate(void *c, unsigned long r)
{
	struct sclk *k = c;
	unsigned long div = (k->parent->rate + r - 1) / r;

	snprintf(trace + strlen(trace), sizeof(trace) - strlen(trace), "R %s=%lu;", k->name, r);
	k->rate = k->parent->rate / div;
	return 0;
}
static void lg(const char *m, const char *a) { (void)m; (void)a; logs++; }
static const struct t23_clk_ops ops = { get, put, set_parent, set_rate, lg };

static void reset(void)
{
	cgu_isp.parent = cgu_vpu.parent = &mpll;
	cgu_isp.rate = 150000000UL;
	cgu_vpu.rate = 400000000UL;
	trace[0] = 0;
	logs = 0;
}

int main(void)
{
	void *clks[3] = { &cgu_isp, &cgu_vpu, NULL };
	unsigned int i;

	assert(t23_clk_parent_index("mpll") == 0);
	assert(t23_clk_parent_index("sclka") == 1);
	assert(t23_clk_parent_index("sclk") == -1);
	assert(t23_clk_parent_index("sclkab") == -1);
	assert(t23_clk_parent_index(NULL) == -1);

	/* QC3 / vendor boot line: clk_name=sclka isp_clk=200000000 clka_name=sclka isp_clka=600000000 */
	reset();
	assert(t23_core_apply_clk_params(&ops, clks, 3, "sclka", "sclka", 200000000UL, 600000000UL) == 2);
	assert(!strcmp(trace, "P cgu_isp<-sclka;P cgu_vpu<-sclka;R cgu_isp=200000000;R cgu_vpu=600000000;"));
	assert(cgu_isp.rate == 198000000UL && cgu_vpu.rate == 594000000UL);
	assert(logs == 0);
	for (i = 0; i < 4; i++)
		assert(all[i]->refs == 0);

	/* no parent names (module defaults): rates only, parent unchanged (vendor would crash) */
	reset();
	assert(t23_core_apply_clk_params(&ops, clks, 3, NULL, NULL, 153000000UL, 416000000UL) == 2);
	assert(!strcmp(trace, "R cgu_isp=153000000;R cgu_vpu=416000000;"));
	assert(cgu_isp.parent == &mpll && cgu_isp.rate == 150000000UL && cgu_vpu.rate == 400000000UL);

	/* unknown parent name: vendor "The %s was not found!", rates still applied */
	reset();
	assert(t23_clk_set_parent(&ops, "apll", "cgu_isp") == -2);
	assert(logs == 1 && cgu_isp.parent == &mpll);

	/* rate 0 is not applied */
	reset();
	assert(t23_core_apply_clk_params(&ops, clks, 3, "mpll", "mpll", 0, 0) == 0);
	assert(!strcmp(trace, "P cgu_isp<-mpll;P cgu_vpu<-mpll;"));
	puts("t23_clk_host: ok");
	return 0;
}
