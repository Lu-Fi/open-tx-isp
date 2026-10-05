#!/bin/sh
# compare.sh OUT CALIB REF VARIANT...
# Runs both scenarios (compact "simple" AE/AWB default, OEM paths) on REF and
# on each VARIANT and requires identical output: trace hash per checkpoint,
# every printed line, and the hash of every firmware data object present in
# both builds.  REF is also run with two stack-poison bytes; a difference
# there means the firmware reads uninitialised stack memory.
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
cmp_files() { # a b label
	awk '
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
			if (names[k] == 3 && st[1, k] != st[2, k]) { if (shown++ < 30) print "  state differs: " k; rc = 1 }
			else if (names[k] != 3) only[substr(k, index(k, " ") + 1)] = 1
		for (k in only) o = o " " k
		if (o != "") print "  objects in one build only:" o
		exit rc
	}' "$1" "$2" > "$1.cmp"
	rc=$?
	if [ $rc = 0 ]; then echo "  same: $3"; else echo "  DIFF: $3"; fi
	sort "$1.cmp" | head -40
	return $rc
}
for scen in "" "oem"; do
	tag=${scen:-simple}
	run "$ref" "p5a $scen" "$out/run-$ref-$tag-p5a.txt"
	run "$ref" "pa5 $scen" "$out/run-$ref-$tag-pa5.txt"
	echo "[$tag] $ref, stack poison 0x5a vs 0xa5 (uninitialised stack reads)"
	cmp_files "$out/run-$ref-$tag-p5a.txt" "$out/run-$ref-$tag-pa5.txt" "$ref $tag" || fail=1
	for v in "$@"; do
		run "$v" "p5a $scen" "$out/run-$v-$tag-p5a.txt"
		run "$v" "pa5 $scen" "$out/run-$v-$tag-pa5.txt"
		echo "[$tag] $ref vs $v"
		cmp_files "$out/run-$ref-$tag-p5a.txt" "$out/run-$v-$tag-p5a.txt" "$ref/$v $tag" || fail=1
		echo "[$tag] $v, stack poison 0x5a vs 0xa5"
		cmp_files "$out/run-$v-$tag-p5a.txt" "$out/run-$v-$tag-pa5.txt" "$v $tag" || fail=1
	done
done
grep -h '^END\|^ABORT\|^BUG\|^exit=' "$out"/run-*.txt | sort | uniq -c
exit $fail
