#!/usr/bin/env bash
#
# Run Resolume's bundled demo clips through the plugin and write one still
# per clip, three seconds in, for a look.
#
#     tools/clips.sh [out-dir] [--set "Name=value" ...]
#
# Not part of verify.sh: it needs ffmpeg and a Resolume install, and the
# result is a picture to judge by eye -- which is what "the defaults look
# good on real footage" comes down to. The stills are 640x360, decoded with
# the clip's own alpha (the demo clips are DXV with alpha), and written with
# the plugin's output alpha; `alpha.txt` beside them lists each still's
# alpha range, which is the deliberate decision in AGENTS.md made visible.
set -uo pipefail
cd "$(dirname "$0")/.."

OUT="${1:-${TMPDIR:-/tmp}/flipdot-clips}"
shift || true
mkdir -p "$OUT"
MEDIA="/Applications/Resolume Arena/media"
FDTEST="${FDTEST:-build/fdtest}"
: >"$OUT/alpha.txt"

for clip in "AV/Beat 001.mov" "AV/Bass 003.mov" "AV/Synth 002.mov" "Shop74/Trinity_09.mov" \
            "Shop74/IntoTheGlow_02.mov" "Shop74/Cyberspace_09.mov" "Shop74/OrganicMotions_06.mov" \
            "Shop74/NeonRoom2_32.mov" "Shop74/FogAndDust_3.mov" "Shop74/Metalive 01.mov"; do
	[ -f "$MEDIA/$clip" ] || continue
	name="$(basename "${clip%.*}" | tr ' ' '_')"
	ffmpeg -v error -i "$MEDIA/$clip" -t 3 -vf "scale=640:360,fps=60" -f rawvideo -pix_fmt rgba - \
		| "$FDTEST" --pipe --size 640x360 --fps 60 --frames 180 "$@" 2>/dev/null \
		| tail -c $(( 640 * 360 * 4 )) >"$OUT/$name.rgba"
	ffmpeg -v error -y -f rawvideo -pix_fmt rgba -s 640x360 -i "$OUT/$name.rgba" "$OUT/$name.png"
	python3 - "$OUT/$name.rgba" "$name" >>"$OUT/alpha.txt" <<'PY'
import sys
data = open(sys.argv[1], "rb").read()
alpha = data[3::4]
print(f"{sys.argv[2]}: alpha {min(alpha)}..{max(alpha)}, {sum(1 for a in alpha if a < 255)} pixels under 255")
PY
	rm -f "$OUT/$name.rgba"
	echo "$OUT/$name.png"
done
