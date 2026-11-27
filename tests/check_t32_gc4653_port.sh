#!/bin/sh
# check_t32_gc4653_port.sh [sensor-src/t32/gc4653.c]   (Lu-Fi/openimp#923 #924)
# Source guard: the T32 gc4653 port has the vendor PRJ007 stream-on / stream-off lists and the INIT state
# at stream-off (gc4653.c:449 / :463 / :986), vts - 8 / min 2 integration limits (:1166 / :736) and the
# vendor analog gain table up to 409249 without sensor digital gain.
f=${1:-$(dirname "$0")/../sensor-src/t32/gc4653.c}
[ -f "$f" ] || { echo "usage: $0 [gc4653.c]" >&2; exit 2; }
fail() { echo "check_t32_gc4653_port: $1"; exit 1; }
list() { awk -v n="$1" '$0 ~ "^static struct regval_list " n "\\[\\]"{p=1;next} p&&/^};/{exit} p&&/\{0x/{gsub(/[ \t{}]/,"");printf "%s;",$0}' "$f"; }
on=$(list sensor_stream_on); off=$(list sensor_stream_off)
[ "$on" = "0x0320,0x77,;0x0334,0x40,;0x0324,0xc8,;0x0317,0x00,;0x0180,0x46,;0x03fe,0x10,;0x03fe,0x00,;0x031d,0x2d,;0x0100,0x09,;0x031d,0x28,;" ] || fail "stream-on list differs from the vendor: $on"
[ "$off" = "0x031d,0x2d,;0x0180,0x06,;0x0100,0x08,;0x0324,0x88,;0x0334,0x00,;0x0320,0x66,;0x0317,0x01,;0x031d,0x28,;" ] || fail "stream-off list differs from the vendor: $off"
awk '/^static int sensor_s_stream\(/{p=1} p&&/sensor_stream_off\);/{o=1} p&&o&&/video.state = TX_ISP_MODULE_INIT/{ok=1} p&&/^}/{exit} END{exit !ok}' "$f" || fail "s_stream(0) does not set the state to INIT"
grep -q 'integration_time.max = vts - 1;' "$f" && fail "linear integration max vts - 1 (vendor vts - 8)"
grep -q 'integration_time.max = vts - 8;' "$f" || fail "no vts - 8 integration max"
grep -q '1920 - 1;' "$f" && fail "default integration max 1920 - 1 (vendor - 8)"
grep -q '^	.integration_time.min = 2,' "$f" || fail "integration min is not 2"
grep -q '\.again\.max = 409249,' "$f" || fail "again.max != 409249"
grep -q '0x19, 0x3c, 0x1, 0x4b, 0x35, 0x10, 0x42, 0x80, 0x01, 0x00, 409249}' "$f" || fail "again table index 0x19 is not the vendor analog row"
grep -q '0x15, 0x9c' "$f" && fail "again table still holds the 32x code above index 0x14"
echo "check_t32_gc4653_port: ok"
