// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: state getters of tx_isp_t32_api_get.h (tisp_get_ae_info,
 * tisp_get_system_sensor_num, tisp_get_msca_osd_info, tisp_get_tuning,
 * tisp_g_wdr_en, tisp_g_module_ratio_attr, tisp_g_riscv_info,
 * tisp_g_raw_rw_handler and the five get_sensor_* stubs) against the vendor
 * functions (Matt Davis' recovered C, PR #15 @ 2e2e06bc).  Random init
 * parameters (stIspInitPar = dc284), tuning attribute block (tisp_tattr =
 * f0904), OSD byte (e28bc), RISC-V state (riscvEnable f0900, stRiscvPara
 * e4100) and random-filled output buffers: return values, every output byte
 * (canaries around the written fields) and the RISC-V state are compared.
 * Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_api_get.h"

u8 t32_data_e28bc[4] __attribute__((aligned(4), weak));	/* .bss +0x98bc: MSCA OSD state byte */
u32 t32_data_f0904 __attribute__((weak));		/* .bss +0x17904: tisp_tattr (tuning attribute block pointer) */
extern u8 t32_data_f0900[0x10000], t32_data_e4100[0x10000];

extern int tisp_get_ae_info(uintptr_t);
extern u32 ref_tisp_get_system_sensor_num(void);
extern u64 tisp_get_msca_osd_info(u32, u32, u32, u32);
extern u32 tisp_get_tuning(void);
extern int tisp_g_wdr_en(uintptr_t);
extern u64 tisp_g_module_ratio_attr(u32, u32, u32, u32);
extern int tisp_g_raw_rw_handler(void);
extern int get_sensor_start_iq_index(void), get_sensor_setting(void), get_sensor_section_num(void),
	   get_sensor_setting_len(void);
extern u64 get_sensor_setting_md5(u32, u32, u32, u32);
extern u64 tisp_g_riscv_info(u32, u32, u32, u32);

#define BUF	256
#define TATTR	2048
#define NF	13
static const char *const names[NF] = {
	"get_ae_info", "get_system_sensor_num", "get_msca_osd_info", "get_tuning", "g_wdr_en",
	"g_module_ratio_attr", "g_raw_rw_handler", "get_sensor_start_iq_index", "get_sensor_setting",
	"get_sensor_section_num", "get_sensor_setting_len", "get_sensor_setting_md5", "g_riscv_info"
};

static u32 rs = 0x2b9d40e3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static void fill(u8 *p, unsigned int n)
{
	while (n--)
		*p++ = (u8)rnd();
}

static u8 riscv_en[16], riscv_para[4 * T32_RISCV_PARA_SZ];

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 6000;
	unsigned int c, k, cmp = 0;
	static u8 ibuf[BUF], obuf[BUF], tattr_in[TATTR], src[T32_RISCV_PARA_SZ];

	for (c = 0; c < n; c++)
		for (k = 0; k < NF; k++) {
			u8 *vbuf, *tattr, *vsrc;
			u32 ch = rnd() & 7, a1 = rnd(), rv = 0, ro = 0;

			fill(ibuf, BUF);
			fill(tattr_in, TATTR);
			t32o_reset();
			vbuf = t32o_alloc(BUF);
			tattr = t32o_alloc(TATTR);
			memcpy(vbuf, ibuf, BUF);
			memcpy(obuf, ibuf, BUF);
			memcpy(tattr, tattr_in, TATTR);
			t32_data_f0904 = T32O_PTR(tattr);
			t32_data_dc284[36] = (u8)rnd();
			t32_data_dc284[900] = (u8)rnd();
			t32_data_e28bc[0] = (u8)rnd();
			switch (k) {
			case 0:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_get_ae_info, T32O_PTR(vbuf), 0, 0, 0, 0);
				ro = (u32)t32_get_ae_info((u32 *)obuf);
				break;
			case 1:
				rv = ref_tisp_get_system_sensor_num();
				ro = t32_get_system_sensor_num(t32_data_dc284);
				break;
			case 2:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_get_msca_osd_info, ch, a1, 0, 0, 0);
				ro = t32_get_msca_osd_info(t32_data_e28bc);
				break;
			case 3: {
				void *tp = T32O_HOST(t32_data_f0904);

				rv = tisp_get_tuning();
				ro = T32O_PTR(t32_get_tuning(&tp));
				break;
			}
			case 4:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_g_wdr_en, T32O_PTR(vbuf + 8), 0, 0, 0, 0);
				ro = (u32)t32_g_wdr_en((u32 *)(obuf + 8), t32_data_dc284);
				break;
			case 5:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_g_module_ratio_attr, ch, T32O_PTR(vbuf + 16), 0, 0, 0);
				ro = (u32)t32_g_module_ratio_attr(ch, obuf + 16, tattr_in);
				break;
			case 6: rv = (u32)tisp_g_raw_rw_handler(); ro = (u32)t32_g_raw_rw_handler(); break;
			case 7: rv = (u32)get_sensor_start_iq_index(); ro = (u32)t32_get_sensor_start_iq_index(); break;
			case 8: rv = (u32)get_sensor_setting(); ro = (u32)t32_get_sensor_setting(); break;
			case 9: rv = (u32)get_sensor_section_num(); ro = (u32)t32_get_sensor_section_num(); break;
			case 10: rv = (u32)get_sensor_setting_len(); ro = (u32)t32_get_sensor_setting_len(); break;
			case 11:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))get_sensor_setting_md5, ch, a1, 0, 0, 0);
				ro = (u32)t32_get_sensor_setting_md5();
				break;
			default: {
				u32 en, i, chr = ch & 3;

				if (c % 4 == 3)		/* the RISC-V copy is 4 x 12800 B: every 4th case */
					break;
				en = rnd() % 3 ? (rnd() % 4 ? 1 : 0x100 + 1) : rnd();	/* 1, 0x101 (u8 1), other */
				if (c % 5 == 0)
					en = rnd() & 0xff00;		/* u8 0 */
				fill(src, sizeof(src));
				vsrc = t32o_alloc(sizeof(src));
				memcpy(vsrc, src, sizeof(src));
				for (i = 0; i < 16; i++)
					t32_data_f0900[i] = riscv_en[i] = (u8)rnd();
				for (i = 0; i < sizeof(riscv_para); i++)
					t32_data_e4100[i] = riscv_para[i] = (u8)(i * 7 + c);
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_g_riscv_info, chr, en, T32O_PTR(vsrc), 0, 0);
				ro = (u32)t32_g_riscv_info(chr, en, src, riscv_en, riscv_para);
				if (memcmp(t32_data_f0900, riscv_en, 16) || memcmp(t32_data_e4100, riscv_para, sizeof(riscv_para))) {
					printf("t32_getd_oracle: case %u %s (ch %u en 0x%x): RISC-V state differs\n", c, names[k], chr, en);
					return 1;
				}
				if (memcmp(vsrc, src, sizeof(src))) {
					printf("t32_getd_oracle: case %u %s: source changed\n", c, names[k]);
					return 1;
				}
				break;
			}
			}
			if (rv != ro || memcmp(vbuf, obuf, BUF) || memcmp(tattr, tattr_in, TATTR)) {
				printf("t32_getd_oracle: case %u %s (ch %u): return vendor 0x%x ours 0x%x, buffer %s, tattr %s\n",
				       c, names[k], ch, rv, ro, memcmp(vbuf, obuf, BUF) ? "differs" : "equal",
				       memcmp(tattr, tattr_in, TATTR) ? "changed" : "equal");
				return 1;
			}
			cmp++;
		}
	printf("t32_getd_oracle: %u calls compared (13 functions, return + 256 B output buffer + tuning block + RISC-V state), 0 mismatches\n", cmp);
	return 0;
}
