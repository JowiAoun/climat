#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Google Play: check the listing, and send a bundle, the listing or a promotion.
#
#   scripts/play.sh check                 hold the listing to Play's limits (CI gate)
#   scripts/play.sh stage [dir]           the listing in fastlane's layout, in build/play
#   scripts/play.sh upload <file.aab>     the bundle to $PLAY_TRACK, with its release
#                                         note, the text, the icon and the screenshots
#   scripts/play.sh listing               the text and the pictures, no bundle
#   scripts/play.sh promote <from> <to>   this version's release from one track to another
#
# `check` and `stage` need python3 and nothing else. The other three send
# things to Google with fastlane's `supply`, from `nix develop .#play`, and
# need a service account key, as the file's contents or its path:
#
#   PLAY_SERVICE_ACCOUNT_JSON   the key itself (how the release workflow has it)
#   PLAY_SERVICE_ACCOUNT_FILE   a path to the key
#
# And two settings, with the defaults the release workflow uses:
#
#   PLAY_TRACK            internal      internal, alpha (closed), beta (open) or production
#   PLAY_RELEASE_STATUS   completed     draft until the app's first release is
#                                       out; Play refuses anything else before
#   PLAY_ROLLOUT                        a fraction such as 0.2, for a staged
#                                       rollout to production
#
# docs/releasing.md says how to make the key and what has to be done by hand
# in the Play Console before any of this works.
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"

package="io.github.JowiAoun.Climat"
staged="$root/build/play/metadata"
track="${PLAY_TRACK:-internal}"
status="${PLAY_RELEASE_STATUS:-completed}"

# fastlane asks its own servers for updates and reports usage to them unless
# told not to. This app sends nothing anywhere it does not have to, and its
# release tooling should not either.
export FASTLANE_OPT_OUT_USAGE=1
export FASTLANE_SKIP_UPDATE_CHECK=1
export FASTLANE_HIDE_CHANGELOG=1
export FASTLANE_HIDE_GITHUB_ISSUES=1
export FASTLANE_DISABLE_COLORS=1
export LC_ALL=C.UTF-8 LANG=C.UTF-8

supply() {
  if ! command -v fastlane > /dev/null 2>&1; then
    echo "play: no fastlane - run this inside \`nix develop .#play\`" >&2
    exit 1
  fi
  if [ -n "${PLAY_SERVICE_ACCOUNT_JSON:-}" ]; then
    export SUPPLY_JSON_KEY_DATA="$PLAY_SERVICE_ACCOUNT_JSON"
  elif [ -n "${PLAY_SERVICE_ACCOUNT_FILE:-}" ]; then
    export SUPPLY_JSON_KEY="$PLAY_SERVICE_ACCOUNT_FILE"
  else
    echo "play: no key - set PLAY_SERVICE_ACCOUNT_JSON or PLAY_SERVICE_ACCOUNT_FILE" >&2
    exit 1
  fi
  fastlane supply --package_name "$package" "$@"
}

stage() {
  rm -rf "$staged"
  python3 "$here/play-listing.py" stage "$staged"
}

# The version code of the version in CMakeLists.txt, the same sum
# app/CMakeLists.txt gives Qt.
version_code() {
  local version major minor patch
  version="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -1)"
  IFS=. read -r major minor patch <<< "$version"
  echo $((major * 10000 + minor * 100 + patch))
}

rollout=()
if [ -n "${PLAY_ROLLOUT:-}" ]; then
  rollout=(--rollout "$PLAY_ROLLOUT")
fi

command="${1:-}"
shift || true

case "$command" in
  check)
    python3 "$here/play-listing.py" check
    ;;

  stage)
    if [ -n "${1:-}" ]; then
      python3 "$here/play-listing.py" stage "$1"
    else
      stage
    fi
    ;;

  upload)
    aab="${1:-}"
    if [ -z "$aab" ] || [ ! -f "$aab" ]; then
      echo "play: usage: play.sh upload <file.aab>" >&2
      exit 2
    fi
    # Play refuses an unsigned bundle with an error that does not say so.
    if ! jarsigner -verify "$aab" 2> /dev/null | grep -q '^jar verified'; then
      echo "play: $aab is not signed - docs/releasing.md says how to add the upload key" >&2
      exit 1
    fi
    stage
    # The same images every release, so only the ones that changed go up.
    supply --aab "$aab" --track "$track" --release_status "$status" "${rollout[@]}" \
      --metadata_path "$staged" --sync_image_upload true
    ;;

  listing)
    stage
    # Play keeps the listing per app and the release notes per release, and
    # supply looks a release up even for the listing: so it is read from
    # $PLAY_TRACK, and the notes are left alone.
    supply --track "$track" --metadata_path "$staged" --sync_image_upload true \
      --skip_upload_changelogs true
    ;;

  promote)
    if [ $# -ne 2 ]; then
      echo "play: usage: play.sh promote <from track> <to track>" >&2
      exit 2
    fi
    supply --track "$1" --track_promote_to "$2" --version_code "$(version_code)" \
      --track_promote_release_status "$status" "${rollout[@]}" \
      --skip_upload_metadata true --skip_upload_images true \
      --skip_upload_screenshots true --skip_upload_changelogs true
    ;;

  *)
    sed -n '5,11p' "${BASH_SOURCE[0]}" | sed 's/^# \?//' >&2
    exit 2
    ;;
esac
