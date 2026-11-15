"""Randomised inputs for side-effect-free helpers (vendor vs ours)."""
import random
import struct
from emu import HEAP, SP0, u32

# name: argument kinds
#   u32/s32: random width; u8; sh: 0..31; n16: 0..16; i16: signed 16
#   t16:<len>/t32:<len>: pointer to a sorted random table of that many
#   (x,y) entries (len given by the next 'len' argument when 'L')
PURE = {
    'leading_one_position': ['u32'],
    'math_log2': ['u32', 'n16', 'n16'],
    'log2_int_to_fixed': ['u32', 'n16', 'n16'],
    # mostly in-domain values (integer part <= 30 - output precision) and
    # small negative ones (exposure differences), plus random words
    'math_exp2': ['exp2'],
    'sqrt32': ['u32'],
    'sqrt16': ['i16'],
    'log16': ['u32'],
    'solving_lin_equation_a': ['s32', 's32', 's32', 's32', 'n16'],
    'div_fixed': ['s32', 's32', 'n16'],
    'apical_cosine': ['s32'],
    'apical_sine': ['s32'],
    'interpl': ['s32', 's32', 's32', 's32', 's32'],
    'calc_modulation_u16': ['u16', 'mod16', 'L'],
    'calc_modulation_u32': ['u32', 'mod32', 'L'],
    'calc_equidistant_modulation_u16': ['u16', 'tab16', 'L'],
    'calc_equidistant_modulation_u32': ['u32', 'tab32', 'L'],
    'calc_inv_equidistant_modulation_u32': ['u32', 'tab32s', 'L'],
}


def rnd(kind, r):
    if kind in ('u32', 's32'):
        return r.getrandbits(r.randint(0, 32))
    if kind == 'u16':
        return r.getrandbits(r.randint(0, 16))
    if kind == 'u8':
        return r.getrandbits(8)
    if kind == 'sh':
        return r.randint(0, 31)
    if kind == 'n16':
        return r.randint(0, 16)
    if kind == 'i16':
        return u32(r.randint(-32768, 32767))
    raise Exception(kind)


def make_args(m, kinds, r):
    args, n = [], r.randint(1, 12)
    p = HEAP + 0x1000
    if kinds == ['exp2']:
        ip, op = r.randint(0, 16), r.randint(0, 16)
        k = r.randint(0, 2)
        if k == 0:
            v = (r.randint(0, 30 - op) << ip) | r.getrandbits(ip)
        elif k == 1:
            v = u32(-r.getrandbits(ip + 4))
        else:
            v = r.getrandbits(32)
        return [v, ip, op]
    for k in kinds:
        if k == 'L':
            args.append(n)
        elif k in ('mod16', 'mod32', 'tab16', 'tab32', 'tab32s'):
            w = 2 if k.endswith('16') else 4
            mx = (1 << (8 * w)) - 1
            if k.startswith('mod'):
                xs = sorted(r.sample(range(0, mx), n)) if n < mx else list(range(n))
                ys = [r.getrandbits(r.randint(1, 8 * w)) for _ in range(n)]
                data = b''.join(struct.pack('<HH' if w == 2 else '<II', x, y) for x, y in zip(xs, ys))
            else:
                ys = [r.getrandbits(r.randint(1, 8 * w)) for _ in range(n)]
                if k == 'tab32s':
                    ys.sort()
                data = struct.pack('<%d%s' % (n, 'H' if w == 2 else 'I'), *ys)
            m.uc.mem_write(p, data)
            args.append(p)
            p += 0x400
        else:
            args.append(rnd(k, r))
    return args


def fuzz(m, names, n=3000, seed=1):
    out = {}
    for name in names:
        if name not in m.V.sym or name not in m.O.sym:
            continue
        r = random.Random(seed)
        kinds = PURE[name]
        bad = []
        for i in range(n):
            args = make_args(m, kinds, r)
            a = m.call(m.V.sym[name][0], args, sp=SP0)[0]
            fa = m.fault
            b = m.call(m.O.sym[name][0], args, sp=SP0)[0]
            fb = m.fault
            if a != b or fa != fb:
                bad.append((args, a, b, fa, fb))
        out[name] = (n, bad)
    return out


def api_names(m):
    """small API accessors system_*(ctx, value, dir, *ret) (<= 0x3c bytes)"""
    return sorted(n for n, (a, s, t) in m.V.sym.items()
                  if t == 'F' and n.startswith('system_') and 0x18 <= s <= 0x3c
                  and n in m.O.sym and n not in ('system_isp_read_32', 'system_isp_read_16',
                                                  'system_isp_read_8', 'system_isp_write_32',
                                                  'system_isp_write_16', 'system_isp_write_8'))


def fuzz_api(m, n=200, seed=2):
    """value/direction/ret/stab behaviour of the API accessors"""
    stab = m.V.sym['stab'][0]
    ssz = m.V.sym['stab'][1]
    retp = HEAP + 0x800
    out = {}
    for name in api_names(m):
        r = random.Random(seed)
        bad = []
        for i in range(n):
            st = bytes(r.getrandbits(8) for _ in range(ssz))
            val = r.getrandbits(32)
            d = r.choice((0, 1, 1, 0, 2, 255, r.getrandbits(32)))
            res = []
            for img in (m.V, m.O):
                m.uc.mem_write(stab, st)
                m.w32(retp, 0xdeadbeef)
                v0 = m.call(img.sym[name][0], (0, val, d, retp), sp=SP0)[0]
                res.append((v0, m.r32(retp), bytes(m.uc.mem_read(stab, ssz)), m.fault))
            if res[0] != res[1]:
                (a, ra, sa, fa), (b, rb, sb, fb) = res
                diff = [k for k in range(ssz) if sa[k] != sb[k]]
                bad.append(([0, val, d, retp], 'ret V=0x%x O=0x%x *ret V=0x%x O=0x%x stab bytes %s%s' % (
                    a, b, ra, rb, diff[:4], (' fault V=%s O=%s' % (fa, fb)) if fa or fb else '')))
        out[name] = (n, bad)
    return out


# sensor bus sample functions (apical_sbus_i2c / apical_sbus_spi):
# (bus, addr[, sample], sample_size); the transfers end in the environment
# stubs I2C_write/I2C_read/spi_rw32/spi_rw48, whose arguments and buffers
# are logged
SBUS = ('i2c_io_read_sample', 'i2c_io_write_sample', 'spi_io_read_sample', 'spi_io_write_sample')
SBUS_BITS = (1, 2, 8, 0x10, 0x20, 0x400, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000)


def fuzz_sbus(m, n=3000, seed=3):
    """bus mode flags x lock state x sample size: same transfers (order,
    arguments, buffer bytes, interrupt bracketing) and same read result"""
    bus, lock = HEAP + 0x2000, HEAP + 0x2040
    env = m.env
    keep = env.io_ret, env.i2c_ret, env.i2c_data
    out = {}
    for name in SBUS:
        if name not in m.V.sym or name not in m.O.sym:
            continue
        r = random.Random(seed)
        rd = '_read_' in name
        bad = []
        for i in range(n):
            mask = sum(b for b in SBUS_BITS if r.random() < 0.4)
            if r.random() < 0.1:
                mask |= r.getrandbits(32)
            words = (mask, r.getrandbits(8) | (r.getrandbits(24) << 8), r.getrandbits(32),
                     r.choice((0, lock)))
            lockw = (r.getrandbits(32), r.choice((0, 0, 1, r.getrandbits(32))))
            addr = r.getrandbits(r.randint(0, 32))
            sample = r.getrandbits(r.randint(0, 32))
            size = r.choice((1, 2, 4, 4, 1, 2, 0, 3))
            args = [bus, addr, size] if rd else [bus, addr, sample, size]
            env.io_ret = r.choice((r.getrandbits(32), r.getrandbits(32), 0xffffffff, 0))
            env.i2c_ret = (r.choice((0, 0, 0, 1)), r.choice((0, 0, 0, 2)))
            env.i2c_data = bytes(r.getrandbits(8) for _ in range(8))
            res = []
            for img in (m.V, m.O):
                for k, w in enumerate(words):
                    m.w32(bus + 4 * k, w)
                m.w32(lock, lockw[0])
                m.w32(lock + 4, lockw[1])
                env.log = []
                v0 = m.call(img.sym[name][0], args, sp=SP0)[0]
                res.append((v0 if rd else 0, env.log, m.fault))
            if res[0] != res[1]:
                (a, la, fa), (b, lb, fb) = res
                k = 0
                while k < min(len(la), len(lb)) and la[k] == lb[k]:
                    k += 1
                bad.append((args, 'mask=0x%x lock=%s ret V=0x%x O=0x%x calls V=%s O=%s%s' % (
                    mask, 'none' if not words[3] else hex(lockw[1]), a, b, la[k:k + 2], lb[k:k + 2],
                    (' fault V=%s O=%s' % (fa, fb)) if fa or fb else '')))
        out[name] = (n, bad)
    env.log = []
    env.io_ret, env.i2c_ret, env.i2c_data = keep
    return out
