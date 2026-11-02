#pragma once
#include "x86movonly/asm.hpp"
#include <iosfwd>
#include <string>
#include <vector>

namespace x86mo {

struct RewriteStats {
    int lowered = 0;        // source instructions rewritten to mov-only
    int passthrough = 0;    // source instructions kept verbatim (NOT mov-only)
    std::vector<std::string> unsupported;  // mnemonics seen but not lowered
    void print(std::ostream& os) const;
};

// Rewrite a parsed program so that supported instructions become mov-only.
// Unsupported instructions are currently passed through verbatim and counted
// in `stats` so the report stays honest.
Program rewrite(const Program& in, RewriteStats& stats);

} // namespace x86mo
