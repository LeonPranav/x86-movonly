#include "x86movonly/rewrite.hpp"

#include <ostream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <map>
#include <vector>
#include <string>

namespace x86mo {

void RewriteStats::print(std::ostream& os) const {
    os << "x86-movonly report:\n";
    os << "  lowered to mov-only : " << lowered << " source instruction(s)\n";
    os << "  passed through      : " << passthrough << " source instruction(s)\n";
    if (!unsupported.empty()) {
        os << "  unsupported mnemonics (kept verbatim, NOT mov-only):\n";
        for (const auto& m : unsupported) os << "    " << m << "\n";
    }
}

namespace {

// ----- register model -----------------------------------------------------
struct Slot { int off; int size; bool ok; };
Slot regSlot(const std::string& r) {
    static const std::unordered_map<std::string, Slot> m = {
        {"eax",{0,4,true}},{"ax",{0,2,true}},{"al",{0,1,true}},{"ah",{1,1,true}},
        {"ecx",{4,4,true}},{"cx",{4,2,true}},{"cl",{4,1,true}},{"ch",{5,1,true}},
        {"edx",{8,4,true}},{"dx",{8,2,true}},{"dl",{8,1,true}},{"dh",{9,1,true}},
        {"ebx",{12,4,true}},{"bx",{12,2,true}},{"bl",{12,1,true}},{"bh",{13,1,true}},
        {"esp",{16,4,true}},{"sp",{16,2,true}},
        {"ebp",{20,4,true}},{"bp",{20,2,true}},
        {"esi",{24,4,true}},{"si",{24,2,true}},
        {"edi",{28,4,true}},{"di",{28,2,true}},
    };
    auto it = m.find(r); return it==m.end()? Slot{0,0,false} : it->second;
}
std::string cellSym(int off){ return "mo_regs+" + std::to_string(off); }
std::string tExpr(int slot){ return "mo_t+" + std::to_string(slot*4); }
char sizeSuffix(int s){ return s==1?'b':s==2?'w':'l'; }
std::string axOf(int s){ return s==1?"%al":s==2?"%ax":"%eax"; }

bool isJcc(const std::string& m){
    static const std::vector<std::string> v = {
        "je","jz","jne","jnz","jl","jnge","jge","jnl","jg","jnle","jle","jng",
        "jb","jnae","jc","jae","jnb","jnc","ja","jnbe","jbe","jna","js","jns"};
    for (auto& x : v) if (x==m) return true; return false;
}

// ----- basic blocks --------------------------------------------------------
struct Block {
    std::vector<std::string> labels;     // labels that map to this block
    std::vector<Line> body;              // non-terminator instructions
    Line term;                           // terminator (jmp/jcc/ret) or empty
    bool hasTerm = false;
    int id = 0;
};

// ----- the lowering engine -------------------------------------------------
class Lowering {
public:
    explicit Lowering(RewriteStats& s) : stats(s) {}
    std::vector<Line> out;
    RewriteStats& stats;
    std::map<std::string,int> labelId;

    void raw(const std::string& s){ Line L; L.kind=LineKind::Raw; L.text=s; out.push_back(L); }
    void comment(const std::string& s){ raw("\t# " + s); }
    void rlabel(const std::string& s){ Line L; L.kind=LineKind::Label; L.text=s; out.push_back(L); }
    void directive(const std::string& s){ Line L; L.kind=LineKind::Directive; L.text=s; out.push_back(L); }

    // ---- arithmetic primitives (write only private scratch) ----------
    void addsub32(const char* table, const std::string& dst,
                  const std::string& a, const std::string& b){
        raw("\tmovb $0, mo_carry");
        for (int i=0;i<4;++i){
            raw("\tmovb "+b+"+"+std::to_string(i)+", %cl");
            raw("\tmovb %cl, mo_idx+0");
            raw("\tmovb "+a+"+"+std::to_string(i)+", %cl");
            raw("\tmovb %cl, mo_idx+1");
            raw("\tmovb mo_carry, %cl");
            raw("\tmovb %cl, mo_idx+2");
            raw("\tmovb $0, mo_idx+3");
            raw("\tmovl mo_idx, %edx");
            raw(std::string("\tmovw ")+table+"(,%edx,2), %ax");
            raw("\tmovb %al, "+dst+"+"+std::to_string(i));
            raw("\tmovb %ah, mo_carry");
        }
    }
    void add32(const std::string&d,const std::string&a,const std::string&b){addsub32("mo_add",d,a,b);}
    void sub32(const std::string&d,const std::string&a,const std::string&b){addsub32("mo_sub",d,a,b);}
    void bit32(const char* table,const std::string&dst,const std::string&a,const std::string&b){
        for(int i=0;i<4;++i){
            raw("\tmovb "+b+"+"+std::to_string(i)+", %cl");
            raw("\tmovb %cl, mo_idx+0");
            raw("\tmovb "+a+"+"+std::to_string(i)+", %cl");
            raw("\tmovb %cl, mo_idx+1");
            raw("\tmovb $0, mo_idx+2");
            raw("\tmovb $0, mo_idx+3");
            raw("\tmovl mo_idx, %edx");
            raw(std::string("\tmovb ")+table+"(%edx), %al");
            raw("\tmovb %al, "+dst+"+"+std::to_string(i));
        }
    }

    // byte boolean helpers (0/1) -> destCell(byte)
    void byteEqConst(const std::string& src, int k, const std::string& dst){
        raw("\tmovb "+src+", %cl"); raw("\tmovb %cl, mo_idx+0");
        raw("\tmovb $"+std::to_string(k)+", mo_idx+1");
        raw("\tmovb $0, mo_idx+2"); raw("\tmovb $0, mo_idx+3");
        raw("\tmovl mo_idx, %edx"); raw("\tmovb mo_eqb(%edx), %al");
        raw("\tmovb %al, "+dst);
    }
    void andBit(const std::string& a,const std::string& b,const std::string& dst){
        raw("\tmovb "+b+", %cl"); raw("\tmovb %cl, mo_idx+0");
        raw("\tmovb "+a+", %cl"); raw("\tmovb %cl, mo_idx+1");
        raw("\tmovb $0, mo_idx+2"); raw("\tmovb $0, mo_idx+3");
        raw("\tmovl mo_idx, %edx"); raw("\tmovb mo_and(%edx), %al");
        raw("\tmovb %al, "+dst);
    }
    void orBit(const std::string& a,const std::string& b,const std::string& dst){
        raw("\tmovb "+b+", %cl"); raw("\tmovb %cl, mo_idx+0");
        raw("\tmovb "+a+", %cl"); raw("\tmovb %cl, mo_idx+1");
        raw("\tmovb $0, mo_idx+2"); raw("\tmovb $0, mo_idx+3");
        raw("\tmovl mo_idx, %edx"); raw("\tmovb mo_or(%edx), %al");
        raw("\tmovb %al, "+dst);
    }
    void notBit(const std::string& a,const std::string& dst){ byteEqConst(a,0,dst); }

    // isZero(dword cell) -> dstByte (1 if all four bytes zero)
    void isZero(const std::string& cell,const std::string& dstByte){
        for(int i=0;i<4;++i) byteEqConst(cell+"+"+std::to_string(i),0,"mo_tb+"+std::to_string(i));
        andBit("mo_tb+0","mo_tb+1","mo_tb+4");
        andBit("mo_tb+2","mo_tb+3","mo_tb+5");
        andBit("mo_tb+4","mo_tb+5",dstByte);
    }

    // unsigned dword less-than a<b -> dstByte, via MSB-first scan
    void ltuDword(const std::string& a,const std::string& b,const std::string& dstByte){
        raw("\tmovb $0, mo_tb+6");   // lt = 0
        raw("\tmovb $1, mo_tb+7");   // eq_so_far = 1
        for(int i=3;i>=0;--i){
            std::string ai=a+"+"+std::to_string(i), bi=b+"+"+std::to_string(i);
            // ltb = (ai < bi)
            raw("\tmovb "+bi+", %cl"); raw("\tmovb %cl, mo_idx+0");
            raw("\tmovb "+ai+", %cl"); raw("\tmovb %cl, mo_idx+1");
            raw("\tmovb $0, mo_idx+2"); raw("\tmovb $0, mo_idx+3");
            raw("\tmovl mo_idx, %edx"); raw("\tmovb mo_ltb(%edx), %al");
            raw("\tmovb %al, mo_tb+0");          // ltb_i
            // eqb = (ai==bi)
            raw("\tmovb "+bi+", %cl"); raw("\tmovb %cl, mo_idx+0");
            raw("\tmovb "+ai+", %cl"); raw("\tmovb %cl, mo_idx+1");
            raw("\tmovb $0, mo_idx+2"); raw("\tmovb $0, mo_idx+3");
            raw("\tmovl mo_idx, %edx"); raw("\tmovb mo_eqb(%edx), %al");
            raw("\tmovb %al, mo_tb+1");          // eqb_i
            // contrib = eq_so_far & ltb_i ; lt = lt | contrib
            andBit("mo_tb+7","mo_tb+0","mo_tb+2");
            orBit("mo_tb+6","mo_tb+2","mo_tb+6");
            // eq_so_far = eq_so_far & eqb_i
            andBit("mo_tb+7","mo_tb+1","mo_tb+7");
        }
        raw("\tmovb mo_tb+6, %cl"); raw("\tmovb %cl, "+dstByte);
    }

    // ---- effective address (into temp slot) --------------------------
    void computeEA(const Operand& op,int dstSlot){
        const int ACC=2, IDX=3;
        std::string disp = op.disp.empty()? "0" : op.disp;
        raw("\tmovl $"+disp+", "+tExpr(ACC));
        if(!op.base.empty()){ Slot b=regSlot(op.base);
            if(!b.ok||b.size!=4) throw std::runtime_error("bad base: "+op.base);
            add32(tExpr(ACC),tExpr(ACC),cellSym(b.off)); }
        if(!op.index.empty()){ Slot ix=regSlot(op.index);
            if(!ix.ok||ix.size!=4) throw std::runtime_error("bad index: "+op.index);
            raw("\tmovl "+cellSym(ix.off)+", %eax"); raw("\tmovl %eax, "+tExpr(IDX));
            int s=op.scale; while(s>1){ add32(tExpr(IDX),tExpr(IDX),tExpr(IDX)); s>>=1; }
            add32(tExpr(ACC),tExpr(ACC),tExpr(IDX)); }
        if(dstSlot!=ACC){ raw("\tmovl "+tExpr(ACC)+", %eax"); raw("\tmovl %eax, "+tExpr(dstSlot)); }
    }

    // read operand value -> temp slot (guarded for memory sources)
    void readDword(const Operand& op,int slot){
        if(op.kind==OpKind::Immediate){ raw("\tmovl $"+op.imm+", "+tExpr(slot)); return; }
        if(op.kind==OpKind::Register){ Slot r=regSlot(op.reg);
            if(!r.ok) throw std::runtime_error("bad reg: "+op.reg);
            if(r.size==4) raw("\tmovl "+cellSym(r.off)+", %eax");
            else if(r.size==2) raw("\tmovzwl "+cellSym(r.off)+", %eax");
            else raw("\tmovzbl "+cellSym(r.off)+", %eax");
            raw("\tmovl %eax, "+tExpr(slot)); return; }
        if(op.kind==OpKind::Memory){
            computeEA(op,slot);                        // addr -> slot
            // ptr = active ? addr : &mo_discard (safe read)
            raw("\tmovl $mo_discard, mo_ptrtab+0");
            raw("\tmovl "+tExpr(slot)+", %eax"); raw("\tmovl %eax, mo_ptrtab+4");
            raw("\tmovzbl mo_active, %edx");
            raw("\tmovl mo_ptrtab(,%edx,4), %edx");
            raw("\tmovl (%edx), %eax");
            raw("\tmovl %eax, "+tExpr(slot)); return; }
        throw std::runtime_error("cannot read: "+op.raw);
    }

    // guarded write of temp slot value -> operand (size-aware)
    void writeDword(const Operand& op,int srcSlot,int eaSlot,int size){
        char sz=sizeSuffix(size);
        raw("\tmovl $mo_discard, mo_ptrtab+0");
        if(op.kind==OpKind::Register){ Slot r=regSlot(op.reg);
            raw("\tmovl $"+cellSym(r.off)+", mo_ptrtab+4"); }
        else if(op.kind==OpKind::Memory){
            raw("\tmovl "+tExpr(eaSlot)+", %eax"); raw("\tmovl %eax, mo_ptrtab+4"); }
        else throw std::runtime_error("cannot write: "+op.raw);
        raw("\tmovzbl mo_active, %edx");
        raw("\tmovl mo_ptrtab(,%edx,4), %edx");
        raw(std::string("\tmov")+sz+" "+tExpr(srcSlot)+", "+axOf(size));
        raw(std::string("\tmov")+sz+" "+axOf(size)+", (%edx)");
    }

    // guarded write of an immediate dword -> a fixed symbol cell
    void guardedSetSym(const std::string& symImm,int valueSlot){
        raw("\tmovl $mo_discard, mo_ptrtab+0");
        raw("\tmovl $"+symImm+", mo_ptrtab+4");
        raw("\tmovzbl mo_active, %edx");
        raw("\tmovl mo_ptrtab(,%edx,4), %edx");
        raw("\tmovl "+tExpr(valueSlot)+", %eax");
        raw("\tmovl %eax, (%edx)");
    }

    // ---- per-instruction lowering ------------------------------------
    int suffixSize(const std::string& m){ char c=m.back(); return c=='b'?1:c=='w'?2:4; }

    bool lowerBody(const Line& L){
        const std::string& m=L.mnemonic;
        if(m=="movl"||m=="movb"||m=="movw"){ lowerMov(L,suffixSize(m)); return true; }
        if(m=="movzbl"||m=="movsbl"){ lowerMovx(L,1,m[3]=='s'); return true; }
        if(m=="movzwl"||m=="movswl"){ lowerMovx(L,2,m[3]=='s'); return true; }
        if(m=="addl"){ lowerBin(L,"add"); return true; }
        if(m=="subl"){ lowerBin(L,"sub"); return true; }
        if(m=="andl"){ lowerBin(L,"and"); return true; }
        if(m=="orl"){ lowerBin(L,"or"); return true; }
        if(m=="xorl"){ lowerBin(L,"xor"); return true; }
        if(m=="incl"){ lowerIncDec(L,true); return true; }
        if(m=="decl"){ lowerIncDec(L,false); return true; }
        if(m=="negl"){ lowerNeg(L); return true; }
        if(m=="notl"){ lowerNot(L); return true; }
        if(m=="cmpl"){ lowerCmp(L,false); return true; }
        if(m=="testl"){ lowerCmp(L,true); return true; }
        if(m=="nop"||m=="endbr32"){ comment("(dropped) "+L.raw); return true; }
        return false;
    }

    void lowerMov(const Line& L,int size){
        const Operand&src=L.operands[0]; const Operand&dst=L.operands[1];
        comment("mov: "+L.raw);
        if(dst.kind==OpKind::Memory) computeEA(dst,0);
        readDword(src,1);
        writeDword(dst,1,0,size);
    }
    void lowerMovx(const Line& L,int fromSize,bool sign){
        const Operand&src=L.operands[0]; const Operand&dst=L.operands[1];
        comment("movx: "+L.raw);
        std::string ext = sign?(fromSize==1?"movsbl":"movswl"):(fromSize==1?"movzbl":"movzwl");
        if(src.kind==OpKind::Register){ Slot r=regSlot(src.reg);
            raw("\t"+ext+" "+cellSym(r.off)+", %eax"); }
        else if(src.kind==OpKind::Memory){ computeEA(src,1);
            raw("\tmovl $mo_discard, mo_ptrtab+0");
            raw("\tmovl "+tExpr(1)+", %eax"); raw("\tmovl %eax, mo_ptrtab+4");
            raw("\tmovzbl mo_active, %edx"); raw("\tmovl mo_ptrtab(,%edx,4), %edx");
            raw("\t"+ext+" (%edx), %eax"); }
        else throw std::runtime_error("movx src: "+src.raw);
        raw("\tmovl %eax, "+tExpr(1));
        if(dst.kind==OpKind::Memory) computeEA(dst,0);
        writeDword(dst,1,0,4);
    }
    void lowerBin(const Line& L,const std::string& op){
        const Operand&src=L.operands[0]; const Operand&dst=L.operands[1];
        comment(op+": "+L.raw);
        readDword(src,4);
        if(dst.kind==OpKind::Memory) computeEA(dst,0);
        readDword(dst,5);
        std::string A=tExpr(5),B=tExpr(4),R=tExpr(6);
        if(op=="add") add32(R,A,B);
        else if(op=="sub") sub32(R,A,B);
        else if(op=="and") bit32("mo_and",R,A,B);
        else if(op=="or")  bit32("mo_or",R,A,B);
        else if(op=="xor") bit32("mo_xor",R,A,B);
        writeDword(dst,6,0,4);
    }
    void lowerIncDec(const Line& L,bool inc){
        const Operand&dst=L.operands[0];
        comment((inc?"inc: ":"dec: ")+L.raw);
        raw("\tmovl $1, "+tExpr(4));
        if(dst.kind==OpKind::Memory) computeEA(dst,0);
        readDword(dst,5);
        if(inc) add32(tExpr(6),tExpr(5),tExpr(4)); else sub32(tExpr(6),tExpr(5),tExpr(4));
        writeDword(dst,6,0,4);
    }
    void lowerNeg(const Line& L){ const Operand&dst=L.operands[0];
        comment("neg: "+L.raw); raw("\tmovl $0, "+tExpr(4));
        if(dst.kind==OpKind::Memory) computeEA(dst,0);
        readDword(dst,5); sub32(tExpr(6),tExpr(4),tExpr(5)); writeDword(dst,6,0,4); }
    void lowerNot(const Line& L){ const Operand&dst=L.operands[0];
        comment("not: "+L.raw); raw("\tmovl $-1, "+tExpr(4));
        if(dst.kind==OpKind::Memory) computeEA(dst,0);
        readDword(dst,5); bit32("mo_xor",tExpr(6),tExpr(5),tExpr(4)); writeDword(dst,6,0,4); }

    // cmp a,b (AT&T: cmp src,dst computes dst - src). test: dst & src.
    void lowerCmp(const Line& L,bool isTest){
        const Operand& src=L.operands[0]; const Operand& dst=L.operands[1];
        comment((isTest?"test: ":"cmp: ")+L.raw);
        readDword(src,4);            // b (the subtrahend / mask)
        readDword(dst,5);            // a
        std::string A=tExpr(5),B=tExpr(4),R=tExpr(6);
        if(isTest){
            bit32("mo_and",R,A,B);                 // r = a & b
            isZero(R,"mo_fe");                      // ZF
            byteEqConst(R+"+3",0,"mo_tb+0");        // sign byte: build fsf = (bit7 of r3)
            // fsf = high bit of byte3: compute via ltb(0x7f, r3)? simpler: r3 >= 0x80
            // use ltb(0x7f, r3) -> 1 if r3 > 0x7f
            raw("\tmovb "+R+"+3, %cl"); raw("\tmovb %cl, mo_idx+0");
            raw("\tmovb $127, mo_idx+1"); raw("\tmovb $0, mo_idx+2"); raw("\tmovb $0, mo_idx+3");
            raw("\tmovl mo_idx, %edx"); raw("\tmovb mo_ltb(%edx), %al"); // (127 < r3)
            raw("\tmovb %al, mo_fsf");
            // For test, OF=CF=0: signed "less" reduces to SF, unsigned to 0.
            raw("\tmovb $0, mo_fltu");
            raw("\tmovb mo_fsf, %cl"); raw("\tmovb %cl, mo_flts");
            return;
        }
        // cmp: d = a - b
        sub32(R,A,B);
        isZero(R,"mo_fe");                          // ZF = (a==b)
        ltuDword(A,B,"mo_fltu");                     // unsigned a<b
        // signed a<b: if sign_a!=sign_b -> sign_a ; else ltu
        // sign_a = (a3>127), sign_b=(b3>127)
        raw("\tmovb "+A+"+3, %cl"); raw("\tmovb %cl, mo_idx+0");
        raw("\tmovb $127, mo_idx+1"); raw("\tmovb $0, mo_idx+2"); raw("\tmovb $0, mo_idx+3");
        raw("\tmovl mo_idx, %edx"); raw("\tmovb mo_ltb(%edx), %al"); raw("\tmovb %al, mo_tb+0"); // sign_a
        raw("\tmovb "+B+"+3, %cl"); raw("\tmovb %cl, mo_idx+0");
        raw("\tmovb $127, mo_idx+1"); raw("\tmovb $0, mo_idx+2"); raw("\tmovb $0, mo_idx+3");
        raw("\tmovl mo_idx, %edx"); raw("\tmovb mo_ltb(%edx), %al"); raw("\tmovb %al, mo_tb+1"); // sign_b
        // diff = sign_a XOR sign_b
        raw("\tmovb mo_tb+1, %cl"); raw("\tmovb %cl, mo_idx+0");
        raw("\tmovb mo_tb+0, %cl"); raw("\tmovb %cl, mo_idx+1");
        raw("\tmovb $0, mo_idx+2"); raw("\tmovb $0, mo_idx+3");
        raw("\tmovl mo_idx, %edx"); raw("\tmovb mo_xor(%edx), %al"); raw("\tmovb %al, mo_tb+2"); // diff
        // flts = diff ? sign_a : ltu  (select via 2-entry table)
        raw("\tmovb mo_fltu, %cl"); raw("\tmovb %cl, mo_tb+3");   // table[0]=ltu
        raw("\tmovb mo_tb+0, %cl"); raw("\tmovb %cl, mo_tb+4");   // table[1]=sign_a
        raw("\tmovzbl mo_tb+2, %edx");                            // diff (0/1)
        raw("\tmovb mo_tb+3(%edx), %al");                         // select
        raw("\tmovb %al, mo_flts");
        // fsf = sign bit of (a-b)
        raw("\tmovb mo_tb+2, %cl"); // placeholder; compute from R
        raw("\tmovb "+R+"+3, %cl"); raw("\tmovb %cl, mo_idx+0");
        raw("\tmovb $127, mo_idx+1"); raw("\tmovb $0, mo_idx+2"); raw("\tmovb $0, mo_idx+3");
        raw("\tmovl mo_idx, %edx"); raw("\tmovb mo_ltb(%edx), %al"); raw("\tmovb %al, mo_fsf");
    }

    // Compute a jcc's taken bit (0/1) into mo_tb+5 from stored flags.
    void jccCond(const std::string& m){
        auto cpy=[&](const std::string& f){ raw("\tmovb "+f+", %cl"); raw("\tmovb %cl, mo_tb+5"); };
        if(m=="je"||m=="jz"){ cpy("mo_fe"); }
        else if(m=="jne"||m=="jnz"){ notBit("mo_fe","mo_tb+5"); }
        else if(m=="jl"||m=="jnge"){ cpy("mo_flts"); }
        else if(m=="jge"||m=="jnl"){ notBit("mo_flts","mo_tb+5"); }
        else if(m=="jg"||m=="jnle"){ notBit("mo_flts","mo_tb+0"); notBit("mo_fe","mo_tb+1"); andBit("mo_tb+0","mo_tb+1","mo_tb+5"); }
        else if(m=="jle"||m=="jng"){ orBit("mo_flts","mo_fe","mo_tb+5"); }
        else if(m=="jb"||m=="jnae"||m=="jc"){ cpy("mo_fltu"); }
        else if(m=="jae"||m=="jnb"||m=="jnc"){ notBit("mo_fltu","mo_tb+5"); }
        else if(m=="ja"||m=="jnbe"){ notBit("mo_fltu","mo_tb+0"); notBit("mo_fe","mo_tb+1"); andBit("mo_tb+0","mo_tb+1","mo_tb+5"); }
        else if(m=="jbe"||m=="jna"){ orBit("mo_fltu","mo_fe","mo_tb+5"); }
        else if(m=="js"){ cpy("mo_fsf"); }
        else if(m=="jns"){ notBit("mo_fsf","mo_tb+5"); }
        else throw std::runtime_error("unsupported jcc: "+m);
    }
};

// Partition the instruction stream into basic blocks.
std::vector<Block> buildBlocks(const std::vector<Line>& body){
    std::vector<Block> blocks;
    Block cur; bool curOpen=false;
    auto flush=[&](){ if(curOpen){ blocks.push_back(cur); cur=Block{}; curOpen=false; } };
    for(const Line& L : body){
        if(L.kind==LineKind::Label){
            // a label starts a new block
            flush(); curOpen=true; cur.labels.push_back(L.text); continue;
        }
        if(L.kind!=LineKind::Instruction) continue; // drop comments
        if(!curOpen){ curOpen=true; }
        const std::string& m=L.mnemonic;
        if(m=="jmp"||m=="ret"||isJcc(m)){
            cur.term=L; cur.hasTerm=true; flush();   // terminator ends block
        } else {
            cur.body.push_back(L);
        }
    }
    flush();
    for(size_t i=0;i<blocks.size();++i) blocks[i].id=(int)i;
    return blocks;
}

} // namespace

Program rewrite(const Program& in, RewriteStats& stats){
    Lowering lo(stats);

    // Walk the input tracking the current section. Instructions and labels in a
    // text section form the function body we rewrite; everything in a data/bss
    // section (labels + .long/.align/...) is kept verbatim so global data the
    // program reads is still defined. We emit our own `mo_pass` entry.
    std::vector<Line> body;
    std::vector<std::string> dataOut;   // verbatim data/preamble emitted last
    bool inText = true;

    auto isTextDir=[&](const std::string& d){
        if(d==".text"||d.rfind(".text",0)==0) return true;
        if(d.rfind(".section",0)==0 && d.find(".text")!=std::string::npos) return true;
        return false;
    };
    auto isDataDir=[&](const std::string& d){
        if(d.rfind(".data",0)==0||d.rfind(".bss",0)==0) return true;
        if(d.rfind(".section",0)==0 &&
           (d.find("rodata")!=std::string::npos||d.find(".data")!=std::string::npos||
            d.find(".bss")!=std::string::npos||d.find("note")!=std::string::npos))
            return true;
        return false;
    };

    for(const Line& L : in.lines){
        if(L.kind==LineKind::Raw){ lo.out.push_back(L); continue; }
        if(L.kind==LineKind::Directive){
            const std::string& d = L.text;
            auto starts=[&](const char* p){ return d.rfind(p,0)==0; };
            if(isTextDir(d)){ inText=true; continue; }       // we emit our own .text
            if(isDataDir(d)){ inText=false; dataOut.push_back("\t"+d); continue; }
            if(starts(".size")||starts(".type")||starts(".cfi")||
               starts(".file")||starts(".ident")) continue;  // drop debug/meta
            // Other directives: data definitions (.long/.align/.globl/.zero/...)
            // go with whatever section we're in.
            if(inText) { /* harmless text directive: drop (e.g. .p2align) */ }
            else dataOut.push_back("\t"+d);
            continue;
        }
        if(L.kind==LineKind::Label){
            if(inText) body.push_back(L);
            else dataOut.push_back(L.text+":");
            continue;
        }
        if(L.kind==LineKind::Instruction){ body.push_back(L); }
    }

    std::vector<Block> blocks = buildBlocks(body);
    // map labels -> block id
    for(auto& b : blocks) for(auto& lab : b.labels) lo.labelId[lab]=b.id;
    int nblocks=(int)blocks.size();
    auto idOf=[&](const std::string& lab)->int{
        auto it=lo.labelId.find(lab);
        if(it==lo.labelId.end()) throw std::runtime_error("unknown label: "+lab);
        return it->second;
    };

    lo.directive(".text");
    lo.directive(".globl mo_pass");
    lo.rlabel("mo_pass");

    for(const Block& b : blocks){
        lo.comment("==== block "+std::to_string(b.id)+
                   (b.labels.empty()?"":(" ("+b.labels[0]+")")) + " ====");
        // mo_active = (mo_target == id)
        lo.raw("\tmovl $"+std::to_string(b.id)+", mo_t+16"); // slot4
        lo.sub32("mo_t+24","mo_target","mo_t+16");           // slot6 = target-id
        lo.isZero("mo_t+24","mo_active");

        for(const Line& L : b.body){
            if(lo.lowerBody(L)) stats.lowered++;
            else { stats.passthrough++; stats.unsupported.push_back(L.mnemonic);
                   Line keep=L; lo.out.push_back(keep); }
        }

        // terminator -> set mo_next (guarded)
        if(!b.hasTerm){
            // fall through to next block (or halt if last)
            int succ = (b.id+1<nblocks)? b.id+1 : -1;
            if(succ<0){ lo.raw("\tmovl $0xFFFFFFFF, mo_t+28"); }  // slot7 = HALT
            else       { lo.raw("\tmovl $"+std::to_string(succ)+", mo_t+28"); }
            lo.guardedSetSym("mo_next",7);
        } else {
            const Line& t=b.term;
            if(t.mnemonic=="ret"){
                lo.comment("ret: materialise eax, halt");
                lo.raw("\tmovl $0xFFFFFFFF, mo_t+28"); lo.guardedSetSym("mo_next",7);
            } else if(t.mnemonic=="jmp"){
                int tgt=idOf(t.operands[0].label);
                lo.raw("\tmovl $"+std::to_string(tgt)+", mo_t+28"); lo.guardedSetSym("mo_next",7);
            } else { // jcc
                int taken=idOf(t.operands[0].label);
                int nottaken=(b.id+1<nblocks)? b.id+1 : (int)-1;
                int notval = nottaken<0? (int)0 : nottaken;
                lo.jccCond(t.mnemonic);                    // mo_tb+5 = taken?
                // next = cond ? taken : nottaken  (select via table)
                lo.raw("\tmovl $"+std::to_string(nottaken<0?-1:notval)+", mo_t+32"); // slot8 table[0]
                lo.raw("\tmovl $"+std::to_string(taken)+", mo_t+36");                // slot9 table[1]
                lo.raw("\tmovzbl mo_tb+5, %edx");
                lo.raw("\tmovl mo_t+32(,%edx,4), %eax");
                lo.raw("\tmovl %eax, mo_t+28");
                lo.guardedSetSym("mo_next",7);
            }
        }
    }
    // end of pass: mo_target = mo_next (unguarded) then return to driver
    lo.comment("end of pass: advance target, return to driver");
    lo.raw("\tmovl mo_next, %eax");
    lo.raw("\tmovl %eax, mo_target");
    lo.raw("\tret");

    // Emit any data section the program defined, verbatim.
    if(!dataOut.empty()){
        lo.comment("---- program data (verbatim) ----");
        for(const auto& s : dataOut) lo.raw(s);
    }

    Program out; out.lines=std::move(lo.out); return out;
}

} // namespace x86mo
