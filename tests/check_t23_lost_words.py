#!/usr/bin/env python3
"""check_t23_lost_words.py [driver/t23]  (Lu-Fi/openimp#770)

The T23 lift resolved vendor .bss-relative references (lui %hi(.bss+X) /
%lo) to the module words ivdc_threshold_line / ivdc_mem_line / direct_mode /
isp_memopt.  Since #770 these names are private statics of the lifted code.
An object analysis (tisp core built -fno-inline, each word in its own
section, call graph from init/exit, data tables, __ksymtab and the other
objects) showed that only private_driver_get_interface and
private_copy_to_user reached them at run time; both are fixed to the vendor
targets (pfaces, .bss+0x170).  Every other function that still names the
words is listed in t23_lost_words_baseline.txt: it is unreachable, or it
only loads the address into a variable that is never dereferenced.

Fails when
- private_driver_get_interface / private_copy_to_user lose the vendor form,
- apical_isp_core_ops_g_ctrl calls private_copy_to_user with one argument,
- a function outside the baseline names one of the words (outside #if 0).
"""
import os, re, sys

d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'driver', 't23')
src = os.path.join(d, 'tx_isp_t23_core.c')
base = os.path.join(os.path.dirname(os.path.abspath(__file__)), 't23_lost_words_baseline.txt')
L = open(src).read().split('\n')
fail = []

# strip #if 0 blocks (with nesting)
act = []; depth = 0
for l in L:
    s = l.strip()
    if depth:
        if s.startswith('#if'): depth += 1
        elif s.startswith('#endif'): depth -= 1
        act.append('')
        continue
    if re.match(r'#if\s+0\b', s):
        depth = 1; act.append(''); continue
    act.append(l)

def bodies():
    fn = None; body = []
    for i, l in enumerate(act):
        if fn is None:
            if l == '{' or re.match(r'^[A-Za-z_].*\)\s*\{\s*$', l):
                j = i if l != '{' else i - 1
                hdr = act[j]
                while j > 0 and '(' not in hdr:
                    j -= 1; hdr = act[j] + hdr
                m = re.findall(r'(\w+)\s*\(', act[j])
                fn = m[0] if m else '?'; body = []
        else:
            if l.startswith('}'):
                yield fn, '\n'.join(body); fn = None
            else:
                body.append(l)

words = re.compile(r'&(ivdc_threshold_line|ivdc_mem_line|direct_mode|isp_memopt)\b')
allowed = set(x.split()[0] for x in open(base) if x.strip() and not x.startswith('#'))
users = set(); seen = {}
for fn, b in bodies():
    seen.setdefault(fn, []).append(b)
    if words.search(b):
        users.add(fn)

for fn in sorted(users - allowed):
    fail.append('%s names a lost module word (not in the baseline; check the vendor target)' % fn)

def one(fn):
    b = seen.get(fn)
    if not b:
        fail.append('%s not found' % fn); return ''
    return b[-1]

b = one('private_copy_to_user')
if not re.search(r'return\s+copy_to_user\(\s*to\s*,\s*from\s*,\s*size\s*\)\s*;', b):
    fail.append('private_copy_to_user is not the vendor pfaces->priv_copy_to_user (= copy_to_user(to, from, size))')
b = one('private_driver_get_interface')
if 'pfaces' not in b or 'private_get_driver_interface' not in b or '-1' not in b:
    fail.append('private_driver_get_interface does not fill pfaces / return -1 on NULL (vendor @0x9d88)')
b = one('apical_isp_core_ops_g_ctrl')
if re.search(r'private_copy_to_user\)\((a0|\*\(uint32_t \*\)\(\(char \*\)\(s0\) \+ 4\))\)', b):
    fail.append('apical_isp_core_ops_g_ctrl calls private_copy_to_user with one argument')

for f in fail:
    print('check_t23_lost_words: ' + f)
if not fail:
    print('check_t23_lost_words: OK (%d baseline users, 0 new)' % len(users))
sys.exit(1 if fail else 0)
