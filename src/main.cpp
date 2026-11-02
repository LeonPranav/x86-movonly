#include "x86movonly/parser.hpp"
#include "x86movonly/rewrite.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <iostream>

static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    std::ostringstream ss; ss << f.rdbuf();
    return ss.str();
}

static void usage(const char* argv0) {
    std::fprintf(stderr,
        "x86-movonly: rewrite 32-bit AT&T x86 assembly to use only `mov`.\n\n"
        "Usage: %s [options] <input.s>\n"
        "  -o <file>     write output assembly to <file> (default: stdout)\n"
        "  --passthrough parse and re-emit without rewriting (round-trip check)\n"
        "  --report      print a mov/non-mov summary to stderr\n"
        "  -h, --help    show this help\n",
        argv0);
}

int main(int argc, char** argv) {
    std::string in, out;
    bool passthrough = false, report = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (a == "--passthrough") passthrough = true;
        else if (a == "--report") report = true;
        else if (a == "-o") { if (++i >= argc) { usage(argv[0]); return 2; } out = argv[i]; }
        else if (!a.empty() && a[0] == '-') { usage(argv[0]); return 2; }
        else in = a;
    }
    if (in.empty()) { usage(argv[0]); return 2; }

    try {
        x86mo::Program prog = x86mo::parse(readFile(in));
        std::string result;
        if (passthrough) {
            result = x86mo::emit(prog);
        } else {
            x86mo::RewriteStats stats;
            x86mo::Program rewritten = x86mo::rewrite(prog, stats);
            result = x86mo::emit(rewritten);
            if (report) stats.print(std::cerr);
        }
        if (out.empty()) {
            std::fwrite(result.data(), 1, result.size(), stdout);
        } else {
            std::ofstream f(out, std::ios::binary);
            f << result;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "x86-movonly: error: %s\n", e.what());
        return 1;
    }
    return 0;
}
