"""Extra vdiff scenarios (openimp #539 vendor proof, 2026-11-16): appended after the default run with
`vdiff.py --ext night,dn,wdr,flicker,tuning` (any subset).  They only drive the firmware through the
same step kinds as scenario.py (apical_command traffic, calibration bank switch, scene / CT changes,
frames with their interrupts), with values a camera sends, so that functions the default scenario
reaches rarely or with few values run with more states.  Command ids: SDK apical_command_api.h
(TSYSTEM 1, TIMAGE 2, TALGORITHMS 3, TSCENE_MODES 4; set = d 0, get = d 1)."""

TSYSTEM, TIMAGE, TALG, TSCENE = 1, 2, 3, 4


def _b():
    S = []
    cmd = lambda t, i, v, d=0: S.append(('cmd', t, i, v, d))
    fr = lambda n=1, *irqs: S.extend([('frame',) + irqs] * n)
    return S, cmd, fr


def night_steps():
    """night bank, a dark scene (AE at high gains), manual exposure / gain points, NR strengths at night"""
    S, cmd, fr = _b()
    S += [('calib', 1), ('ct', -40), ('scene', 10 << 16, -40)]
    fr(40)
    for ev in (8, 6, 4):
        S.append(('scene', ev << 16, -40))
        fr(12)
    cmd(TSYSTEM, 0x0d, 1)                       # SYSTEM_MANUAL_EXPOSURE on
    for it, ag in ((1100, 0x60), (600, 0xa0), (1121, 0xe0), (200, 0x20)):
        cmd(TSYSTEM, 0x22, it)                  # SYSTEM_INTEGRATION_TIME
        cmd(TSYSTEM, 0x24, ag)                  # SYSTEM_SENSOR_ANALOG_GAIN
        fr(4)
    cmd(TSYSTEM, 0x0d, 0)
    for i in (0x6d, 0x6f):                      # SINTER / TEMPER strength at night
        for v in (0, 64, 128, 200, 255):
            cmd(TALG, i, v)
            fr(2)
            cmd(TALG, i, 0, 1)
    S += [('scene', 12 << 16, -40)]
    fr(10)
    S += [('calib', 0), ('ct', 0), ('scene', 22 << 16, 40)]
    fr(20)
    return S


def dn_steps():
    """day / night switches: bank switch with scene and CT jumps, a few frames each, four cycles"""
    S, cmd, fr = _b()
    for k in range(4):
        S += [('calib', 1), ('ct', -60), ('scene', (6 + k) << 16, -60)]
        fr(8)
        S += [('calib', 0), ('ct', 20 * k), ('scene', (20 + k) << 16, 30)]
        fr(8)
    S += [('calib', 1), ('ct', -40)]
    fr(1)
    S += [('calib', 0), ('ct', 0), ('scene', 22 << 16, 40)]   # switch back after one frame
    fr(15)
    return S


def wdr_steps():
    """WDR FS-HDR for a longer run (WDR buffer interrupt 10), scene steps, exposure ratio and iridix in WDR"""
    S, cmd, fr = _b()
    cmd(TIMAGE, 0x45, 0x0f)                     # WDR mode FS-HDR
    fr(20, 10)
    for ev in (28, 16, 24, 10, 22):
        S.append(('scene', ev << 16, 30))
        fr(8, 10)
    for r in (2, 8, 16, 32):                    # SYSTEM_MANUAL_EXPOSURE_RATIO / SYSTEM_EXPOSURE_RATIO / MAX
        cmd(TSYSTEM, 0x0e, 1)
        cmd(TSYSTEM, 0x20, r << 6)
        cmd(TSYSTEM, 0x21, r << 6)
        fr(4, 10)
    cmd(TSYSTEM, 0x0e, 0)
    for v in (0, 100, 200, 255):                # IRIDIX strength in WDR
        cmd(TALG, 0x6a, v)
        fr(3, 10)
    S += [('calib', 1), ('scene', 8 << 16, -30)]
    fr(10, 10)                                  # WDR at night
    S += [('calib', 0), ('scene', 22 << 16, 40)]
    fr(6, 10)
    cmd(TIMAGE, 0x45, 0x0e)                     # back to linear
    fr(10)
    return S


def flicker_steps():
    """antiflicker 50 / 60 / off with brightness ramps so the exposure crosses the flicker steps"""
    S, cmd, fr = _b()
    for hz in (50, 60, 0, 50):
        cmd(TALG, 0x68, hz)
        cmd(TSYSTEM, 0x23, 1121 if hz != 60 else 1000)   # SYSTEM_MAX_INTEGRATION_TIME
        for ev in (24, 20, 16, 12, 9, 12, 18, 23):
            S.append(('scene', ev << 16, 40))
            fr(5)
        cmd(TALG, 0x68, 0, 1)
    cmd(TALG, 0x68, 0)
    S.append(('scene', 22 << 16, 40))
    fr(10)
    return S


def tuning_steps():
    """tuning API sweeps with in-range values while the stream runs (TALGORITHMS strengths, TSYSTEM targets)"""
    S, cmd, fr = _b()
    for i in (0x76, 0x77, 0x78, 0x79, 0x6d, 0x6f, 0x6a, 0x5b):   # sharpening brightness contrast saturation sinter temper iridix comp
        for v in (0, 32, 96, 128, 160, 224, 255, 128):
            cmd(TALG, i, v)
            fr(2)
            cmd(TALG, i, 0, 1)
    for i in range(0x2a, 0x39):                 # SYSTEM_*_TARGET / MAXIMUM / MINIMUM (sharpening, iridix, sinter, temper)
        for v in (0, 40, 128, 255):
            cmd(TSYSTEM, i, v)
            fr(1)
            cmd(TSYSTEM, i, 0, 1)
    for i, vals in ((0x13, (0, 64, 255)), (0x14, (0, 64, 255)), (0x15, (0, 128, 255)), (0x18, (0, 100)),
                    (0x19, (0, 100)), (0x1c, (0, 128, 255))):   # SYSTEM_MANUAL_* switches
        for v in vals:
            cmd(TSYSTEM, i, v)
            fr(2)
        cmd(TSYSTEM, i, 0)
    for v in (0, 1, 0x100, 0x7fff):             # AE_INTEGRATOR_SET (set_integrator_ae)
        cmd(TALG, 0x5d, v)
        fr(2)
    for v in (8, 9, 8):                         # FPS_MODE (FPS25 / FPS30 enum values of driver_steps)
        cmd(TIMAGE, 0x44, v)
        fr(2)
        cmd(TIMAGE, 0x44, 0, 1)
    for v in (0x0c, 0x0d):                      # ORIENTATION_HFLIP ENABLE / DISABLE
        cmd(TIMAGE, 0x47, v)
        fr(2)
        cmd(TIMAGE, 0x47, 0, 1)
    fr(6)
    return S


EXT = {'night': night_steps, 'dn': dn_steps, 'wdr': wdr_steps, 'flicker': flicker_steps, 'tuning': tuning_steps}


def steps(names):
    S = []
    for n in names:
        S += [('mark', 'ext:' + n)] + EXT[n]()
    return S
