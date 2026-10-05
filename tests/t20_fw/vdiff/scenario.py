"""Scenario (mirror of the OEM run of tests/t20_fw/harness.c) for one image."""
import struct
from emu import CALIB, SEQ, SEQ_SIZE, SP0, u32

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


def setup_sequence(m, sdk):
    """apical_custom_sequence() of the SDK (apical_custom_initialization.c):
    SENSOR_ISP_SEQUENCE_DEFAULT from isp_config_seq.h, the table the driver
    hands the firmware on every camera.  apical_init() loads sequence 2,
    general_set_wdr_mode() sequences 0 and 1."""
    import re
    if not sdk:
        return
    t = open(sdk + '/apical-isp/isp_config_seq.h').read()
    body = t[t.index('#define SENSOR_ISP_SEQUENCE_DEFAULT '):].split('\n')[1:]
    s = ''
    for line in body:
        line = line.strip()
        if not line.startswith('"'):
            break
        s += ''.join(re.findall(r'"([^"]*)"', line))
    assert re.fullmatch(r'(\\x[0-9a-fA-F]{2})*', s), 'unexpected escape in isp_config_seq.h'
    b = bytes(int(x, 16) for x in re.findall(r'\\x([0-9a-fA-F]{2})', s))
    assert 8 < len(b) <= SEQ_SIZE
    m.uc.mem_write(SEQ, b)
    m.env.seq_ptr = SEQ


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
            for i in (7, 3, 4, 5, 6, 12, 0) + tuple(st[1:]):
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
        elif k == 'irq':
            self.irq(st[1])
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
    S += driver_steps()
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


def driver_steps():
    """apical_command() traffic of the driver (driver/t20/sdk: tx-isp-core.c,
    tx-isp-core-tuning.c) with the exact values it sends: stream pause/run,
    output formats, crop and scaler per channel, vflip, sensor fps, WDR
    FS-HDR on/off with the WDR buffer interrupt, test pattern and the enum
    values of the v4l2 controls.  The API sweep below only uses 0/1/2/0x32/
    0x80/0xff and skips type 2 ids 0x3f..0x4d, so none of these are reached
    there."""
    S = []
    cmd = lambda t, i, v, d=0: S.append(('cmd', t, i, v, d))
    fr = lambda n=1, *irqs: S.extend([('frame',) + irqs] * n)
    # stream off/on: ISP_SYSTEM_STATE PAUSE / RUN
    cmd(1, 0x09, 0)
    fr(2)
    cmd(1, 0x09, 1)
    fr(2)
    # output format per channel: FR/DS1/DS2_OUTPUT_MODE_ID YUV420/YUV422/YUV444/RGB
    for i in (0x73, 0x74, 0x75):
        for v in (0x57, 0x56, 0x55, 0x54, 0x57):
            cmd(4, i, v)
            cmd(4, i, 0, 1)
        fr()
    # crop (isp_core_frame_channel_set_crop): width, height, left, top, ENABLE
    for chan, (w, h, x, y) in ((0x13, (1280, 720, 320, 180)), (0x14, (640, 360, 0, 0)),
                               (0x16, (640, 352, 16, 8))):
        for i, v in ((0x4a, w), (0x4b, h), (0x4c, x), (0x4d, y), (0x49, 0x0c)):
            cmd(2, i, (chan << 16) + v)
        fr(3)
        for i in (0x49, 0x4a, 0x4b, 0x4c, 0x4d):
            cmd(2, i, chan << 16, 1)
    # scaler (isp_core_frame_channel_set_scaler): DISABLE, width, height, ENABLE
    for chan, (w, h) in ((0x15, (640, 360)), (0x17, (320, 180)), (0x15, (1280, 720))):
        for i, v in ((0x49, 0x0d), (0x4a, w), (0x4b, h), (0x49, 0x0c)):
            cmd(2, i, (chan << 16) + v)
        fr(3)
    for chan in (0x13, 0x14, 0x15, 0x16, 0x17):
        cmd(2, 0x49, (chan << 16) + 0x0d)
    fr(2)
    # vflip ENABLE / DISABLE
    for v in (0x0c, 0x0d):
        cmd(2, 0x48, v)
        fr(2)
        cmd(2, 0x48, 0, 1)
    # sensor fps change: SENSOR_FPS_MODE_ID FPS25
    cmd(2, 0x40, 8)
    fr(2)
    cmd(2, 0x40, 0, 1)
    # WDR FS-HDR on (with the WDR buffer interrupt, IRQ 10), then linear
    cmd(2, 0x45, 0x0f)
    fr(6, 10)
    cmd(2, 0x45, 0, 1)
    fr(4, 10)
    cmd(2, 0x45, 0x0e)
    fr(4)
    cmd(2, 0x45, 0, 1)
    # test pattern: MODE, ENABLE ON, ... ENABLE OFF
    cmd(1, 0x0b, 2)
    cmd(1, 0x0a, 2)
    fr(2)
    cmd(1, 0x0a, 3)
    fr(2)
    # v4l2 control enums
    for t, i, vals in ((3, 0x60, (0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x33, 0x32)),  # AWB mode
                       (3, 0x56, (0x26, 0x25)),                        # AE mode
                       (3, 0x69, (0x3c, 0x3d, 0x3e, 0x3f, 0x3b, 0x03, 0x3c)),  # iridix (raw drc)
                       (3, 0x57, (0x2a, 0x29)),                        # AE split preset
                       (3, 0x5f, (0x2f,)), (3, 0x5e, (0x2e, 0x2d)),    # antifog
                       (3, 0x6c, (0x3b, 0x40)),                        # sinter mode
                       (3, 0x5c, (0x2c,)), (3, 0x67, (0x2c,)),         # AE/AWB unfreeze
                       (3, 0x68, (50, 60, 0)),                         # antiflicker
                       (3, 0x6b, (1, 0)),                              # DIS (v4l2 0..1)
                       (4, 0x71, tuple(range(0x40, 0x4e)) + (0x40,)),  # scene
                       (4, 0x72, (0x50, 0x51, 0x52, 0x53, 0x4f)),      # colorfx
                       (1, 0x19, (1, 0)), (1, 0x0f, (1, 0))):          # manual temper / integration
        for v in vals:
            cmd(t, i, v)
            fr()
            cmd(t, i, 0, 1)
    fr(4)
    return S
