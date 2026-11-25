#!/bin/sh
# check_t32_defog_lifetime.sh [driver/t32]   (Lu-Fi/openimp#892)
# Source guard: the T32 defog statistics buffer (0xa210..0xa21c, written by the core) lives until module
# exit, as in the vendor (tisp_defog_deinit only from lib_tisp_deinit after tisp_top_deinit, vendor
# tx-isp-t32.ko 0x25d1c, reloc 0x25da0).  The driver detaches the defog at stream-off while only the VICs
# stop, so a per-stream kmalloc / kfree let a frame still in the core write its statistics into freed slab
# memory; plain kmalloc memory was also not cache-clean before the first DMA.
d=${1:-$(dirname "$0")/../driver/t32}
[ -f "$d/tx_isp_t32_defogd.c" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
fail=0
err() { echo "check_t32_defog_lifetime: $*"; fail=1; }
g=$d/tx_isp_t32_defogd.c; h=$d/tx_isp_t32_defog.h; w=$d/tx_isp_t32_hw.c
grep -q '^	\.dma_buf = drv_dma_buf,' "$g" || err "driver ops lost .dma_buf (statistics buffer back to kmalloc)"
grep -q '^	\.dma_put = drv_dma_put,' "$g" || err "driver ops lost .dma_put (statistics buffer freed at stream-off)"
put=$(awk '/^static void drv_dma_put\(/{p=1} p{print} p&&/}/{exit}' "$g")
[ -n "$put" ] || err "drv_dma_put not found"
echo "$put" | grep -q -E 'kfree|free_pages|vfree' && err "drv_dma_put frees the statistics buffer (must live until module exit)"
grep -q 'x->buf = o->dma_buf ? o->dma_buf(T32_DEFOG_DMA_BYTES) :' "$h" || err "tisp_defog_init does not take the buffer from ops->dma_buf"
grep -q 'o->dma_put(x->buf);' "$h" || err "tisp_defog_deinit does not hand the buffer back through ops->dma_put"
area=$(awk '/^void \*t32_defog_dma_area\(/{p=1} p{print} p&&/^}/{exit}' "$w")
[ -n "$area" ] || err "t32_defog_dma_area not found"
echo "$area" | grep -q 't32_dma_zpages(T33_DEFOG_ORDER)' || err "t32_defog_dma_area does not use the clean static pages"
echo "$area" | grep -q 'dma_cache_wback_inv(t33_defog_pages' || err "t32_defog_dma_area lost its cache write-back before the hardware gets it"
echo "$area" | grep -q -E 'kmalloc|free_pages' && err "t32_defog_dma_area allocates / frees outside the static area"
# the static area is freed only in t33_mod_free (module exit)
n=$(grep -n 'free_pages(t33_defog_pages' "$w" | wc -l)
[ "$n" = 1 ] || err "t33_defog_pages freed in $n places (expected only t33_mod_free)"
[ $fail = 0 ] && echo "check_t32_defog_lifetime: OK"
exit $fail
