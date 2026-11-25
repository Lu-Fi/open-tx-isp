#!/bin/sh
# check_t32_ipc_dma.sh [driver/t32]   (Lu-Fi/openimp#850, #852)
# Source guard for the T32 (PRJ007) start-up: the IPC start (0x1040 = 1) is only taken by the
# silicon after tisp_top_ipc_init (0x1080..0x10c8), and once the core runs every non-bypassed
# DMA block needs its address: MDNS (tisp_mdns_set_mem_cfg at SET_ISP_MEM_INFO) and the defog
# statistics area (0xa210..0xa21c).  Without them the T32 got no frames (IPC) or hung silently
# (the core wrote to physical 0).  The zero-address guard must stay in front of 0x1040.
d=${1:-$(dirname "$0")/../driver/t32}
[ -f "$d/tx_isp_t32_hw.c" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }
hw=$d/tx_isp_t32_hw.c core=$d/tx_isp_t32_core.c
fail=0
chk() { grep -q -- "$2" "$1" || { echo "check_t32_ipc_dma: missing in $(basename "$1"): $3"; fail=1; }; }
chk "$hw" 'n = t32_top_ipc_init(info, tw);' "T32 tisp_top_ipc_init at input set-up"
chk "$hw" 'isp_wr(0xa210, pa);' "T32 defog statistics DMA address"
chk "$hw" 'n = t32_mdns_set_mem_cfg(&m, NULL, width, height, pa, 0, &chk, w);' "T32 MDNS set_mem_cfg"
chk "$core" 't32_hw_mdns_mem_cfg(bi.word1, vi->width, vi->height, isp_memopt);' "SET_ISP_MEM_INFO -> MDNS set_mem_cfg"
# the guard runs before the IPC start in t32_hw_stream_on
awk '/^void t32_hw_stream_on\(void\)/{f=1} f&&/t32_dma_addr_guard\(\);/{g=NR} f&&/isp_wr\(0x1040, 1\);/{if(!g){print "check_t32_ipc_dma: zero-address guard not before 0x1040 = 1"; exit 1} exit 0}' "$hw" || fail=1
# #852: the ISR acknowledges finished register lists at 0x2202c with st[5] (vendor 0xac390), else
# 0x22014 stays 0x20000 on the silicon; defog stays bypassed until tisp_defog_init is wired (streaks)
chk "$hw" 'isp_wr(T32_RDMA_IRQ_CLR, s4);' "T32 ISR: 0x2202c = st[5] (list done acknowledge, #852)"
chk "$hw" 't32_defog_unwired_bypass();' "T32 defog bypass when the defog path did not attach (#852)"
# #854: tisp_defog_init at stream-on, bit 10 in the block dispatch, the bypass only without it (else no defog)
chk "$hw" 't32_defog_attach(bin, bytes, width, height);' "T32 tisp_defog_init at stream-on (#854)"
chk "$hw" 'if (t32_defog_attached() || (byp & 0x100))' "T32 defog bypass only when the defog did not attach (#854)"
chk "$d/tx_isp_t32.h" 'T32_BLK_IRQ_LCE_BITS | T32_BLK_IRQ_TMO_BITS | T32_BLK_IRQ_DEFOG_BITS' "T32 block dispatch bit 10 (tisp_defog_interrupt_static, #854)"
chk "$d/tx_isp_t32_defogd.c" 't32_defog_par(&defog_c, 0, defog_w, &n);' "T32 event 3: tisp_defog_par (#854)"
chk "$d/Kbuild" 'tx_isp_t32_defogd.o' "T32 defog driver part in the module (#854)"
grep -n 'isp_wr(T32_CORE_IDLE_REQ, 0)' "$hw" && { echo "check_t32_ipc_dma: 0x200 = 0 is back (vendor writes 1 only)"; fail=1; }
# #853: the day/night bin switch writes the CSCCR UV clip (night 0x14054 = 0 -> grey, day 0x7fff80)
chk "$hw" 'isp_wr(0x14054, mode ? 0 : 0x007fff80);' "T32 lib_tisp_set_csccr UV clip at the day/night switch (#853)"
# #859: the AE work unpacks the AE histogram (tisp_ae_interrupt_hist) into the AE block, else GetAeStatistics has one bin
chk "$d/tx_isp_t32_ae.c" 't32_ae_interrupt_hist(aec.zones + 0x4000, t32_hw_rd(T32_AE_CUR), aec.b.B' "T32 AE histogram unpack (#859)"
[ $fail = 0 ] && echo "check_t32_ipc_dma: OK"
exit $fail
