#!/bin/bash
# T33 LCE / LSF per-frame path (driver/t32/tx_isp_t33_lcepf.h: tisp_lce_init's lceInfo part, irq 8 tisp_lce_interrupt_static,
# irq 30 tisp_lsf_interrupt_static, event 0xe tisp_lce_soft_process with Tisp_lce_soft and its leaves, tisp_lsf_write_curve) built
# for mipsel and run against the instructions of the vendor tx-isp-t33.ko in Unicorn (tools/ko_emu/diff_t33_lcepf.py): the whole
# lceInfo block, section, curve buffer, lists 16 / 37, LSF buffer, register reads, list starts and event pushes equal, plus
# one-token mutations of the header that must be detected (openimp #444).
# Needs the vendor module (T33_OEM_KO), a mipsel-linux-gcc (T33_CC, else T40_CC) and python3 unicorn + pyelftools, else SKIP.
HERE=$(cd "$(dirname "$0")" && pwd)
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
if [ ! -r "$KO" ] || [ ! -x "$CC" ] || ! python3 -c "import unicorn, elftools" 2>/dev/null; then echo "t33_lcepf_host: SKIP (T33_OEM_KO / T33_CC / python3 unicorn+pyelftools missing)"; [ -z "$T40_ORACLE_STRICT" ] || exit 1; exit 0; fi
W=$(mktemp -d); trap 'rm -rf $W' EXIT
D=$HERE/t33_lcepf
"$D/build_emu.sh" "$CC" $W/ok.o || { echo "t33_lcepf_host: FAIL build"; exit 1; }
python3 "$HERE/../tools/ko_emu/diff_t33_lcepf.py" "$KO" $W/ok.o --n ${N:-4} --frames ${FRAMES:-2} --leaf ${LEAF:-300} || { echo "t33_lcepf_host: FAIL"; exit 1; }
NM=0; NF=0
mut() {		# mut FNS FROM TO : edit a copy of the header
	rm -rf $W/d; mkdir -p $W/d; cp "$HERE"/../driver/t32/*.h $W/d/
	python3 - "$2" "$3" $W/d/tx_isp_t33_lcepf.h <<'PY' || { echo "t33_lcepf_host: mutation text not found: $2"; exit 1; }
import sys
a, b, p = sys.argv[1:4]; t = open(p).read()
if a not in t: sys.exit(1)
open(p, "w").write(t.replace(a, b, 1))
PY
	"$D/build_emu.sh" "$CC" $W/m.o $W/d || { echo "t33_lcepf_host: FAIL mutation build"; exit 1; }
	python3 "$HERE/../tools/ko_emu/diff_t33_lcepf.py" "$KO" $W/m.o --fn $1 --n 2 --frames 1 --leaf 120 > /dev/null && { echo "t33_lcepf_host: FAIL mutation not detected: $2 => $3"; NF=$((NF + 1)); return; }
	NM=$((NM + 1))
}
mut leaf 'u32 at = 9 * g + k;' 'u32 at = 8 * g + k;'
mut leaf 'narrow = (int)(mid - lo) < 3;' 'narrow = (int)(mid - lo) < 2;'
mut leaf 'd = t33_lcepf_tri(hi - mid) - 1;' 'd = t33_lcepf_tri(hi - mid);'
mut leaf 'int n = str <= 0x80 ? 0x21 - (int)mid' 'int n = str < 0x80 ? 0x21 - (int)mid'
mut leaf 'a[i] = (u8)(((8 * i) * 0x100 + 0x80 + (u32)adj) >> 8);' 'a[i] = (u8)(((8 * i) * 0x100 + (u32)adj) >> 8);'
mut leaf 'd = (int)((v - t) * gain + 0x40) >> 7;' 'd = (int)((v - t) * gain + 0x3f) >> 7;'
mut leaf '(32 * (i & 7) + 16) << 16' '(32 * (i & 7) + 15) << 16'
mut init 'p[0x13f] = t33_lcepf_le32(s + 0x2e4);' 'p[0x13f] = t32_tp_le16(s + 0x2e4);'
mut init '{ 0x50, 0x35 }' '{ 0x50, 0x36 }'
mut init 'c->prev[i] = 0x400;' 'c->prev[i] = 0x3ff;'
mut frame 'c->zsum[r * T33_LCEPF_COLS + i] = t33_lcepf_le32(row + 0x600 + 4 * i) & 0x3fffff;' 'c->zsum[r * T33_LCEPF_COLS + i] = t33_lcepf_le32(row + 0x600 + 4 * i) & 0x7fffff;'
mut frame 'clip[0] = (u16)(c->p[0x10] << 10);' 'clip[0] = (u16)(c->p[0x10] << 9);'
mut frame 'if (cum >> 15)' 'if (cum >> 16)'
mut frame 'c->cur20[z * 20 + 9 + i] = a[10 + 2 * i];' 'c->cur20[z * 20 + 9 + i] = a[9 + 2 * i];'
mut frame 'T33_SW(l37, T33_LSF_SLOT_STAT, stat_pa + part);' 'T33_SW(l37, T33_LSF_SLOT_STAT, stat_pa);'
mut frame '(int)((u32)c->hist[i] * sm + (u32)c->prev[i] * (u32)om + 0x40) >> 7' '(int)((u32)c->hist[i] * sm + (u32)c->prev[i] * (u32)om) >> 7'
echo "t33_lcepf_host: mutations $NM detected, $NF not detected"
[ $NF = 0 ] || exit 1
echo "t33_lcepf_host: PASS"
