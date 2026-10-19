#!/bin/bash
# Differential host test of the generated T41 vendor AE (see
# t41_vae/vae_diff_harness.c): reference = git ref (default HEAD~0 files of
# REF, e.g. origin/next) vs the working tree.  Prints "t41_vae_diff: IDENTICAL"
# when every per-step digest matches.
#   t41_vae_diff.sh [REF] [SEEDS] [STEPS]
set -e
HERE=$(cd "$(dirname "$0")" && pwd); REPO=$HERE/..; REF=${1:-origin/next}; N=${2:-200}; S=${3:-8}
W=$(mktemp -d); [ -n "$KEEP" ] || trap 'rm -rf $W' EXIT
prep() { # $1 = out prefix, stdin: header, $2: inc
	sed -n '/^#include "tx_isp_t41_vae_gen.inc"/q;p' > $1.top.h
	sed -i -e 's/^\(static u32 vae_[a-z_]*\[[^;]*\)\(;\)/\1 __attribute__((section(".vdata")))\2/' \
	       -e 's/a >= 0x80000000U \&\& a < 0xfffff000U \&\& !(a \& align)/harness_ok(a, align)/' \
	       -e 's/tg < 0x80000000U || (tg \& 3)/tg < 0x1000U || (tg \& 3)/' \
	       -e 's/return ((vae_fn_t)(uintptr_t)tg)(a0, a1, a2, a3, sp);/return harness_dispatch(tg, a0, a1, a2, a3, sp);/' $1.top.h
	echo '#endif' >> $1.top.h
	grep -q harness_ok $1.top.h
	sed -e 's/u32 sp = (u32)(uintptr_t)vframe;/memset(vframe, 0x5a, sizeof vframe); u32 sp = (u32)(uintptr_t)vframe;/' -e 's/^static const u8 vae_rodata\(\[[0-9]*\]\) __aligned(8)/static u8 vae_rodata\1 __aligned(8) __attribute__((section(".vdata")))/' $2 > $1.gen.inc
}
git -C $REPO show $REF:driver/t41/tx_isp_t41_vae.h | prep $W/ref <(git -C $REPO show $REF:driver/t41/tx_isp_t41_vae_gen.inc)
prep $W/new <(cat $REPO/driver/t41/tx_isp_t41_vae_gen.inc) < $REPO/driver/t41/tx_isp_t41_vae.h
grep -o '^u64 vae_[A-Za-z0-9_]*' $W/new.gen.inc | sed 's/u64 vae_/F(/; s/$/)/' > $W/vae_fns.h
grep -o '^u64 vae_[A-Za-z0-9_]*' $W/new.gen.inc | sed 's/u64 vae_/F(/; s/$/)/' | head -17 > $W/vae_roots.h
for v in ref new; do
	gcc -O1 -g0 -falign-functions=16 -no-pie -fno-pie -w -fno-strict-aliasing -Wl,--section-start=.vdata=0x30000000 -I$W -DVAE_TOP="\"$W/$v.top.h\"" -DVAE_INC="\"$W/$v.gen.inc\"" \
		$HERE/t41_vae/vae_diff_harness.c -o $W/$v
	${TMO:+timeout $TMO }setarch -R $W/$v $N $S > $W/$v.out
done
if cmp -s $W/ref.out $W/new.out; then
	echo "t41_vae_diff: IDENTICAL ($(grep -c . $W/new.out) lines, $N seeds x $S steps, faults max $(sed -n 's/.* f=\([0-9]*\) .*/\1/p' $W/new.out | sort -n | tail -1))"
else
	echo "t41_vae_diff: DIFFER"; diff $W/ref.out $W/new.out | head -5; cp $W/ref.out $W/new.out /tmp/ 2>/dev/null; exit 1
fi
