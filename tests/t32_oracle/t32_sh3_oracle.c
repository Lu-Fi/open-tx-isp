// SPDX-License-Identifier: GPL-2.0
/*
 * T32 host oracle: t32_tx_isp_ivdc_show and t32_isp_framesource_show of driver/t32/tx_isp_t32_show.h against
 * the vendor tx_isp_ivdc_show / isp_framesource_show (Matt Davis' recovered C, PR #15 @ 2e2e06bc).
 * Random IVDC device / statistics (registers mapped or not, AOV words) and frame-source devices (proc data /
 * device NULL, error pointer or valid; -1..3 channels, running or not, scaler / crop on or off, queued and done
 * lists of 0..5 buffers, 64 buffer slots).  Compared: the print stream with every spin lock / unlock (lock,
 * flags) in order, the result, the seq_file bytes and that no input changed.  Exit 0 = all equal, 1 = first mismatch.
 */
#include "t32_show_rec.h"
#include "tx_isp_t32_show.h"

u64 tx_isp_ivdc_show(u32, u32, u32, u32), isp_framesource_show(u32, u32, u32, u32);
extern u32 t32_data_d9004[1], t32_data_d9014[1];
u32 t32o_ivdc_st[40] __attribute__((aligned(16)));	/* t32_data_d9898 .. d9934 */
__asm__(".globl t32_data_d9898, t32_data_d98a0, t32_data_d98a4, t32_data_d98a8, t32_data_d98ac\n"
	".set t32_data_d9898, t32o_ivdc_st\n.set t32_data_d98a0, t32o_ivdc_st+8\n.set t32_data_d98a4, t32o_ivdc_st+12\n"
	".set t32_data_d98a8, t32o_ivdc_st+16\n.set t32_data_d98ac, t32o_ivdc_st+20\n");
#define D(a) __asm__(".globl t32_data_" #a "\n.set t32_data_" #a ", t32o_ivdc_st+(0x" #a "-0xd9898)")
D(d98bc); D(d98c0); D(d98c4); D(d98c8); D(d98cc); D(d98d0); D(d98d4); D(d98d8); D(d98dc); D(d98e0);
D(d98e4); D(d98e8); D(d98ec); D(d98f0); D(d98f4); D(d98f8); D(d98fc); D(d9900); D(d9904); D(d9908);
D(d990c); D(d9910); D(d9914); D(d9918); D(d991c); D(d9920); D(d9924); D(d9928); D(d992c); D(d9930);

static u32 lock_flags;
u64 __private_spin_lock_irqsave(u32 a0, u32 a1, u32 a2, u32 a3)
{
	char t[32];

	(void)a2; (void)a3;
	*(u32 *)T32O_HOST(a1) = lock_flags;
	snprintf(t, sizeof(t), "lock %08x", a0);
	sr_put(SR_HOOK, 1, a0, 0, t);
	return 0;
}
u64 private_spin_unlock_irqrestore(u32 a0, u32 a1, u32 a2, u32 a3)
{
	char t[32];

	(void)a2; (void)a3;
	snprintf(t, sizeof(t), "unlock %08x %08x", a0, a1);
	sr_put(SR_HOOK, 2, a0, a1, t);
	return 0;
}
static u32 o_flags;	/* below 4 GiB */
static void o_lock(void *l, unsigned long *f) { __private_spin_lock_irqsave(T32O_PTR(l), T32O_PTR(&o_flags), 0, 0); *f = o_flags; }
static void o_unlock(void *l, unsigned long f) { private_spin_unlock_irqrestore(T32O_PTR(l), (u32)f, 0, 0); }

/* a circular list of n buffers through the node at +node, head at h */
static void mklist(u8 *h, u32 node, u32 n)
{
	u8 *prev = h;
	u32 i;

	for (i = 0; i < n; i++) {
		u8 *b = t32o_alloc(256);

		sr_fill(b, 256);
		*(u32 *)(prev + (prev == h ? 0 : node)) = T32O_PTR(b + node);
		prev = b;
	}
	*(u32 *)(prev + (prev == h ? 0 : node)) = T32O_PTR(h);
}

#define FSZ	(4 * 816)

int main(void)
{
	unsigned int n = getenv("T32O_NRAND") ? (unsigned int)atoi(getenv("T32O_NRAND")) : 3000, c, prints = 0, i, k;
	static const struct t32_show_lock_ops os = { o_lock, o_unlock };
	static u8 snap0[FSZ + 1024], snap1[FSZ + 1024];

	for (c = 0; c < n; c++) {
		u8 *m, m0[128], m1[128], *dev, *iv, *regs, *fs, *ch;
		u32 r0, r1, mode = sr_rnd() % 10, st0[40];

		/* ---- tx_isp_ivdc_show ---- */
		t32o_reset();
		m = t32o_alloc(128); dev = t32o_alloc(512); iv = t32o_alloc(512); regs = t32o_alloc(256);
		sr_fill(m, 128); sr_fill(dev, 512); sr_fill(iv, 512); sr_fill(regs, 256);
		*(u32 *)(m + 12) = sr_rnd() % 4096;
		*(u32 *)(m + 72) = T32O_PTR(dev);
		*(u32 *)(dev + 280) = T32O_PTR(iv);
		*(u32 *)(iv + 284) = T32O_PTR(regs);
		sr_fill(t32o_ivdc_st, sizeof(t32o_ivdc_st));
		if (sr_rnd() & 1) t32o_ivdc_st[0] = 0;
		t32_data_d9004[0] = sr_rnd() % 3; t32_data_d9014[0] = sr_rnd();
		memcpy(st0, t32o_ivdc_st, sizeof(st0)); memcpy(m0, m, 128);
		sr_begin();
		r0 = (u32)t32o_call(SR_R(tx_isp_ivdc_show), T32O_PTR(m), sr_rnd(), sr_rnd(), sr_rnd(), 0);
		memcpy(m1, m, 128); memcpy(m, m0, 128);
		sr_side = 1;
		r1 = (u32)t32_tx_isp_ivdc_show(m, t32o_ivdc_st, t32_data_d9004, t32_data_d9014);
		if (sr_cmp("t32_sh3_oracle", c, "tx_isp_ivdc_show")) return 1;
		if (r0 != r1 || memcmp(m1, m, 128) || memcmp(st0, t32o_ivdc_st, sizeof(st0))) {
			printf("t32_sh3_oracle: case %u ivdc: ret %08x/%08x\n", c, r0, r1);
			return 1;
		}
		prints += sr_n[0];

		/* ---- isp_framesource_show ---- */
		t32o_reset();
		sr_errpage();
		m = t32o_alloc(128); dev = t32o_alloc(512); fs = t32o_alloc(512); ch = t32o_alloc(FSZ);
		sr_fill(m, 128); sr_fill(dev, 512); sr_fill(fs, 512); sr_fill(ch, FSZ);
		*(u32 *)(m + 12) = sr_rnd() % 4096;
		*(u32 *)(m + 72) = mode == 0 ? 0 : mode == 1 ? sr_errptr() : T32O_PTR(dev);
		*(u32 *)(dev + 280) = mode == 2 ? 0 : mode == 3 ? sr_errptr() : T32O_PTR(fs);
		*(u32 *)(fs + 288) = T32O_PTR(ch);
		*(int *)(fs + 292) = (c % 16 == 0) ? -(int)(sr_rnd() % 3) : (int)(sr_rnd() % 5);
		for (i = 0; i < 4; i++) {
			u8 *cc = ch + 816 * i;

			*(u32 *)(cc + 736) = (sr_rnd() % 4) ? 4 : sr_rnd() % 6;
			cc[656] = (sr_rnd() & 1) ? 0 : (u8)sr_rnd();
			cc[636] = (sr_rnd() & 1) ? 0 : (u8)sr_rnd();
			mklist(cc + 536, 124, sr_rnd() % 6);
			mklist(cc + 548, 132, sr_rnd() % 6);
			for (k = 0; k < 64; k++) {
				u8 *b = 0;

				if (sr_rnd() % 3 == 0) {
					b = t32o_alloc(128);
					sr_fill(b, 128);
				}
				*(u32 *)(cc + 276 + 4 * k) = T32O_PTR(b);
			}
		}
		lock_flags = sr_rnd();
		memcpy(snap0, ch, FSZ); memcpy(snap0 + FSZ, fs, 512); memcpy(m0, m, 128);
		sr_begin();
		r0 = (u32)t32o_call(SR_R(isp_framesource_show), T32O_PTR(m), sr_rnd(), sr_rnd(), sr_rnd(), 0);
		memcpy(m1, m, 128); memcpy(m, m0, 128);
		sr_side = 1;
		r1 = (u32)t32_isp_framesource_show(m, &os);
		memcpy(snap1, ch, FSZ); memcpy(snap1 + FSZ, fs, 512);
		if (sr_cmp("t32_sh3_oracle", c, "isp_framesource_show")) return 1;
		if (r0 != r1 || memcmp(m1, m, 128) || memcmp(snap0, snap1, FSZ + 512)) {
			printf("t32_sh3_oracle: case %u framesource: ret %08x/%08x\n", c, r0, r1);
			return 1;
		}
		prints += sr_n[0];
	}
	printf("t32_sh3_oracle: %u cases x 2 functions compared (%u prints / lock calls), 0 mismatches\n", n, prints);
	return 0;
}
