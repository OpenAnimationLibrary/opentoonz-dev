#!/usr/bin/env bash
# Run before opening or force-pushing an OT-master staging PR.
set -euo pipefail

base_ref="${1:-upstream/master}"
format_bin="${CLANG_FORMAT:-clang-format-14}"

command -v "$format_bin" >/dev/null ||
  { echo "Required formatter not found: $format_bin" >&2; exit 2; }
git rev-parse --verify --quiet "${base_ref}^{commit}" >/dev/null ||
  { echo "Required upstream base not found: $base_ref" >&2; exit 2; }

files=()
while IFS= read -r -d '' file; do
  case "$file" in
    toonz/sources/*.c|toonz/sources/*.cc|toonz/sources/*.cpp|toonz/sources/*.cxx|\
    toonz/sources/*.h|toonz/sources/*.hh|toonz/sources/*.hpp|toonz/sources/*.hxx)
      files+=("$file") ;;
  esac
done < <(git diff --name-only --diff-filter=ACMR -z "$base_ref...HEAD")

for file in "${files[@]}"; do
  ranges=()
  while read -r start count; do
    count="${count:-1}"
    (( count > 0 )) && ranges+=(--lines="$start:$((start + count - 1))")
  done < <(
    git diff --unified=0 --no-color "$base_ref...HEAD" -- "$file" |
      sed -nE 's/^@@ -[0-9]+(,[0-9]+)? \+([0-9]+)(,([0-9]+))? @@.*/\2 \4/p'
  )
  (( ${#ranges[@]} == 0 )) && continue
  printf 'Checking %s (%s)\n' "$file" "${ranges[*]}"
  "$format_bin" --dry-run --Werror -style=file "${ranges[@]}" "$file"
done

echo "OT-master clang-format gate passed."
