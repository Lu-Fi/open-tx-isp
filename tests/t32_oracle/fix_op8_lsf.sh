#!/bin/sh
# Build-time fix of the reference copy of tisp_lsf_interrupt_static (ref/ stays untouched): the
# shared env.c already has strong placeholder definitions of tisp_lsf_interrupt_static and
# private_dma_cache_sync; the reference is renamed t32o8_lsf_interrupt_static and its cache sync
# goes to the oracle's recorder t32o8_dma_cache_sync; the channel byte t32_data_e1d78 (e1d74 + 4,
# inside an env.c array) is the oracle's t32o8_e1d78.
sed -e 's/^u64 tisp_lsf_interrupt_static(/u64 t32o8_lsf_interrupt_static(/' \
    -e 's/__asm__("private_dma_cache_sync")/__asm__("t32o8_dma_cache_sync")/' \
    -e 's/__asm__("t32_data_e1d78")/__asm__("t32o8_e1d78")/'
