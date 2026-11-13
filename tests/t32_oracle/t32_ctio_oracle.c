// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: tisp_code_tuning_ioctl (tx_isp_t32_ctune.h) against the vendor
 * function (Matt Davis' recovered C, PR #15 @ 2e2e06bc; build-time fix
 * fix_op11_ctio.sh: gp = fake thread_info, __copy_user's a2 result).  The vendor jump
 * tables t32_data_bef30 / bef54 / bf008 are defined here with their data.c values
 * (lines 2031 .. 2128) and the two version strings with their rodata bytes.
 * Per case: command (the nine valid ones, foreign magic, _IOC_NR >= 51, other numbers),
 * user buffer (random block id 0 .. 47 or any, channel, sub id, data), addr_limit
 * (passing or hitting the buffer address), short user copies (bytes not copied).
 * Every block function / info function writes case bytes into its buffers.
 * Compared: every callee call in order (kind, block id, channel, sub id, buffer
 * offsets of data / length; user copies: destination, source, size), the kernel buffer
 * (canary filled) and the user buffer whole, the channel byte (t32_data_dada4), the
 * return value.  Exit 1 on the first mismatch.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "t32_oracle_env.h"
#include "tx_isp_t32_ctune.h"

/* data.c:2031 .. 2128: code addresses of the vendor's three switches */
u32 t32_data_bef30[9] = { 0x1f1ac, 0x1f1e8, 0x1f874, 0x1f8c4, 0x1f928, 0x1f938, 0x1fa54, 0x1f9a0, 0x1f9ec };
u32 t32_data_bef54[45] = { 0x1f270, 0x1f570, 0x1f284, 0x1f2ac, 0x1f298, 0x1f2c0, 0x1f2d4, 0x1f570, 0x1f2e8, 0x1f410,
	0x1f2fc, 0x1f310, 0x1f324, 0x1f338, 0x1f35c, 0x1f370, 0x1f384, 0x1f3fc, 0x1f398, 0x1f3ac, 0x1f3d4, 0x1f3e8,
	0x1f424, 0x1f3c0, 0x1f438, 0x1f458, 0x1f570, 0x1f46c, 0x1f570, 0x1f570, 0x1f570, 0x1f570, 0x1f570, 0x1f570,
	0x1f570, 0x1f490, 0x1f49c, 0x1f598, 0x1f4e4, 0x1f4bc, 0x1f4d0, 0x1f4f8, 0x1f51c, 0x1f530, 0x1f550 };
u32 t32_data_bf008[44] = { 0x1f600, 0x1f844, 0x1f610, 0x1f634, 0x1f624, 0x1f644, 0x1f654, 0x1f844, 0x1f664, 0x1f744,
	0x1f674, 0x1f684, 0x1f694, 0x1f6a4, 0x1f6c4, 0x1f6d4, 0x1f6e4, 0x1f734, 0x1f6f4, 0x1f704, 0x1f724, 0x1f754,
	0x1f764, 0x1f714, 0x1f774, 0x1f78c, 0x1f844, 0x1f79c, 0x1f844, 0x1f844, 0x1f844, 0x1f844, 0x1f844, 0x1f844,
	0x1f844, 0x1f7b4, 0x1fae4, 0x1fae4, 0x1f7ec, 0x1f7cc, 0x1f7dc, 0x1f7fc, 0x1f818, 0x1f828 };
/* rodata.str1.4 + 0x4598 / 0x45a4 (data.c:6633 ..) */
char t32_data_c7ca8[12] = "V-20241122", t32_data_c7cb4[16] = "V-ISP-20241122";
/* printk formats, only handed to isp_printf */
u8 t32_data_c7c08[4], t32_data_c7c28[4], t32_data_c7c48[4], t32_data_bf0fc[4], t32_data_c7c70[4], t32_data_c7c90[4],
	t32_data_bf0e4[4], t32_data_bf114[4], t32_data_c7cc4[4];
u32 t32_data_dada8[1];
u8 t32_data_dada4[4];
u8 t32o_cti_ti[64] __attribute__((aligned(16)));
extern u64 tisp_code_tuning_ioctl(u32, u32, u32, u32);

static u32 rs = 0x27d4eb2fu;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

#define BUF T32_CTUNE_BUF_SIZE
#define LOGN 6
struct ent { u32 k, id, ch, sub, p, q, n; };
struct hlog { unsigned int n, ncopy; struct ent e[LOGN]; };
static struct hlog vl, ol, *cur;
static u8 *kb, *ub, *ubase;		/* one kernel / user buffer: both sides run on the same addresses */
static u32 seed, fail[4];
static u32 tok(const void *p)
{
	const u8 *b = p;

	if (b >= kb && b < kb + BUF + 64) return 0x4b000000u | (u32)(b - kb);
	if (b >= ub && b < ub + BUF + 64) return 0x55000000u | (u32)(b - ub);
	return 0xee000000u;
}
static void rec(u32 k, u32 id, u32 ch, u32 sub, u32 p, u32 q, u32 n)
{
	if (cur->n < LOGN) { struct ent e = { k, id, ch, sub, p, q, n }; cur->e[cur->n] = e; }
	cur->n++;
}
static void scribble(u8 *p, u32 n, u32 k)
{
	u32 i, x = seed ^ k;

	for (i = 0; i < n; i++) { x = x * 1103515245u + 12345u; p[i] = x >> 16; }
}
static u32 do_copy(u8 *dst, const u8 *src, u32 n)
{
	u32 r = cur->ncopy < 4 ? fail[cur->ncopy] : 0;

	cur->ncopy++;
	if (r > n) r = n;
	rec(1, 0, 0, 0, tok(dst), tok(src), n);
	memcpy(dst, src, n - r);
	return r;
}
#define VP(a) ((u8 *)(unsigned long)(a))
u64 __copy_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return (u64)do_copy(VP(a0), VP(a1), a2) << 32; }
static void g_common(u32 id, u32 ch, u32 sub, u8 *d, u8 *len)
{
	rec(2, id, ch, sub, tok(d), tok(len), 0);
	scribble(d + 2, 6, id);
	scribble(len, 4, id + 100);
}
static void s_common(u32 id, u32 ch, u32 sub, u8 *d)
{
	rec(3, id, ch, sub, tok(d), 0, 0);
	scribble(d + 1, 3, id + 200);
}
#define GSTD(n, id) u64 tisp_##n##_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; g_common(id, a0, 0, VP(a1), VP(a2)); return rnd(); }
#define SSTD(n, id) u64 tisp_##n##_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; s_common(id, a0, 0, VP(a1)); return rnd(); }
#define BOTH(n, id) GSTD(n, id) SSTD(n, id)
BOTH(top, 1) BOTH(wdr, 4) BOTH(tmo, 5) BOTH(dpc, 6) BOTH(gib, 7) BOTH(dmsc, 9) BOTH(ccm, 10) BOTH(gamma, 11)
BOTH(defog, 12) BOTH(lce, 13) BOTH(ydns, 15) BOTH(cdns, 16) BOTH(bcsh, 17) BOTH(clm, 18) BOTH(ysp, 19)
BOTH(sdns, 20) BOTH(ldc, 21) BOTH(ae, 22) BOTH(awb, 23) BOTH(af, 24) BOTH(msca, 26) BOTH(rdns, 39) BOTH(csc, 40)
BOTH(csccr, 41) BOTH(dma, 43) BOTH(decmp, 44)
GSTD(lsc, 3) GSTD(comn, 37) GSTD(version, 45)
u64 tisp_lsc_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; s_common(3, a0, a1, VP(a2)); return rnd(); }
u64 tisp_mdns_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3) { g_common(14, a0, a1, VP(a2), VP(a3)); return rnd(); }
u64 tisp_mdns_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; s_common(14, a0, a1, VP(a2)); return rnd(); }
u64 tisp_tstp_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; g_common(25, 0, 0, VP(a0), VP(a1)); return rnd(); }
u64 tisp_tstp_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; s_common(25, 0, 0, VP(a0)); return rnd(); }
u64 tisp_reg_map_get(u32 a0, u32 a1, u32 a2, u32 a3) { g_common(28, a0, a1, VP(a2), VP(a3)); return rnd(); }
u64 tisp_reg_map_set(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; s_common(28, a0, 0, VP(a1)); return rnd(); }
u64 tisp_api_param_array_get(u32 a0, u32 a1, u32 a2, u32 a3) { g_common(42, a0, a3, VP(a1), VP(a2)); return rnd(); }
u64 tisp_api_param_array_set(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; s_common(42, a0, a2, VP(a1)); return rnd(); }
static void i_common(u32 w, u8 *b) { rec(4, w, 0, 0, tok(b), 0, 0); scribble(b + 4, 8, w + 300); }
u64 tisp_set_ae_info(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; i_common(T32_CTIO_I_SET_AE, VP(a0)); return rnd(); }
u64 tisp_set_ae_minmax(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; i_common(T32_CTIO_I_SET_AE_MINMAX, VP(a0)); return rnd(); }
u64 tisp_get_awb_info(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; i_common(T32_CTIO_I_GET_AWB, VP(a0)); return rnd(); }
u64 tisp_set_awb_info(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; i_common(T32_CTIO_I_SET_AWB, VP(a0)); return rnd(); }

/* port hooks */
static u32 g_limit;
static u32 o_limit(void) { return g_limit; }
static u32 o_from(void *dst, u32 u, u32 n) { return do_copy(dst, VP(u), n); }
static u32 o_to(u32 u, const void *src, u32 n) { return do_copy(VP(u), src, n); }
static void o_get(u32 id, u32 ch, u32 sub, u8 *d, u8 *len) { g_common(id, ch, sub, d, len); }
static void o_set(u32 id, u32 ch, u32 sub, u8 *d) { s_common(id, ch, sub, d); }
static void o_info(enum t32_ctio_info w, u8 *b) { i_common(w, b); }
static const struct t32_ctio_ops ops = { o_limit, o_from, o_to, o_get, o_set, o_info };

static void fill(u8 *p, u32 n) { u32 i; for (i = 0; i < n; i++) p[i] = rnd(); }

int main(void)
{
	unsigned long n, N = 30000, per[12] = { 0 }, ncall = 0;
	u32 i;
	u8 *uinit = malloc(BUF + 64), *kinit = malloc(BUF + 64), *ko = malloc(BUF + 64), *uo = malloc(BUF + 64);

	kb = t32o_alloc(BUF + 64);
	ubase = t32o_alloc(BUF + 64 + 0x4000);
	for (n = 0; n < N; n++) {
		struct t32_ctune t;
		u32 r = rnd(), nr = r % 10 ? rnd() % 9 : rnd() % 60, cmd = T32_CTIO_CMD0 + nr, id, rv, ro, uaddr;

		if (r % 31 == 0) cmd = (cmd & ~0xff00u) | (rnd() & 0xff00u);
		if (r % 37 == 0) cmd = rnd();
		seed = rnd();
		for (i = 0; i < 4; i++) fail[i] = rnd() % 8 ? 0 : (rnd() & 1 ? 1 + rnd() % 64 : BUF);
		fill(uinit, BUF + 64); fill(kinit, BUF + 64);
		id = rnd() % 8 ? rnd() % 48 : rnd();
		t32_ctio_put32(uinit + T32_CTIO_ID, id);
		ub = ubase + rnd() % 0x4000;	/* the address bits vary */
		uaddr = T32O_PTR(ub);
		g_limit = rnd() % 6 ? 0x80000000u : rnd() % 3 == 0 ? uaddr & -uaddr : rnd() & 1 ? 1u << (rnd() % 31) :
			  (uaddr ^ (uaddr + BUF)) & uaddr;	/* a bit of arg only */
		memcpy(t32o_cti_ti + 24, &g_limit, 4);

		memcpy(ub, uinit, BUF + 64); memcpy(kb, kinit, BUF + 64);
		memset(&ol, 0, sizeof(ol)); cur = &ol;
		memset(&t, 0, sizeof(t));
		t.buf = kb; t.mem_sel = 0x5a;
		ro = (u32)t32_code_tuning_ioctl(&t, &ops, cmd, uaddr);
		memcpy(ko, kb, BUF + 64); memcpy(uo, ub, BUF + 64);

		memcpy(ub, uinit, BUF + 64); memcpy(kb, kinit, BUF + 64);
		t32o_reset();
		memset(&vl, 0, sizeof(vl)); cur = &vl;
		t32_data_dada8[0] = T32O_PTR(kb); t32_data_dada4[0] = 0x5a;
		rv = (u32)t32o_call((t32o_fn)(void (*)(void))tisp_code_tuning_ioctl, rnd(), cmd, uaddr, rnd(), 0);

		if (rv != ro || vl.n != ol.n || memcmp(vl.e, ol.e, sizeof(vl.e)) || t32_data_dada4[0] != t.mem_sel ||
		    memcmp(kb, ko, BUF + 64) || memcmp(ub, uo, BUF + 64)) {
			printf("t32_ctio_oracle: FAIL case %lu cmd %#x id %u limit %#x: ret %d/%d calls %u/%u sel %#x/%#x\n", n, cmd,
			       id, g_limit, (int)rv, (int)ro, vl.n, ol.n, t32_data_dada4[0], t.mem_sel);
			for (i = 0; i < LOGN && i < vl.n; i++)
				if (memcmp(&vl.e[i], &ol.e[i], sizeof(vl.e[i])))
					printf("  call %u: k %u/%u id %u/%u ch %u/%u sub %#x/%#x p %#x/%#x q %#x/%#x n %u/%u\n", i,
					       vl.e[i].k, ol.e[i].k, vl.e[i].id, ol.e[i].id, vl.e[i].ch, ol.e[i].ch, vl.e[i].sub,
					       ol.e[i].sub, vl.e[i].p, ol.e[i].p, vl.e[i].q, ol.e[i].q, vl.e[i].n, ol.e[i].n);
			for (i = 0; i < BUF + 64; i++) if (kb[i] != ko[i]) { printf("  kbuf +%u: %#x / %#x\n", i, kb[i], ko[i]); break; }
			for (i = 0; i < BUF + 64; i++) if (ub[i] != uo[i]) { printf("  ubuf +%u: %#x / %#x\n", i, ub[i], uo[i]); break; }
			return 1;
		}
		per[nr < 9 && cmd == T32_CTIO_CMD0 + nr ? nr : 9]++;
		per[10] += (u32)ro == 0;
		ncall += ol.n;
	}
	printf("t32_ctio_oracle: PASS %lu cases (", N);
	for (i = 0; i < 10; i++) printf("%s%lu", i ? " " : "", per[i]);
	printf(" per command, last = foreign; %lu returned 0, %lu calls)\n", per[10], ncall);
	return 0;
}
