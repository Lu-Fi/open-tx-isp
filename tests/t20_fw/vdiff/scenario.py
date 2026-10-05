"""Scenario (mirror of the OEM run of tests/t20_fw/harness.c) for one image."""
import struct
from emu import CALIB, SP0, u32

LT = 12  # sizeof(LookupTable)
NCAL = 277

def setup_calibrations(m, calib_bin):
    """apicalCalibrations = SDK defaults, then <sensor>.bin day set (calib.c)."""
    V = m.V
    cal = V.sym['apicalCalibrations'][0]
    m.call(V.sym['get_dynamic_calibrations'][0], (cal,))
    m.call(V.sym['get_static_calibrations'][0], (cal,))
    m.calib_banks = None
    if not calib_bin:
        return
    d = open(calib_bin, 'rb').read()
    assert d[:4] == b'1.38' and d[8:15] == b'header0', 'bad calib header'
    size, = struct.unpack_from('<I', d, 16)
    cur = 24
    fw = CALIB
    banks = [dict(), dict()]
    for i in range(NCAL):
        c = m.r32(cal + 4 * i)
        if not c or not m.r32(c):
            continue
        rows, cols, width = struct.unpack_from('<HHH', d, cur + 4)
        sz = rows * cols * width
        day = d[cur + LT:cur + LT + sz]
        cur += sz + LT
        rows1, cols1, width1 = struct.unpack_from('<HHH', d, cur + 4)
        night = d[cur + LT:cur + LT + sz]
        cur += sz + LT
        m.uc.mem_write(fw, day)
        m.w32(c, fw)
        m.uc.mem_write(c + 4, struct.pack('<HHH', rows, cols, width))
        fw += (sz + 3) & ~3
        banks[0][i] = day
        banks[1][i] = night
    m.calib_used = fw
    m.calib_banks = banks


BANK_NAMES = """NP_LUT_MEAN EVTOLUX_PROBABILITY_ENABLE AE_EXPOSURE_AVG_COEF IRIDIX_AVG_COEF
AF_MIN_TABLE AF_MAX_TABLE AF_WINDOW_RESIZE_TABLE EXP_RATIO_TABLE CCM_ONE_GAIN_THRESHOLD FLASH_RG
FLASH_BG AE_BALANCED_LINEAR AE_BALANCED_WDR AE_CORRECTION_FS_HDR AE_CORRECTION_LINEAR
AE_EXPOSURE_CORRECTION DEMOSAIC_LINEAR DEMOSAIC_NP_OFFSET_FS_HDR DEMOSAIC_NP_OFFSET_LINEAR
DP_SLOPE_FS_HDR DP_SLOPE_LINEAR DP_THRESHOLD_FS_HDR DP_THRESHOLD_LINEAR EVTOLUX_EV_LUT_FS_HDR
EVTOLUX_EV_LUT_LINEAR EVTOLUX_LUX_LUT IRIDIX_BLACK_PRC IRIDIX_EV_LIM_FULL_STR
IRIDIX_EV_LIM_NO_STR_FS_HDR IRIDIX_EV_LIM_NO_STR_LINEAR IRIDIX_GAIN_MAX IRIDIX_MIN_MAX_STR
IRIDIX_STRENGTH_MAXIMUM_LINEAR IRIDIX_STRENGTH_MAXIMUM_WDR IRIDIX_STRENGTH_TABLE
MESH_SHADING_STRENGTH NOISE_PROFILE_LINEAR RGB2YUV_CONVERSION SATURATION_STRENGTH_LINEAR
SHARP_ALT_D_FS_HDR SHARP_ALT_D_LINEAR SHARP_ALT_UD_FS_HDR SHARP_ALT_UD_LINEAR SHARPEN_DS1_LINEAR
SHARPEN_DS1_WDR SHARPEN_FR_LINEAR SHARPEN_FR_WDR SINTER_STRENGTH1_FS_HDR SINTER_STRENGTH1_LINEAR
SINTER_STRENGTH_FS_HDR SINTER_STRENGTH_LINEAR SINTER_THRESH1_FS_HDR SINTER_THRESH1_LINEAR
SINTER_THRESH4_FS_HDR SINTER_THRESH4_LINEAR STITCHING_ERROR_THRESH TEMPER_STRENGTH""".split()


def bank_ids(sdk):
    """(api id, table index) pairs of calib.c calib_bank_ids[] from the SDK headers"""
    import re
    txt = open(sdk + '/include/apical-isp/apical_calibrations_id.h').read()
    idx = {}
    for en in ('enum EWDRModeID', 'enum ECalibrationID'):
        body = txt[txt.index(en):]
        body = body[body.index('{') + 1:body.index('}')]
        body = re.sub(r'//[^\n]*|/\*.*?\*/', '', body, flags=re.S)
        n = 0
        for item in body.split(','):
            item = item.strip()
            if not item:
                continue
            if '=' in item:
                name, val = [x.strip() for x in item.split('=', 1)]
                n = eval(re.sub(r'\b[A-Za-z_]\w*\b', lambda mm: str(idx[mm.group(0)]), val))
            else:
                name = item
            idx[name] = n
            n += 1
        idx['WDR_MODE_POSITION'] = idx.get('WDR_MODE_FS_HDR', 1)    # #define
    api = {}
    for m in re.finditer(r'#define\s+(CALIBRATION_\w+)\s+(0x[0-9a-fA-F]+|\d+)',
                         open(sdk + '/apical-isp/apical_command_api.h').read()):
        api[m.group(1)] = int(m.group(2), 0)
    return [(api['CALIBRATION_' + b], idx['_CALIBRATION_' + b]) for b in BANK_NAMES]


class Runner:
    """Drives one image (V or O) of a Machine through scenario steps."""

    def __init__(self, m, img):
        self.m, self.img = m, img
        self.F = lambda n: img.sym[n][0]

    def irq(self, idx):
        h = self.m.env.irq.get(idx)
        if h:
            self.m.call(h[0], (h[1],))

    def step(self, st):
        m, env = self.m, self.m.env
        k = st[0]
        if k == 'init':
            m.call(self.F('apical_init'))
        elif k == 'frame':
            env.frame_no += 1
            env.fill_stats()
            for i in (7, 3, 4, 5, 6, 12, 0):
                self.irq(i)
                if m.fault:
                    return
            for f in ('apical_process', 'apical_cmd_process', 'apical_process', 'apical_cmd_process'):
                m.call(self.F(f))
                if m.fault:
                    return
        elif k == 'cmd':
            t, i, v, d = st[1:]
            # *ret lives above the stack pointer so that the
            # function-level comparison (caller stack) covers it
            retp = SP0 + 0x100
            m.w32(retp, 0xdeadbeef)
            r = m.call(self.F('apical_command'), (t, i, v, d, retp))
            env.log.append(('CMD', t, i, v, d, r[0], m.r32(retp)))
        elif k == 'scene':
            env.scene_ev, env.ct_bias = st[1], st[2]
        elif k == 'scene_add':
            env.scene_ev += st[1]
        elif k == 'calib':
            # calib.c calib_switch_set(): bank tables through the API
            banks = getattr(m, 'calib_banks', None)
            if not banks:
                return
            buf = m.calib_used + 0x100
            retp = SP0 + 0x100
            for api, idx in m.bank_ids:
                t = banks[st[1]].get(idx)
                if t is None:
                    continue
                m.uc.mem_write(buf, t)
                m.w32(retp, 0)
                r = m.call(self.F('apical_api_calibration'), (api, 0, buf, len(t), retp))
                env.log.append(('CALSET', api, st[1], r[0], m.r32(retp)))
                if m.fault:
                    return
        elif k == 'ct':
            env.ct_bias = st[1]
        else:
            raise Exception(k)


def scenario(full=True):
    """List of steps, same order as harness.c main() (OEM mode)."""
    S = [('init',), ('scene', 22 << 16, 40)]
    S += [('frame',)] * 40
    S += [('cmd', 3, 0x5b, 160, 0), ('cmd', 3, 0x5b, 0, 1), ('cmd', 3, 0x68, 50, 0),
          ('cmd', 3, 0x56, 0, 1), ('cmd', 3, 0x60, 0, 1), ('cmd', 1, 0x25, 160, 0),
          ('cmd', 1, 0x1e, 0, 1)]
    S += [('frame',)] * 15
    for f in range(30):
        S += [('scene_add', -(1 << 15)), ('frame',)]
    if not full:
        return S
    S += [('calib', 1), ('ct', 0)]
    S += [('frame',)] * 30
    S += [('scene', 23 << 16, 0)] + [('frame',)] * 15
    S += [('calib', 0), ('ct', -30), ('cmd', 3, 0x5b, 128, 0), ('cmd', 3, 0x68, 0, 0)] + [('frame',)] * 30
    S += [('cmd', 1, 0x0d, 1, 0), ('cmd', 1, 0x22, 300, 0), ('cmd', 1, 0x24, 40, 0)] + [('frame',)] * 5
    S += [('cmd', 1, 0x0d, 0, 0)]
    for mode in (0x26, 0x27, 0x28, 0x2a, 0x29, 0x25):
        S += [('cmd', 3, 0x56, mode, 0), ('cmd', 3, 0x58, 64, 0), ('cmd', 3, 0x59, 2000, 0)]
        S += [('frame',)] * 4 + [('cmd', 3, 0x58, 0, 1), ('cmd', 3, 0x59, 0, 1)]
    S += [('cmd', 3, 0x5c, 1, 0)] + [('frame',)] * 3 + [('cmd', 3, 0x5c, 0, 0), ('cmd', 3, 0x68, 60, 0)]
    S += [('frame',)] * 6
    # API sweep: every GET, then SETs with a few values (harness.c order)
    for t in range(7):
        for i in range(0x100):
            S.append(('cmd', t, i, 0, 1))
    for t in range(1, 5):
        for i in range(0x80):
            if t == 1 and i in (0x0c, 0x0a, 0x0b):
                continue        # freeze firmware / test pattern
            if t == 2 and 0x3f <= i <= 0x4d:
                continue        # resolution / fps / wdr / crop: re-init
            for v in (0, 1, 2, 0x32, 0x80, 0xff):
                S.append(('cmd', t, i, v, 0))
            if (i & 15) == 15:
                S.append(('frame',))
    S += [('frame',)] * 10
    return S
