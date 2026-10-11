#!/usr/bin/env python3
"""List the tx-isp-<soc>.ko exports that other kernel modules use.

Usage:
  scan_isp_imports.py --nm NM [--readelf READELF] --isp SOC=tx-isp-SOC.ko [--isp ...]
                      [--ko-root DIR ...] [--src-root DIR ...]
                      [--sdk DIR] > driver/isp_imports.txt

For every SoC the exports of the given tx-isp module (__ksymtab_*) are
matched against
  * the undefined symbols (nm -u) of every *.ko below the --ko-root dirs
    (thingino target/lib/modules, package build dirs; the tx-isp modules
    themselves are skipped), and
  * the identifiers in the C sources below the --src-root dirs and, for an
    ingenic-sdk checkout (--sdk), common/sensor/<soc>* for that SoC plus every
    common/ directory that is neither ISP nor sensor code (avpu, audio,
    misc, ...).
A module counts for the SoC in its name (sensor_*_t31.ko) or else for the
SoC of the thingino output it was found in; source directories named after
another SoC (t40/, a1/, ...) are skipped.
Output lines: "<soc> <symbol> <importer>" for the first importer found,
sorted; tests/check_isp_exports.sh compares a built module with it.
"""
import argparse, os, re, subprocess, sys

def exports(readelf, ko):
    """Exported names: the __ksymtab_strings section (the module is linked
    with -x, so the local __ksymtab_* symbols are gone)."""
    out = subprocess.run([readelf, "-W", "-p", "__ksymtab_strings", ko],
                         capture_output=True, text=True).stdout
    return {m.group(1) for m in re.finditer(r"^\s*\[\s*[0-9a-f]+\]\s+(\S+)\s*$", out, re.M)}

def undefined(nm, ko):
    out = subprocess.run([nm, "-u", ko], capture_output=True, text=True).stdout
    return {l.split()[-1] for l in out.splitlines() if l.strip()}

ident = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")

def src_idents(root):
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.endswith((".c", ".h")):
                p = os.path.join(d, f)
                try:
                    with open(p, errors="replace") as fh:
                        yield p, set(ident.findall(fh.read()))
                except OSError:
                    pass

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--nm", required=True)
    ap.add_argument("--readelf", default="readelf")
    ap.add_argument("--isp", action="append", required=True)
    ap.add_argument("--ko-root", action="append", default=[])
    ap.add_argument("--src-root", action="append", default=[])
    ap.add_argument("--sdk", action="append", default=[])
    a = ap.parse_args()

    exp = {}
    for spec in a.isp:
        soc, ko = spec.split("=", 1)
        exp[soc] = exports(a.readelf, ko)

    found = {}
    def add(soc, sym, who):
        found.setdefault((soc, sym), who)

    socdir = re.compile(r"^(a1|c\d+|t\d\d[a-z]*)$")
    def other_soc(path, soc):
        """path has a SoC-specific component that is not this SoC"""
        return any(socdir.match(c) and not c.startswith(soc)
                   for c in path.split(os.sep))

    def root_soc(root):
        """SoC of a thingino output: the tx-isp-<soc>.ko it built"""
        top = root
        while top != os.path.dirname(top) and not os.path.isdir(os.path.join(top, "target")):
            top = os.path.dirname(top)
        for d, _, fs in os.walk(os.path.join(top, "target", "lib", "modules")):
            for f in fs:
                m = re.match(r"tx-isp-(t\d\d)\.ko$", f)
                if m:
                    return m.group(1)
        return None

    seen = set()
    for root in a.ko_root:
        rsoc = root_soc(root)
        for d, _, fs in os.walk(root):
            for f in fs:
                if not f.endswith(".ko") or f.startswith("tx-isp-"):
                    continue
                p = os.path.realpath(os.path.join(d, f))
                m = re.search(r"_(t\d\d)[a-z]*\.ko$", f)
                ksoc = m.group(1) if m else rsoc
                if (p, ksoc) in seen:
                    continue
                seen.add((p, ksoc))
                und = undefined(a.nm, p)
                for soc, e in exp.items():
                    if ksoc in (None, soc):
                        for s in und & e:
                            add(soc, s, "ko:" + f)

    for root in a.src_root:
        for p, ids in src_idents(root):
            rel = os.path.relpath(p, root)
            for soc, e in exp.items():
                if not other_soc(rel, soc):
                    for s in ids & e:
                        add(soc, s, "src:" + rel)

    for sdk in a.sdk:
        common = os.path.join(sdk, "common")
        for soc, e in exp.items():
            roots = [os.path.join(common, d) for d in sorted(os.listdir(common))
                     if d not in ("isp", "sensor")]
            roots += [os.path.join(common, "sensor", d)
                      for d in sorted(os.listdir(os.path.join(common, "sensor")))
                      if d.startswith(soc)]
            for root in roots:
                for p, ids in src_idents(root):
                    rel = os.path.relpath(p, sdk)
                    if os.path.relpath(p, common).startswith("sensor") or not other_soc(rel, soc):
                        for s in ids & e:
                            add(soc, s, "sdk:" + rel)

    for (soc, sym), who in sorted(found.items()):
        print(soc, sym, who)

if __name__ == "__main__":
    sys.exit(main())
