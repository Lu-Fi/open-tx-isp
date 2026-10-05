"""MIPS32 LE emulation environment for the T20 firmware differential test.

Loads the vendor module (tx-isp-t20.ko) and our MIPS build of
driver/t20/tx_isp_t20_firmware.c (relocatable objects) into one unicorn
address space.  Our object's undefined symbols bind to the vendor module's
environment functions (calibration access, system_* glue, ...) or to Python
stubs, so both firmware copies run against byte-identical surroundings.
"""
import math
import struct
from unicorn import Uc, UcError, UC_ARCH_MIPS, UC_MODE_MIPS32, UC_MODE_LITTLE_ENDIAN
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_HOOK_INTR
from unicorn.mips_const import *

VBASE = 0x10000000
OBASE = 0x11000000
STUB = 0x1f000000
RET = 0x1fff0000
CALIB = 0x58000000
SEQ = 0x5c000000        # sensor/ISP init sequence (apical_custom_sequence)
SEQ_SIZE = 0x10000
HEAP = 0x60000000
STACK = 0x7f000000
STACK_SIZE = 0x400000
SP0 = STACK + STACK_SIZE - 0x10000
HEAP_SIZE = 0x400000
CALIB_SIZE = 0x100000

A = [UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3]


def u32(x):
    return x & 0xffffffff


def s32(x):
    x &= 0xffffffff
    return x - 0x100000000 if x & 0x80000000 else x


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


class Elf:
    """ELF32 LE relocatable: sections, symbols, relocations."""

    def __init__(self, path):
        d = self.d = open(path, 'rb').read()
        assert d[:4] == b'\x7fELF' and d[4] == 1 and d[5] == 1
        e_shoff, = struct.unpack_from('<I', d, 0x20)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from('<HHH', d, 0x2e)
        self.sh = [struct.unpack_from('<IIIIIIIIII', d, e_shoff + i * e_shentsize) for i in range(e_shnum)]
        st = self.sh[e_shstrndx]
        self.secname = [self._str(st, s[0]) for s in self.sh]
        symi = [i for i, s in enumerate(self.sh) if s[1] == 2][0]
        sy = self.sh[symi]
        strt = self.sh[sy[6]]
        self.syms = []
        for k in range(sy[5] // 16):
            n, val, size, info, oth, shndx = struct.unpack_from('<IIIBBH', d, sy[4] + k * 16)
            self.syms.append((self._str(strt, n) if n else '', val, size, info, shndx))

    def _str(self, tab, off):
        o = tab[4] + off
        return self.d[o:self.d.index(b'\0', o)].decode()


class Image:
    """One relocatable object placed at a base address."""

    def __init__(self, path, base, tag):
        self.elf = e = Elf(path)
        self.base, self.tag = base, tag
        self.secaddr = {}
        self.writable = []          # (addr, size)
        cur = 0
        for i, s in enumerate(e.sh):
            name, typ, flags, addr, off, size, link, info, align, ent = s
            if flags & 2 and size and typ != 0x70000003 and typ != 6 and e.secname[i] not in ('.reginfo', '.MIPS.abiflags'):
                a = max(align, 4)
                cur = (cur + a - 1) // a * a
                self.secaddr[i] = base + cur
                if flags & 1:
                    self.writable.append((base + cur, size))
                cur += size
        self.size = (cur + 0xffff) & ~0xffff
        self.mem = bytearray(self.size)
        for i, a in self.secaddr.items():
            s = e.sh[i]
            if s[1] != 8:
                self.mem[a - base:a - base + s[5]] = e.d[s[4]:s[4] + s[5]]
        # symbols
        self.sym = {}       # name -> (addr, size, type)  defined only
        self.undef = set()
        self.symaddr = []
        for name, val, size, info, shndx in e.syms:
            typ = info & 15
            if shndx == 0:
                if name:
                    self.undef.add(name)
                continue
            if shndx in self.secaddr and name and typ in (1, 2):
                a = self.secaddr[shndx] + val
                bind = info >> 4
                if name not in self.sym or bind == 1:
                    self.sym[name] = (a, size, 'F' if typ == 2 else 'O')
        self.by_addr = sorted((a, s, n, t) for n, (a, s, t) in self.sym.items())
        self._starts = [x[0] for x in self.by_addr]

    def relocate(self, resolve):
        e = self.elf
        for i, s in enumerate(e.sh):
            if s[1] != 9 or s[7] not in self.secaddr:
                continue
            tbase = self.secaddr[s[7]] - self.base
            hipend = []
            for k in range(s[5] // 8):
                r_off, r_info = struct.unpack_from('<II', e.d, s[4] + k * 8)
                typ, si = r_info & 0xff, r_info >> 8
                name, val, size, info, shndx = e.syms[si]
                if shndx == 0:
                    S = resolve(name) if name else 0
                elif shndx in self.secaddr:
                    S = self.secaddr[shndx] + val
                elif shndx == 0xfff1:
                    S = val
                else:
                    S = 0
                o = tbase + r_off
                w, = struct.unpack_from('<I', self.mem, o)
                if typ == 2:
                    struct.pack_into('<I', self.mem, o, u32(w + S))
                elif typ == 4:
                    P = self.base + o
                    t = (((w & 0x3ffffff) << 2) | (P & 0xf0000000)) + S
                    if (t ^ P) & 0xf0000000 and (S & 0xf0000000) != (P & 0xf0000000):
                        raise Exception('R_MIPS_26 out of range %s' % name)
                    struct.pack_into('<I', self.mem, o, (w & 0xfc000000) | ((t >> 2) & 0x3ffffff))
                elif typ == 5:
                    hipend.append((o, S))
                elif typ == 6:
                    lo = s16(w & 0xffff)
                    for ho, hS in hipend:
                        hw, = struct.unpack_from('<I', self.mem, ho)
                        v = u32(hS + ((hw & 0xffff) << 16) + lo)
                        struct.pack_into('<I', self.mem, ho, (hw & 0xffff0000) | (((v + 0x8000) >> 16) & 0xffff))
                    hipend = []
                    struct.pack_into('<I', self.mem, o, (w & 0xffff0000) | (u32(S + lo) & 0xffff))
                elif typ == 0:
                    pass
                else:
                    raise Exception('reloc type %d in %s' % (typ, e.secname[i]))

    def contains(self, a):
        return self.base <= a < self.base + self.size

    def symof(self, a):
        """(name, offset) of the symbol containing address a, or None."""
        import bisect
        i = bisect.bisect_right(self._starts, a) - 1
        while i >= 0:
            sa, sz, n, t = self.by_addr[i]
            if a < sa + max(sz, 1):
                return n, a - sa
            if sa + max(sz, 1) <= a and i < len(self.by_addr) - 1 and self.by_addr[i + 1][0] > a:
                break
            i -= 1
        return None


# vendor functions replaced by Python (environment, not under test)
ENV_FUNCS = [
    'system_isp_read_32', 'system_isp_read_16', 'system_isp_read_8',
    'system_isp_write_32', 'system_isp_write_16', 'system_isp_write_8',
    'sensor_init', 'system_set_interrupt_handler', 'system_init_interrupt',
    'system_hw_interrupts_disable', 'system_hw_interrupts_enable',
    'init_semaphore', 'raise_semaphore', 'wait_semaphore',
    'system_timer_timestamp', 'system_timer_frequency', 'system_timer_init',
    'I2C_init', 'I2C_read', 'I2C_write', 'spi_rw32', 'spi_rw48', 'spi_init_access',
    'init_sensor_interface', 'reset_sensor_interface', 'load_sensor_interface',
    'apical_custom_sequence', 'apical_custom_initialization',
    'init_isp_set', 'preview_set_supported',
]
# vendor object -> our renamed object of identical layout
ALIASES = {'API2FRM_IDX': 't20_api2frm_idx', 'rg_avg': 'fifo_rg_avg', 'gb_avg': 'fifo_gb_avg'}
# our helpers outside the vendor's firmware (stubbed for both)
OUR_ONLY_STUBS = {'t20fw_bug', 'tx_isp_t20_fw_parked', 'tx_isp_t20_simple_ae_apply'}
CB_NAMES = ['hw_reset_disable', 'hw_reset_enable', 'alloc_again', 'alloc_dgain', 'alloc_it',
            'set_it', 'start_changes', 'end_changes', 'set_again', 'set_dgain', 'get_normal_fps',
            'read_black', 'set_mode', 'set_wdr', 'fps_control', 'get_id', 'disable_isp', 'lps']

JX_TOTAL_W, JX_TOTAL_H, JX_MAX_AGAIN = 2560, 1125, 259142


class Env:
    """Hardware/sensor model (mirror of tests/t20_fw/harness.c, OEM mode)."""

    def __init__(self):
        self.regs = bytearray(0x20000)
        self.log = []           # ('W', w, off, val) / ('CB', name, args) / ...
        self.irq = {}
        self.frame_no = 0
        self.scene_ev = 22 << 16
        self.ct_bias = 40
        self.cur_int_time = 500
        self.cur_again = 0
        self.cur_exp_log2 = 0
        self.heapp = HEAP
        self.traps = 0
        self.seq_ptr = 0        # apical_custom_sequence() result

    def clone_state(self):
        return (bytes(self.regs), dict(self.irq), self.frame_no, self.scene_ev, self.ct_bias,
                self.cur_int_time, self.cur_again, self.cur_exp_log2, self.heapp)

    def set_state(self, st):
        (r, irq, self.frame_no, self.scene_ev, self.ct_bias, self.cur_int_time, self.cur_again,
         self.cur_exp_log2, self.heapp) = st
        self.regs[:] = r
        self.irq = dict(irq)

    def exposure(self):
        it = self.cur_int_time or 1
        self.cur_exp_log2 = int(math.log2(it) * 65536) + (self.cur_again << 12) + (2 << 16)

    @staticmethod
    def hist_pack(v):
        e = 0
        if v < 0x1000:
            return v
        while v >= 0x2000 and e < 14:
            v >>= 1
            e += 1
        return ((e + 1) << 12) | (v & 0xfff)

    def fill_stats(self):
        lvl = self.scene_ev + self.cur_exp_log2 - (24 << 16)
        if lvl > (8 << 16):
            mean = 255
        elif lvl < 0:
            mean = 1
        else:
            ip, fr = lvl >> 16, lvl & 0xffff
            mean = ((0x10000 + fr) << ip) >> 16
        mean = max(1, min(255, mean))
        rng = [u32(0x1234567 + self.frame_no * 7919 + self.scene_ev)]

        def prand():
            rng[0] = u32(rng[0] * 1664525 + 1013904223)
            return rng[0] >> 8
        r = self.regs
        for i in range(256):
            d = i - mean
            cnt = (2500 - d * d) * 3 + (prand() & 63) if d * d < 2500 else (prand() & 7)
            if i == 255 and mean > 230:
                cnt += 40000
            struct.pack_into('<I', r, 0x10000 + i * 4, self.hist_pack(cnt))
        for i in range(0x10400, 0x20000, 4):
            rr = u32(mean * 4 + self.ct_bias + (prand() & 15))
            b = u32(mean * 4 - self.ct_bias + (prand() & 15))
            struct.pack_into('<I', r, i, (rr & 0xfff) | ((b & 0xfff) << 16) | (prand() & 0xf000))


class Machine:
    def __init__(self, vendor_path, ours_path):
        self.V = Image(vendor_path, VBASE, 'V')
        self.O = Image(ours_path, OBASE, 'O')
        self.stubs = {}         # addr -> name
        self.stub_by_name = {}
        self.env = Env()
        self.uc = None

        def stub(name):
            if name not in self.stub_by_name:
                a = STUB + 8 * len(self.stub_by_name)
                self.stub_by_name[name] = a
                self.stubs[a] = name
            return self.stub_by_name[name]
        for n in CB_NAMES:
            stub('cb_' + n)

        def vres(name):
            return stub(name)

        def ores(name):
            if name in OUR_ONLY_STUBS or name in ('memcpy', 'memset', 'printk', 'div64_u64',
                                                  'div64_s64', 'usleep_range'):
                return stub(name)
            if name in self.V.sym:
                return self.V.sym[name][0]
            return stub(name)
        self.V.relocate(vres)
        self.O.relocate(ores)
        # patch vendor environment functions to "jr ra; nop" + Python hook
        self.envaddr = {}
        for n in ENV_FUNCS:
            a = self.V.sym[n][0]
            struct.pack_into('<II', self.V.mem, a - VBASE, 0x03e00008, 0)
            self.envaddr[a] = n
        self.stubmem = bytearray(0x10000)
        for i in range(0x10000 // 8):
            struct.pack_into('<II', self.stubmem, i * 8, 0x03e00008, 0)
        self.common = self._common_objects()
        self._build_uc()

    def _common_objects(self):
        """Writable firmware data objects present in both images."""
        out = []
        for vn, on in ALIASES.items():
            if vn in self.V.sym and on in self.O.sym and self.V.sym[vn][1] == self.O.sym[on][1]:
                out.append((on, self.V.sym[vn][0], self.O.sym[on][0], self.O.sym[on][1]))
        wr = lambda img, a: any(s <= a < s + n for s, n in img.writable)
        for n, (a, s, t) in self.O.sym.items():
            if t != 'O' or n not in self.V.sym or n in ALIASES:
                continue
            va, vs, vt = self.V.sym[n]
            # sizes may differ (our tables are padded): compare the prefix
            if vs and s and wr(self.O, a) and wr(self.V, va):
                out.append((n, va, a, min(vs, s)))
        return out

    def _build_uc(self):
        uc = self.uc = Uc(UC_ARCH_MIPS, UC_MODE_MIPS32 + UC_MODE_LITTLE_ENDIAN)
        uc.mem_map(VBASE, self.V.size)
        uc.mem_write(VBASE, bytes(self.V.mem))
        uc.mem_map(OBASE, self.O.size)
        uc.mem_write(OBASE, bytes(self.O.mem))
        uc.mem_map(STUB, 0x10000)
        uc.mem_write(STUB, bytes(self.stubmem))
        uc.mem_map(RET, 0x1000)
        uc.mem_map(CALIB, CALIB_SIZE)
        uc.mem_map(SEQ, SEQ_SIZE)
        uc.mem_map(HEAP, HEAP_SIZE)
        uc.mem_map(STACK, STACK_SIZE)
        uc.hook_add(UC_HOOK_CODE, self._hook_stub, begin=STUB, end=STUB + 0xffff)
        for a in self.envaddr:
            uc.hook_add(UC_HOOK_CODE, self._hook_env, begin=a, end=a)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._hook_unmapped)
        uc.hook_add(UC_HOOK_INTR, self._hook_intr)
        self.fault = None
        self.entry_hooks = {}

    # ---------------------------------------------------------------- memory
    def r32(self, a):
        return struct.unpack('<I', self.uc.mem_read(a, 4))[0]

    def w32(self, a, v):
        self.uc.mem_write(a, struct.pack('<I', u32(v)))

    def r16(self, a):
        return struct.unpack('<H', self.uc.mem_read(a, 2))[0]

    def w16(self, a, v):
        self.uc.mem_write(a, struct.pack('<H', v & 0xffff))

    def cstr(self, a, n=256):
        try:
            b = bytes(self.uc.mem_read(a, n))
        except UcError:
            return '?'
        return b.split(b'\0')[0].decode('latin1')

    def malloc(self, n):
        p = self.env.heapp
        self.env.heapp += (n + 15) & ~15
        return p

    # ---------------------------------------------------------------- hooks
    def _hook_unmapped(self, uc, access, addr, size, value, ud):
        self.fault = 'unmapped access %08x pc=%08x' % (addr, uc.reg_read(UC_MIPS_REG_PC))
        return False

    def _hook_intr(self, uc, intno, ud):
        # teq (division by zero trap of the vendor build) or break
        pc = uc.reg_read(UC_MIPS_REG_PC)
        self.env.traps += 1
        self.env.log.append(('TRAP', intno, self.where(pc)))
        uc.reg_write(UC_MIPS_REG_PC, pc + 4)

    def where(self, a):
        for img in (self.V, self.O):
            if img.contains(a):
                s = img.symof(a)
                return '%s:%s+%x' % (img.tag, s[0], s[1]) if s else '%s:%x' % (img.tag, a)
        return '%08x' % a

    def args(self, n):
        uc = self.uc
        r = [uc.reg_read(x) for x in A[:min(n, 4)]]
        sp = uc.reg_read(UC_MIPS_REG_SP)
        for i in range(4, n):
            r.append(self.r32(sp + 16 + 4 * (i - 4)))
        return r

    def ret(self, v, v1=None):
        self.uc.reg_write(UC_MIPS_REG_V0, u32(v))
        if v1 is not None:
            self.uc.reg_write(UC_MIPS_REG_V1, u32(v1))

    def _hook_env(self, uc, addr, size, ud):
        self._call_py(self.envaddr[addr])

    def _hook_stub(self, uc, addr, size, ud):
        if addr & 7:
            return
        n = self.stubs.get(addr)
        if n is None:
            self.fault = 'call to unknown stub %08x' % addr
            uc.emu_stop()
            return
        self._call_py(n)

    def _call_py(self, n):
        env, uc = self.env, self.uc
        a0, a1, a2, a3 = self.args(4)
        L = env.log
        if n.startswith('system_isp_read_'):
            w = int(n[16:]) // 8
            v = int.from_bytes(env.regs[a0:a0 + w], 'little') if a0 + w <= len(env.regs) else 0
            if a0 <= 0x134 < a0 + w:
                # frame stitch buffer status: busy bits 0..2 read as idle
                # (apical_wdr_fs_isp_setup() polls them after the reset)
                v &= ~(7 << (8 * (0x134 - a0)))
            self.ret(v)
        elif n.startswith('system_isp_write_'):
            w = int(n[17:]) // 8
            v = a1 & ((1 << (8 * w)) - 1)
            L.append(('W%d' % (8 * w), a0, v))
            if a0 + w <= len(env.regs):
                env.regs[a0:a0 + w] = v.to_bytes(w, 'little')
            self.ret(0)
        elif n == 'sensor_init':
            self._sensor_init(a0)
        elif n == 'system_set_interrupt_handler':
            env.irq[a0] = (a1, a2)
            self.ret(0)
        elif n == 'system_timer_timestamp':
            self.ret(env.frame_no * 40000)
        elif n == 'system_timer_frequency':
            self.ret(1000000)
        elif n in ('memcpy', 'memmove'):
            if a2:
                uc.mem_write(a0, bytes(uc.mem_read(a1, a2)))
            self.ret(a0)
        elif n == 'memset':
            if a2:
                uc.mem_write(a0, bytes([a1 & 0xff]) * a2)
            self.ret(a0)
        elif n == 'printk':
            L.append(('PRINTK', self.cstr(a0)))
            self.ret(0)
        elif n in ('div64_u64', 'div64_s64'):
            x, y = a0 | (a1 << 32), a2 | (a3 << 32)
            if n == 'div64_s64':
                x = x - (1 << 64) if x >> 63 else x
                y = y - (1 << 64) if y >> 63 else y
            if y == 0:
                L.append(('DIV0', n))
                q = 0
            else:
                q = abs(x) // abs(y) * (1 if (x < 0) == (y < 0) else -1)
            q &= (1 << 64) - 1
            self.ret(q & 0xffffffff, q >> 32)
        elif n == '__div64_32':
            x = self.r32(a0) | (self.r32(a0 + 4) << 32)
            if a1 == 0:
                L.append(('DIV0', n))
                q, r = 0, 0
            else:
                q, r = divmod(x, a1)
            self.w32(a0, q & 0xffffffff)
            self.w32(a0 + 4, q >> 32)
            self.ret(r)
        elif n in ('__ashldi3', '__lshrdi3', '__ashrdi3'):
            x = a0 | (a1 << 32)
            if n == '__ashldi3':
                v = x << a2
            elif n == '__lshrdi3':
                v = x >> a2
            else:
                v = ((x - (1 << 64)) if x >> 63 else x) >> a2
            v &= (1 << 64) - 1
            self.ret(v & 0xffffffff, v >> 32)
        elif n in ('__udivdi3', '__umoddi3', '__divdi3', '__moddi3'):
            x, y = a0 | (a1 << 32), a2 | (a3 << 32)
            if n in ('__divdi3', '__moddi3'):
                x = x - (1 << 64) if x >> 63 else x
                y = y - (1 << 64) if y >> 63 else y
            if y == 0:
                L.append(('DIV0', n))
                v = 0
            elif n in ('__udivdi3', '__divdi3'):
                v = abs(x) // abs(y) * (1 if (x < 0) == (y < 0) else -1)
            else:
                v = x - y * (abs(x) // abs(y) * (1 if (x < 0) == (y < 0) else -1))
            v &= (1 << 64) - 1
            self.ret(v & 0xffffffff, v >> 32)
        elif n in ('arch_local_irq_save',):
            self.ret(1)
        elif n == 'apical_custom_sequence':
            L.append(('CALL', n))
            self.ret(env.seq_ptr)
        elif n == 'preview_set_supported':
            self.ret(0)
        elif n == 'init_isp_set':
            self.ret(a0)
        elif n == 'tx_isp_t20_fw_parked':
            self.ret(0)
        elif n == 't20fw_bug':
            self.fault = 'BUG()'
            uc.emu_stop()
        elif n.startswith('cb_'):
            self._sensor_cb(n[3:], a0, a1, a2, a3)
        else:
            L.append(('CALL', n))
            self.ret(0)

    # ---------------------------------------------------------------- sensor
    def _sensor_init(self, c):
        self.env.log.append(('SENS', 'init'))
        self.w32(c + 0, 0x0f)
        self.uc.mem_write(c + 4, b'\x40')
        self.w32(c + 24 + 24, JX_MAX_AGAIN)
        self.w32(c + 24 + 28, 0)
        self.uc.mem_write(c + 24 + 50, b'\x02\x02\x02')
        for i, n in enumerate(CB_NAMES):
            self.w32(c + 96 + 4 * i, self.stub_by_name['cb_' + n])
        # tail call apical_sbus_i2c_init(&c->sbus) of the calling image
        ra = self.uc.reg_read(UC_MIPS_REG_RA)
        img = self.O if self.O.contains(ra) else self.V
        self.uc.reg_write(UC_MIPS_REG_A0, c)
        self.uc.reg_write(UC_MIPS_REG_PC, img.sym['apical_sbus_i2c_init'][0])

    def _sensor_cb(self, n, a0, a1, a2, a3):
        env, L = self.env, self.env.log
        if n == 'alloc_again':
            g = max(0, min(s32(a0), JX_MAX_AGAIN))
            q = g & ~0xfff
            self.w16(a1, q >> 12)
            L.append(('CB', n, s32(a0), q))
            self.ret(q)
        elif n == 'alloc_dgain':
            L.append(('CB', n, s32(a0)))
            self.w16(a1 + 2, 0)
            self.ret(0)
        elif n == 'alloc_it':
            t = self.r16(a0)
            t2 = max(2, min(t, JX_TOTAL_H - 5))
            self.w16(a0, t2)
            L.append(('CB', n, t, t2))
        elif n == 'set_it':
            env.cur_int_time = a1 & 0xffff
            env.exposure()
            L.append(('CB', n, a1 & 0xffff))
        elif n == 'set_again':
            env.cur_again = a1
            env.exposure()
            L.append(('CB', n, a1))
        elif n == 'set_dgain':
            L.append(('CB', n, a1))
        elif n == 'get_normal_fps':
            L.append(('CB', n))
            self.ret(25 << 8)
        elif n == 'read_black':
            L.append(('CB', n, a1, a2))
            self.ret(0)
        elif n in ('set_mode', 'fps_control'):
            p = a2
            L.append(('CB', n, a1 & 0xff))
            if n == 'set_mode':
                self.w16(p + 6, 1920)
                self.w16(p + 8, 1080)
                self.uc.mem_write(p, bytes([a1 & 0xff]))
            self.w16(p + 2, JX_TOTAL_W)
            self.w16(p + 4, JX_TOTAL_H)
            self.w32(p + 32, 2)
            for o in (36, 40, 44):
                self.w32(p + o, JX_TOTAL_H - 5)
            self.ret(25)
        elif n == 'get_id':
            L.append(('CB', n))
            self.ret(0x0f23)
        elif n in ('hw_reset_enable', 'hw_reset_disable'):
            L.append(('CB', n))         # void (*)(void): a0/a1 are stale
        elif n == 'lps':
            L.append(('CB', n))
            self.ret(0)
        else:
            L.append(('CB', n, a0, a1))
            self.ret(0)

    # ---------------------------------------------------------------- calls
    def call(self, addr, args=(), sp=SP0, maxins=20_000_000):
        uc = self.uc
        for i, a in enumerate(args[:4]):
            uc.reg_write(A[i], u32(a))
        for i, a in enumerate(args[4:]):
            self.w32(sp + 16 + 4 * i, a)
        uc.reg_write(UC_MIPS_REG_SP, sp)
        uc.reg_write(UC_MIPS_REG_RA, RET)
        self.fault = None
        try:
            uc.emu_start(addr, RET, count=maxins)
        except UcError as e:
            self.fault = self.fault or ('%s at %s' % (e, self.where(uc.reg_read(UC_MIPS_REG_PC))))
        if not self.fault and uc.reg_read(UC_MIPS_REG_PC) != RET:
            self.fault = 'did not return (pc=%s)' % self.where(uc.reg_read(UC_MIPS_REG_PC))
        return uc.reg_read(UC_MIPS_REG_V0), uc.reg_read(UC_MIPS_REG_V1)

    def fn(self, img, name):
        return img.sym[name][0]
