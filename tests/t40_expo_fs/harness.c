/* tests/t40_expo_fs/harness.c: host side of t40_expo_fs_host.sh.  One case per stdin line, one result line per case:
 *   T ch value core_hex attr_hex  -> t40_efs_set_it      : "ret core_hex attr_hex"
 *   G ch value core_hex attr_hex  -> t40_efs_set_again   : "ret core_hex attr_hex"
 *   F 0 0 core_hex attr_hex       -> t40_efs_slots_take(fs = 1, ch 0) : "n cmd:value ... core_hex"
 *   D 0 0 core_hex attr_hex       -> t40_efs_slots_take(fs = 0, ch 0) : "n cmd:value ... core_hex"
 *   V idx pending sel0 sel1 vic_hex -> t40_vic_irq_account : "work vic_hex"
 * core_hex = core bytes 0x300..0x38f, attr_hex = attr bytes 0x000..0x12f, vic_hex = vic bytes 0x2c0..0x2ef. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include EXPO_FS_H

static uint8_t core[0x400], attr[0x130], vic[0x300];

static int unhex(const char *s, uint8_t *d, size_t n)
{
	size_t i;
	unsigned int v;

	if (strlen(s) != 2 * n)
		return -1;
	for (i = 0; i < n; i++) {
		if (sscanf(s + 2 * i, "%2x", &v) != 1)
			return -1;
		d[i] = (uint8_t)v;
	}
	return 0;
}

static void hex(const uint8_t *d, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		printf("%02x", d[i]);
}

int main(void)
{
	static char a[4096], b[4096];
	char op;
	unsigned int p1, p2, p3, p4;
	struct t40_efs_cmd cmds[9];
	unsigned int n, i;

	for (;;) {
		if (scanf(" %c", &op) != 1)
			break;
		if (op == 'V') {
			if (scanf("%u %x %x %x %4095s", &p1, &p2, &p3, &p4, a) != 5 || unhex(a, vic + 0x2c0, 0x30))
				return 2;
			memcpy(vic + 0x24c, &p3, 4);
			memcpy(vic + 0x250, &p4, 4);
			printf("%d ", t40_vic_irq_account(vic, p1, p2));
			hex(vic + 0x2c0, 0x30);
			printf("\n");
			continue;
		}
		if (scanf("%u %x %4095s %4095s", &p1, &p2, a, b) != 4 || unhex(a, core + 0x300, 0x90) || unhex(b, attr, 0x130))
			return 2;
		if (op == 'T' || op == 'G') {
			printf("%d ", op == 'T' ? t40_efs_set_it(core, attr, p1, p2) : t40_efs_set_again(core, attr, p1, p2));
			hex(core + 0x300, 0x90);
			printf(" ");
			hex(attr, 0x130);
			printf("\n");
			continue;
		}
		n = t40_efs_slots_take(core, attr, 0, op == 'F' ? 1U : 0U, cmds);
		printf("%u", n);
		for (i = 0; i < n; i++)
			printf(" %x:%x", cmds[i].cmd, cmds[i].value);
		printf(" ");
		hex(core + 0x300, 0x90);
		printf("\n");
	}
	return 0;
}
