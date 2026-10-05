#!/bin/sh
# compare.sh OUT CALIB REF VARIANT...
# Runs each scenario (compact "simple" AE/AWB default, OEM paths, and the
# extended / register-poke scenarios when they are selected) on REF and on each
# VARIANT and requires identical output: trace hash per checkpoint, every
# printed line, and the hash of every firmware data object present in both
# builds (pointers normalised to symbol+offset).  A firmware constant that
# merely lies inside the image (e.g. packed table bytes 0x080b0d04) is
# normalised to a different symbol in each build; when an object's hash differs
# between two builds, both are re-run with dump=<object> and the object is
# accepted only if every word has the same normalised form or the same raw
# value (an identical raw value cannot be a pointer into two different
# layouts).  The accepted words are listed.  REF is also run with two
# stack-poison bytes; a difference there means the firmware reads uninitialised
# stack memory.
#
# SCENARIOS selects the scenario list, default "simple oem ext ext-oem".
set -u
LC_ALL=C; export LC_ALL
out=$1; calib=${2:--}; ref=$3; shift 3
[ -n "$calib" ] || calib=-
fail=0
run() { # variant args outfile
	# no ASLR: stack addresses must be reproducible between runs
	setarch -R "$out/t20fw-$1" "$out/t20fw-$1.sym" "$calib" $2 > "$3" 2>&1
	echo "exit=$?" >> "$3"
}
cmp_files() { # a b label [variant-a variant-b args]
	awk -v stf="$1.st" '
	FNR == 1 { f++ }
	/^T20FW harness:/ { next }
	/^STATE / { st[f, $2 " " $3] = $4; names[$2 " " $3] = names[$2 " " $3] + f; next }
	/^CP / { sub(/ state=.*/, ""); }
	{ line[f, ++n[f]] = $0 }
	END {
		rc = 0; shown = 0
		m = n[1] > n[2] ? n[1] : n[2]
		for (i = 1; i <= m; i++)
			if (line[1, i] != line[2, i]) {
				if (shown++ < 6) printf "  < %s\n  > %s\n", line[1, i], line[2, i]
				rc = 1
			}
		for (k in names)
			if (names[k] == 3 && st[1, k] != st[2, k]) print k > stf
			else if (names[k] != 3) only[substr(k, index(k, " ") + 1)] = 1
		for (k in only) o = o " " k
		if (o != "") print "  objects in one build only:" o
		exit rc
	}' "$1" "$2" > "$1.cmp"
	rc=$?
	if [ -s "$1.st" ]; then
		if [ $# -ge 6 ] && [ "$4" != "$5" ]; then
			recheck_state "$4" "$5" "$6" "$1.st" >> "$1.cmp" || rc=1
		else
			sed 's/^/  state differs: /' "$1.st" | head -30 >> "$1.cmp"; rc=1
		fi
	fi
	rm -f "$1.st"
	if [ $rc = 0 ]; then echo "  same: $3"; else echo "  DIFF: $3"; fi
	sort "$1.cmp" | head -40
	return $rc
}
# Per-word re-check of objects whose state hash differs between two builds:
# a word is equal when its normalised hash or its raw value matches.
recheck_state() { # variant-a variant-b args keyfile
	for robj in $(sed 's/^[^ ]* //; s/#[0-9]*$//' "$4" | sort -u); do
		run "$1" "$3 dump=$robj" "$out/dump-$1-$robj.txt"
		run "$2" "$3 dump=$robj" "$out/dump-$2-$robj.txt"
		grep '^DUMP ' "$out/dump-$1-$robj.txt" > "$out/dump-$1-$robj.d"
		grep '^DUMP ' "$out/dump-$2-$robj.txt" > "$out/dump-$2-$robj.d"
	done
	rrc=0
	while read -r rtag robjn; do
		awk -v tag="$rtag" -v objn="$robjn" '
		$1 != "DUMP" || $2 != tag || substr($3, 1, index($3, "+") - 1) != objn { next }
		{ if (FILENAME != last) { f++; last = FILENAME }
		  n[f]++; raw[f, n[f]] = $4; nh[f, n[f]] = $5; at[n[f]] = $3 }
		END {
			if (n[1] != n[2] || n[1] == 0) { print "  state differs: " tag " " objn " (no comparable dump)"; exit 1 }
			bad = 0; c = ""
			for (i = 1; i <= n[1]; i++) {
				if (nh[1, i] == nh[2, i]) continue
				if (raw[1, i] == raw[2, i]) { c = c " " at[i] "=" raw[1, i]; continue }
				if (bad++ < 4) print "  state differs: " tag " " objn " at " at[i] ": " raw[1, i] " vs " raw[2, i]
			}
			if (bad) exit 1
			print "  state same per word: " tag " " objn ", raw-identical in-image constants:" c
		}' "$out/dump-$1-$robj.d" "$out/dump-$2-$robj.d" || rrc=1
	done < "$4"
	return $rrc
}

# scenario name -> harness arguments (SCENARIOS is a plain word list)
scenario_args() {
	case $1 in
	simple) echo "";;
	ext) echo "ext";;
	ext-oem) echo "ext oem";;
	poke) echo "poke";;
	poke-oem) echo "poke oem";;
	*) echo "$1";;
	esac
}

for s in ${SCENARIOS:-simple oem ext ext-oem}; do
	scen=$(scenario_args "$s"); tag=$s
	run "$ref" "p5a $scen" "$out/run-$ref-$tag-p5a.txt"
	run "$ref" "pa5 $scen" "$out/run-$ref-$tag-pa5.txt"
	echo "[$tag] $ref, stack poison 0x5a vs 0xa5 (uninitialised stack reads)"
	cmp_files "$out/run-$ref-$tag-p5a.txt" "$out/run-$ref-$tag-pa5.txt" "$ref $tag" || fail=1
	for v in "$@"; do
		run "$v" "p5a $scen" "$out/run-$v-$tag-p5a.txt"
		run "$v" "pa5 $scen" "$out/run-$v-$tag-pa5.txt"
		echo "[$tag] $ref vs $v"
		cmp_files "$out/run-$ref-$tag-p5a.txt" "$out/run-$v-$tag-p5a.txt" "$ref/$v $tag" "$ref" "$v" "p5a $scen" || fail=1
		echo "[$tag] $v, stack poison 0x5a vs 0xa5"
		cmp_files "$out/run-$v-$tag-p5a.txt" "$out/run-$v-$tag-pa5.txt" "$v $tag" || fail=1
	done
done
grep -h '^END\|^ABORT\|^BUG\|^exit=' "$out"/run-*.txt | sort | uniq -c
# ae_calculate_target used to divide by the high word of a 64-bit sum (0):
# the OEM AE scenario starts at minimum exposure and must not divide by zero
# behavioural checks inside the scenarios (e.g. AWB MANUAL -> AUTO)
if grep -l '^FAIL' "$out"/run-*.txt; then
	grep -h '^FAIL' "$out"/run-*.txt | sort | uniq -c; fail=1
fi
if grep -l 'div64-by-zero=[1-9]' "$out"/run-*.txt; then
	echo "FAIL: div64 by zero"; fail=1
fi
exit $fail
