#!/bin/sh
# t23_hvflip_route_host.sh [driver/t23/tx_isp_t23_core.c]   (Lu-Fi/openimp#829)
# Host proof of the T23 flip routing: regtrace_t23_flip_route / regtrace_t23_hvflip_update are cut out of the
# driver and run against a model sensor (picture V = (sensor value ^ mount) V ^ MSCA V, H likewise).
# Every sequence of HV_FLIP / HFLIP / VFLIP, running_mode switch (streamer re-asserts H/V), stream restart (sensor init) (direct_mode 0 and 1, shvflip 0/1, mount 0/3, sensor refusing)
# must give picture == request == GET; the HFLIP/VFLIP case must route through regtrace_t23_flip_route(mode, false).
f=${1:-$(dirname "$0")/../driver/t23/tx_isp_t23_core.c}
[ -f "$f" ] || { echo "usage: $0 [tx_isp_t23_core.c]" >&2; exit 2; }
grep -q 'regtrace_t23_flip_route(mode, false);' "$f" || { echo "t23_hvflip_route_host: HFLIP/VFLIP (direct_mode 0) does not use regtrace_t23_flip_route"; exit 1; }
t=$(mktemp -d /tmp/t23_hvflip_route.XXXXXX) || exit 2
trap 'rm -rf "$t"' EXIT
awk '/^static bool regtrace_t23_hvflip_sensor_v;/ {on = 1} on {print} on && /^static void regtrace_t23_hvflip_update\(/ {u = 1} u && /^}/ {exit}' "$f" > "$t/route.c"
grep -q 'regtrace_t23_hvflip_update' "$t/route.c" || { echo "t23_hvflip_route_host: functions not found"; exit 1; }
cat > "$t/main.c" <<'EOT'
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#define KERN_WARNING ""
#define printk(...) ((void)0)
static unsigned int sensor_mount_flip, regtrace_t23_hvflip_request, isp, sens, refuse, shv;
static int regtrace_t23_sensor_flip_mode = -1;
static int regtrace_t23_sensor_flip_set(uint32_t m) { if (refuse) return -22; sens = m & 3U; regtrace_t23_sensor_flip_mode = (int)(m & 3U); return 0; }
static void regtrace_t23_isp_flip_apply(uint32_t m) { isp = m & 3U; }
static bool regtrace_t23_sensor_shvflip(void) { return shv; }
#include "route.c"
static void ctl(int id, unsigned v, int dm);
static void event(int id, int dm)	/* 3: running_mode switch (no sensor access) + streamer re-asserts H, V; 4: stream restart */
{
	unsigned r = regtrace_t23_hvflip_request;
	if (id == 3) { ctl(1, r & 1U, dm); ctl(2, (r >> 1) & 1U, dm); return; }
	sens = sensor_mount_flip & 3U;	/* sensor core init: its init orientation (= mount) */
	regtrace_t23_sensor_flip_resync();
}
static void ctl(int id, unsigned v, int dm)	/* id 0 HV_FLIP, 1 HFLIP, 2 VFLIP (driver case logic) */
{
	unsigned bit = id, mode;
	if (id >= 3) { event(id, dm); return; }
	if (!id) { regtrace_t23_hvflip_update(v); return; }
	mode = v ? (regtrace_t23_hvflip_request | bit) : (regtrace_t23_hvflip_request & ~bit);
	if (dm) regtrace_t23_hvflip_update(mode); else regtrace_t23_flip_route(mode, false);
}
int main(void)
{
	unsigned long n = 0, bad = 0, seed = 1;
	for (unsigned cfg = 0; cfg < 16; cfg++) {
		unsigned dm = cfg & 1, mount = (cfg & 2) ? 3 : 0, rf = (cfg >> 3) & 1;
		shv = (cfg >> 2) & 1;
		for (int run = 0; run < 2000; run++) {
			sensor_mount_flip = mount; sens = mount; isp = 0; regtrace_t23_hvflip_request = 0;
			regtrace_t23_hvflip_sensor_v = false; refuse = 0; regtrace_t23_sensor_flip_mode = -1;
			for (int k = 0; k < 8; k++) {
				seed = seed * 1103515245 + 12345;
				int id = (seed >> 16) % 5; unsigned v = (seed >> 20) % (id ? 2 : 4);
				refuse = rf && ((seed >> 24) & 1);	/* a sensor that refuses some events */
				ctl(id, v, dm);
				unsigned pic = ((sens ^ mount) ^ isp) & 3U, req = regtrace_t23_hvflip_request;
				n++;
				if (pic != req) {
					if (bad++ < 5) printf("FAIL cfg dm%u shv%u mount%u refuse%u: step %d id %d v %u -> pic %u req %u (sensor %u isp %u)\n",
							     dm, shv, mount, rf, k, id, v, pic, req, sens, isp);
				}
				if (dm && shv && !rf && (isp & 2U)) {	/* #787: direct mode never gets an MSCA vflip (a refusing sensor falls back to the MSCA, warned) */
					if (bad++ < 5) printf("FAIL dm1 shvflip: MSCA vflip set (step %d)\n", k);
				}
			}
		}
	}
	/* the review case: HV_FLIP(2), VFLIP(0) / VFLIP(1) with direct_mode 0, shvflip 1, mount 0 */
	shv = 1; sensor_mount_flip = 0; sens = 0; isp = 0; regtrace_t23_hvflip_request = 0; regtrace_t23_hvflip_sensor_v = false; refuse = 0;
	ctl(0, 2, 0); ctl(2, 0, 0);
	if (((sens ^ isp) & 3U) != 0) { printf("FAIL HV_FLIP(2) VFLIP(0): picture %u\n", (sens ^ isp) & 3U); bad++; }
	ctl(0, 2, 0); ctl(2, 1, 0);
	if (((sens ^ isp) & 3U) != 2) { printf("FAIL HV_FLIP(2) VFLIP(1): picture %u\n", (sens ^ isp) & 3U); bad++; }
	/* device sequence (Tapo C500, #829): HV 3, H/V off, then a running_mode switch with the streamer re-asserting H/V 1, then a stream restart */
	for (unsigned mt = 0; mt < 4; mt += 3) {
		sensor_mount_flip = mt; sens = mt; isp = 0; regtrace_t23_hvflip_request = 0; regtrace_t23_hvflip_sensor_v = false; regtrace_t23_sensor_flip_mode = -1;
		ctl(1, 1, 0); ctl(2, 1, 0); ctl(0, 3, 0); ctl(1, 0, 0); ctl(2, 0, 0); ctl(1, 1, 0); ctl(2, 1, 0); ctl(3, 0, 0);
		if ((((sens ^ mt) ^ isp) & 3U) != 3) { printf("FAIL device sequence (mount %u): picture %u\n", mt, ((sens ^ mt) ^ isp) & 3U); bad++; }
		ctl(0, 3, 0); ctl(4, 0, 0);
		if ((((sens ^ mt) ^ isp) & 3U) != 3) { printf("FAIL stream restart after HV 3 (mount %u): picture %u\n", mt, ((sens ^ mt) ^ isp) & 3U); bad++; }
	}
	if (bad) { printf("t23_hvflip_route_host: FAIL %lu of %lu\n", bad, n); return 1; }
	printf("t23_hvflip_route_host: PASS (%lu steps, picture == request == GET for HV_FLIP / HFLIP / VFLIP mixes + running_mode switches + stream restarts, dm0/1, shvflip, mount 0/3, refusing sensor)\n", n);
	return 0;
}
EOT
${CC:-cc} -O1 -std=gnu99 -Wall -Wextra -Werror -Wno-unused-function -o "$t/run" "$t/main.c" && "$t/run"
