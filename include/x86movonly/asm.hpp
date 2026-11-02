// x86-movonly — AT&T x86-32 assembly representation
//
// A small, faithful model of the subset of GNU assembler (AT&T) syntax that
// gcc/g++ emit at -O0/-O1 for 32-bit integer code. We keep things we do not
// rewrite (directives, unknown instructions) verbatim so the output still
// assembles.
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace x86mo {

// ---- Operands -------------------------------------------------------------

enum class OpKind {
    Register,   // %eax, %al, ...
    Immediate,  // $5, $symbol
    Memory,     // disp(base,index,scale) or symbol(,,) or symbol
    Label,      // a branch target symbol used as a jump operand
};

struct Operand {
    OpKind kind;

    // Register
    std::string reg;        // without '%', e.g. "eax", "al"

    // Immediate
    std::string imm;        // raw text after '$', e.g. "5", "symbol", "-3"

    // Memory: disp(base,index,scale)
    std::string disp;       // displacement text (may be a symbol or number or empty)
    std::string base;       // base register without '%', or ""
    std::string index;      // index register without '%', or ""
    int scale = 1;          // 1,2,4,8

    // Label
    std::string label;      // target symbol

    std::string raw;        // original text, for passthrough / debugging
};

// ---- Instructions / lines -------------------------------------------------

enum class LineKind {
    Directive,   // .text, .globl, .long 5, ...
    Label,       // "foo:"
    Instruction, // mnemonic + operands
    Comment,     // a standalone comment / blank line we preserve
    Raw,         // verbatim emitted text (used by the rewriter's output)
};

struct Line {
    LineKind kind;

    // Directive / Comment / Label
    std::string text;        // full original text (directive body, label name w/o ':')

    // Instruction
    std::string mnemonic;    // e.g. "movl", "addl", "je"
    std::vector<Operand> operands;  // AT&T order: src, ..., dst (dst last)

    std::string raw;         // original source line (trimmed)
    int lineno = 0;          // source line number (1-based)
};

struct Program {
    std::vector<Line> lines;
};

} // namespace x86mo
