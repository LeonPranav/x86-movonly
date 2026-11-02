#!/usr/bin/env bash
# c2mov.sh <source.c|.cpp|.s> [output-binary]
#
# End-to-end helper: compile a single C/C++ function to 32-bit x86 assembly,
# rewrite it to mov-only with x86movonly, link it against the runtime, and
# leave a runnable binary. Prints the binary's path. The source must define a
# single function (any name) that returns its result like a normal int
# function; it becomes the program entry.
#
# Requires a 32-bit toolchain:  apt-get install -y gcc-multilib g++-multilib \
#   libc6-dev-i386 lib32stdc++-<gccver>-dev
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
tool="${X86MOVONLY:-$here/build/x86movonly}"
runtime="$here/runtime/mo_runtime.c"

src="${1:?usage: c2mov.sh <source> [out]}"
out="${2:-${src%.*}.mov.bin}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

ext="${src##*.}"
asm="$work/in.s"
case "$ext" in
  c)   gcc -m32 -O1 -fno-stack-protector -fomit-frame-pointer \
           -fcf-protection=none -fno-pic -S "$src" -o "$asm" ;;
  cc|cpp|cxx|C)
       g++ -m32 -O1 -fno-stack-protector -fomit-frame-pointer \
           -fcf-protection=none -fno-pic -S "$src" -o "$asm" ;;
  s|S) cp "$src" "$asm" ;;
  *)   echo "unknown source type: .$ext" >&2; exit 2 ;;
esac

"$tool" --report "$asm" -o "$work/out.s"
gcc -m32 -no-pie -o "$out" "$work/out.s" "$runtime"
echo "built: $out"
