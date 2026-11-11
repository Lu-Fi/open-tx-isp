#!/bin/sh
# REFFIX for lib_tisp_stream_on: env.c's tisp_single_lsc_rcfg_cfg_trig is a strong do-nothing stub; point the
# reference at the oracle's recording stub t32o7_single_lsc_rcfg_cfg_trig.  Stdin -> stdout.
exec sed 's/__asm__("tisp_single_lsc_rcfg_cfg_trig")/__asm__("t32o7_single_lsc_rcfg_cfg_trig")/'
