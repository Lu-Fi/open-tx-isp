#!/bin/sh
# check_t32_dma_zero.sh [driver/t32]   (Lu-Fi/openimp#508, #530)
# Source guard: every zeroing page allocation of the T32 / T33 driver goes through t32_dma_zpages
# (tx_isp_t32_hw.c), which writes the zero lines back and drops them from the cache.  Pages zeroed
# through the cache and then used through KSEG1 or by ISP DMA lost their contents on the T33 silicon
# (register lists ran zeros) when the dirty zero lines were evicted later.
d=${1:-$(dirname "$0")/../driver/t32}
[ -f "$d/tx_isp_t32_hw.c" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
fail=0
n=$(grep -n -E '__GFP_ZERO|get_zeroed_page' "$d"/*.c "$d"/*.h "$d"/../t33/*.c 2>/dev/null | grep -v -E '^[^:]*:[0-9]+:\s*(/?\*|//)' | grep -v 'unsigned long p = __get_free_pages(GFP_KERNEL | __GFP_ZERO, order);')
if [ -n "$n" ]; then
	echo "check_t32_dma_zero: zeroing page allocation outside t32_dma_zpages:"; echo "$n"; fail=1
fi
grep -q 'dma_cache_wback_inv(p, PAGE_SIZE << order);' "$d/tx_isp_t32_hw.c" || { echo "check_t32_dma_zero: t32_dma_zpages lost its cache write-back"; fail=1; }
[ $fail = 0 ] && echo "check_t32_dma_zero: OK"
exit $fail
