#!/usr/bin/env python3
"""Emulator audit of the T23 AWB cluster stage, the colour temperature trend
and the object selection of driver/t23/tx_isp_t23_awb_cluster.h against the
stock tx-isp-t23.ko (md5 8237acb1).

usage: awb_cluster_emu.py <stock tx-isp-t23.ko> <sensor IQ .bin> [<built tx-isp-t23.ko with local symbols>]

1. stage: random 15x15 zone statistics, random cluster objects, several runs
   in a row (the histogram and the cluster lists are state).  Per run the
   stock JZ_Isp_Awb is run twice on equal state-free inputs, once with
   ClusterEn off (gives the weights the stage starts from, and the clamped
   zone ratios) and once with the random object; the C code of the header,
   started from the state before the run, must give the same state
   (Cluster_rgbg_index_num / _index_max / _value1 / _value2) and the same
   final zone weights (rgbg_wght).
2. trend: JZ_Isp_Awb_Awbg2reg with random trend objects, status words and
   colour temperatures against t23x_awb_obj_pick + t23x_awb_gain_trend.
3. with the built module: the open AWB run (regtrace_t23_source_awb_hlil_work)
   against stock JZ_Isp_Awb with the cluster object and the trend set in
   both (gains and colour temperature); only meaningful where the open
   runtime matches stock without them (indoor scenes).
Prints "bad 0" per check when identical.
"""
import os, sys, struct, subprocess, tempfile, random
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from memu import Mod, CPU

STOCK, IQ = sys.argv[1:3]
BUILT = sys.argv[3] if len(sys.argv) > 3 else None
S0 = int(os.environ.get('SEED0', '0'))
CLON = os.environ.get('CLON', '1') == '1'
NODIST = os.environ.get('NODIST', '1') == '1'
iq = open(IQ, 'rb').read()
BANK, ACT, HDR = 0x15844, 0x13100, 0x18
DAY = iq[HDR:HDR + BANK]
W = lambda o, n=1: struct.unpack_from('<%dI' % n, iq, o)
POS_RG = W(0x1180, 15)
POS_BG = W(0x11bc, 15)
Q = W(0x10e8)[0]
TMP = tempfile.mkdtemp()
INC = os.path.join(HERE, '..', 'tx_isp_t23_awb_cluster.h')

C_SRC = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "%s"
struct in {
	uint32_t q, en, cl[10], pos_rg[15], pos_bg[15], zr[225], zb[225], w[225];
	struct t23x_awb_cluster_state st;
};
int main(int argc, char **argv)
{
	static struct in in;
	int applied = 0;
	FILE *f = fopen(argv[1], "rb");
	if (!f || fread(&in, sizeof(in), 1, f) != 1) return 1;
	fclose(f);
	t23x_awb_cluster_count(&in.st, in.zr, in.zb, in.pos_rg, in.pos_bg, in.q);
	if (in.cl[0] == 1U)
		applied = t23x_awb_cluster_weights(&in.st, in.cl, in.zr, in.zb, in.w,
						   in.pos_rg, in.pos_bg, in.q);
	f = fopen(argv[2], "wb");
	fwrite(&in.st, sizeof(in.st), 1, f);
	fwrite(in.w, 4, 225, f);
	fwrite(&applied, 4, 1, f);
	fclose(f);
	return 0;
}
''' % INC
GAIN_SRC = r'''
#include <stdio.h>
#include "%s"
int main(void)
{
	uint32_t v[31];
	while (fread(v, 4, 31, stdin) == 31) {
		/* obj[7] api[7] status[2] ct gr gb  (cluster layout reused: only trend) */
		uint32_t *obj = v, *api = v + 7, *st = v + 14, ct = v[16], g[2] = { v[17], v[18] };
		const uint32_t *tr = t23x_awb_obj_pick(obj, api, st);
		printf("%%u %%u\n", t23x_awb_gain_trend(tr, ct, g[0], 0) | 0x04000000U,
		       t23x_awb_gain_trend(tr, ct, g[1], 1) | 0x04000000U);
	}
	return 0;
}
''' % INC
os.chdir(TMP)
open('stage.c', 'w').write(C_SRC)
open('gain.c', 'w').write(GAIN_SRC)
subprocess.check_call(['gcc', '-O1', '-Wall', '-o', 'stage', 'stage.c'])
subprocess.check_call(['gcc', '-O1', '-Wall', '-o', 'gain', 'gain.c'])


def zone_words(r, g, b, pix):
    return [(r & 0x1fffff) | ((g & 0x7ff) << 21),
            ((g >> 11) & 0x3ff) | ((b & 0x1fffff) << 10),
            (pix & 0xfff) << 20, (pix >> 12) & 1]


def fill_stats(c, buf, zones):
    for z, (r, g, b, p) in enumerate(zones):
        for k, w in enumerate(zone_words(r, g, b, p)):
            c.w32(buf + 16 * z + 4 * k, w)


class Stock:
    def __init__(self):
        m = self.m = Mod(STOCK); c = self.c = CPU(m)
        zero = lambda c_, R: 0
        for n in ('system_irq_func_set', 'tisp_event_set_cb', 'private_dma_cache_sync',
                  'tisp_event_push', 'tisp_ae_mean_update'):
            c.hook(n, zero)
        c.wrbytes(m.addr('tparams') + ACT, DAY)
        self.buf = c.heapp; c.heapp += 0x4000
        c.w32(m.addr('tispinfo') + 60, self.buf)
        c.regs_hw[0xb050] = 0
        c.call('tiziano_awb_init', (1080, 1920))

    def arr(self, name, n):
        return list(struct.unpack('<%dI' % n, self.c.rdbytes(self.m.addr(name), 4 * n)))

    def setw(self, name, vals):
        self.c.wrbytes(self.m.addr(name), struct.pack('<%dI' % len(vals), *vals))

    def set_cluster(self, cl):
        self.setw('_awb_cluster', cl)
        self.setw('awb_cluster_api_para', cl)
        self.setw('awb_cluster_api_status', [0, 2])

    def state(self):
        return (self.arr('Cluster_rgbg_index_num', 196) + self.arr('Cluster_rgbg_index_max', 36) +
                self.arr('Cluster_rgbg_value1', 180) + self.arr('Cluster_rgbg_value2', 240))

    def set_state(self, words):
        self.setw('Cluster_rgbg_index_num', words[0:196])
        self.setw('Cluster_rgbg_index_max', words[196:232])
        self.setw('Cluster_rgbg_value1', words[232:412])
        self.setw('Cluster_rgbg_value2', words[412:652])

    def run(self, zones, ev):
        c = self.c
        c.call('tisp_awb_ev_update', (ev << 10, 0))
        fill_stats(c, self.buf, zones)
        c.call('awb_interrupt_static')
        c.call('JZ_Isp_Awb')
        return (c.regs_hw.get(0x1804, 0) & 0x3fff, c.regs_hw.get(0x1808, 0) & 0x3fff,
                c.r32(self.m.addr('_awb_ct')))


def rand_scene(rnd, sparse=True):
    """15x15 zones around a few random colour centres (raw R/G, B/G), some
    empty or sparse zones."""
    centres = []
    for _ in range(rnd.randint(1, 5)):
        rg = rnd.uniform(POS_RG[0] - 15, POS_RG[14] + 25) / 432.0
        bg = rnd.uniform(POS_BG[0] - 15, POS_BG[14] + 25) / 432.0 * 1.1
        centres.append((rg, bg))
    out = []
    for z in range(225):
        rg, bg = rnd.choice(centres)
        rg *= rnd.uniform(0.97, 1.03) if rnd.random() < 0.8 else rnd.uniform(0.7, 1.4)
        bg *= rnd.uniform(0.97, 1.03) if rnd.random() < 0.8 else rnd.uniform(0.7, 1.4)
        # zones of at most the IQ threshold (25) pixels are dropped (both)
        pix = rnd.choice([0, 3, 20, 26, 400, 2000, 2000, 6000, 8000] if sparse else
                         [0, 26, 400, 2000, 2000, 6000, 8000])
        g = int(pix * rnd.uniform(5, 90))
        if rnd.random() < 0.03:
            g = 0
        out.append((min(int(g * rg), 0x1fffff), min(g, 0x1fffff), min(int(g * bg), 0x1fffff), pix))
    return out


def rand_cluster(rnd):
    # (cluster[0] = ClusterEn, [1] mean shift radius^2, [2] merge radius^2,
    #  [3] membership radius^2, [4] convergence, [5] iteration limit)
    return [rnd.choice([1, 1, 1, 0, 2]), rnd.choice([10, 40, 100, 300, 800, 2500]),
            rnd.choice([5, 30, 150, 300, 900, 3000]), rnd.choice([10, 30, 150, 300, 900]),
            rnd.choice([0, 0, 1, 2, 5]), rnd.choice([0, 1, 2, 3, 10]), 0, 0, 0, 32]


def stage_test(nseq, nframes):
    bad = tot = 0
    used = 0
    for seed in range(S0, S0 + nseq):
        rnd = random.Random(1000 + seed)
        A, B = Stock(), Stock()
        cl = rand_cluster(rnd)
        off = list(cl); off[0] = 0
        A.set_cluster(cl); B.set_cluster(off)
        if seed % 3 == 2:       # a random, but plausible, previous state
            st = [rnd.choice([0, 0, 1, 5, 40]) for _ in range(196)]
            st += [rnd.randint(0, 13) for _ in range(24)] + [rnd.choice([0, 0, 3, 90]) for _ in range(12)]
            st += [rnd.choice([0, 0, 170, 250, 400]) for _ in range(120)] + [rnd.choice([0, 0, 1, 7, 30]) for _ in range(60)]
            st += [rnd.choice([0, 0, 170, 250, 400]) for _ in range(120)] + [rnd.choice([0, 0, 1, 7, 30]) for _ in range(60)]
            st += [rnd.choice([0, 0, 1000, 90000]) for _ in range(60)]
            A.set_state(st)
        for f in range(nframes):
            zones = rand_scene(rnd)
            ev = rnd.choice([30, 94, 300, 2000])
            B.run(zones, ev)
            zr = B.arr('zone_rgbg', 450)
            zb = zr[225:450]; zr = zr[:225]
            wpre = B.arr('rgbg_wght', 225)
            pre = A.state()
            A.run(zones, ev)
            post, wpost = A.state(), A.arr('rgbg_wght', 225)
            zr2 = A.arr('zone_rgbg', 450)
            assert zr2[:225] == zr and zr2[225:] == zb
            inw = [Q, cl[0]] + cl + list(POS_RG) + list(POS_BG) + zr + zb + wpre + pre
            open('in.bin', 'wb').write(struct.pack('<%dI' % len(inw), *inw))
            subprocess.check_call(['./stage', 'in.bin', 'out.bin'])
            out = struct.unpack('<%dI' % (652 + 225 + 1), open('out.bin', 'rb').read())
            tot += 1
            ok = list(out[:652]) == post and list(out[652:877]) == wpost
            if cl[0] == 1 and out[877]:
                used += 1
            if not ok:
                bad += 1
                if bad < 4:
                    print('stage mismatch seed', seed, 'frame', f, 'cl', cl, 'ev', ev)
                    for nm, a, b in (('hist', post[0:196], out[0:196]), ('idxmax', post[196:232], out[196:232]),
                                     ('v1', post[232:412], out[232:412]), ('v2', post[412:652], out[412:652]),
                                     ('w', wpost, list(out[652:877]))):
                        d = [i for i in range(len(a)) if a[i] != b[i]]
                        print(' ', nm, 'diff at', d[:8], [(a[i], b[i]) for i in d[:4]])
                break
    print('stage: bad %d / %d runs (cluster stage changed the weights in %d)' % (bad, tot, used))
    return bad


def trend_test(n):
    rnd = random.Random(5)
    m = Mod(STOCK)
    c = CPU(m)
    vec = []
    res = []
    for _ in range(n):
        obj = [rnd.choice([0, 1, 2]) if i == 0 else rnd.choice([0, 700, 1024, 1500, 2300]) for i in range(7)]
        api = [rnd.choice([0, 1, 2]) if i == 0 else rnd.choice([0, 700, 1024, 1500, 2300]) for i in range(7)]
        st = [rnd.choice([0, 1, 2]), rnd.choice([0, 1, 2])]
        ct = rnd.choice([1000, 2999, 3000, 3001, 4000, 4999, 5000, 5001, 8000])
        g = [rnd.choice([100, 600, 1000, 1500, 3000, 4000, 4096]), rnd.choice([100, 600, 1000, 1500, 3000, 4000, 4096])]
        c.wrbytes(m.addr('_awb_trend'), struct.pack('<7I', *obj))
        c.wrbytes(m.addr('awb_trend_api_para'), struct.pack('<7I', *api))
        c.wrbytes(m.addr('awb_trend_api_status'), struct.pack('<2I', *st))
        c.w32(m.addr('_awb_ct'), ct)
        a0 = c.heapp; a1 = a0 + 16; c.heapp += 64
        c.w32(a0, g[0]); c.w32(a0 + 4, g[1])
        c.call('JZ_Isp_Awb_Awbg2reg', (a0, a1))
        res.append('%d %d' % (c.r32(a1), c.r32(a1 + 4)))
        vec += obj + api + st + [ct] + g + [0] * 12
        vec = vec[:len(vec)]
    # the C helper reads 31 words per vector
    raw = b''
    k = 0
    data = b''
    for i in range(n):
        v = vec[i * 31 + 0: i * 31 + 31]
        data += struct.pack('<31I', *v)
    got = subprocess.run(['./gain'], input=data, stdout=subprocess.PIPE, check=True).stdout.decode().split('\n')[:-1]
    bad = sum(1 for a, b in zip(res, got) if a != b)
    print('trend: bad %d / %d' % (bad, n))
    return bad


class Ours:
    """The built module's AWB run (regtrace_t23_source_awb_hlil_work) with
    the IQ AWB block loaded the way regtrace_t23_source_awb_hlil_load_tuning
    does."""
    OFF = {
        0x10f8: ('regtrace_t23_awb_hlil_mf_parameters', 6),
        0x1124: ('regtrace_t23_awb_hlil_wb_static', 2),
        0x112c: ('regtrace_t23_awb_hlil_light_sources', 20),
        0x1180: ('regtrace_t23_awb_hlil_rg_positions', 15),
        0x11bc: ('regtrace_t23_awb_hlil_bg_positions', 15),
        0x1238: ('regtrace_t23_awb_hlil_distance_parameters', 3),
        0x1244: ('regtrace_t23_awb_hlil_indoor_ct_weight_mesh', 225),
        0x15c8: ('regtrace_t23_awb_hlil_color_temperature_mesh', 225),
        0x194c: ('regtrace_t23_awb_hlil_zone_weight_mesh', 225),
        0x1cd0: ('regtrace_t23_awb_hlil_outdoor_ct_weight_mesh', 225),
        0x2054: ('regtrace_t23_awb_hlil_light_source_weight_lut', 514),
    }

    def __init__(self):
        m = self.m = Mod(BUILT); c = self.c = CPU(m)
        for n in ('schedule_work', 'private_dma_cache_sync', 'regtrace_t23_text_check'):
            if n in m.byname or n in m.stubs:
                c.hook(n, lambda c_, R: 0)
        for off, (sym, n) in self.OFF.items():
            c.wrbytes(m.addr(sym), iq[off:off + 4 * n])
        sc = {'regtrace_t23_awb_hlil_pixel_threshold': W(0x10dc)[0],
              'regtrace_t23_awb_hlil_point_position': W(0x10e8)[0],
              'regtrace_t23_awb_hlil_history_window': W(0x10f0)[0],
              'regtrace_t23_awb_hlil_tolerance_default': W(0x10f4)[0],
              'regtrace_t23_awb_hlil_outdoor_ev': W(0x1110)[0],
              'regtrace_t23_awb_hlil_indoor_ev': W(0x1114)[0],
              'regtrace_t23_awb_hlil_startup_ev': W(0x1118)[0] << 10,
              'regtrace_t23_awb_hlil_startup_ct': W(0x111c)[0],
              'regtrace_t23_awb_hlil_light_source_count': W(0x117c)[0],
              'regtrace_t23_source_awb_hlil_tuning_loaded': 1,
              'regtrace_t23_source_awb_profile_rbias': 1024,
              'regtrace_t23_source_awb_profile_bbias': 1024}
        for k, v in sc.items():
            if k not in m.byname:
                continue
            if k == 'regtrace_t23_source_awb_hlil_tuning_loaded':
                c.w8(m.addr(k), v)
            else:
                c.w32(m.addr(k), v)
        self.snap = m.addr('regtrace_t23_awb_hlil_snapshot_data')
        # regtrace_t23_source_awb_hlil_load_tuning: the ramp state from the IQ
        c.wrbytes(m.addr('_awb_mf_para'), iq[0x10f8:0x10f8 + 24])

    def arr(self, name, n):
        return list(struct.unpack('<%dI' % n, self.c.rdbytes(self.m.addr(name), 4 * n)))

    def setw(self, name, vals):
        self.c.wrbytes(self.m.addr(name), struct.pack('<%dI' % len(vals), *vals))

    def set_cluster(self, cl):
        self.setw('_awb_cluster', cl)
        self.setw('awb_cluster_api_para', cl)
        self.setw('awb_cluster_api_status', [0, 2])

    def set_trend(self, tr):
        self.setw('_awb_trend', tr)
        self.setw('awb_trend_api_para', tr)
        self.setw('awb_trend_api_status', [0, 2])

    def run(self, zones, ev):
        c, m = self.c, self.m
        c.w32(m.addr('regtrace_t23_source_ae_hlil_ev'), ev << 10)
        for z, (r, g, b, p) in enumerate(zones):
            c.w32(self.snap + 4 * z, r)
            c.w32(self.snap + 4 * (225 + z), g)
            c.w32(self.snap + 4 * (450 + z), b)
            c.w32(self.snap + 4 * (675 + z), p)
        c.call('regtrace_t23_source_awb_hlil_work', (0,))
        return (c.r32(m.addr('regtrace_t23_source_awb_last_rgain')),
                c.r32(m.addr('regtrace_t23_source_awb_last_bgain')),
                c.r32(m.addr('regtrace_t23_source_awb_hlil_ct')))


def integrated_test(nseq, nframes):
    """weights (zones the open run uses) and gains, stock vs built module."""
    wbad = gbad = tot = used = 0
    for seed in range(nseq):
        rnd = random.Random(7000 + seed)
        st, ou = Stock(), Ours()
        cl = rand_cluster(rnd)
        cl[0] = 1 if CLON else 0
        st.set_cluster(cl); ou.set_cluster(cl)
        if rnd.random() < 0.6:
            tr = [1] + [rnd.choice([0, 800, 1000, 1024, 1100, 1300, 2000]) for _ in range(6)]
            st.setw('_awb_trend', tr)
            st.setw('awb_trend_api_para', tr)
            st.setw('awb_trend_api_status', [0, 2])
            ou.set_trend(tr)
        if NODIST:
            # the distance refinement of the open runtime differs from stock
            # (not part of this audit): switch it off in both
            st.setw('_awb_dis_tw', [0, 2048, 1536])
            ou.setw('regtrace_t23_awb_hlil_distance_parameters', [0, 2048, 1536])
        for f in range(nframes):
            zones = rand_scene(rnd, sparse=False)
            ev = rnd.choice([30, 94, 300, 2000])
            a = st.run(zones, ev)
            b = ou.run(zones, ev)
            sw = st.arr('rgbg_wght', 225)
            ow = ou.arr('regtrace_t23_awb_hlil_mesh_weight', 225)
            op = ou.arr('regtrace_t23_awb_hlil_pixel_weight', 225)
            tot += 1
            bad = [i for i in range(225) if op[i] and sw[i] != ow[i]]
            if ou.arr('regtrace_t23_awb_cluster_state', 652)[:196] != st.state()[:196]:
                wbad += 1
                print('integrated: histogram differs, seed', seed, 'frame', f)
            elif bad:
                wbad += 1
                if wbad < 4:
                    print('integrated: weights differ seed', seed, 'frame', f, 'ev', ev, 'cl', cl,
                          [(i, sw[i], ow[i]) for i in bad[:5]])
            if a != b:
                gbad += 1
                if gbad < 4:
                    print('integrated: gains stock', [hex(x) for x in a], 'ours', [hex(x) for x in b],
                          'seed', seed, 'frame', f, 'ev', ev, 'cl', cl)
    print('integrated: weight/state bad %d, gain bad %d / %d runs' % (wbad, gbad, tot))
    return wbad


def load_test(n):
    """IQ bank load: stock tiziano_awb_params_refresh vs
    regtrace_t23_awb_api_load_bank (cluster / trend objects, api_para, status
    words) from random banks and random previous state."""
    bad = 0
    for seed in range(n):
        rnd = random.Random(300 + seed)
        bank = bytearray(DAY)
        for off, words in ((0x2844, 1), (0x284c, 10), (0x2874, 1), (0x287c, 7)):
            for i in range(words):
                struct.pack_into('<I', bank, off + 4 * i,
                                 rnd.choice([0, 1, 2, 300, 1024, rnd.getrandbits(32)]))
        st, ou = Stock(), Ours()
        prev = {nm: [rnd.getrandbits(32) for _ in range(nw)] for nm, nw in
                (('cl', 10), ('clapi', 10), ('clst', 2), ('tr', 7), ('trapi', 7), ('trst', 2))}
        for e in (st, ou):
            e.setw('_awb_cluster', prev['cl']); e.setw('awb_cluster_api_para', prev['clapi'])
            e.setw('awb_cluster_api_status', prev['clst'])
            e.setw('_awb_trend', prev['tr']); e.setw('awb_trend_api_para', prev['trapi'])
            e.setw('awb_trend_api_status', prev['trst'])
        st.c.wrbytes(st.m.addr('tparams') + ACT, bytes(bank))
        st.c.call('tiziano_awb_params_refresh')
        bp = ou.c.heapp; ou.c.heapp += 0x20000
        ou.c.wrbytes(bp, bytes(bank))
        ou.c.w32(ou.m.addr('regtrace_t23_source_active_bank'), bp)
        ou.c.call('regtrace_t23_awb_api_load_bank')
        names = (('_awb_cluster', 10), ('awb_cluster_api_para', 10), ('awb_cluster_api_status', 2),
                 ('_awb_trend', 7), ('awb_trend_api_para', 7), ('awb_trend_api_status', 2))
        if any(st.arr(nm, nw) != ou.arr(nm, nw) for nm, nw in names):
            bad += 1
            if bad < 3:
                for nm, nw in names:
                    print(' ', nm, st.arr(nm, nw), ou.arr(nm, nw))
    print('load: bad %d / %d' % (bad, n))
    return bad


if __name__ == '__main__':
    tot = trend_test(2000)
    tot += stage_test(int(os.environ.get('SEQ', '30')), int(os.environ.get('FRAMES', '5')))
    if BUILT:
        tot += load_test(int(os.environ.get('LOADS', '40')))
        tot += integrated_test(int(os.environ.get('ISEQ', '8')), int(os.environ.get('IFRAMES', '5')))
    print('total bad', tot)
    sys.exit(1 if tot else 0)
