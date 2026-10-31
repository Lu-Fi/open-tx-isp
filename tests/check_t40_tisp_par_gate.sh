#!/bin/sh
# check_t40_tisp_par_gate.sh <tx-isp-t40.ko> <y|n>   (#164)
#
# The /dev/tisp parameter get/set commands (generic read/write of module parameter blocks and ISP
# registers) exist only in a debug build (make TX_ISP_T40_TISP_PAR=y).  A default build must not carry
# the t40_tisp_par module parameter; a debug build must.  Exit 0 = ok, 1 = wrong, 2 = usage.
[ $# -eq 2 ] && [ -f "$1" ] || { echo "usage: $0 <module.ko> <y|n>" >&2; exit 2; }
if grep -a -q 'parm=t40_tisp_par:' "$1"; then has=y; else has=n; fi
want=n; [ "$2" = y ] && want=y
if [ "$has" != "$want" ]; then
	echo "check_t40_tisp_par_gate: $(basename "$1"): t40_tisp_par parameter present=$has, debug build=$want"
	exit 1
fi
echo "check_t40_tisp_par_gate: $(basename "$1"): ok (debug build=$want)"
