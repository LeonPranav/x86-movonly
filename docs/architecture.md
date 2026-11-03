# Architecture

This document describes how `x86movonly` turns ordinary 32-bit x86 assembly
into a program whose instructions are all `mov`.

## The idea in one paragraph

A `mov` with a memory operand makes the CPU (a) compute an address and (b) read
or write memory there. If memory holds precomputed lookup tables, a single
`mov` can evaluate any function of its index — and the index itself is built
with more `mov`s. That is enough to emulate the ALU. Control flow is handled by
turning branches into *data*: a block-selection variable, updated by `mov`,
decides which basic block's writes take effect, so no `jmp`/`jcc` is ever
executed.

## 1. Front end — parsing (`src/parser.cpp`)

The parser reads GNU-assembler (AT&T) text into a simple line model
(`include/x86movonly/asm.hpp`): directives, labels, and instructions with typed
operands (register / immediate / memory `disp(base,index,scale)` / jump label).
Comments and unmodelled directives are preserved so the output still assembles.

## 2. Register virtualization

The translated program never keeps live state in hardware registers. Each
architectural register is a slot in a 32-byte `mo_regs` array (x86 encoding
order; 8/16-bit sub-registers are sub-offsets). This frees `eax`/`ecx`/`edx` to
be used freely as scratch while assembling table indices. `%esp` is initialized
(by the runtime) to point near the top of a dedicated `mo_stack` buffer.

## 3. The ALU as tables (`runtime/mo_runtime.c` + engine in `src/rewrite.cpp`)

The runtime fills these tables once at startup:

| Table            | Index                         | Value (per entry)           |
|------------------|-------------------------------|-----------------------------|
| `mo_add`,`mo_sub`| `(carry<<16)\|(a<<8)\|b`      | `result_byte \| (carry_out<<8)` |
| `mo_and/or/xor`  | `(a<<8)\|b`                   | `result_byte`               |
| `mo_eqb`,`mo_ltb`| `(a<<8)\|b`                   | `a==b` / `a<b` (0/1)        |

A 32-bit add is four table reads with carry chained between bytes. To look up
`mo_add[(carry<<16)|(a<<8)|b]` the engine writes the three index bytes into a
scratch dword `mo_idx` and then does `movw mo_add(,%edx,2),%ax` — `%al` is the
sum byte, `%ah` the carry out. Subtraction is identical with `mo_sub`; bitwise
ops use single-byte-result tables. Larger relations are composed from these:
unsigned dword `<` scans bytes most-significant-first combining `mo_ltb`/`mo_eqb`;
signed `<` is derived from the unsigned result and the two sign bits; equality
is "all four XOR/compare bytes are zero".

Effective addresses for memory operands (`disp + base + index*scale`) are
computed with the same add engine (scale by repeated doubling), leaving the
address in a scratch cell that a final `mov` dereferences.

## 4. Branch-free control flow

The instruction stream is split into **basic blocks** at labels and after every
`jmp`/`jcc`/`ret`. Each block gets an integer id. Two variables drive
execution: `mo_target` (the block to run now) and `mo_next` (its successor).

The generated function `mo_pass` emits every block back-to-back. At the top of
block *i* it computes `mo_active = (mo_target == i)`. Within the block, **every
architecturally visible write is guarded**: the destination pointer is selected,
by table lookup on `mo_active`, between the real target and a discard cell
(`mo_discard`). Memory *reads* are guarded the same way (an inactive block reads
from the safe discard cell instead of a possibly-invalid computed address). So
all blocks execute on every pass, but only the selected block changes anything.

At a block's end the terminator writes `mo_next` (guarded):

- `jmp L` → `mo_next = id(L)`
- `jcc L` → `mo_next = cond ? id(L) : id(fallthrough)`, where `cond` is derived
  from the flags that the preceding `cmp`/`test` stored (`mo_fe`, `mo_fltu`,
  `mo_flts`, `mo_fsf`) — there is no hardware EFLAGS in play.
- `ret` → `mo_next = HALT`
- fall-through → `mo_next = id(next block)`

After all blocks, `mo_pass` copies `mo_next` into `mo_target` and returns to the
driver. Using a separate `mo_next` prevents a block that runs earlier in a pass
from re-triggering a later block in the *same* pass.

## 5. The gateway

`runtime/mo_runtime.c` is compiled normally and is the only non-`mov` code:

```c
mo_target = 0;
while (mo_target != MO_HALT) mo_pass();   // the driver loop
```

This loop (and the single `ret` per pass) is the analogue of the one
irreducible non-`mov` primitive every single-instruction machine needs. The
translated program's own instructions — all of `mo_pass` except that `ret` —
are `mov`. `scripts/movcheck.sh` disassembles the function and fails if any
non-`mov`, non-`ret` instruction appears.

## Multiply, divide and shift lowering

These share the same "tables + address arithmetic" discipline as the rest of
the ALU and add nothing non-`mov` to the generated code:

- **Multiply** (`imull`, 2-/3-operand): a byte-wise schoolbook product. An
  `8×8 → 16-bit` table (`mo_mul8`) gives each partial product `a[i]*b[j]`,
  which is placed at byte offset `i+j` and summed with the existing `mo_add`
  carry-propagating adder. Only the low 32 bits are kept.
- **Shifts** (`sall`/`shll`, `shrl`, `sarl`): a shift of `k` splits into a
  whole-byte move plus a sub-byte shift. Two `256·8`-entry tables (`mo_shl8`,
  `mo_shr8`) shift one byte by `0..7`; adjacent bytes are OR-ed to carry bits
  across byte boundaries, and the arithmetic variant feeds a sign-fill byte in
  at the top. A **variable** shift (`%cl`) composes five fixed shifts of
  `1,2,4,8,16`, each applied or skipped by selecting on one bit of the count —
  a branch-free `select` done with an indexed `mov`.
- **Divide** (`divl`/`idivl` + `cltd`/`cdq`): branch-free **restoring
  division**, 32 unrolled iterations of shift-in / trial-subtract / select,
  using the adder, the unsigned-compare helper and the `select` primitive.
  Signed division wraps the unsigned core with sign-adjust/negate steps, and
  `cltd` is a `select` on the sign bit of `eax`.

All of the above use a private scratch region (`mo_m`, `mo_mb`) for their
intermediate values, so the guarded-write discipline for architecturally
visible state is unchanged.

## Roadmap

1. **Stack & calls**: `push`/`pop`/`call`/`leave` and an extern "call gateway"
   so full gcc `-O0` output and libc calls (e.g. `printf`) work — this unlocks
   translating whole programs, not just single register-resident functions.
2. **Remaining ALU**: one-operand `mull`/`imull` (full 64-bit `edx:eax`
   product), `setcc`, `cmovcc`, 8/16-bit arithmetic.
3. **`lea`** via the address engine.
4. **Object/ELF input** (disassemble-then-rewrite) in addition to assembly text.
5. **Floating point** via a mov-only soft-float path (large; last).
