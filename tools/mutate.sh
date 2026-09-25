#!/usr/bin/env bash
#
# Mutation-test the harness: change ONE character in the shipped GLSL or the
# disc model, rebuild, and require the check that should notice to fail. A
# harness that passes a mutated plugin is measuring something other than the
# plugin.
#
# Not part of verify.sh (each mutant is an arm64 rebuild of fdtest, a couple
# of minutes in all); run it when a check or the code it guards changes. Each
# mutant is built in its own copy of the tree, so the working tree is never
# touched.
#
#     tools/mutate.sh
#
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$( mktemp -d )"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it means
MUTANTS=(
	"source/Shaders.cpp|			float r = 0.5 * DiscSize * Pitch;|			float r = 0.6 * DiscSize * Pitch;|rotation|GLSL: every disc drawn 20% too wide"
	"source/Shaders.cpp|				vec3 paint = c >= 0.0 ? kBlackFace : FaceColour;|				vec3 paint = c <= 0.0 ? kBlackFace : FaceColour;|wipe|GLSL: the faces swapped, so a disc shows the side it is not on"
	"source/Shaders.cpp|	vec2 px = BoardOrigin + ( vec2( disc ) + vec2( 0.5 + shift, 0.5 ) + offset ) * Pitch;|	vec2 px = BoardOrigin + ( vec2( disc ) + vec2( 0.5 + shift, 1.5 ) + offset ) * Pitch;|changes|GLSL: each disc's mean taken from the cell below it"
	"source/Disc.cpp|			return 2.0 * kPi * s * ( ek - s );|			return 3.0 * kPi * s * ( ek - s );|rotation|model: every rebound half as deep again"
	"source/Disc.cpp|	if( u < 1.0 )|	if( u < 1.0  )|rotation|model: a no-op edit, which must NOT be caught -- the control for the controls"
)

caught=0
missed=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check meaning <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree"
	( cd "$REPO" && tar cf - --exclude='./build*' --exclude='./external' --exclude='./.git' . ) | ( cd "$tree" && tar xf - )
	mkdir -p "$tree/external"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY' || { echo "  MUTANT NOT APPLIED: $meaning"; missed=$(( missed + 1 )); continue; }
import sys
path, a, b = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(path).read()
if text.count(a) != 1:
    print(f"the original text occurs {text.count(a)} times, not once", file=sys.stderr)
    sys.exit(1)
open(path, "w").write(text.replace(a, b))
PY

	if ! cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null 2>&1 \
	   || ! cmake --build "$tree/build" --target fdtest -j4 >/dev/null 2>&1; then
		echo "  (did not build: $meaning)"
		missed=$(( missed + 1 ))
		continue
	fi

	expectFail=1
	case "$meaning" in *"must NOT be caught"*) expectFail=0 ;; esac

	if "$tree/build/fdtest" "--$check" >"$WORK/out.log" 2>&1; then
		if [ "$expectFail" = 1 ]; then
			echo "  MISSED  --$check passed the mutant: $meaning"
			missed=$(( missed + 1 ))
		else
			echo "  ok      --$check passed the no-op edit, as it must: $meaning"
			caught=$(( caught + 1 ))
		fi
	else
		if [ "$expectFail" = 1 ]; then
			echo "  caught  --$check failed the mutant ($(grep -c FAIL "$WORK/out.log") assertion(s)): $meaning"
			caught=$(( caught + 1 ))
		else
			echo "  WRONG   --$check failed a no-op edit: $meaning"
			missed=$(( missed + 1 ))
		fi
	fi
done

echo
echo "$caught of $(( caught + missed )) mutants behaved as expected"
exit $(( missed > 0 ? 1 : 0 ))
