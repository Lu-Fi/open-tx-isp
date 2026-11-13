#!/usr/bin/env python3
"""optdiff.py A.o B.o [--seeds N] [--only RE] [--shard i/n] [--max INSNS]

Differential fuzz of two builds of the SAME source (e.g. recovered unit -O0 vs -Os), Lu-Fi/openimp#274.
Every function defined in both objects runs in Unicorn on the same pseudo-random inputs, twice per
object: stack and scratch registers pre-filled with 0x00 and with 0xa5.  Compared: stop reason, v0,
ordered external calls (kernel + register io, canonical args), changed heap / low-memory words,
changed module data (per symbol).

  UNINIT  same object, different result for the two fills  -> reads stack/register residue
  OPT     A and B differ although each is fill-stable      -> behaviour depends on the optimiser (UB)
  RET     only v0 differs (often a void-like lifted function; check the callers)

Module / stack addresses inside results are canonicalised (symbol+offset / "stk").
"""
import sys, os, re, struct, random, argparse, zlib, bisect
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ko_emu
ko_emu.BASE = BASE = 0x5a000000
from ko_emu import KoEmu, HEAP, STACK_TOP, STACK_SZ, STUB
from unicorn import UC_HOOK_CODE, UcError, UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED, UC_HOOK_MEM_FETCH_UNMAPPED
from unicorn.mips_const import *

ARENA = 0x40000            # fuzzed heap bytes
KHEAP = HEAP + 0x80000     # allocator returns
LOW = 0x40000              # low memory (NULL-based access)
M32 = 0xffffffff
A = (UC_MIPS_REG_A0, UC_MIPS_REG_A1, UC_MIPS_REG_A2, UC_MIPS_REG_A3)
ALLOC = re.compile(r"alloc|ioremap|get_free_pages|drvdata|PDE_DATA|clk_get$|filp_open|i2c_get_adapter|i2c_new_device|kthread_create|proc_create|proc_mkdir|platform_get_resource")
SKIP = re.compile(r"^(init_module|cleanup_module|regtrace_patch_relocated_data|init_isp_set)$")

NARGS = dict(printk=1, __might_sleep=2, up=1, down_timeout=2, mutex_lock=1, mutex_unlock=1, msleep=1, __udelay=1, usleep_range=2,
             kfree=1, __kmalloc=2, kmem_cache_alloc=2, complete=1, schedule=0, schedule_timeout=1, arch_local_irq_save=0,
             arch_local_irq_restore=1, preempt_schedule=0, wake_up_process=1, __init_waitqueue_head=3, prepare_to_wait=3,
             finish_wait=2, seq_printf=2, snprintf=3, scnprintf=3, dump_stack=0, __mutex_init=3, getrawmonotonic=1,
             sched_clock=0, kthread_should_stop=0, enable_irq=1, disable_irq=1, disable_irq_nosync=1, synchronize_irq=1,
             clk_enable=1, clk_disable=1, clk_get_rate=1, clk_put=1, clk_set_rate=2, clk_get=2, dev_get_drvdata=1,
             dev_set_drvdata=2, PDE_DATA=1, proc_remove=1, msecs_to_jiffies=1, capable=1, filp_close=2, kthread_stop=1,
             module_put=1, try_module_get=1, isp_free_buffer=1, isp_malloc_buffer=1, __get_free_pages=2, free_irq=2,
             gpio_free=1, gpio_request=2, gpio_direction_output=2, __iounmap=1, misc_register=1, misc_deregister=1,
             i2c_put_adapter=1, i2c_get_adapter=1, i2c_unregister_device=1, i2c_del_driver=1, i2c_transfer=3,
             platform_device_register=1, platform_device_unregister=1, platform_driver_unregister=1, platform_driver_register=1,
             platform_get_irq=2, platform_get_resource=3, wait_for_completion_interruptible=1, jzgpio_set_func=3,
             proc_create_data=1, jz_proc_mkdir=1, autoremove_wake_function=4)
CACHE = {}
BOOLSAFE = bool(os.environ.get("BOOLSAFE"))    # .bss fuzz bytes only 0/1 (a _Bool holding 2..255 is UB the fuzz would inject)
def strip(n): return re.sub(r"\.\d+$", "", n)

class Inst:
    def __init__(self, path, max_insns):
        e = self.e = KoEmu(path, hooks=False); self.max = max_insns
        uc = e.uc
        uc.mem_map(0, LOW)
        uc.hook_add(UC_HOOK_CODE, self.on_stub, begin=STUB, end=STUB + 0x10 * (len(e.stubs) + 2))
        uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED | UC_HOOK_MEM_FETCH_UNMAPPED, self.on_unmapped)
        self.funcs = {}; self.objs = []; syms = []; self.bss = []
        st = e.elf.get_section_by_name(".symtab")
        for sym in st.iter_symbols():
            sh = sym["st_shndx"]
            if not isinstance(sh, int) or sh not in e.secbase or not sym.name: continue
            a = e.secbase[sh] + sym["st_value"]; t = sym["st_info"]["type"]
            if t == "STT_FUNC": self.funcs[sym.name] = a
            sec = e.elf.get_section(sh)
            if t == "STT_OBJECT" and sec["sh_flags"] & 1: self.objs.append((strip(sym.name), a, sym["st_size"]))
            if t == "STT_OBJECT" and sec["sh_type"] == "SHT_NOBITS" and sym["st_size"] and not re.match(r"apicalCalibrations|__key|.*_lock$|.*mutex", sym.name): self.bss.append((strip(sym.name), a, sym["st_size"]))
            if t in ("STT_FUNC", "STT_OBJECT"): syms.append((a, strip(sym.name)))
        syms.sort(); self.sa = [s[0] for s in syms]; self.sn = [s[1] for s in syms]
        self.wsec = [(b, e.elf.get_section(i)["sh_size"]) for i, b in e.secbase.items() if e.elf.get_section(i)["sh_flags"] & 1]
        self.regs = {}; self.log = []; self.kptr = KHEAP; self.stop = None
        self.rosec = [(b, e.elf.get_section(i)["sh_size"]) for i, b in e.secbase.items() if e.secname[i].startswith(".rodata")]
        if "regtrace_patch_relocated_data" in self.funcs: self.run_raw(self.funcs["regtrace_patch_relocated_data"], (0, 0, 0, 0), (), 0, 2_000_000)
        if "init_isp_set" in self.funcs: self.seed = 0; self.run_raw(self.funcs["init_isp_set"], (0, 0, 0, 0), (), 0, 2_000_000); self.init_stop = self.stop
        if "apicalCalibrations" in e.symaddr:        # entries the sensor bin would provide: synthetic 4x4 u16 tables
            r = random.Random(7); z = HEAP + 0x200000
            for i in range(277):
                p = e.symaddr["apicalCalibrations"] + 4 * i
                if struct.unpack("<I", e.read(p, 4))[0] == 0:
                    uc.mem_write(z, struct.pack("<IHHH", z + 16, 4, 4, 2) + bytes(6) + struct.pack("<64H", *sorted(r.randrange(0, 1024) for _ in range(64))))
                    uc.mem_write(p, struct.pack("<I", z)); z += 16 + 128
        self.snap = [(b, bytes(uc.mem_read(b, n))) for b, n in self.wsec]

    def canon(self, v):
        e = self.e
        if BASE <= v < e.end:
            i = bisect.bisect_right(self.sa, v) - 1
            for b, n in self.rosec:
                if b <= v < b + n:
                    t = bytes(e.uc.mem_read(v, min(48, b + n - v))).split(b"\0")[0]
                    return "ro:" + t.decode("latin1")
            if i >= 0: return "%s+%x" % (self.sn[i], v - self.sa[i])
            return "mod"
        if STACK_TOP - STACK_SZ <= v < STACK_TOP: return "stk"
        if v in e.stubname: return "ext:" + e.stubname[v]
        return v

    def on_unmapped(self, uc, access, addr, size, value, ud):
        self.stop = "unmapped"; return False

    def on_stub(self, uc, addr, size, ud):
        e = self.e; name = e.stubname.get(addr)
        if name is None: return
        a = [uc.reg_read(r) for r in A]; rv = 0; v1 = 0
        if name in ("memcpy", "__copy_user"):
            n = min(a[2], 0x4000)
            try: uc.mem_write(a[0], bytes(uc.mem_read(a[1], n)))
            except UcError: self.stop = "unmapped"; uc.emu_stop(); return
            rv = a[0]
            if name == "__copy_user": uc.reg_write(UC_MIPS_REG_A2, 0); self.log.append((name, self.canon(a[0]), self.canon(a[1]), a[2]))
        elif name == "memset":
            try: uc.mem_write(a[0], bytes([a[1] & 255]) * min(a[2], 0x4000))
            except UcError: self.stop = "unmapped"; uc.emu_stop(); return
            rv = a[0]
        elif name == "memcmp":
            try: x = bytes(uc.mem_read(a[0], min(a[2], 0x1000))); y = bytes(uc.mem_read(a[1], min(a[2], 0x1000)))
            except UcError: self.stop = "unmapped"; uc.emu_stop(); return
            rv = (x > y) - (x < y)
        elif name in e.hle:
            r = e.hle[name](e); rv, v1 = r if isinstance(r, tuple) else (r, 0)
        elif name in ("div64_u64", "div64_s64"):
            n = a[0] | a[1] << 32; d = a[2] | a[3] << 32
            if name == "div64_s64":
                n = n - (1 << 64) if n >> 63 else n; d = d - (1 << 64) if d >> 63 else d
                q = (abs(n) // abs(d) * (1 if (n < 0) == (d < 0) else -1)) if d else 0
            else: q = n // d if d else 0
            q &= (1 << 64) - 1; rv, v1 = q & M32, q >> 32
        elif name == "__div64_32":
            try:
                n = struct.unpack("<Q", bytes(uc.mem_read(a[0], 8)))[0]; d = a[1] or 1
                uc.mem_write(a[0], struct.pack("<Q", n // d)); rv = n % d
            except UcError: self.stop = "unmapped"; uc.emu_stop(); return
        elif name in ("system_isp_read_32", "system_isp_read_8"):
            rv = self.regs.get(a[0], zlib.crc32(struct.pack("<II", a[0], self.seed)) & (0xff if name.endswith("8") else M32))
            self.log.append((name, a[0]))
        elif name.startswith("system_isp_write"):
            self.regs[a[0]] = a[1]; self.log.append((name, a[0], a[1]))
        else:
            if ALLOC.search(name):
                rv = self.kptr; self.kptr += 0x4000
                if self.kptr > HEAP + 0x100000 - 0x4000: self.kptr = KHEAP
            self.log.append((name,) + tuple(self.canon(x) for x in a[:NARGS.get(name, 4)]))
            if len(self.log) > 4000: self.stop = "calls"; uc.emu_stop(); return
        uc.reg_write(UC_MIPS_REG_V0, rv & M32); uc.reg_write(UC_MIPS_REG_V1, v1 & M32)
        uc.reg_write(UC_MIPS_REG_PC, uc.reg_read(UC_MIPS_REG_RA))

    def run_raw(self, pc, args, stack_args, fill, max_insns):
        uc = self.e.uc; sp = STACK_TOP - 0x8000
        g = (fill * 0x01010101) & M32
        for r in range(UC_MIPS_REG_0, UC_MIPS_REG_31 + 1): uc.reg_write(r, g)
        uc.reg_write(UC_MIPS_REG_0, 0)
        for r, v in zip(A, args): uc.reg_write(r, v & M32)
        uc.mem_write(STACK_TOP - 0x20000, bytes([fill]) * 0x20000)
        for k, v in enumerate(stack_args): uc.mem_write(sp + 16 + 4 * k, struct.pack("<I", v & M32))
        uc.reg_write(UC_MIPS_REG_SP, sp); uc.reg_write(UC_MIPS_REG_RA, STUB); uc.reg_write(UC_MIPS_REG_T9, pc)
        uc.reg_write(UC_MIPS_REG_GP, KHEAP - 0x8000)       # thread_info
        self.stop = None
        try: uc.emu_start(pc, STUB, count=max_insns)
        except UcError as ex: self.stop = self.stop or "err"; self.exc = str(ex)
        if self.stop is None and uc.reg_read(UC_MIPS_REG_PC) != STUB: self.stop = "max"
        return uc.reg_read(UC_MIPS_REG_V0)

    def run(self, fn, seed, fill):
        e = self.e; uc = e.uc; self.seed = seed
        rnd = random.Random(seed)
        def words(n):
            out = []
            for _ in range(n // 4):
                k = rnd.random()
                if k < 0.30: v = 0
                elif k < 0.55: v = rnd.randrange(0, 9)
                elif k < 0.70: v = rnd.randrange(0, 256)
                elif k < 0.80: v = rnd.randrange(0, 65536)
                elif k < 0.83: v = rnd.getrandbits(32)
                else: v = HEAP + 0x8000 + 4 * rnd.randrange(0, (ARENA - 0x10000) // 4)
                out.append(v)
            return struct.pack("<%dI" % len(out), *out)
        if CACHE.get("seed") != seed: CACHE.clear(); CACHE["seed"] = seed; CACHE["v"] = (words(ARENA), words(LOW), rnd.getstate())
        heap0, low0, st = CACHE["v"]; rnd.setstate(st)
        uc.mem_write(HEAP, heap0); uc.mem_write(KHEAP - 0x10000, bytes(0x90000)); uc.mem_write(0, low0)
        for b, d in self.snap: uc.mem_write(b, d)
        snap = self.snap
        if seed & 1:                                   # odd seeds: module .bss fuzzed too (same bytes per symbol name)
            for name, a_, sz in self.bss:
                r2 = random.Random(zlib.crc32(name.encode()) ^ seed); n4 = (sz + 3) // 4; out = []
                for _ in range(n4):
                    k = r2.random()
                    if BOOLSAFE: out.append(r2.choice((0, 0, 1, 0x101, 0x10001, 0x1010101, 0x1000000, 0x10000, 0x100, HEAP + 0x8000 + 4 * r2.randrange(0, (ARENA - 0x10000) // 4)))); continue
                    out.append(0 if k < 0.35 else r2.randrange(0, 9) if k < 0.6 else r2.randrange(0, 256) if k < 0.75 else r2.randrange(0, 65536) if k < 0.85 else HEAP + 0x8000 + 4 * r2.randrange(0, (ARENA - 0x10000) // 4))
                uc.mem_write(a_, struct.pack("<%dI" % n4, *out)[:sz])
            snap = [(b, bytes(uc.mem_read(b, len(d)))) for b, d in self.snap]
        self.regs = {}; self.log = []; self.kptr = KHEAP
        def arg():
            k = rnd.random()
            if k < 0.75: return HEAP + 0x8000 + 4 * rnd.randrange(0, (ARENA - 0x10000) // 4)
            if k < 0.9: return rnd.randrange(0, 9)
            return rnd.randrange(0, 0x10000)
        args = [arg() for _ in range(4)]; sargs = [arg() for _ in range(6)]
        v0 = self.run_raw(self.funcs[fn], args, sargs, fill, self.max)
        sig = {"stop": self.stop, "v0": self.canon(v0), "calls": tuple(self.log)}
        def delta(base, old, n):
            new = bytes(uc.mem_read(base, n)); out = []
            if new == old: return ()
            for o in range(0, n, 4096):
                if new[o:o + 4096] == old[o:o + 4096]: continue
                for w in range(o, min(o + 4096, n), 4):
                    if new[w:w + 4] != old[w:w + 4]: out.append((w, self.canon(struct.unpack("<I", new[w:w + 4])[0])))
            return tuple(out)
        sig["heap"] = delta(HEAP, heap0, ARENA); sig["low"] = delta(0, low0, LOW)
        sig["kheap"] = delta(KHEAP - 0x10000, bytes(0x90000), 0x90000)
        d = []
        for (b, old) in snap:
            new = bytes(uc.mem_read(b, len(old)))
            if new == old: continue
            for name, a, sz in self.objs:
                if b <= a < b + len(old) and new[a - b:a - b + sz] != old[a - b:a - b + sz]:
                    x = new[a - b:a - b + sz]; x += bytes(-len(x) % 4)
                    d.append((name, tuple(self.canon(v) for v in struct.unpack("<%dI" % (len(x) // 4), x))))
        sig["data"] = tuple(sorted(d))
        return sig

def cmp(x, y):
    if x["stop"] or y["stop"]: return ["stop"] if x["stop"] != y["stop"] else []
    return [k for k in ("stop", "calls", "heap", "low", "kheap", "data", "v0") if x[k] != y[k]]

def first(x, y, k):
    if k in ("stop", "v0"): return "%s | %s" % (x[k], y[k])
    a, b = x[k], y[k]
    for i in range(max(len(a), len(b))):
        p = a[i] if i < len(a) else None; q = b[i] if i < len(b) else None
        if p != q:
            def sh(t):
                if t is None: return "-"
                if k == "data" and q is not None and p is not None and p[0] == q[0]:
                    j = [n for n in range(min(len(p[1]), len(q[1]))) if p[1][n] != q[1][n]]
                    return "%s w%s=%s" % (t[0], j[:4], [t[1][n] for n in j[:4]])
                return "(" + ",".join(("%#x" % v if isinstance(v, int) else (str(v) if not isinstance(v, tuple) else "[%d]" % len(v))) for v in t) + ")"
            return "#%d/%d,%d %s | %s" % (i, len(a), len(b), sh(p), sh(q))
    return "?"

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("a"); ap.add_argument("b")
    ap.add_argument("--seeds", type=int, default=6); ap.add_argument("--only"); ap.add_argument("--shard", default="0/1")
    ap.add_argument("--max", type=int, default=400000); ap.add_argument("-v", action="store_true"); ap.add_argument("--after", default="")
    o = ap.parse_args(); si, sn = map(int, o.shard.split("/"))
    X = Inst(o.a, o.max); Y = Inst(o.b, o.max)
    print("init", X.init_stop, Y.init_stop, sum(1 for i in range(277) if struct.unpack("<I", X.e.read(X.e.symaddr["apicalCalibrations"] + 4 * i, 4))[0]), file=sys.stderr)
    names = sorted(set(X.funcs) & set(Y.funcs)); names = [n for n in names if not SKIP.match(n)]
    if o.only: names = [n for n in names if re.search(o.only, n)]
    names = [n for n in names[si::sn] if n > o.after]
    for fn in names:
        print("BEGIN", fn, file=sys.stderr, flush=True)
        res = {"UNINIT_A": [], "UNINIT_B": [], "OPT": [], "RET": [], "RETU_A": [], "RETU_B": []}; ok = 0; bad = 0
        for s in range(o.seeds):
            seed = (zlib.crc32(fn.encode()) * 31 + s) & M32
            a0 = X.run(fn, seed, 0); a1 = X.run(fn, seed, 0xa5); b0 = Y.run(fn, seed, 0); b1 = Y.run(fn, seed, 0xa5)
            ua = cmp(a0, a1); ub = cmp(b0, b1); ab = cmp(a0, b0)
            if o.v: print("   ", s, a0["stop"], a1["stop"], b0["stop"], b1["stop"], len(a0["calls"]), X.e.where(X.e.uc.reg_read(UC_MIPS_REG_PC)), Y.e.where(Y.e.uc.reg_read(UC_MIPS_REG_PC)), getattr(Y, "exc", ""))
            if a0["stop"] or b0["stop"]: bad += 1
            else: ok += 1
            if "err" in (a0["stop"], a1["stop"]): ua = []
            if "err" in (b0["stop"], b1["stop"]): ub = []
            if ua: res["RETU_A" if ua == ["v0"] else "UNINIT_A"].append((s, ua, first(a0, a1, ua[0])))
            if ub: res["RETU_B" if ub == ["v0"] else "UNINIT_B"].append((s, ub, first(b0, b1, ub[0])))
            if ab and not ua and not ub:
                if a0["stop"] or b0["stop"]:
                    if a0["stop"] != b0["stop"] and {a0["stop"], b0["stop"]} <= {None, "max", "calls"}: res["OPT"].append((s, ["stop"], first(a0, b0, "stop")))
                    continue            # faulted runs: store order is not comparable
                (res["RET"] if ab == ["v0"] else res["OPT"]).append((s, ab, first(a0, b0, ab[0])))
        line = "%-48s ok=%d fault=%d" % (fn, ok, bad)
        for k, v in res.items():
            if v: line += " %s=%d" % (k, len(v))
        print(line, flush=True)
        for k, v in res.items():
            for s, what, det in v[:(0 if k.startswith("RET") else 2)]: print("    %s seed%d %s: %s" % (k, s, ",".join(what), det[:300]), flush=True)

if __name__ == "__main__":
    main()
