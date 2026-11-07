#!/bin/sh
# Copy reference functions from Matt Davis' recovered T32 driver
# (opensensor/open-tx-isp PR #15, our branch origin/t32/recovered-baseline)
# into tests/t32_oracle/ref/.  The function body is unchanged; only a
# provenance header is added and the include of ../compat.h (kernel headers)
# is pointed at the host compat header.
#   usage: import_ref.sh <git-dir-of-open-tx-isp> fn_<addr>_<name>.c ...
set -e
REV=2e2e06bc
G=$1; shift
D=$(dirname "$0")/ref
for f in "$@"; do
	p=driver/t32/recovered/$f
	{
		echo "/* Reference copy for the host oracle, NOT driver code."
		echo " * Author: Matt Davis <matteius@gmail.com> (opensensor/open-tx-isp PR #15,"
		echo " * all-C recovery of the Ingenic T32 tx-isp module)."
		echo " * Source: $p@$REV, unchanged except the include line below. */"
		git -C "$G" show "$REV:$p" | sed 's|#include "../compat.h"|#include "../ref_compat.h"|'
	} > "$D/$f"
done
