/* x86-movonly runtime support (the "gateway" glue).
 *
 * This file is compiled NORMALLY (it is not mov-only). It plays the same role
 * as the C runtime / crt glue and the single extern "gateway" in other
 * single-instruction systems: it sets up the lookup tables the translated code
 * reads from, holds the virtualized register file and scratch cells, and
 * provides the entry boundary that calls into the translated program.
 *
 * The *translated program's* own instructions contain only `mov`. The handful
 * of ordinary instructions live here, at the boundary, by design.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- Virtualized register file (x86 encoding order) ---------------------
 * 0:eax 1:ecx 2:edx 3:ebx 4:esp 5:ebp 6:esi 7:edi
 * Byte/word sub-registers are sub-offsets of each 4-byte slot (little-endian).
 */
uint8_t mo_regs[32];

/* Dedicated stack region so the virtual ESP does not collide with the real
 * machine stack. ESP is initialised near the top. */
#define MO_STACK_WORDS (1 << 16)
uint32_t mo_stack[MO_STACK_WORDS];

/* ---- Scratch cells used while building table indices -------------------- */
uint32_t mo_idx;          /* packed byte index into a table                  */
uint8_t  mo_carry;        /* running carry / borrow (0 or 1)                 */
uint32_t mo_t[16];        /* general temporaries for operand materialisation */
uint32_t mo_discard;      /* sink for suppressed (inactive-block) writes AND
                             safe source for suppressed (inactive) reads     */
uint32_t mo_target;       /* current basic-block id for the dispatcher       */
uint32_t mo_next;         /* next basic-block id (written by the active block)*/
uint8_t  mo_active;       /* 1 while the currently-selected block is active  */
uint32_t mo_ptrtab[2];    /* [0]=discard/safe ptr, [1]=real ptr; idx=active  */
uint8_t  mo_fe, mo_fltu, mo_flts, mo_fsf;  /* compare/test result flags      */
uint8_t  mo_tb[8];        /* scratch bytes for boolean flag combination      */

#define MO_HALT 0xFFFFFFFFu

/* ---- Lookup tables ------------------------------------------------------ */
/* add/sub: index = (cin<<16)|(a<<8)|b ; value low byte = result, high = cout */
uint16_t mo_add[1 << 17];
uint16_t mo_sub[1 << 17];
/* bitwise: index = (a<<8)|b ; value = result byte */
uint8_t  mo_and[1 << 16];
uint8_t  mo_or [1 << 16];
uint8_t  mo_xor[1 << 16];
/* equality/compare helpers over whole bytes: index=(a<<8)|b -> 0/1 */
uint8_t  mo_eqb[1 << 16];   /* a == b                                        */
uint8_t  mo_ltb[1 << 16];   /* a <  b  (unsigned)                            */

void mo_init(void) {
    for (int c = 0; c < 2; ++c)
        for (int a = 0; a < 256; ++a)
            for (int b = 0; b < 256; ++b) {
                unsigned s = (unsigned)a + b + c;
                mo_add[(c << 16) | (a << 8) | b] =
                    (uint16_t)((s & 0xff) | ((s >> 8) << 8));
                int d = a - b - c;
                unsigned borrow = (d < 0) ? 1u : 0u;
                mo_sub[(c << 16) | (a << 8) | b] =
                    (uint16_t)(((unsigned)d & 0xff) | (borrow << 8));
            }
    for (int a = 0; a < 256; ++a)
        for (int b = 0; b < 256; ++b) {
            int i = (a << 8) | b;
            mo_and[i] = (uint8_t)(a & b);
            mo_or [i] = (uint8_t)(a | b);
            mo_xor[i] = (uint8_t)(a ^ b);
            mo_eqb[i] = (a == b) ? 1 : 0;
            mo_ltb[i] = (a <  b) ? 1 : 0;
        }

    /* ESP near the top of the dedicated stack region, 16-byte aligned-ish. */
    uint32_t sp = (uint32_t)(uintptr_t)&mo_stack[MO_STACK_WORDS - 4];
    memcpy(&mo_regs[16], &sp, 4);
}

/* The translated program is emitted as a single pass over all basic blocks.
 * Each call executes the block whose id == mo_target (all blocks run, but only
 * the active one's writes take effect) and records the successor in mo_next.
 * This driver loop is the single control-flow "gateway": the generated code
 * itself contains no jmp/jcc. */
extern void mo_pass(void);

int main(void) {
    mo_init();
    mo_target = 0;
    mo_next = 0;
    while (mo_target != MO_HALT) {
        mo_pass();
    }
    int r;
    memcpy(&r, &mo_regs[0], 4);   /* virtual EAX = return value */
    printf("%d\n", r);
    return 0;
}
