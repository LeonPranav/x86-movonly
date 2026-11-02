#!/usr/bin/env bash
# movcheck.sh <binary> <symbol>
# Disassemble one function and report how many real instructions it contains
# and how many are NOT in the mov family. Exit 0 iff the only non-mov
# instructions are boundary gateways (ret).
#
# objdump prints instruction-byte continuations on their own lines; we count
# only lines that carry a mnemonic (the field after the second tab).
set -euo pipefail
bin=$1; sym=$2
dis=$(objdump -d --no-show-raw-insn "$bin" \
      | awk -v s="<$sym>:" '$0 ~ s {f=1;next} f && /^[[:space:]]*$/ {f=0} f')
# Keep only lines that have a mnemonic: "  addr:\tmnemonic ..."
insns=$(printf '%s\n' "$dis" | sed -nE 's/^[[:space:]]+[0-9a-f]+:[[:space:]]+([a-z][a-z0-9.]*).*/\1/p')
total=$(printf '%s\n' "$insns" | grep -c . || true)
nonmov=$(printf '%s\n' "$insns" | grep -vE '^(mov|movl|movb|movw|movz[bw]l|movs[bw]l)$' || true)
nonmov_count=$(printf '%s\n' "$nonmov" | grep -c . || true)
echo "symbol: $sym"
echo "instructions: $total"
echo "non-mov: $nonmov_count"
if [ "$nonmov_count" -gt 0 ]; then
  echo "non-mov mnemonics:"; printf '%s\n' "$nonmov" | sort | uniq -c
fi
# Allow only ret (the function-return gateway).
bad=$(printf '%s\n' "$nonmov" | grep -vE '^(ret)$' || true)
bad_count=$(printf '%s\n' "$bad" | grep -c . || true)
if [ "$bad_count" -gt 0 ]; then
  echo "FAIL: non-mov, non-gateway instructions present"; exit 1
fi
echo "OK: generated code is mov-only (plus ret gateway)"
