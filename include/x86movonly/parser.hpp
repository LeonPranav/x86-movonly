#pragma once
#include "x86movonly/asm.hpp"
#include <string>

namespace x86mo {

// Parse a GNU-assembler (AT&T) source file into a Program.
// Throws std::runtime_error on operand forms it cannot model.
Program parse(const std::string& source);

// Serialise a Program back to GNU-assembler text (round-trips Directive/Label/
// Comment lines verbatim; re-emits Instruction lines from their operands).
std::string emit(const Program& p);

} // namespace x86mo
