#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Jowi Aoun
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Every gate CI runs, in one command, before you push.
#
#   scripts/preflight.sh          all of it; configures and builds if needed
#   scripts/preflight.sh --fast   only the checks that read the tree
#
# CLIMAT_BUILD_DIR selects the build; it defaults to build/dev.
#
# ---- why this exists --------------------------------------------------------
#
# CONTRIBUTING.md used to end with six commands to run before pushing, and CI
# runs thirteen. The gap is not a documentation problem: nobody types thirteen
# commands, so what actually happened was that a pull request went red on the
# fourteenth minute of a CI run over something `reuse` would have said in two
# seconds.
#
# So the list lives here, in the order that finds a problem soonest - the checks
# that need no build first, the ones that need a built tree after, the ones that
# render pictures last. Every one of them keeps running after a failure and the
# summary at the end is the whole list, because the answer to "what else is
# wrong" should not cost another twenty minutes.
#
# It is deliberately not a pre-commit hook. A hook that takes twenty minutes is
# a hook people learn to pass --no-verify to, and a gate nobody runs is worse
# than no gate at all: it is a gate everybody believes in.
set -uo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/.." && pwd)"
cd "$root" || exit 1

# Everything below wants the pinned toolchain. Re-enter it rather than reporting
# thirteen missing tools; CLIMAT_PREFLIGHT_INNER stops a second lap if the shell
# somehow still has no reuse in it.
if [ -z "${CLIMAT_PREFLIGHT_INNER:-}" ] && ! command -v reuse > /dev/null 2>&1; then
  if command -v nix > /dev/null 2>&1; then
    echo "preflight: entering the devshell"
    export CLIMAT_PREFLIGHT_INNER=1
    exec nix develop "$root" --command bash "${BASH_SOURCE[0]}" "$@"
  fi
  echo "preflight: no reuse and no nix - run this inside \`nix develop\`" >&2
  exit 2
fi

fast=0
case "${1:-}" in
  --fast) fast=1 ;;
  "") ;;
  -h|--help) sed -n '5,10p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; exit 0 ;;
  *) echo "usage: preflight.sh [--fast]" >&2; exit 2 ;;
esac

build_dir="${CLIMAT_BUILD_DIR:-$root/build/dev}"
log_dir="$(mktemp -d)"
trap 'rm -rf "$log_dir"' EXIT

passed=() failed=() skipped=()

# Runs one check, keeps its output, and prints it only when it is wanted: a
# green line for a pass, the whole log for a failure. Thirteen checks each
# printing their own preamble is a screen nobody reads to the end of.
check() {
  local name="$1"; shift
  local log="$log_dir/${name//[^a-zA-Z0-9]/_}.log"

  printf '  %-26s ' "$name"
  if "$@" > "$log" 2>&1; then
    echo "ok"
    passed+=("$name")
    return 0
  fi

  echo "FAILED"
  failed+=("$name")
  sed 's/^/      /' "$log"
  echo
  return 1
}

skip() {
  printf '  %-26s skipped - %s\n' "$1" "$2"
  skipped+=("$1")
}

# A check whose tool is not here says so rather than failing. gjs is the one CI
# installs from apt and a laptop may not have; sway needs a machine that can
# host a compositor.
have() { command -v "$1" > /dev/null 2>&1; }

# The two checks that are a pipeline rather than a command, as functions, so
# that `check` still gets one thing to run and the quoting stays readable. Both
# are called through it by name, which is the indirection SC2329 cannot see.
# shellcheck disable=SC2329
run_shellcheck() { shellcheck --external-sources "$root"/scripts/*.sh; }
# shellcheck disable=SC2329
run_lychee() { git ls-files '*.md' | xargs lychee --offline --no-progress --include-fragments; }

echo
echo "preflight: the tree"

check "reuse lint"          reuse lint
check "shellcheck"          run_shellcheck
check "actionlint"          actionlint
check "zizmor"              zizmor --no-progress .github/workflows
check "typos"               typos
check "links (offline)"     run_lychee
check "project URLs"        bash scripts/check-urls.sh
check "QML module lists"    bash scripts/check-qml-files.sh
check "translatable strings" bash scripts/i18n.sh check
check "icons"               bash scripts/icons.sh check
check "Play listing"        bash scripts/play.sh check

if have gjs; then
  check "GNOME extension"   bash scripts/check-extension.sh
else
  skip "GNOME extension"    "no gjs"
fi

if [ "$fast" -eq 0 ]; then
  echo
  echo "preflight: the build"

  if [ ! -d "$build_dir" ]; then
    check "configure" cmake -S "$root" -B "$build_dir" -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      -DCLIMAT_BUILD_TESTS=ON \
      -DCLIMAT_BUILD_GALLERY=ON \
      -DCLIMAT_DEV_TOOLS=ON
  fi

  if check "build" cmake --build "$build_dir"; then
    # ctest already covers the golden images, the licence check over the linked
    # binaries and the three no-GUI assertions, so none of them is run twice
    # here. tests/CMakeLists.txt is where they are registered.
    check "ctest"             ctest --test-dir "$build_dir" --output-on-failure
    check "qmllint ratchet"   env CLIMAT_BUILD_DIR="$build_dir" bash scripts/check-qmllint.sh
    check "packaging metadata" env CLIMAT_BUILD_DIR="$build_dir" bash scripts/check-packaging.sh
    check "README images"     env CLIMAT_BUILD_DIR="$build_dir" bash scripts/shots.sh check
    check "Play screenshots"  env CLIMAT_BUILD_DIR="$build_dir" bash scripts/play-shots.sh check

    if have sway; then
      check "widgets pin"     bash scripts/check-layer-shell.sh "$build_dir/widgets/climat-widget"
    else
      skip "widgets pin"      "no sway"
    fi
  fi
fi

echo
if [ "${#failed[@]}" -eq 0 ]; then
  printf 'preflight: %d ok' "${#passed[@]}"
  [ "${#skipped[@]}" -gt 0 ] && printf ', %d skipped' "${#skipped[@]}"
  [ "$fast" -eq 1 ] && printf ' (--fast: nothing that needs a build was run)'
  printf '\n'
  exit 0
fi

printf 'preflight: %d ok, %d FAILED' "${#passed[@]}" "${#failed[@]}"
[ "${#skipped[@]}" -gt 0 ] && printf ', %d skipped' "${#skipped[@]}"
printf '\n'
printf '  %s\n' "${failed[@]}"
exit 1
