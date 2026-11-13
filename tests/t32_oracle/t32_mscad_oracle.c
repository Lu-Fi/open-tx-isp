// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: MSCA register leaf functions (tx_isp_t32_msca_io.h).
 * Runs the unmodified vendor functions (Matt Davis' recovered C, PR #15 @
 * 2e2e06bc) and our port from the same arguments in both register modes
 * (t32_data_d03a0 = 0: direct, 1: shadow) and compares every register write
 * (address / shadow word, value, order), every register read (order), the
 * values stored through pointers and the return values the callers use.
 * tisp_reg_write / tisp_reg_read (fn_1c790 / fn_1c74c) are replaced here:
 * mode 0 is their system_reg_* tail call, mode 1 logs 0x80000000 | group << 16
 * | index.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_msca_io.h"

extern u64 tisp_msca_ram_write(u32, u32, u32, u32);
extern u64 tisp_msca_addr_fifo_clear_byindex(u32, u32, u32, u32);
extern u64 tisp_msca_addr_fifo_clear_reg_byindex(u32, u32, u32, u32);
extern u64 tisp_msca_addr_fifo_clear_all(u32, u32, u32, u32);
extern u64 tisp_msca_addr_fifo_write(u32, u32, u32, u32);
extern u64 tisp_msca_addr_fifo_read(u32, u32, u32, u32);
extern u64 tisp_msca_addr_fifo_state(u32, u32, u32, u32);
extern u64 tisp_msca_work_state(u32, u32, u32, u32);
extern u64 tisp_msca_ir_init(u32, u32, u32, u32);
extern u64 tisp_msca_addr_ir_fifo_read(u32, u32, u32, u32);
extern u64 tisp_msca_addr_ir_fifo_write(u32, u32, u32, u32);
extern u64 tisp_msca_get_shdram_flag(void);
extern u64 tisp_msca_set_shdram_flag(u32, u32, u32, u32);
extern u64 lib_tisp_msca_chx_reg_stream_off(u32, u32, u32, u32);
extern u64 lib_tisp_get_msca_chx_en(u32, u32, u32, u32);
extern u64 lib_tisp_api_set_msca_dma_stride(u32, u32, u32, u32);
u64 system_reg_write(u32 a0, u32 a1, u32 a2, u32 a3);

#define VC(f) t32o_call((t32o_fn)(void (*)(void))f, a[0], a[1], a[2], a[3], 0)

static u32 rs = 0x2f1a9c3u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

static u32 rlog[64], rn, salt;

static u32 rdv(u32 reg)
{
	u32 v = (reg ^ salt) * 2654435761u;

	if (rn < 64)
		rlog[rn] = reg;
	rn++;
	return v ^ (v >> 15);
}

u64 tisp_reg_write(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a3;
	if (t32_data_d03a0[0] != 1)
		return system_reg_write(a1, a2, 0, 0);
	if (t32o_log.n < T32O_MAX_WRITES) {
		t32o_log.w[t32o_log.n].reg = T32_MS_SHADOW_IDX(a0, a1);
		t32o_log.w[t32o_log.n].val = a2;
		t32o_log.n++;
	}
	return 0;
}

u64 tisp_reg_read(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	return rdv(t32_data_d03a0[0] != 1 ? a1 : T32_MS_SHADOW_IDX(a0, a1));
}

#define NF 16
static const char *const names[NF] = {
	"ram_write", "clear_byindex", "clear_reg_byindex", "clear_all", "fifo_write",
	"fifo_read", "fifo_state", "work_state", "ir_init", "ir_fifo_read", "ir_fifo_write",
	"get_shdram_flag", "chx_reg_stream_off", "get_chx_en", "dma_stride", "set_shdram_flag"
};

struct res {
	unsigned int n, rn;
	struct t32_front_write w[32];
	u32 rl[64];
	u32 ret, o1, o2;
};

static unsigned int compared;
static unsigned int seen[NF][2];

static int run(int which, u32 mode, const u32 a[4], int ref, struct res *r)
{
	struct t32_ms_io io;
	u32 o1 = 0xa5a5a5a5, o2 = 0x5a5a5a5a, ret = 0;
	if (which == 15) {
		o1 = a[2] & 0xffff;
		o2 = a[2] >> 16;
	}
	unsigned int i;

	rn = 0;
	if (ref) {
		t32o_reset();
		t32_data_d03a0[0] = (u8)mode;
		t32o_rd_hook = rdv;
		switch (which) {
		case 0: ret = (u32)VC(tisp_msca_ram_write); break;
		case 1: ret = (u32)VC(tisp_msca_addr_fifo_clear_byindex); break;
		case 2: ret = (u32)VC(tisp_msca_addr_fifo_clear_reg_byindex); break;
		case 3: ret = (u32)VC(tisp_msca_addr_fifo_clear_all); break;
		case 4: ret = (u32)VC(tisp_msca_addr_fifo_write); break;
		case 5: {
			u32 *p1 = t32o_alloc(4), *p2 = t32o_alloc(4);

			*p1 = o1; *p2 = o2;
			ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_msca_addr_fifo_read, a[0], a[1],
					     T32O_PTR(p1), T32O_PTR(p2), 0);
			o1 = *p1; o2 = *p2;
			break;
		}
		case 6: ret = (u32)VC(tisp_msca_addr_fifo_state); break;
		case 7: ret = (u32)VC(tisp_msca_work_state); break;
		case 8: ret = (u32)VC(tisp_msca_ir_init); break;
		case 9: {
			u32 *p1 = t32o_alloc(4);

			*p1 = o1;
			ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_msca_addr_ir_fifo_read, T32O_PTR(p1), 0, 0, 0, 0);
			o1 = *p1;
			break;
		}
		case 10: ret = (u32)VC(tisp_msca_addr_ir_fifo_write); break;
		case 11: ret = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_msca_get_shdram_flag, 0, 0, 0, 0, 0); break;
		case 12: ret = (u32)VC(lib_tisp_msca_chx_reg_stream_off); break;
		case 13: ret = (u32)VC(lib_tisp_get_msca_chx_en); break;
		case 14: ret = (u32)VC(lib_tisp_api_set_msca_dma_stride); break;
		default: {
			u16 *cp = t32o_alloc(4);
			u8 *blk = t32o_alloc(320);
			u32 i2;

			*cp = (u16)o1;
			memcpy(blk + 302, &(u16){ (u16)o2 }, 2);
			for (i2 = 0; i2 < 4; i2++)
				t32_data_e28ac[i2] = (a[3] & (1u << i2)) ? 0 : T32O_PTR(blk);
			t32o_call((t32o_fn)(void (*)(void))tisp_msca_set_shdram_flag, a[0], a[1],
				  T32O_PTR(cp), 0, 0);
			ret = t32o_log.printfs;
			o1 = *cp;
			break;
		}
		}
		r->n = t32o_log.n;
		if (r->n > 32) {
			fprintf(stderr, "too many writes\n");
			exit(2);
		}
		for (i = 0; i < r->n; i++) {
			r->w[i].reg = t32o_log.w[i].reg;
			r->w[i].val = t32o_log.w[i].val;
		}
	} else {
		static struct t32_front_write w[32];

		io.shadow = (u8)mode;
		io.w = w;
		io.n = 0;
		io.rd = rdv;
		switch (which) {
		case 0: t32_msca_ram_write(&io, a[0], a[1]); break;
		case 1: t32_msca_addr_fifo_clear_byindex(&io, a[0]); break;
		case 2: t32_msca_addr_fifo_clear_reg_byindex(&io, a[0]); break;
		case 3: t32_msca_addr_fifo_clear_all(&io); break;
		case 4: t32_msca_addr_fifo_write(&io, a[0], a[1], a[2], a[3]); break;
		case 5: t32_msca_addr_fifo_read(&io, a[0], a[1], &o1, &o2); break;
		case 6: ret = t32_msca_addr_fifo_state(&io, a[1]); break;
		case 7: ret = t32_msca_work_state(&io, a[1]); break;
		case 8: t32_msca_ir_init(&io, a[0], a[1], a[2]); break;
		case 9: o1 = ret = t32_msca_addr_ir_fifo_read(&io); break;
		case 10: t32_msca_addr_ir_fifo_write(&io, a[0]); break;
		case 11: ret = t32_msca_get_shdram_flag(&io); break;
		case 12: t32_msca_chx_reg_stream_off(&io, a[0]); break;
		case 13: ret = t32_msca_get_chx_en(&io, a[0], a[1]); break;
		case 14: t32_msca_set_dma_stride(&io, a[0], a[1], a[2], a[3]); break;
		default: {
			u16 w = (u16)o2, cv = (u16)o1;

			ret = t32_msca_set_shdram_flag(&io, a[0], a[1], !(a[3] & (1u << a[0])) ? &w : NULL, &cv);
			o1 = cv;
			break;
		}
		}
		r->n = io.n;
		memcpy(r->w, w, io.n * sizeof(w[0]));
	}
	r->rn = rn;
	memcpy(r->rl, rlog, sizeof(rlog));
	r->ret = ret;
	r->o1 = o1;
	r->o2 = o2;
	return 0;
}

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000;
	unsigned int c, i, k;

	for (c = 0; c < n; c++) {
		for (k = 0; k < NF; k++) {
			u32 a[4] = { rnd(), rnd(), rnd(), rnd() }, mode = rnd() & 1;
			struct res r, o;

			if (rnd() & 1)
				a[1] = (a[1] & ~0xffu) | (rnd() % 5);
			if (rnd() & 1)
				a[0] = (a[0] & ~0xffu) | (rnd() % 5);
			if (k == 13 && (rnd() & 1))
				a[1] &= 0xff;
			if (k == 15) {
				a[0] = rnd() % 6;
				a[1] = rnd() % 4;
			}
			salt = rnd();
			run(k, mode, a, 1, &r);
			run(k, mode, a, 0, &o);
			compared++;
			seen[k][mode]++;
			if (r.n != o.n || r.rn != o.rn || r.o1 != o.o1 || r.o2 != o.o2 ||
			    memcmp(r.rl, o.rl, sizeof(u32) * (r.rn < 64 ? r.rn : 64))) {
				printf("MISMATCH %s mode %u: writes %u/%u reads %u/%u out %x,%x / %x,%x\n", names[k], mode,
				       r.n, o.n, r.rn, o.rn, r.o1, r.o2, o.o1, o.o2);
				return 1;
			}
			for (i = 0; i < r.n; i++)
				if (r.w[i].reg != o.w[i].reg || r.w[i].val != o.w[i].val) {
					printf("MISMATCH %s mode %u: write #%u vendor %08x=%08x ours %08x=%08x\n", names[k],
					       mode, i, r.w[i].reg, r.w[i].val, o.w[i].reg, o.w[i].val);
					return 1;
				}
			if ((k == 6 || k == 7 || k == 9 || k == 11 || k == 13 || k == 15) && r.ret != o.ret) {
				printf("MISMATCH %s mode %u: return %x / %x\n", names[k], mode, r.ret, o.ret);
				return 1;
			}
		}
	}
	for (k = 0; k < NF; k++)
		if (!seen[k][0] || !seen[k][1]) {
			printf("t32_mscad_oracle: %s never ran in both modes\n", names[k]);
			return 1;
		}
	printf("t32_mscad_oracle: %u compared, 0 mismatches\n", compared);
	return 0;
}
