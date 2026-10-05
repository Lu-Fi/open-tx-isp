/* Host test (source guard): the T21 bayer_override diagnostic must be
 * applied at tisp_init (after the mbus-derived switch) and at once when
 * set through sysfs while the ISP runs, and must never touch reg 0x8 when
 * off (-1) or out of range.
 * Usage: t21_bayer_override_host_test <path to tx_isp_t21_recovered.c> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	FILE *f = fopen(argc > 1 ? argv[1] : "../driver/t21/tx_isp_t21_recovered.c", "r");
	long n;
	char *s, *init, *sw;
	int bad = 0;

	if (!f) { puts("FAIL (open)"); return 1; }
	fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
	s = calloc(n + 1, 1);
	if (fread(s, 1, n, f) != (size_t)n) { puts("FAIL (read)"); return 1; }
	bad += !strstr(s, "static int t21_bayer_override = -1;");
	bad += !strstr(s, "module_param_cb(bayer_override, &t21_bayer_override_ops, &t21_bayer_override, 0644);");
	/* sysfs set path: range check, only while the ISP runs */
	bad += !strstr(s, "if (t21_bayer_override >= 0 && t21_bayer_override <= 0x17 &&\n"
			  "\t    !t21_tisp_stats_quiesced) {\n"
			  "\t\tsystem_reg_write(8, (uint32_t)t21_bayer_override);");
	/* init path: after the mbus-derived switch, before the TOP 0x10 write */
	init = strstr(s, "int32_t tisp_init(int32_t *arg1)");
	sw = init ? strstr(init, "switch (bayer) {") : NULL;
	bad += !sw;
	if (sw) {
		char *ovr = strstr(sw, "if (t21_bayer_override >= 0 && t21_bayer_override <= 0x17) {\n"
				       "\t\tsystem_reg_write(8, (uint32_t)t21_bayer_override);");
		char *top = strstr(sw, "system_reg_write(0x10, topreg_val);");
		bad += !ovr || !top || ovr > top;
	}
	printf("%s (%d)\n", bad ? "FAIL" : "ok", bad);
	return bad != 0;
}
