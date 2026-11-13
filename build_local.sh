#!/bin/bash
# Local compile baseline for the open Ingenic ISP driver.
#
# Builds the per-SoC driver from driver/<SOC>/ (default t31 -> tx-isp-t31.ko)
# plus the shared diagnostic driver/tx_isp_trace.ko, out-of-tree against a
# thingino buildroot output. If no matching SoC output is built locally, any
# Ingenic 3.10.14 output works: same kernel version + same mipsel uclibc
# toolchain exercise the identical 3.10 kernel API surface. The resulting .ko
# is a *compile baseline*, not necessarily a loadable artifact for that SoC.
#
# Two gcc-15 diagnostics are relaxed because the source predates gcc-14's
# promotion of these to hard errors (older Ingenic SDK toolchains only warned):
#   - implicit-function-declaration  (e.g. get_driver_common_interfaces, an
#                                      external SDK symbol resolved at load time)
#   - int-conversion
# These are pre-existing latent issues, independent of file naming.
#
# Parameters (all overridable via environment):
#   TH      buildroot checkout            (default: ../thingino-firmware)
#   ROOT    a single output/<...> dir     (default: first output with a toolchain)
#   KDIR    kernel build tree             (default: $ROOT/build/linux-*)
#   CROSS   cross-compile prefix          (default: mipsel-linux-)
#   ARCH    target arch                   (default: mips)
#   SOC     per-SoC driver subdir         (default: t31 -> driver/t31/)
#   OTX_BUILD_ID  build id string in the probe log (default: "local"; a long
#           git-describe id moves .rodata/.data/.bss of the loaded module and
#           so differs in layout from the firmware ko, which uses a short id)
#
# Examples:
#   ./build_local.sh                       # autodetect everything, build modules
#   SOC=t30 ./build_local.sh               # build the T30 recovery baseline
#   SOC=t41 ./build_local.sh               # build the T41 driver instead
#   ROOT=/path/to/output/t31_foo ./build_local.sh
#   TH=~/src/thingino-firmware ./build_local.sh clean
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"

TH="${TH:-$(cd "$HERE/.." && pwd)/thingino-firmware}"
CROSS="${CROSS:-mipsel-linux-}"
ARCH="${ARCH:-mips}"

# ROOT: an output/<config> dir containing host/bin + build/linux-*.
# If unset, pick the first one that ships a usable cross-gcc.
if [ -z "${ROOT:-}" ]; then
	[ -d "$TH/output" ] || { echo "buildroot output not found under: $TH/output (set TH=)"; exit 1; }
	ROOT="$(find "$TH/output" -maxdepth 6 -path '*/host/bin/'"${CROSS}gcc" \
	         -printf '%h\n' 2>/dev/null | sed 's#/host/bin$##' | sort | head -1)"
	[ -n "$ROOT" ] || { echo "no ${CROSS}gcc toolchain found under $TH/output (set ROOT=)"; exit 1; }
fi

# KDIR: the built kernel tree inside ROOT.
if [ -z "${KDIR:-}" ]; then
	KDIR="$(find "$ROOT/build" -maxdepth 1 -type d -name 'linux-*' 2>/dev/null | sort | head -1)"
fi

[ -x "$ROOT/host/bin/${CROSS}gcc" ] || { echo "toolchain missing: $ROOT/host/bin/${CROSS}gcc"; exit 1; }
[ -n "${KDIR:-}" ] && [ -f "$KDIR/Module.symvers" ] || { echo "kernel tree not built (no Module.symvers): ${KDIR:-<unset>}"; exit 1; }

echo "ROOT=$ROOT"
echo "KDIR=$KDIR"

export PATH="$ROOT/host/bin:$PATH"

KCFLAGS="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=implicit-int"
GOAL="${1:-modules}"
# Short fixed build id: same module layout as the firmware package build
# (open-tx-isp.mk passes OTX_BUILD_ID=<release tag>), not the long git describe.
OTX_BUILD_ID="${OTX_BUILD_ID:-local}"
export OTX_BUILD_ID

# Same per-SoC XBurst include paths as the firmware package
# (thingino-firmware package/open-tx-isp/open-tx-isp.mk: EXTRA_CFLAGS with
# soc-$(SOC_FAMILY), core, common). Without them the ko differs from the
# firmware build (openimp #461). Passed for every SoC, T10..T41.
EXTRA_CFLAGS="-I$KDIR/arch/mips/xburst/soc-${SOC:-t31}/include \
-I$KDIR/arch/mips/xburst/core/include \
-I$KDIR/arch/mips/xburst/common/include"

# Per-SoC ISP driver (driver/<SOC>/, default t31) -> tx-isp-<soc>.ko
make -C "$KDIR" M="$HERE/driver/${SOC:-t31}" DIR=. \
     ARCH="$ARCH" CROSS_COMPILE="$CROSS" KCFLAGS="$KCFLAGS" EXTRA_CFLAGS="$EXTRA_CFLAGS" "$GOAL"

# Shared SoC-agnostic diagnostics (driver/) -> tx_isp_trace.ko
make -C "$KDIR" M="$HERE/driver" DIR=. \
     ARCH="$ARCH" CROSS_COMPILE="$CROSS" KCFLAGS="$KCFLAGS" EXTRA_CFLAGS="$EXTRA_CFLAGS" "$GOAL"

# Load check (no camera needed): undefined symbols of the built modules must
# resolve against the kernel's Module.symvers / sibling modules. Fails the build.
if [ "$GOAL" = modules ]; then
	rc=0
	for ko in "$HERE/driver/${SOC:-t31}"/*.ko "$HERE/driver"/*.ko; do
		[ -f "$ko" ] || continue
		CROSS_COMPILE="$CROSS" "$HERE/tests/check_ko_symbols.sh" "$KDIR/Module.symvers" "$ko" \
			"$HERE/driver/${SOC:-t31}"/*.ko "$HERE/driver"/*.ko || rc=1
	done
	[ $rc = 0 ] || { echo "ko symbol check FAILED"; exit 1; }
	# Known importers (sensors, avpu, ...): the module must export what they
	# use (driver/isp_imports.txt).
	ko="$HERE/driver/${SOC:-t31}/tx-isp-${SOC:-t31}.ko"
	if [ -f "$ko" ]; then
		"$HERE/tests/check_isp_exports.sh" "${SOC:-t31}" "$ko" "${CROSS}readelf" ||
			{ echo "ko export check FAILED"; exit 1; }
		# T40/T41 only for now (the other SoCs' lifts were not swept for self loops yet)
		case "${SOC:-t31}" in t40|t41)
			"$HERE/tests/check_ko_selfloops.sh" "${CROSS}objdump" "$ko" ||
				{ echo "ko self-loop check FAILED"; exit 1; } ;;
		esac
		if [ "${SOC:-t31}" = t40 ]; then
			"$HERE/tests/check_ko_zerobase.sh" "${CROSS}objdump" "$ko" ||
				{ echo "ko NULL-base access check FAILED"; exit 1; }
			"$HERE/tests/check_t40_tisp_par_gate.sh" "$ko" "${TX_ISP_T40_TISP_PAR:-n}" ||
				{ echo "ko tisp_par gate check FAILED"; exit 1; }
		fi
	fi
fi
