/* Host test (source guard): T21 tiziano_init must follow day_night so a
 * streamer restart in night mode keeps the night bank / 0x1730 mono value.
 * Usage: t21_night_reinit_host_test <path to tx_isp_t21_recovered.c> */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
	FILE *f = fopen(argc > 1 ? argv[1] : "../driver/t21/tx_isp_t21_recovered.c", "r");
	long n;
	char *s;
	int bad = 0;

	if (!f) { puts("FAIL (open)"); return 1; }
	fseek(f, 0, SEEK_END); n = ftell(f); rewind(f);
	s = calloc(n + 1, 1);
	if (fread(s, 1, n, f) != (size_t)n) { puts("FAIL (read)"); return 1; }
	bad += !strstr(s, "memcpy(tparams, day_night ? tparams_night : tparams_day, 0x15380);");
	bad += !strstr(s, "system_reg_write(0x1730, day_night ? 0xff008080 : 0xff00ff00);");
	bad += !!strstr(s, "\tmemcpy(tparams, tparams_day, 0x15380);\n\tmemset(custom_eff");
	printf("%s (%d)\n", bad ? "FAIL" : "ok", bad);
	return bad != 0;
}
