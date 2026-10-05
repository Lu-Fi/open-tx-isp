#!/usr/bin/env python3
"""Whole default AWB path, stock tx-isp-t23.ko (md5 8237acb1) against the built
open module: random statistics, several frames in a row (history, ramp,
tolerance, cluster and trend are state), the stock IQ banks as they are.

usage: awb_stock_emu.py <stock .ko> <sensor IQ .bin> <built .ko with local symbols>
env: SEQ (sequences, 40), FRAMES (frames per sequence, 24), SEED0, CLON=1,
     TREND=1 (random trend objects), TOL=1 (random ToleranceEn), DIST=1
Compared per frame: the gains (regs 0x1804/0x1808, Q10) and the colour
temperature.  Prints "bad 0" when identical.
"""
import os, sys, random, struct, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import awb_cluster_emu as E

SEQ = int(os.environ.get('SEQ', '40'))
FRAMES = int(os.environ.get('FRAMES', '24'))
S0 = int(os.environ.get('SEED0', '0'))
CLON = os.environ.get('CLON', '1') == '1'
TREND = os.environ.get('TREND', '1') == '1'
TOL = os.environ.get('TOL', '1') == '1'
VERB = int(os.environ.get('VERB', '3'))
GOLDEN = os.environ.get('GOLDEN')   # write the first sequences as a C table (host test)
GOLD_N = int(os.environ.get('GOLD_N', '6'))
GOLD_OUT = []


def seq_scene(rnd, base, mode):
    if mode == 0:
        return E.rand_scene(rnd, sparse=True)
    # slowly drifting scene: perturb the previous one a little (history/ramp)
    out = []
    for (r, g, b, p) in base:
        k = rnd.uniform(0.9, 1.1)
        out.append((min(int(r * k), 0x1fffff), g, min(int(b * rnd.uniform(0.9, 1.1)), 0x1fffff), p))
    return out


def extra_scene(rnd, kind):
    """edge frames: empty, one lit zone, uniform, saturated, no green"""
    if kind == 'empty':
        return [(0, 0, 0, 0)] * 225
    if kind == 'one':
        z = [(0, 0, 0, 0)] * 225
        z[rnd.randrange(225)] = (4000, 8000, 3000, 2000)
        return z
    if kind == 'uniform':
        r, g, b = rnd.choice([(900, 1800, 1000), (1200, 1200, 1200), (3000, 1500, 700)])
        p = rnd.choice([30, 500, 4000])
        c = lambda v: min(v, 0x1fffff)       # the statistics words hold 21 bits
        return [(c(r * p), c(g * p), c(b * p), p)] * 225
    if kind == 'nogreen':
        z = E.rand_scene(rnd, sparse=True)
        return [(r, 0 if rnd.random() < 0.3 else g, b, p) for (r, g, b, p) in z]
    if kind == 'norb':
        z = E.rand_scene(rnd, sparse=True)
        return [(0 if rnd.random() < 0.2 else r, g, 0 if rnd.random() < 0.2 else b, p) for (r, g, b, p) in z]
    return E.rand_scene(rnd, sparse=True)


def arrays(st, ou):
    """stock vs open zone arrays of the run just made"""
    names = (('zone_rgbg', 450, 'regtrace_t23_awb_hlil_ratio'),
             ('zone_pix_wgh', 225, 'regtrace_t23_awb_hlil_pixel_weight'),
             ('rgbg_wght', 225, 'regtrace_t23_awb_hlil_mesh_weight'),
             ('rgbg_dis', 225, 'regtrace_t23_awb_hlil_distance'),
             ('rgbg_d_wght', 225, 'regtrace_t23_awb_hlil_distance_weight'))
    return [(sn, st.arr(sn, n), ou.arr(on, n)) for sn, n, on in names]


COV = dict(frames=0, refine=0, ramp=0, tol=0, skipped=0, noweight=0, indoor=0, outdoor=0, cluster=0, first=0)


HR_SRC = r"""
#include <stdio.h>
#include "%s"
/* one sequence per line: window iq_th tol_en tol_th nframes, then per frame
 * rg bg ct flag manual; prints per frame: update first avg_rg avg_bg avg_ct mf[0..5] */
int main(void)
{
	unsigned window, iq_th, tol_en, tol_th, n, f, i;
	while (scanf("%%u %%u %%u %%u %%u", &window, &iq_th, &tol_en, &tol_th, &n) == 5) {
		struct t23x_awb_history h = { { 0 } };
		uint32_t mf[6], orig_rg = 0, orig_bg = 0;
		for (i = 0; i < 6; i++) scanf("%%u", &mf[i]);
		for (f = 0; f < n; f++) {
			unsigned rg, bg, ct, flag, manual, steps, first, upd;
			bool fi;
			scanf("%%u %%u %%u %%u %%u", &rg, &bg, &ct, &flag, &manual);
			uint32_t r = rg, b = bg, c = ct;
			steps = t23x_awb_history_push(&h, &r, &b, &c, window, &fi);
			first = fi;
			upd = t23x_awb_need_update(fi, r, b, orig_rg, orig_bg, manual, mf);
			if (upd) {
				orig_rg = r; orig_bg = b;
				t23x_awb_ramp(mf, (((1U << 26) / r) + 512U) >> 10, (((1U << 26) / b) + 512U) >> 10, steps, flag, iq_th, tol_en, tol_th);
			}
			printf("%%u %%u %%u %%u %%u %%u %%u %%u %%u %%u %%u\n", upd, first, r, b, c, mf[0], mf[1], mf[2], mf[3], mf[4], mf[5]);
		}
	}
	return 0;
}
""" % os.path.join(HERE, '..', 'tx_isp_t23_awb_stock.h')


def hist_ramp_test(nseq, nframes):
    """Tiziano_awb_fpga's history, change test, ramp and tolerance on scripted
    Tiziano_Awb_Ct_Detect results (the hook replaces Ct_Detect by the next
    scripted rg / bg / ct / empty-run flag), stock module against the
    header the runtime uses."""
    os.chdir(E.TMP)
    open('hr.c', 'w').write(HR_SRC)
    subprocess.check_call(['gcc', '-O1', '-Wall', '-o', 'hr', 'hr.c'])
    bad = tot = 0
    cov = dict(ramp=0, tol=0, flag=0, skip=0, win=set())
    for seed in range(S0, S0 + nseq):
        rnd = random.Random(900 + seed)
        st = E.Stock()
        m, c = st.m, st.c
        script = []
        def hook(c_, R, script=script):
            sp = R[29]
            p_flag, p_rgbg, p_ct = c_.r32(sp + 76), c_.r32(sp + 72), c_.r32(sp + 56)
            rg, bg, ct, flag = script.pop(0)
            c_.w32(p_flag, flag)
            c_.w32(p_rgbg, rg); c_.w32(p_rgbg + 4, bg)
            c_.w32(p_ct, ct)
            return 0
        c.hook('Tiziano_Awb_Ct_Detect', hook)
        c.watch[m.addr('Tiziano_awb_set_gain')] = 'set_gain'
        window = rnd.choice([1, 2, 4, 5, 7, 15, 16, 20, 0])
        tol_en = rnd.choice([0, 1])
        tol_th = rnd.choice([0, 2, 8, 40, 200])
        cl = [0, 300, 300, 300, 2, 10, 0, 0, tol_en, tol_th]
        st.set_cluster(cl)
        iq_th = rnd.choice([0, 1, 4, 10, 50])
        st.setw('_awb_cof', [window, iq_th])
        mf = st.arr('_awb_mf_para', 6)
        base_mf = list(mf)
        base = (rnd.randint(150, 900), rnd.randint(150, 900))
        vec = []
        for f in range(nframes):
            if rnd.random() < 0.25:
                base = (rnd.randint(150, 900), rnd.randint(150, 900))
            j = rnd.choice([0, 0, 1, 3, 20])
            rg = max(1, base[0] + rnd.randint(-j, j)); bg = max(1, base[1] + rnd.randint(-j, j))
            ct = rnd.choice([2800, 3200, 4500, 5000, 6500]) + rnd.randint(-50, 50)
            flag = 1 if rnd.random() < 0.08 else 0
            if flag:
                rg, bg, ct = 256, 256, 5000
            manual = 0
            vec.append((rg, bg, ct, flag, manual))
        script.extend((v[0], v[1], v[2], v[3]) for v in vec)
        inp = '%d %d %d %d %d %s\n' % (window, iq_th, tol_en, tol_th, nframes, ' '.join(map(str, mf)))
        inp += '\n'.join(' '.join(map(str, v)) for v in vec) + '\n'
        got = subprocess.run(['./hr'], input=inp.encode(), stdout=subprocess.PIPE, check=True).stdout.decode().split('\n')[:-1]
        gold = []
        for f, v in enumerate(vec):
            nw = len(c.wlog)
            zones = E.rand_scene(rnd, sparse=True)
            st.run(zones, 300)
            upd = 1 if len(c.wlog) > nw else 0
            orig = c.rdbytes(m.addr('awb_gain_original'), 8)
            orig = struct.unpack('<2I', orig)
            ctv = c.r32(m.addr('_awb_ct'))
            mfs = st.arr('_awb_mf_para', 6)
            want = got[f].split()
            have = [str(upd)] + want[1:2] + [str(orig[0]), str(orig[1]), str(ctv)] + [str(x) for x in mfs]
            gold.append([upd, orig[0], orig[1], ctv] + mfs)
            tot += 1
            cov['ramp'] += mfs[0] == 1; cov['tol'] += tol_en == 1; cov['flag'] += v[3]; cov['skip'] += upd == 0
            if want[0:1] + want[2:] != have[0:1] + have[2:] or (not upd and False):
                bad += 1
                if bad <= VERB:
                    print('hist/ramp mismatch seed %d frame %d window %d iq_th %d tol %d/%d in %s: header %s stock %s' % (
                        seed, f, window, iq_th, tol_en, tol_th, v, want, have))
                break
        if GOLDEN and seed - S0 < GOLD_N:
            GOLD_OUT.append((window, iq_th, tol_en, tol_th, list(base_mf), vec, gold))
    cov['win'] = sorted(cov['win'])
    if GOLDEN:
        with open(GOLDEN, 'w') as fh:
            fh.write('/* generated by driver/t23/audit/awb_stock_emu.py (GOLDEN=): scripted Ct_Detect\n'
                     ' * results and what the stock tx-isp-t23.ko (md5 8237acb1) did with them */\n')
            fh.write('struct gold_frame { unsigned rg, bg, ct, flag; unsigned upd, orig_rg, orig_bg, ct_out, mf[6]; };\n')
            fh.write('struct gold_seq { unsigned window, iq_th, tol_en, tol_th, mf0[6], n; const struct gold_frame *f; };\n')
            for k, (w, iq, te, tt, m0, vec, gold) in enumerate(GOLD_OUT):
                fh.write('static const struct gold_frame gold_f%d[] = {\n' % k)
                for v, g in zip(vec, gold):
                    fh.write('\t{ %d, %d, %d, %d, %d, %d, %d, %d, { %s } },\n' % (v[0], v[1], v[2], v[3], g[0], g[1], g[2], g[3], ', '.join(map(str, g[4:]))))
                fh.write('};\n')
            fh.write('static const struct gold_seq gold_seqs[] = {\n')
            for k, (w, iq, te, tt, m0, vec, gold) in enumerate(GOLD_OUT):
                fh.write('\t{ %d, %d, %d, %d, { %s }, %d, gold_f%d },\n' % (w, iq, te, tt, ', '.join(map(str, m0)), len(vec), k))
            fh.write('};\n')
    print('history/ramp (scripted Ct_Detect): bad %d / %d frames' % (bad, tot), cov)
    return bad


def main():
    bad = tot = 0
    for seed in range(S0, S0 + SEQ):
        rnd = random.Random(31000 + seed)
        st, ou = E.Stock(), E.Ours()
        cl = E.rand_cluster(rnd)
        cl[0] = 1 if (CLON and rnd.random() < 0.7) else rnd.choice([0, 0, 2])
        if TOL and rnd.random() < 0.5:
            cl[8] = 1
            cl[9] = rnd.choice([2, 8, 40, 200])
        else:
            cl[8] = 0
        st.set_cluster(cl); ou.set_cluster(cl)
        if TREND and rnd.random() < 0.5:
            tr = [rnd.choice([0, 1, 1])] + [rnd.choice([0, 800, 1000, 1024, 1100, 1300, 2000]) for _ in range(6)]
            st.setw('_awb_trend', tr); st.setw('awb_trend_api_para', tr); st.setw('awb_trend_api_status', [0, 2])
            ou.set_trend(tr)
        if os.environ.get('DIST', '1') == '0':
            st.setw('_awb_dis_tw', [0, 2048, 1536])
            ou.setw('regtrace_t23_awb_hlil_distance_parameters', [0, 2048, 1536])
        base = E.rand_scene(rnd, sparse=True)
        ev = rnd.choice([10, 30, 94, 149, 150, 300, 2000])
        for f in range(FRAMES):
            if rnd.random() < 0.2:
                base = E.rand_scene(rnd, sparse=True)
            r = rnd.random()
            if r < 0.08:
                zones = extra_scene(rnd, rnd.choice(['empty', 'one', 'uniform', 'nogreen', 'norb']))
            else:
                zones = seq_scene(rnd, base, 1 if rnd.random() < 0.7 else 0)
            if rnd.random() < 0.1:
                ev = rnd.choice([10, 30, 94, 141, 149, 150, 300, 2000])
            mf0 = st.arr('_awb_mf_para', 6)
            a = st.run(zones, ev)
            b = ou.run(zones, ev)
            tot += 1
            ar = arrays(st, ou)
            mf1 = st.arr('_awb_mf_para', 6)
            COV['frames'] += 1
            COV['refine'] += any(x != y for x, y in zip(ar[2][1], ar[4][1]))
            COV['ramp'] += mf1[0] == 1
            COV['tol'] += cl[8] == 1
            COV['skipped'] += (mf1 == mf0 and f > 0 and a[:2] == a0 if False else 0)
            COV['noweight'] += (a[2] == 5000 and mf1[2:] == [256] * 4)
            COV['indoor'] += ev >= 150
            COV['outdoor'] += ev < 150
            COV['cluster'] += cl[0] == 1
            COV['first'] += f == 0
            amiss = [n for (n, x, y) in ar if x != y]
            if a != b or amiss or mf1 != ou.arr('_awb_mf_para', 6):
                bad += 1
                if bad <= VERB:
                    print('mismatch seed %d frame %d ev %d cl %s: stock %s ours %s arrays %s mf %s/%s' % (
                        seed, f, ev, cl, [hex(x) if i < 2 else x for i, x in enumerate(a)],
                        [hex(x) if i < 2 else x for i, x in enumerate(b)], amiss, mf1, ou.arr('_awb_mf_para', 6)))
                    for n, x, y in ar:
                        d = [i for i in range(len(x)) if x[i] != y[i]]
                        if d:
                            print('   ', n, 'diff at', d[:6], [(x[i], y[i]) for i in d[:3]])
    print('coverage', COV)
    print('whole AWB path: bad %d / %d frames' % (bad, tot))
    return bad


if __name__ == '__main__':
    rc = 0
    if os.environ.get('HR', '1') == '1':
        rc |= hist_ramp_test(int(os.environ.get('HRSEQ', '40')), int(os.environ.get('HRFRAMES', '40')))
    if os.environ.get('WHOLE', '1') == '1':
        rc |= 1 if main() else 0
    sys.exit(rc)
