#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Render the phone screenshots the Google Play listing shows.
#
#   scripts/play-shots.sh         write packaging/android/play/screenshots/*.png
#   scripts/play-shots.sh check   re-render to a temp dir and diff (CI gate)
#
# CLIMAT_BUILD_DIR selects the build to photograph; it defaults to build/dev.
#
# Play's rules for a phone screenshot: PNG without alpha or JPEG, every side
# between 320 and 3840 px, and the long side no more than twice the short one.
# 540 by 960 is 9:16 and inside both limits. Raw frames, no bezel:
# scripts/shots.sh explains why a store rejects a composite.
#
# One times, not two, and not because two would not fit the rules. The
# software renderer every headless capture in this tree runs on draws a
# layered Flickable wrongly at any scale factor above one - the phone page
# comes out scrolled to its end with its chart and its day glyphs missing -
# and a store picture of that would be a picture of the capture tool. The
# same page on a phone's GPU is right; docs/known-gaps.md has the
# measurement. So these are the pixels the golden images already vouch for,
# and a phone shows the store a sharper copy of the same thing.
#
# The same pinned environment as scripts/shots.sh and scripts/golden.sh, read
# out of scripts/capture-env.sh so that it stays the same: a fixture at a frozen
# clock, every animation collapsed to zero, one fontconfig, so `check` can tell
# a stale image from an edited one.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
build_dir="${CLIMAT_BUILD_DIR:-$root/build/dev}"
binary="$build_dir/app/climat"
images_dir="$root/packaging/android/play/screenshots"

if [ ! -x "$binary" ]; then
  echo "play-shots: no app at $binary - build it first" >&2
  exit 1
fi

# shellcheck source=scripts/capture-env.sh
. "$here/capture-env.sh"

scratch="$(mktemp -d)"
trap 'rm -rf "$scratch"' EXIT
export XDG_CONFIG_HOME="$scratch/config"
export XDG_DATA_HOME="$scratch/data"
export XDG_CACHE_HOME="$scratch/cache"
mkdir -p "$XDG_CONFIG_HOME/Climat"
printf '[time]\nformat=12h\n' > "$XDG_CONFIG_HOME/Climat/climat.ini"

# Name, then the app's arguments. The order is the order Play shows them, so
# the first one is the one most people see: the Today tab, dark, with a
# warning on it - Toronto's fixture carries one.
#
# The calendar is scrolled. The fixture is recorded on the 31st, so from the
# top the frame is four weeks of empty cells, and the forecast is below them
# in the next month.
shots=(
  "01-today|--tab today"
  "02-hourly|--tab hourly"
  "03-monthly|--tab monthly --scroll 480"
  "04-me|--tab me"
  "05-today-light|--tab today --scheme light"
  "06-hourly-light|--tab hourly --scheme light"
)

render_all() {
  local dest="$1" entry name args
  for entry in "${shots[@]}"; do
    name="${entry%%|*}"
    args="${entry#*|}"
    # shellcheck disable=SC2086
    "$binary" --viewport mobile --size 540x960 $args --grab "$dest/$name.png" > /dev/null
    printf 'play-shots: %-18s %s bytes\n' "$name.png" "$(wc -c < "$dest/$name.png")"
  done
}

case "${1:-render}" in
  render)
    mkdir -p "$images_dir"
    render_all "$images_dir"
    ;;
  check)
    tmp="$scratch/shots"
    mkdir -p "$tmp"
    render_all "$tmp" > /dev/null
    drift=0
    for entry in "${shots[@]}"; do
      name="${entry%%|*}"
      if [ ! -f "$images_dir/$name.png" ]; then
        echo "play-shots: $name.png is missing" >&2
        drift=1
      elif ! cmp -s "$tmp/$name.png" "$images_dir/$name.png"; then
        # Beside the recorded one, for scripts/shots.sh's reason.
        cp "$tmp/$name.png" "$images_dir/$name.actual.png"
        echo "play-shots: $name.png does not match what the app renders" >&2
        echo "play-shots:   what it renders is in $name.actual.png" >&2
        drift=1
      fi
    done
    if [ "$drift" -ne 0 ]; then
      echo "play-shots: run \`scripts/play-shots.sh\` and commit the result" >&2
      exit 1
    fi
    echo "play-shots: ${#shots[@]} screenshots match the app"
    ;;
  *)
    echo "usage: play-shots.sh [render|check]" >&2
    exit 2
    ;;
esac
