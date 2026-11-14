#!/bin/sh
# T33 binary oracle block "append" (openimp #99): tests/t33_binoracle/append.c (driver/t32/tx_isp_t33_append.h) vs the vendor module in
# Unicorn (tools/ko_emu/diff_t33_append.py), then one-token mutations of the header that must be detected (lines: header @ from @ to).
# Needs T33_OEM_KO, T33_CC (else T40_CC) and python3 unicorn + pyelftools, else SKIP (run.sh checks).
H=$(cd "$(dirname "$0")" && pwd)
KO=${T33_OEM_KO:-}; CC=${T33_CC:-${T40_CC:-}}
PY="python3 $H/../../tools/ko_emu/diff_t33_append.py"
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
"$H/build_emu.sh" "$CC" "$H/append.c" "$W/ok.o" || { echo "t33_binoracle append: FAIL build"; exit 1; }
$PY "$KO" "$W/ok.o" --n "${N:-400}" > "$W/out" || { tail -6 "$W/out"; echo "t33_binoracle append: FAIL"; exit 1; }
tail -1 "$W/out"
NM=0; NF=0
while IFS='@' read -r f a b; do
	[ -n "$f" ] || continue
	rm -rf "$W/d"; mkdir -p "$W/d"; cp "$H"/../../driver/t32/*.h "$W/d/"
	python3 - "$W/d/$f" "$a" "$b" <<'PY' || { echo "t33_binoracle append: mutation text not found: $a"; exit 1; }
import sys
p, a, b = sys.argv[1:4]; t = open(p).read()
if a not in t: sys.exit(1)
open(p, 'w').write(t.replace(a, b, 1))
PY
	"$H/build_emu.sh" "$CC" "$H/append.c" "$W/m.o" "$W/d" 2>/dev/null || { echo "t33_binoracle append: mutation does not build: $a"; exit 1; }
	if $PY "$KO" "$W/m.o" --n 100 > /dev/null; then
		echo "t33_binoracle append: mutation NOT detected: $a => $b"; NF=$((NF + 1))
	else NM=$((NM + 1)); fi
done <<'M'
tx_isp_t33_append.h@"APPEND_LDC", "APPEND_LSC",@"APPEND_LSC", "APPEND_LDC",
tx_isp_t33_append.h@"APPEND_TMO", "APPEND_YDNS", "APPEND_YSP",@"APPEND_TMO", "APPEND_YSP", "APPEND_YDNS",
tx_isp_t33_append.h@	return 0x18;@	return 0x17;
tx_isp_t33_append.h@			if (!name[k])@			if (!n[i][k + 1])
tx_isp_t33_append.h@#define T33_APP_RECS		32@#define T33_APP_RECS		31
tx_isp_t33_append.h@#define T33_APP_LSC_REC		0x3606U@#define T33_APP_LSC_REC		0x3604U
tx_isp_t33_append.h@#define T33_APP_LSC_TABLE	0x3602U@#define T33_APP_LSC_TABLE	0x3600U
tx_isp_t33_append.h@#define T33_APP_LSC_HY		0x28U@#define T33_APP_LSC_HY		0x2aU
tx_isp_t33_append.h@#define T33_APP_LSC_FLAG	0x5bU@#define T33_APP_LSC_FLAG	0x5cU
tx_isp_t33_append.h@		l->sec[T33_APP_LSC_FLAG + idx] = src[0x3604];@		l->sec[T33_APP_LSC_FLAG + idx] = src[0x3605];
tx_isp_t33_append.h@		l->sec[T33_APP_LSC_HY + 2 * idx + 1] = src[0x3603];@		l->sec[T33_APP_LSC_HY + 2 * idx + 1] = 0;
tx_isp_t33_append.h@		idx = cnt - n + k;@		idx = cnt - n + k + 1;
tx_isp_t33_append.h@		u32 at = l->size16 + k * T33_APP_LSC_TABLE, i;@		u32 at = (cnt - n + k) * T33_APP_LSC_TABLE, i;
tx_isp_t33_append.h@	if (l->cap < need) {@	if (l->cap <= need) {
tx_isp_t33_append.h@		l->cap = need;@		l->cap = need + 2;
tx_isp_t33_append.h@	l->sec[T33_APP_LSC_CNT] = (u8)cnt;@	l->sec[T33_APP_LSC_CNT] = (u8)n;
tx_isp_t33_append.h@	if (size < 8 || !app) {@	if (size < 7 || !app) {
tx_isp_t33_append.h@	next = t33_le32(app + 4);@	next = t33_le32(app);
tx_isp_t33_append.h@		next = t33_le32(rec + 0x14);@		next = t33_le32(rec + 0x18);
tx_isp_t33_append.h@			if (t33_le32(rec + 0x10) == 1) {@			if (t33_le32(rec + 0x10) != 0) {
tx_isp_t33_append.h@				if (!t33_le32(rec + 0x18))@				if (t33_le32(rec + 0x18) < T33_APP_LSC_REC)
tx_isp_t33_append.h@			ops->log(T33_APP_LOG_TYPE, e);@			;
tx_isp_t33_append.h@	u32 next, at = 8, i, e;@	u32 next, at = 12, i, e;
tx_isp_t33_append.h@				if (t33_ldc_extra_par(exist, par, ch, rec))@				if (t33_ldc_extra_par(exist, par, ch + 1, rec))
tx_isp_t33_append.h@		return -1;@		return 0;
M
echo "t33_binoracle append: mutations $NM detected, $NF not detected"
[ $NF = 0 ] || exit 1
echo "t33_binoracle append: PASS"
