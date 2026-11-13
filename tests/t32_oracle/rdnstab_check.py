#!/usr/bin/env python3
# openimp #234: the RDNS row table of driver/t32/tx_isp_t32_rdnstab.h must equal the vendor
# words of t32_data_c0ac0 in Matt Davis' recovered T32 tree (PR #15 @ 2e2e06bc, driver/t32/data.c,
# offset from data-aliases.lds).  Sibling of tmotab_check.py.  The header is compiled on the host
# and dumped, so the check sees the values the driver gets.  Also checked:
#  - 128 rows of 32 u16, ending where the next vendor symbol (t32_data_c2ac0) starts;
#  - every "row N, data.c:A..B" comment names the data.c lines of that row;
#  - t32_rdns_ctx_init (the driver's per-frame context) leaves context, parameter block and
#    register writes exactly as the ported t32_rdns_init on a tuning base (random sections);
#  - t32_rdns_rows_ok accepts selectors <= 127 and refuses 128 in each of the 33 positions;
#  - the check itself: edited copies of the header (one value, one comment, one row less) FAIL.
#   usage: rdnstab_check.py <open-tx-isp git dir> [header]
import os, re, subprocess, sys, tempfile

g = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
hdr = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, '../../driver/t32/tx_isp_t32_rdnstab.h'))
show = lambda p: subprocess.run(['git', '-C', g, 'show', '2e2e06bc:driver/t32/' + p],
                                check=True, capture_output=True, text=True).stdout
if subprocess.run(['git', '-C', g, 'cat-file', '-e', '2e2e06bc^{commit}'], capture_output=True).returncode != 0:
    # the recovered tree (upstream PR #15) is not part of next; the check runs where 2e2e06bc is fetched
    print('%s: SKIP (reference tree 2e2e06bc not in this repository: upstream PR #15 pending; run on claude/t32-ds-batch)'
          % os.path.basename(__file__)[:-3])
    sys.exit(2 if os.environ.get('T40_ORACLE_STRICT') == '1' else 0)
aliases, lines = show('data-aliases.lds'), show('data.c').split('\n')
ROWS, COLS = 128, 32


def alias(sym):
    m = re.search(r'^t32_data_%s = t32_section_(\w+) \+ (0x[0-9a-f]+);' % sym, aliases, re.M)
    if not m:
        sys.exit('no alias for t32_data_' + sym)
    return m.group(1), int(m.group(2), 16)


sec, off = alias('c0ac0')
nsec, noff = alias('c2ac0')
if sec != '_rodata' or nsec != sec or noff - off != ROWS * COLS * 2:
    sys.exit('rdnstab_check: t32_data_c0ac0 is not 128 x 64 bytes of .rodata before t32_data_c2ac0')
start = next(i for i, l in enumerate(lines) if l.startswith('union t32_word t32_section_%s[' % sec))
vend, vline = [], []          # vendor u16 values, data.c line (1-based) of each word
for k in range(off // 4, (off + ROWS * COLS * 2) // 4):
    w = re.search(r'\.value = 0x([0-9a-f]{8})U', lines[start + 1 + k])
    if not w:
        sys.exit('t32_data_c0ac0: data.c:%d is not a plain value' % (start + 2 + k))
    w = int(w.group(1), 16)
    vend += [w & 0xffff, w >> 16]
    vline.append(start + 2 + k)

DUMP = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int16_t s16; typedef int32_t s32;
typedef uint64_t u64; typedef int64_t s64;
#include "tx_isp_t32_rdns.h"
#include "%s"
static u32 rs = 0x234u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static void *al(unsigned long n) { return malloc(n); }
int main(void)
{
	unsigned int r, i, c, k, bad = 0;
	static u8 tb[T32_RDNS_SEC_OFF + T32_RDNS_SEC_BYTES], sc[T32_RDNS_SEC_BYTES], pa[T32_RDNS_PARAM_BYTES];
	static struct t32_front_write wa[T32_RDNS_MAX_WRITES], wb[T32_RDNS_MAX_WRITES];
	struct t32_rdns_ctx *a, b;

	printf("rows %%u cols %%u\n", (unsigned)(sizeof(t32_rdns_tab_c0ac0) / sizeof(t32_rdns_tab_c0ac0[0])),
	       (unsigned)(sizeof(t32_rdns_tab_c0ac0[0]) / sizeof(t32_rdns_tab_c0ac0[0][0])));
	for (r = 0; r < sizeof(t32_rdns_tab_c0ac0) / sizeof(t32_rdns_tab_c0ac0[0]); r++)
		for (i = 0; i < 32; i++)
			printf("v %%u %%u %%u\n", r, i, (unsigned)t32_rdns_tab_c0ac0[r][i]);
	for (c = 0; c < 3000; c++) {
		int na, nb;

		for (i = 0; i < sizeof(tb); i++)
			tb[i] = (u8)rnd();
		for (i = 0; i < T32_RDNS_TAB_SEL; i++)
			tb[T32_RDNS_SEC_OFF + T32_RDNS_SEC_U8(0) + i] = (u8)(c %% 3 ? rnd() %% 128 : 127 - rnd() %% 4);
		memcpy(sc, tb + T32_RDNS_SEC_OFF, sizeof(sc));
		memset(pa, 0xa5, sizeof(pa));
		memset(wa, 0, sizeof(wa));
		memset(wb, 0, sizeof(wb));
		na = t32_rdns_init(&a, al, al, tb, (const u8 *)t32_rdns_tab_c0ac0, wa);
		nb = t32_rdns_ctx_init(&b, sc, pa, wb);
		if (na != nb || na <= 0 || memcmp(wa, wb, sizeof(wa)) || a->gain != b.gain || a->rsvd != b.rsvd ||
		    a->rows != b.rows || b.param != pa || b.sec != sc || memcmp(a->param, b.param, T32_RDNS_PARAM_BYTES) ||
		    memcmp(a->sec, b.sec, T32_RDNS_SEC_BYTES) || !t32_rdns_rows_ok(sc))
			bad++;
		free(a->param);
		free(a);
	}
	printf("ctxinit %%u %%u\n", c, bad);
	bad = 0;
	for (k = 0; k < T32_RDNS_TAB_SEL; k++) {
		memset(sc, 127, sizeof(sc));
		if (!t32_rdns_rows_ok(sc))
			bad++;
		sc[T32_RDNS_SEC_U8(0) + k] = 128;
		if (t32_rdns_rows_ok(sc))
			bad++;
		sc[T32_RDNS_SEC_U8(0) + k] = 127;
		sc[T32_RDNS_SEC_U8(0) - 1] = sc[T32_RDNS_SEC_U8(0) + T32_RDNS_TAB_SEL] = 255;	/* neighbours are no selectors */
		if (!t32_rdns_rows_ok(sc))
			bad++;
	}
	printf("rowsok %%u\n", bad);
	return 0;
}
'''


def check(h):
    bad = []
    src = open(h).read()
    # row comments: "row N, data.c:A..B" right before row N
    rc = re.findall(r'/\* row (\d+), data\.c:(\d+)\.\.(\d+) \*/', src)
    if len(rc) != ROWS:
        bad.append('%d row comments, expected %d' % (len(rc), ROWS))
    for n, (r, a, b) in enumerate(rc):
        r, a, b = int(r), int(a), int(b)
        if r != n or a != vline[16 * n] or b != vline[16 * n + 15]:
            bad.append('row comment %d: row %d data.c:%d..%d, expected row %d data.c:%d..%d'
                       % (n, r, a, b, n, vline[16 * n], vline[16 * n + 15]))
    with tempfile.TemporaryDirectory() as d:
        c, exe = os.path.join(d, 'dump.c'), os.path.join(d, 'dump')
        open(c, 'w').write(DUMP % h)
        cc = subprocess.run([os.environ.get('HOSTCC', 'cc'), '-O1', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                             '-I', os.path.dirname(h), '-I', os.path.join(here, '../../driver/t32'), '-I', here, c, '-o', exe],
                            capture_output=True, text=True)
        if cc.returncode:
            return bad + ['header does not compile: ' + cc.stderr.strip().split('\n')[0]]
        out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout.split('\n')
    got = {}
    for l in out:
        f = l.split()
        if len(f) == 4 and f[0] == 'rows':
            if (int(f[1]), int(f[3])) != (ROWS, COLS):
                bad.append('table is %s x %s, expected %d x %d' % (f[1], f[3], ROWS, COLS))
        elif len(f) == 4 and f[0] == 'v':
            got[(int(f[1]), int(f[2]))] = int(f[3])
        elif len(f) == 3 and f[0] == 'ctxinit' and f[2] != '0':
            bad.append('t32_rdns_ctx_init differs from t32_rdns_init in %s of %s cases' % (f[2], f[1]))
        elif len(f) == 2 and f[0] == 'rowsok' and f[1] != '0':
            bad.append('t32_rdns_rows_ok wrong in %s cases' % f[1])
    if not any(l.startswith('ctxinit ') for l in out):
        bad.append('no ctxinit result')
    for r in range(ROWS):
        for i in range(COLS):
            if got.get((r, i)) != vend[r * COLS + i]:
                bad.append('t32_rdns_tab_c0ac0[%d][%d] = %s, data.c:%d t32_data_c0ac0: %d'
                           % (r, i, got.get((r, i)), vline[(r * COLS + i) // 2], vend[r * COLS + i]))
    return bad


bad = check(hdr)
if bad:
    print('rdnstab_check: FAIL, %d differences, first: %s' % (len(bad), bad[0]))
    sys.exit(1)

# the check must catch edits of the header
src = open(hdr).read()
first = re.search(r'/\* row 77, data\.c:\d+\.\.\d+ \*/\n\t\{ (\d+)', src)
edits = [('one value', src[:first.start(1)] + str(int(first.group(1)) ^ 1) + src[first.end(1):]),
         ('one line ref', src.replace('row 5, data.c:%d' % vline[80], 'row 5, data.c:%d' % (vline[80] + 1), 1)),
         ('one row less', re.sub(r'\t/\* row 127, [^\n]*\n\t\{[^\n]*\n', '', src, count=1)),
         ('ctx gain', src.replace('ctx->gain = T32_RDNS_GAIN_NONE;', 'ctx->gain = 0;', 1)),
         ('rows_ok bound', src.replace('>= T32_RDNS_TAB_ROWS', '> T32_RDNS_TAB_ROWS', 1))]
with tempfile.TemporaryDirectory() as d:
    for what, text in edits:
        if text == src:
            sys.exit('rdnstab_check: self-test edit "%s" did not apply' % what)
        h = os.path.join(d, os.path.basename(hdr))
        open(h, 'w').write(text)
        if not check(h):
            print('rdnstab_check: FAIL, edited header (%s) passes' % what)
            sys.exit(1)
print('rdnstab_check: PASS, %d values (%d rows) equal data.c @ 2e2e06bc, line refs, ctx init == t32_rdns_init, '
      '%d header edits detected' % (ROWS * COLS, ROWS, len(edits)))
