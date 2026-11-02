#include "x86movonly/parser.hpp"

#include <sstream>
#include <stdexcept>
#include <cctype>
#include <algorithm>

namespace x86mo {

namespace {

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

// Strip a trailing comment introduced by '#'. We keep it simple: '#' that is
// not inside a string literal starts a comment. gcc asm rarely uses strings in
// operands, so this is safe for our subset.
std::string stripComment(const std::string& s) {
    bool inStr = false;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '"') inStr = !inStr;
        else if (c == '#' && !inStr) return s.substr(0, i);
    }
    return s;
}

bool isLabelLine(const std::string& s, std::string& name) {
    // "name:" where name is a label (may contain . _ $ digits letters).
    // Must be exactly label then ':' then optionally more (we only take the
    // label; trailing instruction on same line is uncommon in gcc output).
    size_t colon = s.find(':');
    if (colon == std::string::npos) return false;
    std::string cand = trim(s.substr(0, colon));
    if (cand.empty()) return false;
    for (char c : cand) {
        if (!(std::isalnum((unsigned char)c) || c == '.' || c == '_' ||
              c == '$' || c == '@'))
            return false;
    }
    // Reject "foo: bar" style (rare); require nothing meaningful after colon
    std::string rest = trim(s.substr(colon + 1));
    if (!rest.empty()) return false;
    name = cand;
    return true;
}

// Parse a single AT&T operand.
Operand parseOperand(const std::string& in, bool jumpContext) {
    Operand op;
    op.raw = in;
    std::string s = trim(in);

    if (!s.empty() && s[0] == '%') {
        op.kind = OpKind::Register;
        op.reg = s.substr(1);
        return op;
    }
    if (!s.empty() && s[0] == '$') {
        op.kind = OpKind::Immediate;
        op.imm = s.substr(1);
        return op;
    }
    // Indirect jump/call target "*..." — model as Memory/Register with raw.
    if (!s.empty() && s[0] == '*') {
        Operand inner = parseOperand(s.substr(1), false);
        inner.raw = in;
        return inner;
    }

    // Memory: [disp](base[,index[,scale]]) or plain symbol/number.
    size_t paren = s.find('(');
    if (paren == std::string::npos) {
        // No parens: either a bare symbol (jump target or memory symbol) or a
        // numeric/symbolic displacement used as memory.
        if (jumpContext) {
            op.kind = OpKind::Label;
            op.label = s;
        } else {
            op.kind = OpKind::Memory;
            op.disp = s;
        }
        return op;
    }

    op.kind = OpKind::Memory;
    op.disp = trim(s.substr(0, paren));
    size_t close = s.rfind(')');
    if (close == std::string::npos || close < paren)
        throw std::runtime_error("unbalanced parens in operand: " + in);
    std::string inside = s.substr(paren + 1, close - paren - 1);

    // inside = base,index,scale  (any may be empty)
    std::vector<std::string> parts;
    {
        std::string cur;
        for (char c : inside) {
            if (c == ',') { parts.push_back(trim(cur)); cur.clear(); }
            else cur.push_back(c);
        }
        parts.push_back(trim(cur));
    }
    auto regName = [&](const std::string& t) -> std::string {
        if (t.empty()) return "";
        if (t[0] != '%') throw std::runtime_error("expected register in mem operand: " + in);
        return t.substr(1);
    };
    if (parts.size() >= 1) op.base = regName(parts[0]);
    if (parts.size() >= 2) op.index = regName(parts[1]);
    if (parts.size() >= 3 && !parts[2].empty()) op.scale = std::stoi(parts[2]);
    if (parts.size() > 3) throw std::runtime_error("too many fields in mem operand: " + in);
    return op;
}

// Split an instruction's operand list on commas at paren-depth 0.
std::vector<std::string> splitOperands(const std::string& s) {
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (char c : s) {
        if (c == '(') depth++;
        if (c == ')') depth--;
        if (c == ',' && depth == 0) { out.push_back(trim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    std::string last = trim(cur);
    if (!last.empty() || !out.empty()) out.push_back(last);
    return out;
}

bool isJumpMnemonic(const std::string& m) {
    if (m == "jmp" || m == "call") return true;
    if (m.size() >= 2 && m[0] == 'j') return true; // je, jne, jl, jle, jg...
    return false;
}

} // namespace

Program parse(const std::string& source) {
    Program prog;
    std::istringstream iss(source);
    std::string line;
    int lineno = 0;
    while (std::getline(iss, line)) {
        ++lineno;
        std::string noComment = stripComment(line);
        std::string t = trim(noComment);

        if (t.empty()) {
            Line L; L.kind = LineKind::Comment; L.text = line; L.raw = line;
            L.lineno = lineno; prog.lines.push_back(L);
            continue;
        }

        std::string labelName;
        if (isLabelLine(t, labelName)) {
            Line L; L.kind = LineKind::Label; L.text = labelName;
            L.raw = line; L.lineno = lineno; prog.lines.push_back(L);
            continue;
        }

        if (t[0] == '.') {
            Line L; L.kind = LineKind::Directive; L.text = t; L.raw = line;
            L.lineno = lineno; prog.lines.push_back(L);
            continue;
        }

        // Instruction: mnemonic [operands]
        size_t sp = t.find_first_of(" \t");
        Line L; L.kind = LineKind::Instruction; L.raw = line; L.lineno = lineno;
        if (sp == std::string::npos) {
            L.mnemonic = t;
        } else {
            L.mnemonic = t.substr(0, sp);
            std::string rest = trim(t.substr(sp));
            bool jc = isJumpMnemonic(L.mnemonic);
            for (auto& o : splitOperands(rest)) {
                if (!o.empty()) L.operands.push_back(parseOperand(o, jc));
            }
        }
        prog.lines.push_back(L);
    }
    return prog;
}

std::string emit(const Program& p) {
    std::ostringstream out;
    for (const auto& L : p.lines) {
        switch (L.kind) {
            case LineKind::Raw:
                out << L.text << "\n";
                break;
            case LineKind::Comment:
                out << L.text << "\n";
                break;
            case LineKind::Directive:
                out << "\t" << L.text << "\n";
                break;
            case LineKind::Label:
                out << L.text << ":\n";
                break;
            case LineKind::Instruction: {
                out << "\t" << L.mnemonic;
                for (size_t i = 0; i < L.operands.size(); ++i) {
                    out << (i == 0 ? " " : ", ") << L.operands[i].raw;
                }
                out << "\n";
                break;
            }
        }
    }
    return out.str();
}

} // namespace x86mo
