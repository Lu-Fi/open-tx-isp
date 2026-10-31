#!/usr/bin/env python3
"""T41 #134/#162: every user-reachable call of the sensor core register op must run under t41_sensor_ops_mutex.

t41_sensor_ops_quiesce() (lock+unlock of the mutex) is the barrier that sensor release takes before the
i2c client is freed (#134).  A sensor call outside the mutex is not covered by it (review-week-1030 M4).
The check looks at every call of t41_call_sensor_core_register(): the nearest earlier mutex_lock must be
t41_sensor_ops_mutex with no mutex_unlock in between, and the next mutex_unlock must be the same mutex.
Usage: t41_sensor_barrier_check.py [driver.c]   (default ../driver/t41/tx_isp_t41_recovered.c)
"""
import os, re, sys

src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                          "../driver/t41/tx_isp_t41_recovered.c")
text = open(src, encoding="utf-8", errors="replace").read()
MUT = "&t41_sensor_ops_mutex"
fn = "t41_call_sensor_core_register"
bad, n = [], 0
for m in re.finditer(r"\b%s\s*\(" % fn, text):
    line_start = text.rfind("\n", 0, m.start()) + 1
    head = text[line_start:m.start()]
    if re.match(r"\s*(static\s+)?(int|int32_t)\b", head) and not head.strip().endswith(("?", "=", "(")) and "ret" not in head:
        continue                                    # the definition
    n += 1
    line = text.count("\n", 0, m.start()) + 1
    before = text[:m.start()]
    locks = [x for x in re.finditer(r"mutex_(lock|unlock)\(([^)]*)\)", before[-1500:])]
    ok = bool(locks) and locks[-1].group(1) == "lock" and locks[-1].group(2).strip() == MUT
    after = re.search(r"mutex_(lock|unlock)\(([^)]*)\)", text[m.end():m.end() + 800])
    ok = ok and after and after.group(1) == "unlock" and after.group(2).strip() == MUT
    if not ok:
        bad.append(line)
if n < 2:
    print("FAIL: found only %d call sites of %s (expected S_REGISTER and G_REGISTER)" % (n, fn)); sys.exit(1)
for l in bad:
    print("FAIL: %s:%d %s outside t41_sensor_ops_mutex (the #134 barrier does not cover it)" % (src, l, fn))
if bad:
    sys.exit(1)
print("t41_sensor_barrier_check: %d sensor register call sites, all under t41_sensor_ops_mutex" % n)
