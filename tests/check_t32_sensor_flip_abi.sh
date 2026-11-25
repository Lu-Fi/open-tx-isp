#!/bin/sh
# check_t32_sensor_flip_abi.sh [driver/t32]   (Lu-Fi/openimp#898)
# The T32 2.x sensor flip event TX_ISP_EVENT_SENSOR_HVFLIP (0x2000010) passes { vinum, struct
# tx_isp_hvflip_par * } (vendor tx_isp_core_shvflip; vendor sensor_gc4653_t32.ko gc4653_sensor_ops_ioctl
# 0x12f8: lw s3,4(a2); sw 2,4(s3); sw 0,8(s3); lw v0,0(s3) = mode).  A sensor port from an older SDK that
# reads word 1 as the mode value sees a kernel pointer, matches no mode and writes nothing (ret 0): the
# flip "works" without moving the picture.  Guards:
#  1. t32_core_sensor_flip passes { ch, msg } with msg[3] = { mode, 0, 0 } (hvflip, drop_frame, reset),
#     never the mode itself as word 1;
#  2. the sensor's drop_frame answer (msg[1]) is kept and shown in the "t32 flip:" proc line
#     ("sensor frames", 0 = the sensor did not answer = suspect port);
#  3. driver/t32/README.md documents the ABI for sensor ports and the SENSOR_PROC_OWNED_BY_ISP build flag (#900).
d=${1:-$(dirname "$0")/../driver/t32}
core=$d/tx_isp_t32_core.c hw=$d/tx_isp_t32_hw.c rd=$d/README.md
for f in "$core" "$hw" "$rd"; do [ -f "$f" ] || { echo "usage: $0 [driver/t32]" >&2; exit 2; }; done
fail=0
body() { awk -v s="$2" 'index($0, s) == 1 {on = 1} on {print} on && /^}/ {exit}' "$1"; }
need() { echo "$1" | grep -q -- "$2" || { echo "check_t32_sensor_flip_abi: $3"; fail=1; }; }
c=$(body "$core" 'int t32_core_sensor_flip(')
need "$c" 'u32 msg\[3\] = { mode, 0, 0 };' 'msg must be { mode, drop_frame 0, reset 0 } (tx_isp_hvflip_par)'
need "$c" 'struct { u32 vinum; u32 \*msg; } sv = { ch, msg };' 'the event argument must be { vinum, &msg }, not the mode'
need "$c" 'T32_EVENT_SENSOR_FLIP, &sv' 'the flip event must pass &sv'
need "$c" '\*frames = msg\[1\];' 'the sensor drop_frame answer (msg[1]) must be returned'
need "$(body "$hw" 'static void t33_flip_work(')" 't33_flip_st.sns_frames = frames;' 'flip work does not keep the sensor answer'
need "$(body "$hw" 'void t32_flip_show(')" 'sensor frames %u' 'the proc line does not show the sensor answer'
need "$(cat "$rd")" 'tx_isp_hvflip_par' 'README does not document the sensor flip ABI'
need "$(cat "$rd")" 'SENSOR_PROC_OWNED_BY_ISP' 'README does not tell sensor ports to build with SENSOR_PROC_OWNED_BY_ISP (#900)'
[ $fail -eq 0 ] && echo "check_t32_sensor_flip_abi: ok"
exit $fail
