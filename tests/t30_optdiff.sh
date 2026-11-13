#!/bin/sh
# t30_optdiff.sh: gate before a TX_ISP_T30_OS=y build (Lu-Fi/openimp#274, #478).
# Differential fuzz of the SAME T30 source built twice, -O0 (TX_ISP_T30_OS=n) and -Os (TX_ISP_T30_OS=y, default):
#   1. tools/ko_emu/seq_yuv.py: the b72/b73 YUV matrix sequence (initialize, saturation 128/0/230/128) must write
#      identical 0x540..0x7b0 values in both builds and with stack fills 0x00 / 0xa5.
#   2. tools/ko_emu/optdiff.py: every function of both objects in Unicorn on the same random inputs (SHARDS shards,
#      JOBS in parallel, a shard restarts after a Unicorn crash).  Every function with an UNINIT_A / UNINIT_B / OPT
#      hit must be in tests/t30_optdiff_baseline.txt (reviewed fuzz artefacts); a new one fails.
# Inputs (SKIP without them): T30_O0_O / T30_OS_O = `ld -r tx_isp_t30_recovered.o tx_isp_t30_math.o
# tx_isp_t30_exposure.o` of the two builds (see tests/README.oracles.md).  Slow (~15 min at JOBS=4), therefore
# not part of `make check`; run `make -C tests t30_optdiff` before changing the -Os flags / a T30 release.
H=$(cd "$(dirname "$0")" && pwd); T=$H/../tools/ko_emu; BASE=$H/t30_optdiff_baseline.txt
[ -r "${T30_O0_O:-}" ] && [ -r "${T30_OS_O:-}" ] && python3 -c 'import unicorn, elftools' 2>/dev/null || {
	echo "t30_optdiff: SKIP (set T30_O0_O and T30_OS_O, python3 unicorn+pyelftools)"; [ -z "${T40_ORACLE_STRICT:-}" ] || exit 1; exit 0; }
OUT=${OPTDIFF_OUT:-$(mktemp -d)}; mkdir -p "$OUT"; n=${SHARDS:-10}; rc=0
python3 "$T/seq_yuv.py" "$T30_O0_O" "$T30_OS_O" > "$OUT/seq_yuv.txt" 2>&1
tail -1 "$OUT/seq_yuv.txt"
grep -q "identical in all 4 runs" "$OUT/seq_yuv.txt" || { echo "t30_optdiff: seq_yuv FAIL ($OUT/seq_yuv.txt)"; rc=1; }
shard() {   # i: one shard, restart after an emulator crash (the function is logged as CRASH)
	i=$1; after=; : > "$OUT/run.$i.txt"
	while :; do
		python3 "$T/optdiff.py" "$T30_O0_O" "$T30_OS_O" --seeds "${SEEDS:-8}" --shard "$i/$n" --after "$after" \
			>> "$OUT/run.$i.txt" 2> "$OUT/run.$i.err" && break
		last=$(grep '^BEGIN' "$OUT/run.$i.err" | tail -1 | cut -d' ' -f2); [ -n "$last" ] || break
		echo "$last CRASH(emulator)" >> "$OUT/run.$i.txt"; after=$last
	done
}
i=0; while [ $i -lt $n ]; do
	shard $i 2>/dev/null & i=$((i + 1))   # no shell "Segmentation fault" lines for the restarted Unicorn crashes
	while [ "$(jobs -p | wc -l)" -ge "${JOBS:-4}" ]; do sleep 2; done
done
wait
cat "$OUT"/run.*.txt > "$OUT/run.all"
python3 - "$OUT/run.all" "$BASE" <<'EOF' || rc=1
import re, sys
cur = None; hits = {}
for l in open(sys.argv[1]):
    if not l.startswith(" "): cur = l.split()[0] if l.strip() else None; continue
    m = re.match(r"\s+(UNINIT_A|UNINIT_B|OPT) seed(\d+) ", l)
    if m and cur: hits.setdefault(cur, set()).add(m.group(1))
base = {l.split()[0] for l in open(sys.argv[2]) if l.strip() and not l.startswith("#")}
new = sorted(set(hits) - base)
print("t30_optdiff: %d functions compared, %d with hits, %d baseline, %d new" % (
    sum(1 for l in open(sys.argv[1]) if l.strip() and not l.startswith(" ")), len(hits), len(base & set(hits)), len(new)))
for f in new: print("NEW %s %s" % (f, ",".join(sorted(hits[f]))))
gone = sorted(base - set(hits))
if gone: print("baseline entries without a hit now (remove after review): " + " ".join(gone))
sys.exit(1 if new else 0)
EOF
[ $rc = 0 ] && echo "t30_optdiff: ok ($OUT)" || echo "t30_optdiff: FAIL ($OUT)"
exit $rc
