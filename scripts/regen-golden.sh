#!/usr/bin/env bash
# Regenerate the committed mov-only output files (tests/cases/*.mov.s) from the
# current tool build. Run this after an intentional change to the translator,
# then review the diff before committing.
set -euo pipefail
here="$(cd "$(dirname "$0")/.." && pwd)"
tool="${X86MOVONLY:-$here/build/x86movonly}"
for s in "$here"/tests/cases/*.s; do
  case "$s" in *.mov.s) continue;; esac
  "$tool" "$s" -o "${s%.s}.mov.s"
  echo "regenerated ${s%.s}.mov.s"
done
