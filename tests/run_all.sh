#!/usr/bin/env bash
# Translate every tests/cases/*.s, assemble+link against the runtime, run it,
# and check (a) the printed result matches the .expect file and (b) the
# generated mo_pass function is mov-only (plus the single ret gateway).
set -uo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
tool="${X86MOVONLY:-$here/build/x86movonly}"
runtime="$here/runtime/mo_runtime.c"
work="$(mktemp -d)"; trap 'rm -rf "$work"' EXIT

pass=0; fail=0
for s in "$here"/tests/cases/*.s; do
  name="$(basename "$s" .s)"
  exp="$(cat "${s%.s}.expect")"
  "$tool" "$s" -o "$work/$name.s" 2>"$work/$name.err" || { echo "FAIL $name (translate)"; cat "$work/$name.err"; fail=$((fail+1)); continue; }
  gcc -m32 -no-pie -o "$work/$name" "$work/$name.s" "$runtime" 2>"$work/$name.link" || { echo "FAIL $name (link)"; cat "$work/$name.link"; fail=$((fail+1)); continue; }
  got="$("$work/$name")"
  if [ "$got" != "$exp" ]; then echo "FAIL $name: got '$got' expected '$exp'"; fail=$((fail+1)); continue; fi
  if ! bash "$here/scripts/movcheck.sh" "$work/$name" mo_pass >/dev/null; then
    echo "FAIL $name: generated code is not mov-only"; fail=$((fail+1)); continue; fi
  echo "PASS $name -> $got (mov-only)"
  pass=$((pass+1))
done

# Also exercise the C front-end if a 32-bit toolchain is present.
if echo 'int main(){return 0;}' | gcc -m32 -x c - -o "$work/probe" 2>/dev/null; then
  if bash "$here/scripts/c2mov.sh" "$here/examples/sum.c" "$work/sum.bin" >/dev/null 2>&1; then
    got="$("$work/sum.bin")"
    if [ "$got" = "55" ] && bash "$here/scripts/movcheck.sh" "$work/sum.bin" mo_pass >/dev/null; then
      echo "PASS examples/sum.c -> 55 (mov-only)"; pass=$((pass+1))
    else echo "FAIL examples/sum.c: got '$got'"; fail=$((fail+1)); fi
  fi
else
  echo "SKIP C front-end tests (no 32-bit toolchain)"
fi

echo "----"
echo "passed: $pass  failed: $fail"
[ "$fail" -eq 0 ]
