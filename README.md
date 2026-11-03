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

## Testing a sample program on Linux, Windows, and Apple Silicon

The tool emits **32-bit x86 (i386) Linux** assembly and links it into a Linux
ELF executable, so running the *output* needs a Linux x86 environment. The
`x86movonly` program itself is portable C++; it's the generated binary that is
platform-specific. Below is how to test the `sum.c` sample (expected output
`55`) on each platform.

Every case under `tests/cases/` ships three files: the input `.s`, the expected
run-time value (`.expect`), and the **exact mov-only assembly the tool should
produce** (`.mov.s`). `run_all.sh`/`ctest` checks all three — the program runs
to the right value, the generated code is mov-only, and it matches the
committed `.mov.s` byte-for-byte. The cases range from a few instructions
(`arith`, `branch`, `loop`) up to a full bubble sort with nested loops and
scaled array indexing (`sort`). If you intentionally change the translator,
regenerate the expected outputs with `scripts/regen-golden.sh` and review the
diff.

### Linux (x86-64) — native

```bash
sudo apt-get install -y gcc-multilib g++-multilib libc6-dev-i386 lib32stdc++-13-dev
cmake -S . -B build && cmake --build build -j
scripts/c2mov.sh examples/sum.c /tmp/sum && /tmp/sum      # -> 55
scripts/movcheck.sh /tmp/sum mo_pass                      # -> OK: mov-only
ctest --test-dir build --output-on-failure                # full suite
```

### Windows — via WSL2

Native Windows has no 32-bit x86 Linux toolchain, so use WSL2 (a real Linux
kernel). In PowerShell:

```powershell
wsl --install -d Ubuntu     # first time only, then reboot and open Ubuntu
```

Then inside the Ubuntu shell, run exactly the **Linux (x86-64)** steps above.
(Docker Desktop with the steps in the next section also works on Windows.)

### Apple Silicon (macOS, arm64) — via Docker

Apple Silicon cannot run 32-bit x86 Linux binaries natively (macOS isn't Linux,
and Rosetta 2 doesn't emulate 32-bit x86), so use the included `Dockerfile`,
which runs under the `linux/amd64` platform with QEMU:

```bash
docker build --platform linux/amd64 -t x86movonly .
docker run --rm x86movonly                               # runs the full suite

# test the sample explicitly:
docker run --rm x86movonly bash -c \
  'scripts/c2mov.sh examples/sum.c /tmp/sum && /tmp/sum && scripts/movcheck.sh /tmp/sum mo_pass'

# test your own program (mount the current directory):
docker run --rm -v "$PWD":/work -w /src x86movonly bash -c \
  'scripts/c2mov.sh /work/yourfile.c /tmp/out && /tmp/out'
```

> If execution of the 32-bit binary fails under Docker Desktop, open
> **Settings → General** and turn **off** "Use Rosetta for x86/amd64 emulation"
> so Docker falls back to QEMU, which does handle 32-bit code. The same
> `Dockerfile` is the simplest one-command way to test on Linux and Windows too.

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

Everything below is verified working and lowered to `mov` only. The table is the
complete list of instructions the translator currently recognises; anything not
listed is passed through verbatim and counted in `--report` so you always know
when a program needs something unsupported.

| Category        | Instructions (AT&T mnemonics) | Notes |
|-----------------|-------------------------------|-------|
| Data move       | `movl`, `movw`, `movb` | size-aware register/memory moves |
| Zero/sign extend| `movzbl`, `movzwl`, `movsbl`, `movswl` | byte/word → dword |
| Add / subtract  | `addl`, `subl` | |
| Inc / dec       | `incl`, `decl` | |
| Negate / not    | `negl`, `notl` | |
| Bitwise logic   | `andl`, `orl`, `xorl` | |
| Multiply        | `imull` | two-operand (`dst *= src`) and three-operand (`dst = src * imm`); low 32 bits |
| Divide          | `divl`, `idivl`, `cltd`/`cdq` | 32-bit dividend in `eax`; yields quotient in `eax`, remainder in `edx` |
| Shift           | `sall`/`shll`, `shrl`, `sarl` | count by immediate **or** `%cl` |
| Rotate          | `roll`, `rorl` | count by immediate |
| Compare / test  | `cmpl`, `testl` | set the virtual flags used by the jumps below |
| Unconditional   | `jmp`, `ret` | `ret` is the one gateway instruction per pass |
| Equal / zero    | `je`/`jz`, `jne`/`jnz` | |
| Signed compare  | `jl`/`jnge`, `jle`/`jng`, `jg`/`jnle`, `jge`/`jnl` | |
| Unsigned compare| `jb`/`jnae`/`jc`, `jbe`/`jna`, `ja`/`jnbe`, `jae`/`jnb`/`jnc` | |
| Sign            | `js`, `jns` | |
| No-ops          | `nop`, `endbr32` | dropped |
| Addressing      | `disp(base,index,scale)`, absolute symbols, global data loads | used by every memory operand above |

Together this covers register-resident 32-bit integer code — exactly what
gcc/g++ produce for leaf functions at `-O1`, which the included helper compiles
with. Multiplication is a byte-wise schoolbook product over an 8×8→16-bit table;
division is branch-free restoring division (32 unrolled steps); a variable
(`%cl`) shift composes five fixed power-of-two shifts selected by the count
bits — all still lowered to `mov` only.

**Not yet handled** (passed through verbatim and reported): `pushl`/`popl`/
`call`/`leave` (stack frames and function calls), `leal`, one-operand `mull`/
`imull` (full 64-bit product in `edx:eax`), add/subtract-with-carry (`adcl`,
`sbbl`), `setcc`/`cmovcc`, double shifts/rotate-through-carry (`shld`, `shrd`,
`rcl`, `rcr`) and rotate by `%cl`, bit instructions (`bt`, `bsf`, …), string ops
(`movs`/`stos`/`rep`), 8/16-bit arithmetic, and floating point (x87 and SSE).
These are the roadmap — see
[docs/architecture.md](docs/architecture.md#roadmap).

## Repository layout

```
include/x86movonly/  parser + rewrite engine headers
src/                 parser.cpp, rewrite.cpp, main.cpp
runtime/             mo_runtime.c — tables, register file, driver gateway
scripts/             c2mov.sh (C/C++ → mov-only binary), movcheck.sh (verifier)
examples/            sum.c, gcd.cpp
tests/cases/         each case = input .s + .expect (value) + .mov.s (output)
tests/               run_all.sh (also wired into ctest)
```

## Future translation ideas

- Function calls
- Stack
