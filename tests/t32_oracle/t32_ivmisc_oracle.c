// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our IVDC misc ioctl (driver/t32/tx_isp_t32_ivdc_misc.h)
 * against the unmodified vendor ivdc_misc_unlocked_ioctl (Matt Davis'
 * recovered C, PR #15 @ 2e2e06bc).  Random commands (all handled ones, their
 * neighbours, random words), user copy results and module words; compared:
 * the four module words, the return value and the ordered log of the hook
 * calls (copy direction, user address, size, the word copied out, isp_printf
 * level / format text).
 * Exit 0 = all equal, 1 = first mismatch.
 */
#include <stdio.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ivdc_misc.h"

#define R(f)	((t32o_fn)(void (*)(void))(f))
char t32_data_c5510[] = "unsupport ivdc ioclt cmd\n";
char t32_data_c5478[] = "Set direct_mode err! Failed to copy form user\n";
char t32_data_c5448[] = "Get direct_mode err! Failed to copy to user\n";
char t32_data_c53e4[] = "Get ivdc_mem_line err! Failed to copy to user\n";
char t32_data_c54dc[] = "Set ivdc_mem_paddr err! Failed to copy from user\n";
char t32_data_c5414[] = "Get ivdc_mem_stride err! Failed to copy to user\n";
char t32_data_c54a8[] = "Set ivdc_mem_line err! Failed to copy form user\n";
u32 t32_data_d9014, t32_data_d9010, t32_data_d98b8, t32_data_ce230;
u64 ivdc_misc_unlocked_ioctl(u32, u32, u32, u32);

static u32 rng = 0x0bad5eedu;
static u32 rnd(void)
{
	rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
	return rng;
}

#define MAXE 4
static struct { unsigned int n; u32 e[MAXE][4]; } EV[2];
static int side;
static u32 r_copy, v_in;
static void ev(u32 a, u32 b, u32 c, u32 d)
{
	unsigned int n = EV[side].n++;

	if (n < MAXE) {
		EV[side].e[n][0] = a; EV[side].e[n][1] = b; EV[side].e[n][2] = c; EV[side].e[n][3] = d;
	}
}
static u32 hs(const char *s)
{
	u32 h = 2166136261u;

	while (*s)
		h = (h ^ (u8)*s++) * 16777619u;
	return h;
}
static u32 h_cfu(u8 *d, u32 u, u32 n) { ev(1, u, n, 0); memcpy(d, &v_in, 4); return r_copy; }
static u32 h_ctu(u32 u, const u8 *s, u32 n) { u32 v; memcpy(&v, s, 4); ev(2, u, n, v); return r_copy; }
static const struct t32_ivdc_uos uo = { h_cfu, h_ctu };
u64 private_copy_from_user(u32 d, u32 u, u32 n, u32 a3) { (void)a3; return h_cfu(T32O_HOST(d), u, n); }
u64 private_copy_to_user(u32 u, u32 s, u32 n, u32 a3) { (void)a3; return h_ctu(u, T32O_HOST(s), n); }
int isp_printf(unsigned int level, const char *fmt, ...) { ev(9, level, hs(fmt), 0); return 0; }

int main(void)
{
	static const u32 cmds[] = { 0x2000546fu, 0x20005470u, 0xc0045471u, 0xc0045472u, 0xc0045473u, 0xc0045474u,
				    0xc0045475u, 0xc0045476u, 0x2000546eu, 0x20005471u, 0xc0045470u, 0xc0045477u, 0 };
	unsigned int c, n = 60000, nret[3] = { 0 };
	struct t32_ivdc iv;
	struct t32_ivdc_mem m;
	u32 w[4];

	for (c = 0; c < n; c++) {
		u32 cmd = rnd() % 8 ? cmds[rnd() % 13] : rnd(), arg = rnd(), ro, rr, k;

		for (k = 0; k < 4; k++)
			w[k] = rnd();
		r_copy = rnd() % 2 ? 0 : rnd() % 2 ? 4 : 1 + rnd() % 3;
		v_in = rnd();
		memset(&iv, 0, sizeof(iv));
		iv.mode = w[0]; m.line = w[1]; m.paddr = w[2]; m.stride = w[3];
		side = 0; EV[0].n = 0; memset(EV[0].e, 0, sizeof(EV[0].e));
		ro = (u32)t32_ivdc_misc_unlocked_ioctl(&iv, &m, &uo, cmd, arg);
		side = 1; EV[1].n = 0; memset(EV[1].e, 0, sizeof(EV[1].e));
		t32_data_d9014 = w[0]; t32_data_d9010 = w[1]; t32_data_d98b8 = w[2]; t32_data_ce230 = w[3];
		rr = (u32)t32o_call(R(ivdc_misc_unlocked_ioctl), rnd(), cmd, arg, 0, 0);
		if (ro != rr) {
			printf("t32_ivmisc_oracle: FAIL case %u: return value\n", c);
			return 1;
		}
		if (iv.mode != t32_data_d9014 || m.line != t32_data_d9010 || m.paddr != t32_data_d98b8 ||
		    m.stride != t32_data_ce230) {
			printf("t32_ivmisc_oracle: FAIL case %u: module words\n", c);
			return 1;
		}
		if (EV[0].n != EV[1].n || memcmp(EV[0].e, EV[1].e, sizeof(EV[0].e))) {
			printf("t32_ivmisc_oracle: FAIL case %u: hook call log\n", c);
			return 1;
		}
		nret[ro == 0 ? 0 : ro == (u32)-1 ? 1 : 2]++;
	}
	printf("t32_ivmisc_oracle: %u cases, 0 mismatches; returned 0 / -1 / bytes left: %u %u %u\n", n, nret[0], nret[1],
	       nret[2]);
	return 0;
}
