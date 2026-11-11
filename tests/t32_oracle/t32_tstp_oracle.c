// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the TSTP block of tx_isp_t32_tstp.h (tisp_tstp_init,
 * _deinit, _reg_cfg, _mark1_func, _mark2_func, _param_array_get / _set,
 * _wake_up) against the vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc).  Random 154 B parameter blocks (every bit pattern; fields with
 * bits beyond their register width), random marker arguments (table lengths
 * 0..16, values with high bits), missing block (NULL).  Compared per call:
 * return value, every register write (address, value, order), the whole
 * block after the call plus canaries behind it, the copied-out buffer and
 * size, the vmalloc / vfree calls.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_tstp.h"

u32 t32_data_e28c0 __attribute__((aligned(4), weak));	/* .bss: TSTP parameter block pointer */

extern u64 tisp_tstp_init(u32, u32, u32, u32), tisp_tstp_deinit(u32, u32, u32, u32),
	   tisp_tstp_reg_cfg(u32, u32, u32, u32), tisp_tstp_wake_up(u32, u32, u32, u32),
	   tisp_tstp_param_array_get(u32, u32, u32, u32), tisp_tstp_param_array_set(u32, u32, u32, u32),
	   tisp_tstp_mark1_func(u32, u32, u32, u32, u32, u32), tisp_tstp_mark2_func(u32, u32, u32, u32, u32, u32);

static u32 rs = 0x1f83d9abu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* vendor allocator calls */
static u32 v_vm_n, v_vm_size, v_vf_n, v_vf_p;
u64 private_vmalloc(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u8 *m = t32o_alloc(a0 + 16);

	(void)a1; (void)a2; (void)a3;
	memset(m, 0xa5, a0 + 16);	/* vmalloc memory is not zeroed */
	v_vm_n++; v_vm_size = a0;
	return T32O_PTR(m);
}
u64 private_vfree(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a1; (void)a2; (void)a3;
	v_vf_n++; v_vf_p = a0;
	return 0;
}

/* ours */
static struct { unsigned int n; struct { u32 reg, val; } w[64]; } olog;
static u32 o_vm_n, o_vm_size, o_vf_n, o_vf_p;
static void *o_vmalloc(u32 size)
{
	u8 *m = t32o_alloc(size + 16);

	memset(m, 0xa5, size + 16);
	o_vm_n++; o_vm_size = size;
	return m;
}
static void o_vfree(void *p) { o_vf_n++; o_vf_p = T32O_PTR(p); }
static void o_wr(u32 reg, u32 val)
{
	if (olog.n < 64) {
		olog.w[olog.n].reg = reg;
		olog.w[olog.n].val = val;
	}
	olog.n++;
}
static const struct t32_tstp_ops ops = { .vmalloc = o_vmalloc, .vfree = o_vfree, .reg_write = o_wr };

#define NF 8
static const char *const names[NF] = {
	"init", "deinit", "reg_cfg", "wake_up", "param_array_get", "param_array_set", "mark1", "mark2"
};
#define BLK (T32_TSTP_PAR_SZ + 16)	/* block + canaries */

static void fill(u8 *p, unsigned int n)
{
	unsigned int mode = rnd() % 4;

	while (n--)
		*p++ = mode == 0 ? 0 : mode == 1 ? 0xff : (u8)rnd();
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 30000;
	unsigned int c, k, i, cmp = 0, nwr = 0;
	static u8 blk[BLK], vbuf[256], obuf[256], src[BLK];
	static u16 t1[16], t2[16];

	for (c = 0; c < n; c++)
		for (k = 0; k < NF; k++) {
			u8 *vblk = NULL, *oblk = NULL, *vb, *vs, *vt1, *vt2;
			struct t32_tstp t = { .ops = &ops };
			u32 rv, ro, a0 = rnd(), a1 = rnd(), a2 = rnd(), a3 = rnd(), a4 = rnd(), a5 = rnd(), osz = 0;
			int none = (k != 0 && k != 2 && k != 4 && k != 5 && c % 7 == 3);	/* no block */

			if (c & 1) {
				a0 &= 0xffff; a1 &= 0xffff;
			}
			fill(blk, BLK);
			fill(src, BLK);
			for (i = 0; i < 16; i++) {
				t1[i] = (u16)rnd();
				t2[i] = (u16)rnd();
			}
			memset(vbuf, 0xc3, sizeof(vbuf));
			memset(obuf, 0xc3, sizeof(obuf));
			memset(&olog, 0, sizeof(olog));
			v_vm_n = v_vm_size = v_vf_n = v_vf_p = o_vm_n = o_vm_size = o_vf_n = o_vf_p = 0;
			t32o_reset();
			if (!none) {
				vblk = t32o_alloc(BLK);
				oblk = t32o_alloc(BLK);
				memcpy(vblk, blk, BLK);
				memcpy(oblk, blk, BLK);
			}
			t32_data_e28c0 = T32O_PTR(vblk);
			t.par = oblk;
			vb = t32o_alloc(256);
			memcpy(vb, vbuf, 256);
			vs = t32o_alloc(BLK);
			memcpy(vs, src, BLK);
			vt1 = t32o_alloc(32);
			vt2 = t32o_alloc(32);
			memcpy(vt1, t1, 32);
			memcpy(vt2, t2, 32);
			switch (k) {
			case 0:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_tstp_init, a0, a1, a2, a3, 0);
				ro = (u32)t32_tstp_init(&t);
				vblk = T32O_HOST(t32_data_e28c0);
				oblk = t.par;
				break;
			case 1:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_tstp_deinit, a0, a1, a2, a3, 0);
				ro = (u32)t32_tstp_deinit(&t);
				if (!none && (v_vf_p != T32O_PTR(vblk) || o_vf_p != T32O_PTR(oblk))) {
					printf("t32_tstp_oracle: case %u deinit: vfree of the wrong pointer\n", c);
					return 1;
				}
				v_vf_p = o_vf_p = 0;
				if (t32_data_e28c0 != 0 || t.par) {
					printf("t32_tstp_oracle: case %u deinit: pointer not cleared\n", c);
					return 1;
				}
				break;
			case 2:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_tstp_reg_cfg, a0, a1, a2, a3, 0);
				ro = (u32)t32_tstp_reg_cfg(&t);
				break;
			case 3:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_tstp_wake_up, a0, a1, a2, a3, 0);
				ro = (u32)t32_tstp_wake_up(&t);
				break;
			case 4:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_tstp_param_array_get, T32O_PTR(vb),
						    T32O_PTR(vb + 200), a2, a3, 0);
				ro = (u32)t32_tstp_param_array_get(&t, obuf, &osz);
				memcpy(obuf + 200, &osz, 4);
				break;
			case 5:
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_tstp_param_array_set, T32O_PTR(vs), a1, a2, a3, 0);
				ro = (u32)t32_tstp_param_array_set(&t, src);
				break;
			case 6: {
				u32 n1 = rnd() % 17, n2 = rnd() % 17;

				if (c % 5 == 0)
					n1 = c % 2 ? 16 : 0;
				if (c % 5 == 1)
					n2 = c % 2 ? 0 : 16;
				t32o_arg5 = T32O_PTR(vt2);
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_tstp_mark1_func, a0, a1, n1, T32O_PTR(vt1), n2);
				t32o_arg5 = 0;
				ro = (u32)t32_tstp_mark1(&t, a0 & 0xffff, a1, n1, t1, n2, t2);
				break;
			}
			default:
				t32o_arg5 = a5;
				rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_tstp_mark2_func, a0, a1, a2, a3, a4);
				t32o_arg5 = 0;
				ro = (u32)t32_tstp_mark2(&t, a0, a1, a2, a3, a4, a5);
				break;
			}
			if (rv != ro || t32o_log.n != olog.n) {
				printf("t32_tstp_oracle: case %u %s%s: return vendor 0x%x ours 0x%x, writes %u/%u\n",
				       c, names[k], none ? " (no block)" : "", rv, ro, t32o_log.n, olog.n);
				return 1;
			}
			for (i = 0; i < olog.n; i++)
				if (t32o_log.w[i].reg != olog.w[i].reg || t32o_log.w[i].val != olog.w[i].val) {
					printf("t32_tstp_oracle: case %u %s write %u: vendor 0x%x=0x%x ours 0x%x=0x%x\n", c, names[k],
					       i, t32o_log.w[i].reg, t32o_log.w[i].val, olog.w[i].reg, olog.w[i].val);
					return 1;
				}
			if ((vblk == NULL) != (oblk == NULL) || (vblk && memcmp(vblk, oblk, k == 0 ? T32_TSTP_PAR_SZ : BLK))) {
				printf("t32_tstp_oracle: case %u %s: parameter block differs\n", c, names[k]);
				return 1;
			}
			if (memcmp(vb, obuf, 256) || memcmp(vs, src, BLK) || memcmp(vt1, t1, 32) || memcmp(vt2, t2, 32)) {
				printf("t32_tstp_oracle: case %u %s: output / input buffer differs\n", c, names[k]);
				return 1;
			}
			if (v_vm_n != o_vm_n || v_vm_size != o_vm_size || v_vf_n != o_vf_n || v_vf_p != o_vf_p) {
				printf("t32_tstp_oracle: case %u %s: allocator calls differ\n", c, names[k]);
				return 1;
			}
			nwr += olog.n;
			cmp++;
		}
	printf("t32_tstp_oracle: %u calls compared (8 functions, %u register writes, block + buffers + allocator calls), 0 mismatches\n",
	       cmp, nwr);
	return 0;
}
