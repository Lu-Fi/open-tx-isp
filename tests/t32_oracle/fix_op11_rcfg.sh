#!/bin/sh
# Build-time fix of a reference copy (ref/ itself stays untouched) for t32_rcfgv_oracle: env.c also
# defines tisp_single_lsc_rcfg_cfg_trig (a stub) and private_dma_cache_sync (silent), so the copies
# use ref_tisp_single_lsc_rcfg_cfg_trig and the recording op11_dma_cache_sync.
sed -e 's/^u64 tisp_single_lsc_rcfg_cfg_trig(/u64 ref_tisp_single_lsc_rcfg_cfg_trig(/' \
    -e 's/__asm__("private_dma_cache_sync")/__asm__("op11_dma_cache_sync")/'
