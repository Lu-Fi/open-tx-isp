// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: core-control attribute wrappers
 * (driver/t32/tx_isp_t32_txattr.h) against the vendor tx_isp_gamma_s_attr,
 * _module_ratio_s_attr, _af_weight_s_attr, _statis_config_s_attr,
 * _gamma_g_attr, _autozoom_g_attr, _af_weight_g_attr, _statis_config_g_attr,
 * _module_ratio_g_attr (.isra clones; Matt Davis' recovered C, PR #15 @
 * 2e2e06bc, symbols renamed op3_* by op3_ren.sh).
 *
 * Both sides get the same random user block, scripted copy_from_user /
 * copy_to_user / tisp results and the same bytes from the tisp getter.
 * Compared: the event log (copy calls with user address and size, tisp
 * calls with the channel and an FNV hash of the kernel buffer, prints), the
 * user memory and the return value.
 * Exit 0 = all equal, 1 = mismatch.
 */
#include "t32_op3_glue.h"
#include "tx_isp_t32_txattr.h"

#define NCASE	20000
#define USERB	512u

char op3_t32_data_c4ad4[] = "[ %s:%d ] copy error!!!\n";		/* data.c:7898 */
char op3_t32_data_c4af0[] = "[ %s:%d ] set gamma failed!!!\n";		/* data.c:7905 */
char op3_t32_data_c4b10[] = "[ %s:%d ] set failed!!!\n";		/* data.c:7913 */
char op3_t32_data_c4fcc[] = "[ %s:%d ] get gamma failed!!!\n";		/* data.c:8216 */
char op3_t32_data_c4b2c[] = "[ %s:%d ] get control failed!!!\n";	/* data.c:7920 */
char op3_t32_data_c4fec[] = "[ %s:%d ] get failed!!!\n";		/* data.c:8224 */
char op3_t32_data_be238[] = "tx_isp_gamma_s_attr";			/* data.c:1201 */
char op3_t32_data_be21c[] = "tx_isp_module_ratio_s_attr";		/* data.c:1194 */
char op3_t32_data_be170[] = "tx_isp_af_weight_s_attr";			/* data.c:1151 */
char op3_t32_data_be0d4[] = "tx_isp_statis_config_s_attr";		/* data.c:1112 */
char op3_t32_data_bdf1c[] = "tx_isp_gamma_g_attr";			/* data.c:1002 */
char op3_t32_data_bdeec[] = "tx_isp_autozoom_g_attr";			/* data.c:990 */
char op3_t32_data_bde40[] = "tx_isp_af_weight_g_attr";			/* data.c:947 */
char op3_t32_data_bdd58[] = "tx_isp_statis_config_g_attr";		/* data.c:889 */
char op3_t32_data_bdd3c[] = "tx_isp_module_ratio_g_attr";		/* data.c:882 */

u64 tx_isp_gamma_s_attr_isra_7(u32, u32, u32, u32);
u64 tx_isp_module_ratio_s_attr_isra_8(u32, u32, u32, u32);
u64 tx_isp_af_weight_s_attr_isra_14(u32, u32, u32, u32);
u64 tx_isp_statis_config_s_attr_isra_22(u32, u32, u32, u32);
u64 tx_isp_gamma_g_attr_isra_41(u32, u32, u32, u32);
u64 tx_isp_autozoom_g_attr_isra_43(u32, u32, u32, u32);
u64 tx_isp_af_weight_g_attr_isra_49(u32, u32, u32, u32);
u64 tx_isp_statis_config_g_attr_isra_56(u32, u32, u32, u32);
u64 tx_isp_module_ratio_g_attr_isra_57(u32, u32, u32, u32);

static u32 cfu_ret, ctu_ret, tisp_ret, fill_seed;

static u32 fnv(const u8 *p, u32 n)
{
	u32 h = 2166136261u, i;

	for (i = 0; i < n; i++)
		h = (h ^ p[i]) * 16777619u;
	return h;
}

static u32 cfu_rec(void *dst, u32 user, u32 n)
{
	op3_call("private_copy_from_user", 2, user, n, 0, 0);
	memcpy(dst, T32O_HOST(user), n);
	return cfu_ret;
}

static u32 ctu_rec(u32 user, const void *src, u32 n)
{
	op3_call("private_copy_to_user", 3, user, n, fnv(src, n), 0);
	memcpy(T32O_HOST(user), src, n);
	return ctu_ret;
}

static int set_rec(const char *name, u32 n, u32 ch, void *buf)
{
	op3_call(name, 2, ch, fnv(buf, n), 0, 0);
	return (int)tisp_ret;
}

static int get_rec(const char *name, u32 n, u32 ch, void *buf)
{
	u32 s = fill_seed, i;

	op3_call(name, 1, ch, 0, 0, 0);
	for (i = 0; i < n; i++) {
		s = s * 1103515245u + 12345u;
		((u8 *)buf)[i] = (u8)(s >> 16);
	}
	return (int)tisp_ret;
}

u64 op3_private_copy_from_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return cfu_rec(T32O_HOST(a0), a1, a2); }
u64 op3_private_copy_to_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return ctu_rec(a0, T32O_HOST(a1), a2); }
u32 t32_tx_os_copy_from_user(void *dst, u32 user, u32 n) { return cfu_rec(dst, user, n); }
u32 t32_tx_os_copy_to_user(u32 user, const void *src, u32 n) { return ctu_rec(user, src, n); }

#define OP3_TISP(vname, ours, n, rec) \
	u64 op3_##vname(u32 a0, u32 a1, u32 a2, u32 a3); \
	u64 op3_##vname(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; return (u32)rec(#vname, n, a0, T32O_HOST(a1)); } \
	int ours(u32 ch, void *buf) { return rec(#vname, n, ch, buf); }
OP3_TISP(tisp_s_Gamma, t32_tx_os_tisp_s_gamma, T32_ATTR_GAMMA, set_rec)
OP3_TISP(tisp_g_Gamma, t32_tx_os_tisp_g_gamma, T32_ATTR_GAMMA, get_rec)
OP3_TISP(tisp_s_module_ratio_attr, t32_tx_os_tisp_s_module_ratio, T32_ATTR_MODULE_RATIO, set_rec)
OP3_TISP(tisp_g_module_ratio_attr, t32_tx_os_tisp_g_module_ratio, T32_ATTR_MODULE_RATIO, get_rec)
OP3_TISP(tisp_s_af_weight_attr, t32_tx_os_tisp_s_af_weight, T32_ATTR_AF_WEIGHT, set_rec)
OP3_TISP(tisp_g_af_weight_attr, t32_tx_os_tisp_g_af_weight, T32_ATTR_AF_WEIGHT, get_rec)
OP3_TISP(tisp_s_statis_config_attr, t32_tx_os_tisp_s_statis_config, T32_ATTR_STATIS, set_rec)
OP3_TISP(tisp_g_statis_config_attr, t32_tx_os_tisp_g_statis_config, T32_ATTR_STATIS, get_rec)
OP3_TISP(tisp_g_autozoom_control, t32_tx_os_tisp_g_autozoom, T32_ATTR_AUTOZOOM, get_rec)

enum { F_GS, F_MRS, F_AFS, F_STS, F_GG, F_AZG, F_AFG, F_STG, F_MRG, F_N };
static const char *const fname[F_N] = {
	"gamma_s", "module_ratio_s", "af_weight_s", "statis_config_s", "gamma_g", "autozoom_g",
	"af_weight_g", "statis_config_g", "module_ratio_g"
};
typedef u64 (*vfn)(u32, u32, u32, u32);
static const vfn vend[F_N] = {
	tx_isp_gamma_s_attr_isra_7, tx_isp_module_ratio_s_attr_isra_8, tx_isp_af_weight_s_attr_isra_14,
	tx_isp_statis_config_s_attr_isra_22, tx_isp_gamma_g_attr_isra_41, tx_isp_autozoom_g_attr_isra_43,
	tx_isp_af_weight_g_attr_isra_49, tx_isp_statis_config_g_attr_isra_56, tx_isp_module_ratio_g_attr_isra_57
};
static int (*const ours[F_N])(u32, u32) = {
	t32_tx_gamma_s_attr, t32_tx_module_ratio_s_attr, t32_tx_af_weight_s_attr, t32_tx_statis_config_s_attr,
	t32_tx_gamma_g_attr, t32_tx_autozoom_g_attr, t32_tx_af_weight_g_attr, t32_tx_statis_config_g_attr,
	t32_tx_module_ratio_g_attr
};

static u8 user0[USERB];
struct snap {
	struct op3_log log;
	u8 user[USERB];
	u32 ret;
};
static struct snap sv, so;

static u32 setup(u8 **user)
{
	u32 *arg;

	t32o_reset();
	memset(&op3_log, 0, sizeof(op3_log));
	*user = t32o_alloc(USERB);
	memcpy(*user, user0, USERB);
	arg = t32o_alloc(16);
	arg[0] = T32O_PTR(*user + 8);
	return T32O_PTR(arg);
}

int main(void)
{
	unsigned int i, k;
	u8 *user;

	for (i = 0; i < NCASE; i++) {
		int f = (int)(i % F_N);
		u32 m = op3_rnd(), ch = (m & 0x10) ? op3_rnd() : m >> 29, arg, ua;

		for (k = 0; k < USERB; k++)
			user0[k] = (u8)op3_rnd();
		cfu_ret = (m & 3) ? 0 : op3_rnd() % 300 + 1;
		ctu_ret = (m & 0xc) ? 0 : op3_rnd() % 300 + 1;
		tisp_ret = (m & 0x60) ? 0 : ((m & 0x80) ? (u32)-1 : op3_rnd());
		fill_seed = op3_rnd();

		arg = setup(&user);
		ua = f <= F_STS ? T32O_PTR(user + 4) : arg;
		sv.ret = (u32)t32o_call((t32o_fn)(void (*)(void))vend[f], ch, ua, 0, 0, 0);
		sv.log = op3_log;
		memcpy(sv.user, user, USERB);

		arg = setup(&user);
		ua = f <= F_STS ? T32O_PTR(user + 4) : arg;
		so.ret = (u32)ours[f](ch, ua);
		so.log = op3_log;
		memcpy(so.user, user, USERB);

		if (op3_log_cmp(fname[f], &sv.log, &so.log))
			return 1;
		if (sv.ret != so.ret || memcmp(sv.user, so.user, USERB)) {
			fprintf(stderr, "%s: ret %x/%x or user memory differs\n", fname[f], sv.ret, so.ret);
			return 1;
		}
	}
	printf("t32_txattr_oracle: %u cases equal (9 functions)\n", NCASE);
	return 0;
}
