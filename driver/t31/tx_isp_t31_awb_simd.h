/* SPDX-License-Identifier: GPL-2.0 */
/*
 * T31 AWB colour-temperature detection: fast scalar helpers and MXU2 kernels.
 *
 * Tiziano_Awb_Ct_Detect runs once per frame in isp_fw_process (event 10,
 * ~0.8 ms/frame of the 1.76 ms ISP event work at 15 fps).  Its cost sits in
 *
 *   - the shift/subtract fixed-point divide tx_isp_fixdiv_u32 (up to q
 *     iterations on a 64-bit remainder, called for every zone several
 *     times: zone weights, mesh interpolation, ramp factors), and
 *   - the cluster loops: every cluster iteration (6d), count (6f) and
 *     assignment (6k) sweeps all 225 zones with a squared-distance test.
 *
 * Both are replaced bit-exactly, selected by the t31_awb_impl module
 * parameter:
 *
 *   0  reference C  (the code as recovered from the OEM binary)
 *   1  fast C       (division with one hardware divide, precomputed zone
 *                    coordinates, one 64-bit multiply for the Q multiply)
 *   2  MXU2         (4 zones per instruction; falls back to 1 where the CPU
 *                    has no MXU2 or the caller cannot use it safely)
 *
 * Kernel use of MXU2 on 3.10 (Ingenic): there is no begin/end API.  switch_to
 * saves the VPRs of a task only when the CU2 bit is set in the saved Status
 * of that task; kernel threads never have it.  A kernel thread may therefore
 * set CU2 itself, use the VPRs and clear it again as long as it cannot be
 * preempted in between (nothing saves its VPRs) and no interrupt handler
 * uses them (none does).  The next user task with CU2 gets its own VPRs
 * back in finish_arch_switch.  So the rules enforced by t31_mxu2_begin():
 *   - only in a kernel thread (a user process may hold live VPRs),
 *   - only with interrupts off (the isp_fw_process dispatch has them off,
 *     like the OEM), so the Status read-modify-write and the VPR use are
 *     one atomic section of a few microseconds,
 *   - the used VPRs are cleared before CU2 is dropped (no stats data left
 *     for the next MXU2 user to see).
 *
 * Every function exists as plain C with the same result; the MXU2 versions
 * are written against a tiny op layer (V*) that is inline asm on the target
 * and a lane-by-lane model on the host, so the host test runs the very same
 * algorithm code.  Only the instruction encodings/semantics are left to the
 * on-device selftest (t31_awb_selftest).
 *
 * Requires <linux/types.h> or <stdint.h> types (u32/u64) from the includer.
 */
#ifndef TX_ISP_T31_AWB_SIMD_H
#define TX_ISP_T31_AWB_SIMD_H

#define T31_AWB_ZONES		225
#define T31_AWB_VEC_ZONES	224		/* 56 vectors of 4; zone 224 is scalar */

/* ---------------- fast scalar fixed point (bit-exact) ------------------- */

/*
 * == tx_isp_fixdiv_u32(): low 32 bits of ((u64)num << q) / den.  The
 * reference walks q fractional bits one by one on a u64 remainder.  When
 * num << q fits 32 bits that is a single divide; otherwise the integer part
 * and, if rem << q fits, the fraction take one divide each.
 */
static inline u32 t31_fixdiv_fast(u32 q, u32 num, u32 den)
{
	u32 quo, rem, frac;

	if (!den || q > 31U)
		return 0;
	if (!q)
		return num / den;
	if (num < (1U << (32U - q)))
		return (num << q) / den;	/* exact, one divide */
	quo = num / den;
	rem = num - quo * den;
	if (rem < (1U << (32U - q))) {
		frac = (rem << q) / den;
	} else {
		u64 r = rem;
		u32 bit;

		frac = 0;
		for (bit = 0; bit < q; bit++) {
			r <<= 1;
			frac <<= 1;
			if (r >= den) {
				frac |= 1U;
				r -= den;
			}
		}
	}
	return (quo << q) | frac;
}

/* == tx_isp_fixmul_u32(): low 32 bits of ((u64)a * b) >> q */
static inline u32 t31_fixmul_fast(u32 q, u32 a, u32 b)
{
	if (q > 31U)
		return 0;
	return (u32)(((u64)a * b) >> q);
}

/* ---------------- sweep kernels, plain C (impl 1) ----------------------- */

/* squared distance, truncated to 32 bit like the reference's (u32)dsq */
static inline u32 t31_awb_dsq(u32 zr, u32 zb, u32 cx, u32 cy)
{
	u32 dr = zr - cx;
	u32 db = zb - cy;

	return dr * dr + db * db;	/* |d|^2 == d^2 mod 2^32 */
}

/*
 * 6d: zones within @thr of (cx,cy): sum of their raw rg and bg and the
 * count.  zr/zb are the rounded coordinates ((rounding + raw) >> q), rg/bg
 * the raw values that get summed.
 */
static inline void t31_awb_sweep_sum_c(const u32 *zr, const u32 *zb,
				       const u32 *rg, const u32 *bg,
				       u32 cx, u32 cy, u32 thr,
				       u32 *sum_rg, u32 *sum_bg, u32 *cnt)
{
	u32 s_rg = 0, s_bg = 0, n = 0, i;

	for (i = 0; i < T31_AWB_ZONES; i++) {
		if (thr >= t31_awb_dsq(zr[i], zb[i], cx, cy)) {
			s_rg += rg[i];
			s_bg += bg[i];
			n++;
		}
	}
	*sum_rg = s_rg;
	*sum_bg = s_bg;
	*cnt = n;
}

/* 6f: zones with a non-zero weight within @thr: count and weight sum */
static inline void t31_awb_sweep_wcount_c(const u32 *zr, const u32 *zb,
					  const u32 *w, u32 cx, u32 cy,
					  u32 thr, u32 *cnt, u32 *wsum)
{
	u32 n = 0, s = 0, i;

	for (i = 0; i < T31_AWB_ZONES; i++) {
		if (w[i] && thr >= t31_awb_dsq(zr[i], zb[i], cx, cy)) {
			n++;
			s += w[i];
		}
	}
	*cnt = n;
	*wsum = s;
}

/*
 * 6k: cluster @id (1-based, @ccnt = its zone count) competes for every zone
 * within @thr: it wins when the zone has no cluster yet or its count is
 * strictly larger than the current winner's (bc[]).
 */
static inline void t31_awb_assign_c(const u32 *zr, const u32 *zb,
				    u32 cx, u32 cy, u32 thr, u32 id, u32 ccnt,
				    u32 *bj, u32 *bc)
{
	u32 i;

	for (i = 0; i < T31_AWB_ZONES; i++) {
		if (thr < t31_awb_dsq(zr[i], zb[i], cx, cy))
			continue;
		if (!bj[i] || bc[i] < ccnt) {
			bj[i] = id;
			bc[i] = ccnt;
		}
	}
}

/* ---------------- MXU2 op layer ----------------------------------------- */

#if defined(__mips__) && defined(__KERNEL__)

#define T31_MXU2_ASM 1
#define T31_V_PRE	".set push\n\t.set mxu2\n\t"
#define T31_V_POST	"\n\t.set pop"

#define VLOAD(d, p)	asm volatile(T31_V_PRE "lu1q $vr%0, 0(%1)" T31_V_POST \
			:: "i"(d), "r"(p) : "memory")
#define VSTORE(s, p)	asm volatile(T31_V_PRE "su1q $vr%0, 0(%1)" T31_V_POST \
			:: "i"(s), "r"(p) : "memory")
#define VSPLAT(d, x)	asm volatile(T31_V_PRE "mfcpuw $vr%0, %1" T31_V_POST \
			:: "i"(d), "r"(x))
#define VOP3(op, d, a, b) asm volatile(T31_V_PRE op " $vr%0, $vr%1, $vr%2" \
			T31_V_POST :: "i"(d), "i"(a), "i"(b))
#define VOP2(op, d, a)	asm volatile(T31_V_PRE op " $vr%0, $vr%1" T31_V_POST \
			:: "i"(d), "i"(a))
#define VSUBW(d, a, b)	VOP3("subw", d, a, b)
#define VADDW(d, a, b)	VOP3("addw", d, a, b)
#define VMULW(d, a, b)	VOP3("mulw", d, a, b)
#define VCLEUW(d, a, b)	VOP3("cleuw", d, a, b)	/* a <= b unsigned -> ~0 */
#define VCLTUW(d, a, b)	VOP3("cltuw", d, a, b)	/* a <  b unsigned -> ~0 */
#define VCEQZW(d, a)	VOP2("ceqzw", d, a)	/* a == 0 -> ~0 */
#define VANDV(d, a, b)	VOP3("andv", d, a, b)
#define VORV(d, a, b)	VOP3("orv", d, a, b)
#define VNORV(d, a, b)	VOP3("norv", d, a, b)
#define VXORV(d, a, b)	VOP3("xorv", d, a, b)

#else /* host model: 32 registers x 4 lanes */

#define T31_MXU2_ASM 0
struct t31_vreg { u32 e[4]; };
extern struct t31_vreg t31_vr[32];
#define VLOAD(d, p)	do { int l_; for (l_ = 0; l_ < 4; l_++) \
			t31_vr[d].e[l_] = ((const u32 *)(p))[l_]; } while (0)
#define VSTORE(s, p)	do { int l_; for (l_ = 0; l_ < 4; l_++) \
			((u32 *)(p))[l_] = t31_vr[s].e[l_]; } while (0)
#define VSPLAT(d, x)	do { int l_; for (l_ = 0; l_ < 4; l_++) \
			t31_vr[d].e[l_] = (u32)(x); } while (0)
#define VSUBW(d, a, b)	do { struct t31_vreg A_ = t31_vr[a], B_ = t31_vr[b]; int l_; \
			for (l_ = 0; l_ < 4; l_++) t31_vr[d].e[l_] = A_.e[l_] - B_.e[l_]; } while (0)
#define VADDW(d, a, b)	do { struct t31_vreg A_ = t31_vr[a], B_ = t31_vr[b]; int l_; \
			for (l_ = 0; l_ < 4; l_++) t31_vr[d].e[l_] = A_.e[l_] + B_.e[l_]; } while (0)
#define VMULW(d, a, b)	do { struct t31_vreg A_ = t31_vr[a], B_ = t31_vr[b]; int l_; \
			for (l_ = 0; l_ < 4; l_++) t31_vr[d].e[l_] = A_.e[l_] * B_.e[l_]; } while (0)
#define VCLEUW(d, a, b)	do { struct t31_vreg A_ = t31_vr[a], B_ = t31_vr[b]; int l_; \
			for (l_ = 0; l_ < 4; l_++) t31_vr[d].e[l_] = A_.e[l_] <= B_.e[l_] ? ~0U : 0; } while (0)
#define VCLTUW(d, a, b)	do { struct t31_vreg A_ = t31_vr[a], B_ = t31_vr[b]; int l_; \
			for (l_ = 0; l_ < 4; l_++) t31_vr[d].e[l_] = A_.e[l_] < B_.e[l_] ? ~0U : 0; } while (0)
#define VCEQZW(d, a)	do { struct t31_vreg A_ = t31_vr[a]; int l_; \
			for (l_ = 0; l_ < 4; l_++) t31_vr[d].e[l_] = A_.e[l_] == 0 ? ~0U : 0; } while (0)
#define VANDV(d, a, b)	do { struct t31_vreg A_ = t31_vr[a], B_ = t31_vr[b]; int l_; \
			for (l_ = 0; l_ < 4; l_++) t31_vr[d].e[l_] = A_.e[l_] & B_.e[l_]; } while (0)
#define VORV(d, a, b)	do { struct t31_vreg A_ = t31_vr[a], B_ = t31_vr[b]; int l_; \
			for (l_ = 0; l_ < 4; l_++) t31_vr[d].e[l_] = A_.e[l_] | B_.e[l_]; } while (0)
#define VNORV(d, a, b)	do { struct t31_vreg A_ = t31_vr[a], B_ = t31_vr[b]; int l_; \
			for (l_ = 0; l_ < 4; l_++) t31_vr[d].e[l_] = ~(A_.e[l_] | B_.e[l_]); } while (0)
#define VXORV(d, a, b)	do { struct t31_vreg A_ = t31_vr[a], B_ = t31_vr[b]; int l_; \
			for (l_ = 0; l_ < 4; l_++) t31_vr[d].e[l_] = A_.e[l_] ^ B_.e[l_]; } while (0)
#endif

/*
 * Register use (all kernels): vr0..vr10 only; t31_mxu2_scrub() clears them.
 */
#define T31_VR_CX	8
#define T31_VR_CY	9
#define T31_VR_THR	7

static inline void t31_mxu2_scrub(void)
{
	VXORV(0, 0, 0); VXORV(1, 1, 1); VXORV(2, 2, 2); VXORV(3, 3, 3);
	VXORV(4, 4, 4); VXORV(5, 5, 5); VXORV(6, 6, 6); VXORV(7, 7, 7);
	VXORV(8, 8, 8); VXORV(9, 9, 9); VXORV(10, 10, 10);
}

/* ---------------- MXU2 kernels (impl 2) --------------------------------- */

/*
 * Callers hold the MXU2 section open (t31_mxu2_begin()).  Zones 0..223 go
 * through the vector loop, zone 224 through the scalar tail.
 *
 * Per vector: d = z - c (wrap), dsq = dr*dr + db*db (low 32 bits, identical
 * to the reference's (u32)dsq), mask = dsq <= thr.
 */
static inline void t31_awb_sweep_sum_mxu2(const u32 *zr, const u32 *zb,
					  const u32 *rg, const u32 *bg,
					  u32 cx, u32 cy, u32 thr,
					  u32 *sum_rg, u32 *sum_bg, u32 *cnt)
{
	u32 acc[3][4] __attribute__((aligned(16)));
	u32 i, s_rg, s_bg, n;

	VSPLAT(T31_VR_CX, cx);
	VSPLAT(T31_VR_CY, cy);
	VSPLAT(T31_VR_THR, thr);
	VXORV(4, 4, 4);			/* sum rg */
	VXORV(5, 5, 5);			/* sum bg */
	VXORV(6, 6, 6);			/* count (as -1 steps) */
	for (i = 0; i < T31_AWB_VEC_ZONES; i += 4) {
		VLOAD(0, zr + i);
		VLOAD(1, zb + i);
		VSUBW(0, 0, T31_VR_CX);
		VSUBW(1, 1, T31_VR_CY);
		VMULW(0, 0, 0);
		VMULW(1, 1, 1);
		VADDW(0, 0, 1);
		VCLEUW(0, 0, T31_VR_THR);	/* mask */
		VLOAD(1, rg + i);
		VLOAD(2, bg + i);
		VANDV(1, 1, 0);
		VANDV(2, 2, 0);
		VADDW(4, 4, 1);
		VADDW(5, 5, 2);
		VSUBW(6, 6, 0);			/* mask is -1: count += 1 */
	}
	VSTORE(4, acc[0]);
	VSTORE(5, acc[1]);
	VSTORE(6, acc[2]);
	s_rg = acc[0][0] + acc[0][1] + acc[0][2] + acc[0][3];
	s_bg = acc[1][0] + acc[1][1] + acc[1][2] + acc[1][3];
	n = acc[2][0] + acc[2][1] + acc[2][2] + acc[2][3];
	if (thr >= t31_awb_dsq(zr[224], zb[224], cx, cy)) {
		s_rg += rg[224];
		s_bg += bg[224];
		n++;
	}
	*sum_rg = s_rg;
	*sum_bg = s_bg;
	*cnt = n;
}

static inline void t31_awb_sweep_wcount_mxu2(const u32 *zr, const u32 *zb,
					     const u32 *w, u32 cx, u32 cy,
					     u32 thr, u32 *cnt, u32 *wsum)
{
	u32 acc[2][4] __attribute__((aligned(16)));
	u32 i, s, n;

	VSPLAT(T31_VR_CX, cx);
	VSPLAT(T31_VR_CY, cy);
	VSPLAT(T31_VR_THR, thr);
	VXORV(4, 4, 4);			/* weight sum */
	VXORV(5, 5, 5);			/* count */
	for (i = 0; i < T31_AWB_VEC_ZONES; i += 4) {
		VLOAD(0, zr + i);
		VLOAD(1, zb + i);
		VSUBW(0, 0, T31_VR_CX);
		VSUBW(1, 1, T31_VR_CY);
		VMULW(0, 0, 0);
		VMULW(1, 1, 1);
		VADDW(0, 0, 1);
		VCLEUW(0, 0, T31_VR_THR);	/* near */
		VLOAD(2, w + i);
		VCEQZW(3, 2);
		VNORV(3, 3, 3);			/* weight != 0 */
		VANDV(0, 0, 3);			/* near and weighted */
		VANDV(2, 2, 0);
		VADDW(4, 4, 2);
		VSUBW(5, 5, 0);
	}
	VSTORE(4, acc[0]);
	VSTORE(5, acc[1]);
	s = acc[0][0] + acc[0][1] + acc[0][2] + acc[0][3];
	n = acc[1][0] + acc[1][1] + acc[1][2] + acc[1][3];
	if (w[224] && thr >= t31_awb_dsq(zr[224], zb[224], cx, cy)) {
		n++;
		s += w[224];
	}
	*cnt = n;
	*wsum = s;
}

static inline void t31_awb_assign_mxu2(const u32 *zr, const u32 *zb,
				       u32 cx, u32 cy, u32 thr, u32 id,
				       u32 ccnt, u32 *bj, u32 *bc)
{
	u32 i;

	VSPLAT(T31_VR_CX, cx);
	VSPLAT(T31_VR_CY, cy);
	VSPLAT(T31_VR_THR, thr);
	VSPLAT(6, id);
	VSPLAT(5, ccnt);
	for (i = 0; i < T31_AWB_VEC_ZONES; i += 4) {
		VLOAD(0, zr + i);
		VLOAD(1, zb + i);
		VSUBW(0, 0, T31_VR_CX);
		VSUBW(1, 1, T31_VR_CY);
		VMULW(0, 0, 0);
		VMULW(1, 1, 1);
		VADDW(0, 0, 1);
		VCLEUW(0, 0, T31_VR_THR);	/* within threshold */
		VLOAD(1, bj + i);
		VLOAD(2, bc + i);
		VCEQZW(3, 1);			/* no winner yet */
		VCLTUW(4, 2, 5);		/* winner's count < ours */
		VORV(3, 3, 4);
		VANDV(0, 0, 3);			/* take-over mask */
		VNORV(3, 0, 0);			/* keep mask */
		VANDV(1, 1, 3);
		VANDV(2, 2, 3);
		VANDV(4, 0, 6);			/* our id where we take over */
		VANDV(10, 0, 5);		/* our count likewise */
		VORV(1, 1, 4);
		VORV(2, 2, 10);
		VSTORE(1, bj + i);
		VSTORE(2, bc + i);
	}
	if (thr >= t31_awb_dsq(zr[224], zb[224], cx, cy) &&
	    (!bj[224] || bc[224] < ccnt)) {
		bj[224] = id;
		bc[224] = ccnt;
	}
}

/* ---------------- MXU2 section + dispatch -------------------------------- */

#if T31_MXU2_ASM
#include <asm/mipsregs.h>
#include <asm/hazards.h>
#include <asm/cpu-features.h>
#include <linux/sched.h>

/* true: CU2 is on and the VPRs are ours until t31_mxu2_end() */
static inline bool t31_mxu2_begin(unsigned long *st)
{
	if (!cpu_has_mxu_v2 || !(current->flags & PF_KTHREAD) ||
	    !irqs_disabled())
		return false;
	*st = read_c0_status();
	write_c0_status(*st | ST0_CU2);
	back_to_back_c0_hazard();
	return true;
}

static inline void t31_mxu2_end(unsigned long st)
{
	t31_mxu2_scrub();
	write_c0_status(st);
	back_to_back_c0_hazard();
}
#else
static int t31_mxu2_host_ok = 1;
static inline bool t31_mxu2_begin(unsigned long *st)
{
	*st = 0;
	return t31_mxu2_host_ok;
}
static inline void t31_mxu2_end(unsigned long st) { (void)st; }
#endif

/* how often impl 2 really ran on MXU2 / had to fall back (selftest report) */
static unsigned long t31_awb_mxu2_runs, t31_awb_mxu2_fallbacks;

static inline void t31_awb_sweep_sum(int impl, const u32 *zr, const u32 *zb,
				     const u32 *rg, const u32 *bg,
				     u32 cx, u32 cy, u32 thr,
				     u32 *sum_rg, u32 *sum_bg, u32 *cnt)
{
	unsigned long st;

	if (impl >= 2) {
		if (t31_mxu2_begin(&st)) {
			t31_awb_sweep_sum_mxu2(zr, zb, rg, bg, cx, cy, thr,
					       sum_rg, sum_bg, cnt);
			t31_mxu2_end(st);
			t31_awb_mxu2_runs++;
			return;
		}
		t31_awb_mxu2_fallbacks++;
	}
	t31_awb_sweep_sum_c(zr, zb, rg, bg, cx, cy, thr, sum_rg, sum_bg, cnt);
}

static inline void t31_awb_sweep_wcount(int impl, const u32 *zr, const u32 *zb,
					const u32 *w, u32 cx, u32 cy, u32 thr,
					u32 *cnt, u32 *wsum)
{
	unsigned long st;

	if (impl >= 2) {
		if (t31_mxu2_begin(&st)) {
			t31_awb_sweep_wcount_mxu2(zr, zb, w, cx, cy, thr,
						  cnt, wsum);
			t31_mxu2_end(st);
			t31_awb_mxu2_runs++;
			return;
		}
		t31_awb_mxu2_fallbacks++;
	}
	t31_awb_sweep_wcount_c(zr, zb, w, cx, cy, thr, cnt, wsum);
}

static inline void t31_awb_assign(int impl, const u32 *zr, const u32 *zb,
				  u32 cx, u32 cy, u32 thr, u32 id, u32 ccnt,
				  u32 *bj, u32 *bc)
{
	unsigned long st;

	if (impl >= 2) {
		if (t31_mxu2_begin(&st)) {
			t31_awb_assign_mxu2(zr, zb, cx, cy, thr, id, ccnt,
					    bj, bc);
			t31_mxu2_end(st);
			t31_awb_mxu2_runs++;
			return;
		}
		t31_awb_mxu2_fallbacks++;
	}
	t31_awb_assign_c(zr, zb, cx, cy, thr, id, ccnt, bj, bc);
}

#endif /* TX_ISP_T31_AWB_SIMD_H */
