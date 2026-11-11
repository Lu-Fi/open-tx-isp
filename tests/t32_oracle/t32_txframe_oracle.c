// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: frame channel misc devices (driver/t32/
 * tx_isp_t32_txframe.h) against the vendor tx_isp_frame_chan_misc_register
 * (Matt Davis' recovered C, PR #15 @ 2e2e06bc, symbols renamed op3_* by
 * op3_ren.sh).  Inputs: channel index (-2..12 and random), d9008,
 * scripted misc_register result.  Compared: the event log (name printed),
 * the channel bytes, the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txframe.h"

#define NCASE	30000
#define FCB	816u

u32 op3_t32_data_d9008, t32_tx_g_d9008;
u8 op3_t32_data_cf5b4[64];
char op3_t32_data_c6648[] = "framechan%d";	/* data.c:9655.. */
char op3_t32_data_c6654[] = "framechan0";
char op3_t32_data_c6660[] = "framechan1";
char op3_t32_data_c666c[] = "framechan2";
char op3_t32_data_c6678[] = "framechan3";
char op3_t32_data_c6684[] = "framechan4";
char op3_t32_data_c6690[] = "framechan5";
char op3_t32_data_c669c[] = "framechan6";
char op3_t32_data_c66a8[] = "framechan7";
char op3_t32_data_c66b4[] = "framechan8";	/* data.c:9682 */
char op3_t32_data_c66c0[] = "Failed to register framechan%d!\n";	/* data.c:9685 */
char op3_t32_data_c66e4[] = " Create %s OK! \n";			/* data.c:9694 */
u64 tx_isp_frame_chan_misc_register(u32, u32, u32, u32);

static u32 misc_ret;
static void spf_rec(char *d, const char *f, u32 v)
{
	op3_call("sprintf", 1, v, 0, 0, 0);
	snprintf(d, 20, f, (int)v);
}
u64 op3_sprintf(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; spf_rec(T32O_HOST(a0), T32O_HOST(a1), a2); return 0; }
u64 op3_private_misc_register(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; op3_call("private_misc_register", 1, a0, 0, 0, 0); return misc_ret; }
void t32_tx_os_sprintf_u(char *d, const char *f, u32 v) { spf_rec(d, f, v); }
int t32_tx_os_misc_register(u32 m) { return (int)op3_private_misc_register(m, 0, 0, 0); }

static u64 ours(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) { (void)a1; (void)a2; (void)a3; (void)a4; return (u32)t32_tx_frame_chan_misc_register(a0, T32O_PTR(op3_t32_data_cf5b4)); }

int main(void)
{
	unsigned int i, k;
	static u8 fc0[FCB], wv[FCB];
	u8 *fc;
	u32 rv, ro, d9008;
	struct op3_log lv;

	for (i = 0; i < NCASE; i++) {
		u32 m = op3_rnd();

		for (k = 0; k < FCB; k++)
			fc0[k] = (u8)op3_rnd();
		*(u32 *)(fc0 + T32_FC_INDEX) = (m & 1) ? op3_rnd() : (u32)((int)(op3_rnd() % 15) - 2);
		d9008 = (m & 2) ? 0 : op3_rnd();
		misc_ret = (m & 0xc) ? op3_rnd() & 0xff : (u32)-(int)(op3_rnd() % 20 + 1);

		t32o_reset();
		fc = t32o_alloc(FCB);
		memcpy(fc, fc0, FCB);
		memset(&op3_log, 0, sizeof(op3_log));
		op3_t32_data_d9008 = d9008;
		rv = OP3_VC(tx_isp_frame_chan_misc_register, T32O_PTR(fc), 0, 0, 0, 0);
		lv = op3_log;
		memcpy(wv, fc, FCB);

		t32o_reset();
		fc = t32o_alloc(FCB);
		memcpy(fc, fc0, FCB);
		memset(&op3_log, 0, sizeof(op3_log));
		t32_tx_g_d9008 = d9008;
		ro = OP3_VC(ours, T32O_PTR(fc), 0, 0, 0, 0);
		if (op3_log_cmp("frame_chan_misc_register", &lv, &op3_log))
			return 1;
		if (rv != ro || memcmp(wv, fc, FCB)) {
			fprintf(stderr, "frame_chan_misc_register: ret %x/%x or channel differs\n", rv, ro);
			return 1;
		}
	}
	printf("t32_txframe_oracle: %u cases equal\n", NCASE);
	return 0;
}
