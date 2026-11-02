# x86-movonly

**Rewrite ordinary 32-bit x86 assembly into a program that computes using only
the `mov` instruction.**

`mov` alone is Turing-complete: with lookup tables in memory and the address
arithmetic the CPU performs for a memory operand, you can emulate arithmetic,
logic, comparisons and control flow without ever executing an `add`, `cmp`,
`jmp` or any other instruction. This tool takes the assembly a normal compiler
emits and mechanically lowers it to that form.

Unlike a from-source "mov-only compiler", this operates on **existing x86
assembly**, so you can feed it the output of any compiler that targets 32-bit
x86 — C, C++, or anything else with an x86 backend.

```
   C / C++ source ──(gcc -m32 -S)──▶ normal x86 asm ──(x86movonly)──▶ mov-only asm ──▶ binary
```

## Quick start

```bash
# 32-bit toolchain (Debian/Ubuntu)
sudo apt-get install -y gcc-multilib g++-multilib libc6-dev-i386 lib32stdc++-13-dev

cmake -S . -B build && cmake --build build -j

# Compile a C/C++ function all the way to a mov-only binary and run it:
scripts/c2mov.sh examples/sum.c /tmp/sum && /tmp/sum        # prints 55
scripts/c2mov.sh examples/gcd.cpp /tmp/gcd && /tmp/gcd      # prints 12

# Or translate assembly you already have:
build/x86movonly --report input.s -o output.s

# Prove the result really is mov-only:
scripts/movcheck.sh /tmp/sum mo_pass
#   non-mov: 1
#   non-mov mnemonics: 1 ret
#   OK: generated code is mov-only (plus ret gateway)
```

## How it works

See [docs/architecture.md](docs/architecture.md) for the full description. In
brief:

- **Registers are virtualized into memory.** The program's `eax`,`ebx`,… become
  cells in a `mo_regs` array, freeing the real hardware registers for use as
  scratch while building table indices.
- **Arithmetic and logic are table lookups.** `a + b` is computed one byte at a
  time by reading a precomputed `mo_add[(carry<<16)|(a<<8)|b]` table with a
  `mov`; subtraction, `and`/`or`/`xor`, and comparisons work the same way. The
  index is assembled by writing bytes into a scratch dword — all with `mov`.
- **Control flow is branch-free.** The program is split into basic blocks. A
  dispatcher runs every block on each pass, but a per-block "active" flag
  (computed by comparing a target-id variable) redirects every architecturally
  visible write to a discard cell unless that block is the selected one.
  `jmp`/`jcc` just select the next block id; there are **no `jmp` or `jcc`
  instructions in the generated code**.

### The honest boundary

Just as every real single-instruction system needs one non-`mov` primitive to
actually make progress and talk to the OS, this one keeps a tiny, clearly
marked gateway in `runtime/mo_runtime.c` (compiled normally, like C runtime
glue): a driver loop that re-invokes the mov-only pass until a halt sentinel,
and a single `ret` back to it per pass. **Everything the translated program
computes is `mov`.** `movcheck.sh` enforces this: the only non-`mov`
instruction permitted in the generated function is that one `ret`.

## Scope (v1)

Verified working, lowered to mov-only:

| Category     | Instructions |
|--------------|--------------|
| Data move    | `movl/movb/movw`, `movzbl/movzwl`, `movsbl/movswl` |
| Arithmetic   | `addl`, `subl`, `incl`, `decl`, `negl` |
| Logic        | `andl`, `orl`, `xorl`, `notl` |
| Compare      | `cmpl`, `testl` |
| Control flow | `jmp`, `je/jne/jl/jle/jg/jge/jb/jbe/ja/jae/js/jns`, `ret` |
| Addressing   | `disp(base,index,scale)`, absolute symbols, global data loads |

This covers register-resident integer code — exactly what gcc/g++ produce for
leaf functions at `-O1`. The included helper compiles with those flags.

**Not yet handled** (passed through verbatim and reported, so you always know):
`push`/`pop`/`call`/`leave` (stack frames and function calls), `imul`/`mul`/
`div`, shifts, `lea`, `setcc`, 8/16-bit arithmetic, and floating point. These
are the roadmap — see [docs/architecture.md](docs/architecture.md#roadmap).

## Repository layout

```
include/x86movonly/  parser + rewrite engine headers
src/                 parser.cpp, rewrite.cpp, main.cpp
runtime/             mo_runtime.c — tables, register file, driver gateway
scripts/             c2mov.sh (C/C++ → mov-only binary), movcheck.sh (verifier)
examples/            sum.c, gcd.cpp
tests/               regression cases + run_all.sh (also wired into ctest)
```

## License

MIT — see [LICENSE](LICENSE).
