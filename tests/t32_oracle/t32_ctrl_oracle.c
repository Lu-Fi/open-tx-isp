// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: the control handlers of driver/t32/tx_isp_t32_ctrl.h
 * against Matt Davis' recovered vendor C (ref/, PR #15 @ 2e2e06bc;
 * tisp_write_proc built through fix_op8_wproc.sh): tisp_write_proc,
 * tisp_show_debug_info, tiziano_isp_ccm_attr_g_ctrl.isra.40,
 * mbus_to_bayer_write.
 *
 * Compared: the calls in order (copied bytes, the text handed to kstrtoint,
 * mutex, seq_printf format and value, the 40 attribute bytes copied to the
 * user, isp_printf incl. its stack word, set_bayer arguments), return
 * values, the help flag and level bytes.  Inputs: "help" variants, numbers
 * -3..9 and big, junk, counts 0..300 (truncation to 255), access masks with
 * user addresses on both sides of 0x80000000, partial copies, codes around
 * 0x5200..0x5213.  Exit 0 = all equal, 1 = mismatch.
 */
#include <stdlib.h>
#include <sys/mman.h>
#include "t32o8_rec.h"
#include "tx_isp_t32_ctrl.h"

u64 tisp_write_proc(u32, u32, u32, u32);
u64 tisp_show_debug_info(u32, u32, u32, u32);
u64 tiziano_isp_ccm_attr_g_ctrl_isra_40(u32, u32, u32, u32);
u64 mbus_to_bayer_write(u32, u32, u32, u32);

#define CALL(f, a, b, c) t32o_call((t32o_fn)(void (*)(void))(f), a, b, c, 0, 0)
enum { F_CFU = 1, F_LOCK, F_UNLOCK, F_KSTR, F_SEQ, F_VER, F_DBG, F_GCCM, F_CTU, F_PRINTF, F_BAYER };

u8 t32_data_d08e8[32];
u8 t32_data_dad9c[4], t32_data_dad9d[4];
char t32_data_c7b18[] = "help";	/* data.c strings */
char t32_data_c7a80[] = "===== TISP log information level selection =====\n";
char t32_data_c7ab4[] = "0: ISP basic message\n";
char t32_data_c7acc[] = "==== Current mode %d ====\n";
char t32_data_c7ae8[] = "Write 'echo n > /proc/tisp' to switch mode.\n";
char t32_data_bdf30[] = "tiziano_isp_ccm_attr_g_ctrl";
char t32_data_c4ad4[] = "[ %s:%d ] copy error!!!\n";
char t32_data_c36dc[] = "mbus_to_bayer_write";
char t32_data_cd4b0[] = "%s[%d] the format(0x%08x) of input couldn't be handled!\n";
u32 t32_data_c314c[20] = {	/* data.c:6262: the jump table (code addresses of the four cases) */
	0xbbb1c, 0xbbb24, 0xbbb38, 0xbbb40, 0xbbb1c, 0xbbb24, 0xbbb38, 0xbbb40, 0xbbb1c, 0xbbb24,
	0xbbb38, 0xbbb40, 0xbbb1c, 0xbbb24, 0xbbb38, 0xbbb40, 0xbbb1c, 0xbbb24, 0xbbb38, 0xbbb40 };

static struct t32o8_log *cur;
static u32 cu_left, r_ccm[10];
static u8 *ubuf, *kbuf_v;

static u32 do_copy(u32 to, u32 from, u32 n)	/* both sides: copy n - left bytes, record the source text */
{
	struct t32o8_call *c = t32o8_rec(cur, F_CFU, from, n, 0, 0);
	u32 left = cu_left > n ? n : cu_left;

	memcpy(c->data, T32O_HOST(from), (n - left) < sizeof(c->data) ? n - left : sizeof(c->data));
	memcpy(T32O_HOST(to), T32O_HOST(from), n - left);
	return left;
}
u64 __copy_user(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return do_copy(a0, a1, a2); }
u64 mutex_lock(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_LOCK, a0, 0, 0, 0); return 0; }
u64 mutex_unlock(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_UNLOCK, a0, 0, 0, 0); return 0; }
static int kstr(const char *s, u32 base, int *res)
{
	struct t32o8_call *c = t32o8_rec(cur, F_KSTR, base, 0, 0, 0);
	char *e;
	long v;

	snprintf(c->data, sizeof(c->data), "%s", s);
	v = strtol(s, &e, (int)base);
	if (e == s || (*e && !(*e == '\n' && !e[1])))
		return -22;
	*res = (int)v;
	return 0;
}
u64 kstrtoint(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a3; return (u32)kstr(T32O_HOST(a0), a1, T32O_HOST(a2)); }
u64 private_seq_printf(u32 a0, u32 a1, u32 a2, u32 a3)
{
	u32 w[2] = { a2, a3 };

	t32o8_woff = 2;
	t32o8_rec_fmt(cur, F_SEQ, a0, T32O_HOST(a1), w, 2);
	return 0;
}
u64 tisp_module_version_debug(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_VER, a0, 0, 0, 0); return 0; }
u64 lib_tisp_debug_info(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a1; (void)a2; (void)a3; t32o8_rec(cur, F_DBG, a0, 0, 0, 0); return 0; }
u64 tisp_g_ccm_attr(u32 a0, u32 a1, u32 a2, u32 a3)
{
	(void)a2; (void)a3;
	t32o8_rec(cur, F_GCCM, a0, 0, 0, 0);
	memcpy(T32O_HOST(a1), r_ccm, 40);
	return 0;
}
u64 private_copy_to_user(u32 a0, u32 a1, u32 a2, u32 a3)
{
	struct t32o8_call *c = t32o8_rec(cur, F_CTU, a0, a2, 0, 0);

	(void)a3;
	memcpy(c->data, T32O_HOST(a1), a2 < sizeof(c->data) ? a2 : sizeof(c->data));
	return cu_left;
}
u64 isp_printf(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6)
{
	u32 w[5] = { a2, a3, a4, a5, a6 };

	t32o8_rec_fmt(cur, F_PRINTF, a0, T32O_HOST(a1), w, 5);
	return 0;
}
u64 lib_tisp_api_set_bayer(u32 a0, u32 a1, u32 a2, u32 a3) { (void)a2; (void)a3; t32o8_rec(cur, F_BAYER, a0, a1, 0, 0); return 0; }

/* ours */
static unsigned long o_cfu(void *to, const void *from, unsigned long n)
{
	u32 mask = *(u32 *)(t32o8_ti + 24), a = T32O_PTR(from);
	static u8 tmp[300];
	u32 left;

	if ((a | (a + (u32)n) | (u32)n) & mask) {	/* access_ok fails: zero fill */
		memset(to, 0, n);
		return n;
	}
	left = do_copy(T32O_PTR(tmp), a, (u32)n);	/* via a buffer below 4 GiB */
	memcpy(to, tmp, n - left);
	return left;
}
static unsigned long o_ctu(void *to, const void *from, unsigned long n)
{
	static u8 tmp[64];

	memcpy(tmp, from, n);
	return (unsigned long)private_copy_to_user(T32O_PTR(to), T32O_PTR(tmp), (u32)n, 0);
}
static void o_lock(void *m) { mutex_lock(T32O_PTR(m), 0, 0, 0); }
static void o_unlock(void *m) { mutex_unlock(T32O_PTR(m), 0, 0, 0); }
static int o_kstr(const char *s, unsigned int b, int *r) { return kstr(s, b, r); }
static int o_seq(void *seq, const char *fmt, ...)
{
	u32 w[4];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 4);
	va_end(ap);
	t32o8_rec_fmt(cur, F_SEQ, T32O_PTR(seq), fmt, w, n);
	return 0;
}
static void o_ver(void *s) { tisp_module_version_debug(T32O_PTR(s), 0, 0, 0); }
static void o_dbg(void *s) { lib_tisp_debug_info(T32O_PTR(s), 0, 0, 0); }
static void o_gccm(u32 a, void *attr) { t32o8_rec(cur, F_GCCM, a, 0, 0, 0); memcpy(attr, r_ccm, 40); }
static int o_printf(int level, const char *fmt, ...)
{
	u32 w[6];
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = t32o8_va_words(fmt, ap, w, 6);
	va_end(ap);
	t32o8_rec_fmt(cur, F_PRINTF, (u32)level, fmt, w, n);
	return 0;
}
static void o_bayer(u32 ch, u32 o) { lib_tisp_api_set_bayer(ch, o, 0, 0); }
static const struct t32_ctrl_ops ops = { o_cfu, o_ctu, o_lock, o_unlock, o_kstr, o_seq, o_ver, o_dbg, o_gccm, o_printf,
					 o_bayer };

static u32 rs = 0xc7a1c7a1u;
static u32 rnd(void)
{
	rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5;
	return rs;
}

int main(void)
{
	unsigned int c, k, failed = 0, compared = 0, n = getenv("T32O_NRAND") ? (unsigned)atoi(getenv("T32O_NRAND")) : 30000;
	static const char *words[] = { "help", "help\n", "hel", "helpme", "0", "1\n", "4", "5", "-1", "9", "12345678901",
				       "3x", "", " 2", "x" };
	u8 *edge = mmap((void *)(uintptr_t)0x7ffff000u, 0x1000, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);

	if (edge == MAP_FAILED || T32O_PTR(edge) != 0x7ffff000u) {
		printf("t32_ctrl_oracle: cannot map the page below 0x80000000\n");
		return 1;
	}
	t32o_reset();
	ubuf = t32o_alloc(512);
	kbuf_v = t32o_alloc(64);
	for (c = 0; c < n; c++) {
		u32 which = c % 4, vr = 0, orr = 0, mask = (rnd() % 3) ? 0x80000000u : rnd() & 1 ? 0 : 0xc0000000u;
		struct t32_ctrl_state st = { t32_data_d08e8, 0, 0 };
		u32 count, src, arg = rnd() & 0x3ff, code = (rnd() & 1) ? 0x51fe + rnd() % 26 : rnd();
		int bad;

		for (k = 0; k < 512; k++)
			ubuf[k] = (u8)(' ' + rnd() % 90);
		if (rnd() & 1) {
			const char *w = words[rnd() % 15];

			memcpy(ubuf, w, strlen(w) + 1);
		}
		count = (rnd() & 1) ? rnd() % 16 : rnd() % 300;
		src = (rnd() & 3) ? T32O_PTR(ubuf) : T32O_PTR(edge) + 0x1000 - rnd() % 0x100;
		if (src != T32O_PTR(ubuf))
			memcpy(edge + 0xf00, ubuf, 0x100);
		*(u32 *)(t32o8_ti + 24) = mask;
		cu_left = (rnd() % 4) ? 0 : 1 + rnd() % 4;
		t32_data_dad9c[0] = st.help = (u8)((rnd() % 3) ? rnd() & 1 : rnd());
		t32_data_dad9d[0] = st.level = (u8)((rnd() % 3) ? rnd() % 5 : rnd());
		for (k = 0; k < 10; k++)
			r_ccm[k] = rnd();
		t32o8_clear();
		cur = &t32o8_v;
		switch (which) {
		case 0:
			if (!mask && src != T32O_PTR(ubuf) && src + count > 0x80000000u)
				count = 0x80000000u - src;	/* no access check: stay in the mapped page */
			vr = (u32)CALL(tisp_write_proc, rnd(), src, count);
			break;
		case 1: vr = (u32)CALL(tisp_show_debug_info, 0x5e9u, rnd(), 0); break;
		case 2:
			*(u32 *)kbuf_v = rnd();
			vr = (u32)CALL(tiziano_isp_ccm_attr_g_ctrl_isra_40, arg, T32O_PTR(kbuf_v), 0);
			break;
		default:
			vr = (u32)CALL(mbus_to_bayer_write, arg, code, 0);
			break;
		}
		/* ours: the same arguments */
		cur = &t32o8_o;
		switch (which) {
		case 0: orr = (u32)t32_tisp_write_proc(&ops, &st, T32O_HOST(src), count); break;
		case 1: orr = (u32)t32_tisp_show_debug_info(&ops, &st, T32O_HOST(0x5e9u)); break;
		case 2: orr = (u32)t32_ccm_attr_g_ctrl(&ops, arg, (const u32 *)kbuf_v); break;
		default: orr = (u32)t32_mbus_to_bayer_write(&ops, arg, code); break;
		}
		bad = t32o8_cmp("ctrl", failed < 8) || vr != orr || st.help != t32_data_dad9c[0] || st.level != t32_data_dad9d[0];
		compared++;
		if (bad && failed++ < 8)
			printf("MISMATCH case %u: ret %08x / %08x help %u/%u level %u/%u count %u mask %08x\n", which, vr, orr,
			       t32_data_dad9c[0], st.help, t32_data_dad9d[0], st.level, count, mask);
	}
	printf("t32_ctrl_oracle: %u compared, %u mismatches\n", compared, failed);
	return failed ? 1 : 0;
}
