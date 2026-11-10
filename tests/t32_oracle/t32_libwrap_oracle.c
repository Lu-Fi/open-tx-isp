// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: our libisp forwarders, readers and empty entry points (driver/t32/tx_isp_t32_libwrap.h)
 * against the vendor functions from Matt Davis' recovery (ref/, PR #15 @ 2e2e06bc, normalized C).
 *
 * Compared bit-exact: return value, which callee runs (call count per callee) and its arguments, the register
 * read address.  Inputs: random channels (all 32 bits), arguments, callee return values, channel state bytes.
 * lce_self_light_correct_global: the normalized reference forwards a0..a3 only; the fifth argument is the
 * caller's stack word, which the vendor tail jump keeps for lce_self_light_correct_local (sp + 16).  Our port
 * passes it explicitly; the oracle compares a0..a3 (the fifth is not observable through the reference).
 *
 * Exit 0 = all equal; 1 = mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_libwrap.h"

typedef u32 (*r0_fn)(void);
#define DECL(n)	int n()
DECL(lib_tisp_remove); DECL(lib_tisp_func_stop_reset); DECL(lib_tisp_fw_process); DECL(lib_tisp_fw_process_empty);
DECL(lib_tisp_api_get_csccr_output); DECL(tisp_api_ev_alloc_calc); DECL(lib_tisp_get_ipc_output_pos);
DECL(lce_self_light_correct_global); DECL(ldc_cmd_set); DECL(ldc_cmd_show); DECL(ldc_slake_module);
DECL(ldc_activate_module); DECL(tisp_msca_state); DECL(tisp_ae_ir_update); DECL(tisp_long_again_update);
DECL(tisp_lce_lut_write_curve); DECL(tisp_dma_param_array_get); DECL(tisp_dma_param_array_set);
DECL(tisp_func_get_dma_addr);
unsigned int lib_tisp_api_g_runningMode(void);

#define F(fn)	((t32o_fn)(void (*)(void))(fn))

static u32 rs = 0x6d2b79f5u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

/* call record: callee id, arguments */
struct rec { unsigned int id, n; u32 a[4]; };
static struct rec vr, orr;
static u32 ret_val;

static void put(struct rec *r, unsigned int id, unsigned int n, u32 a0, u32 a1, u32 a2, u32 a3)
{
	r->id = id + 10 * r->n++;
	r->a[0] = a0; r->a[1] = a1; r->a[2] = a2; r->a[3] = a3;
	(void)n;
}

/* vendor callees */
#define VSTUB(name, id)	u64 name(u32 a0, u32 a1, u32 a2, u32 a3) { put(&vr, id, 1, a0, a1, a2, a3); return ret_val; }
VSTUB(tisp_lce_remove, 1)
VSTUB(tisp_func_stop_reset, 2)
VSTUB(tisp_event_process, 3)
VSTUB(tisp_event_work_empty, 4)
VSTUB(lib_tisp_csccr_dn_status, 5)
VSTUB(tisp_ae_api_ev_alloc_calc, 6)
VSTUB(lce_self_light_correct_local, 7)

static u32 rd(u32 reg)
{
	put(&vr, 8, 1, reg, 0, 0, 0);
	return ret_val;
}

/* our hooks */
static u32 h0(void *c, unsigned int id) { (void)c; put(&orr, id, 0, 0, 0, 0, 0); return ret_val; }
static u32 o_lr(void *c) { return h0(c, 1); }
static u32 o_fsr(void *c) { return h0(c, 2); }
#define OSTUB1(name, id) static u32 name(void *c, u32 ch) { (void)c; put(&orr, id, 1, ch, 0, 0, 0); return ret_val; }
OSTUB1(o_ep, 3)
OSTUB1(o_ewe, 4)
OSTUB1(o_dn, 5)
OSTUB1(o_ev, 6)
static u32 o_loc(void *c, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4)
{
	(void)c; (void)a4;
	put(&orr, 7, 4, a0, a1, a2, a3);
	return ret_val;
}
static u32 o_rd(void *c, u32 reg) { (void)c; put(&orr, 8, 1, reg, 0, 0, 0); return ret_val; }

static unsigned int compared, failed;

static void cmp(const char *what, u32 v, u32 o, unsigned int nargs)
{
	unsigned int i, bad = v != o || vr.n != orr.n || vr.id != orr.id;

	for (i = 0; !bad && vr.n && i < nargs; i++)
		bad = vr.a[i] != orr.a[i];
	compared++;
	if (bad && failed++ < 10)
		printf("MISMATCH %s: ret %x/%x calls %u/%u id %u/%u a0 %x/%x\n", what, v, o, vr.n, orr.n,
		       vr.id, orr.id, vr.a[0], orr.a[0]);
	memset(&vr, 0, sizeof(vr));
	memset(&orr, 0, sizeof(orr));
}

#define CALL(fn, ...)	((u32)t32o_call(F(fn), __VA_ARGS__))

int main(void)
{
	unsigned int c, i, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 20000;
	struct t32_lib_hooks h = { 0, o_lr, o_fsr, o_ep, o_ewe, o_dn, o_ev, o_loc, o_rd };
	static const struct { const char *n; int (*f)(); } empty[] = {
		{ "ldc_cmd_show", ldc_cmd_show }, { "ldc_slake_module", ldc_slake_module },
		{ "ldc_activate_module", ldc_activate_module }, { "tisp_msca_state", tisp_msca_state },
		{ "tisp_ae_ir_update", tisp_ae_ir_update }, { "tisp_long_again_update", tisp_long_again_update },
		{ "tisp_lce_lut_write_curve", tisp_lce_lut_write_curve },
		{ "tisp_dma_param_array_get", tisp_dma_param_array_get },
		{ "tisp_dma_param_array_set", tisp_dma_param_array_set },
		{ "tisp_func_get_dma_addr", tisp_func_get_dma_addr },
	};

	t32o_rd_hook = rd;
	for (c = 0; c < n; c++) {
		u32 a[5], ch = c % 4 ? rnd() : rnd() & 0xff;

		for (i = 0; i < 5; i++)
			a[i] = rnd();
		t32o_reset();
		ret_val = rnd();
		for (i = 0; i < sizeof(t32_data_dc284); i++)
			t32_data_dc284[i] = (u8)rnd();
		memset(&vr, 0, sizeof(vr));
		memset(&orr, 0, sizeof(orr));

		cmp("lib_tisp_remove", CALL(lib_tisp_remove, a[0], a[1], a[2], a[3], 0), t32_lib_remove(&h), 0);
		cmp("lib_tisp_func_stop_reset", CALL(lib_tisp_func_stop_reset, a[0], a[1], a[2], a[3], 0),
		    t32_lib_func_stop_reset(&h), 0);
		cmp("lib_tisp_fw_process", CALL(lib_tisp_fw_process, ch, a[1], 0, 0, 0), t32_lib_fw_process(&h, ch), 1);
		cmp("lib_tisp_fw_process_empty", CALL(lib_tisp_fw_process_empty, ch, a[1], 0, 0, 0),
		    t32_lib_fw_process_empty(&h, ch), 1);
		cmp("lib_tisp_api_get_csccr_output", CALL(lib_tisp_api_get_csccr_output, ch, 0, 0, 0, 0),
		    t32_lib_get_csccr_output(&h, ch), 1);
		cmp("tisp_api_ev_alloc_calc", CALL(tisp_api_ev_alloc_calc, ch, a[1], a[2], a[3], 0),
		    t32_api_ev_alloc_calc(&h, ch), 1);
		cmp("lce_self_light_correct_global", CALL(lce_self_light_correct_global, a[0], a[1], a[2], a[3], a[4]),
		    t32_lce_self_light_correct_global(&h, a[0], a[1], a[2], a[3], a[4]), 4);
		cmp("lib_tisp_get_ipc_output_pos", CALL(lib_tisp_get_ipc_output_pos, 0, 0, 0, 0, 0),
		    t32_lib_get_ipc_output_pos(&h), 1);
		cmp("lib_tisp_api_g_runningMode", CALL(lib_tisp_api_g_runningMode, 0, 0, 0, 0, 0),
		    t32_lib_get_running_mode(t32_data_dc284), 0);
		cmp("ldc_cmd_set", CALL(ldc_cmd_set, a[0], a[1], a[2], 0, 0), t32_ldc_cmd_set(a[0], a[1], a[2]), 0);
		for (i = 0; i < sizeof(empty) / sizeof(empty[0]); i++)
			cmp(empty[i].n, CALL(empty[i].f, a[0], a[1], a[2], a[3], 0), t32_lib_empty(), 0);
	}
	printf("t32_libwrap_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
