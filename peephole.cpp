
#include <cassert>
#include <map>
#include <set>

#include "main.h"

// Peephole optimization of one procedure's code lines, before the branch layout.
// See docs/DESIGN.md section 9.1. Runs over the list until no pattern applies.


//////////////////////////////////////////////////////////////////////
// Instructions: operands, flags

enum
{
    FlagN = 1, FlagZ = 2, FlagV = 4, FlagC = 8,
    FlagsNZV = FlagN | FlagZ | FlagV, FlagsAll = FlagN | FlagZ | FlagV | FlagC,
};

struct InstructionInfo
{
    const char* name;
    int     operands;       // 0, 1 or 2
    bool    readsdst;       // The destination (or the only operand) is read
    bool    writesdst;      // The destination (or the only operand) is written
    int     setsflags;      // Flags this instruction determines
    int     readsflags;     // Flags this instruction uses
};

static const InstructionInfo g_instructions[] =
{
    { "MOV",  2, false, true,  FlagsNZV, 0 },
    { "MOVB", 2, false, true,  FlagsNZV, 0 },
    { "ADD",  2, true,  true,  FlagsAll, 0 },
    { "SUB",  2, true,  true,  FlagsAll, 0 },
    { "CMP",  2, true,  false, FlagsAll, 0 },
    { "CMPB", 2, true,  false, FlagsAll, 0 },
    { "BIT",  2, true,  false, FlagsNZV, 0 },
    { "BITB", 2, true,  false, FlagsNZV, 0 },
    { "BIS",  2, true,  true,  FlagsNZV, 0 },
    { "BISB", 2, true,  true,  FlagsNZV, 0 },
    { "BIC",  2, true,  true,  FlagsNZV, 0 },
    { "BICB", 2, true,  true,  FlagsNZV, 0 },
    { "XOR",  2, true,  true,  FlagsNZV, 0 },
    { "CLR",  1, false, true,  FlagsAll, 0 },
    { "CLRB", 1, false, true,  FlagsAll, 0 },
    { "COM",  1, true,  true,  FlagsAll, 0 },
    { "COMB", 1, true,  true,  FlagsAll, 0 },
    { "NEG",  1, true,  true,  FlagsAll, 0 },
    { "INC",  1, true,  true,  FlagsNZV, 0 },
    { "INCB", 1, true,  true,  FlagsNZV, 0 },
    { "DEC",  1, true,  true,  FlagsNZV, 0 },
    { "DECB", 1, true,  true,  FlagsNZV, 0 },
    { "TST",  1, true,  false, FlagsAll, 0 },
    { "TSTB", 1, true,  false, FlagsAll, 0 },
    { "ASL",  1, true,  true,  FlagsAll, 0 },
    { "ASR",  1, true,  true,  FlagsAll, 0 },
    { "ROL",  1, true,  true,  FlagsAll, FlagC },
    { "ROR",  1, true,  true,  FlagsAll, FlagC },
    { "ADC",  1, true,  true,  FlagsAll, FlagC },
    { "SBC",  1, true,  true,  FlagsAll, FlagC },
    { "SWAB", 1, true,  true,  FlagsAll, 0 },
    { "SXT",  1, false, true,  FlagZ | FlagV, FlagN },
    { "CLC",  0, false, false, FlagC, 0 },
    { "SEC",  0, false, false, FlagC, 0 },
};

// MUL, DIV, ASH, ASHC and the FIS instructions are handled separately: register pairs, the stack

static const InstructionInfo* FindInstruction(const string& opcode)
{
    for (const InstructionInfo& info : g_instructions)
    {
        if (opcode == info.name)
            return &info;
    }
    return nullptr;
}

// Flags a conditional branch uses
static int BranchReadsFlags(const string& opcode)
{
    if (opcode == "BEQ" || opcode == "BNE") return FlagZ;
    if (opcode == "BMI" || opcode == "BPL") return FlagN;
    if (opcode == "BVS" || opcode == "BVC") return FlagV;
    if (opcode == "BCS" || opcode == "BCC" || opcode == "BLO" || opcode == "BHIS") return FlagC;
    if (opcode == "BHI" || opcode == "BLOS") return FlagC | FlagZ;
    if (opcode == "BGE" || opcode == "BLT") return FlagN | FlagV;
    if (opcode == "BGT" || opcode == "BLE") return FlagN | FlagV | FlagZ;
    return 0;  // BR
}

static bool IsAddressMode(OperandMode mode)
{
    return mode == OpRegDeferred || mode == OpAutoInc || mode == OpAutoDec || mode == OpIndex || mode == OpIndexDeferred;
}

// The operand uses the register as an address (reads it; autoincrement/autodecrement also write it)
static bool UsesRegAsAddress(const Operand& operand, int reg)
{
    return IsAddressMode(operand.mode) && operand.reg == reg;
}

static bool IsReg(const Operand& operand, int reg)
{
    return operand.mode == OpReg && operand.reg == reg;
}

static bool SameOperand(const Operand& a, const Operand& b)
{
    return a.mode == b.mode && a.ToString() == b.ToString();
}

static int InstructionWords(const CodeLine& line)
{
    return 1 + line.src.ExtraWords() + line.dst.ExtraWords();
}

static string StripColons(const string& label)
{
    string result = label;
    while (!result.empty() && result.back() == ':')
        result.pop_back();
    return result;
}


//////////////////////////////////////////////////////////////////////
// Peephole

class Peephole
{
    std::vector<CodeLine>& m_lines;
    std::map<string, size_t> m_labels;  // Label -> index of its line
public:
    explicit Peephole(std::vector<CodeLine>& lines) : m_lines(lines) {}
    void Run();
private:
    typedef bool (Peephole::*Method)(size_t i);
    struct Pattern { const char* name; Method method; };
    static const Pattern m_patterns[];

    void   CollectLabels();
    bool   RemoveUnusedLabels();
    size_t Next(size_t i) const;
    size_t SkipLabels(size_t i) const;
    bool   IsInstruction(size_t i, const char* opcode) const;
    void   Remove(size_t i);
    // Analysis
    int    RegisterUse(const CodeLine& line, int reg) const;
    bool   IsRegDead(size_t start, int reg, std::set<size_t>& visited) const;
    bool   IsRegDeadAfter(size_t i, int reg) const;
    bool   FlagsRead(size_t start, int mask, std::set<size_t>& visited) const;
    bool   FlagsReadAfter(size_t i, int mask) const;
    size_t LabelIndex(const string& label) const;
    // Patterns
    bool RealReload(size_t i);
    bool MoveThroughRegister(size_t i);
    bool PushPop(size_t i);
    bool Immediate(size_t i);
    bool RedundantTest(size_t i);
    bool BranchToNext(size_t i);
    bool BranchOverBranch(size_t i);
    bool BranchToBranch(size_t i);
    bool BranchToReturn(size_t i);
    bool DeadCode(size_t i);
};

const Peephole::Pattern Peephole::m_patterns[] =
{
    { "RealReload",          &Peephole::RealReload },
    { "MoveThroughRegister", &Peephole::MoveThroughRegister },
    { "PushPop",             &Peephole::PushPop },
    { "Immediate",           &Peephole::Immediate },
    { "RedundantTest",       &Peephole::RedundantTest },
    { "BranchToNext",        &Peephole::BranchToNext },
    { "BranchOverBranch",    &Peephole::BranchOverBranch },
    { "BranchToBranch",      &Peephole::BranchToBranch },
    { "BranchToReturn",      &Peephole::BranchToReturn },
    { "DeadCode",            &Peephole::DeadCode },
};

void Peephole::CollectLabels()
{
    m_labels.clear();
    for (size_t i = 0; i < m_lines.size(); i++)
    {
        if (!m_lines[i].label.empty())
            m_labels[StripColons(m_lines[i].label)] = i;
    }
}

size_t Peephole::LabelIndex(const string& label) const
{
    auto it = m_labels.find(label);
    return (it == m_labels.end()) ? string::npos : it->second;
}

// Next line after i that is not a comment; npos at the end
size_t Peephole::Next(size_t i) const
{
    for (size_t j = i + 1; j < m_lines.size(); j++)
    {
        if (m_lines[j].kind != LineComment)
            return j;
    }
    return string::npos;
}

// First line from i on that is not a label or a comment
size_t Peephole::SkipLabels(size_t i) const
{
    for (size_t j = i; j < m_lines.size(); j++)
    {
        if (m_lines[j].kind != LineComment && m_lines[j].kind != LineLabel)
            return j;
    }
    return string::npos;
}

bool Peephole::IsInstruction(size_t i, const char* opcode) const
{
    return i != string::npos && m_lines[i].kind == LineInstruction && m_lines[i].opcode == opcode;
}

void Peephole::Remove(size_t i)
{
    m_lines.erase(m_lines.begin() + i);
}

// How the instruction uses the register: -1 unknown, 0 not at all, 1 reads it, 2 writes it without reading
int Peephole::RegisterUse(const CodeLine& line, int reg) const
{
    const string& op = line.opcode;
    if (op == "MUL" || op == "DIV" || op == "ASH" || op == "ASHC")
    {
        // src, Rn: reads src and Rn (DIV, ASHC: the pair), writes Rn and Rn+1 for an even Rn (MUL, DIV, ASHC)
        int rn = line.dst.reg;
        bool pair = (op != "ASH") && (rn % 2 == 0);
        if (IsReg(line.src, reg) || UsesRegAsAddress(line.src, reg))
            return 1;
        if (reg == rn)
            return 1;
        if (pair && reg == rn + 1)
            return (op == "MUL") ? 2 : 1;  // MUL writes the pair, DIV and ASHC also read it
        return 0;
    }
    if (op == "FADD" || op == "FSUB" || op == "FMUL" || op == "FDIV")
        return 0;  // The stack only
    const InstructionInfo* info = FindInstruction(op);
    if (info == nullptr)
        return -1;
    if (info->operands == 0)
        return 0;
    const Operand& target = (info->operands == 2) ? line.dst : line.src;
    if (info->operands == 2 && (IsReg(line.src, reg) || UsesRegAsAddress(line.src, reg)))
        return 1;
    if (UsesRegAsAddress(target, reg))
        return 1;
    if (IsReg(target, reg))
    {
        if (info->readsdst)
            return 1;
        if (info->writesdst)
        {
            // A byte write keeps the high byte of a register: that counts as a read
            if (op == "MOVB" || op == "CLRB")
                return 1;
            return 2;
        }
    }
    return 0;
}

// The register is not read before it is written, on every path from the line on
bool Peephole::IsRegDead(size_t start, int reg, std::set<size_t>& visited) const
{
    for (size_t i = start; i < m_lines.size(); i++)
    {
        const CodeLine& line = m_lines[i];
        switch (line.kind)
        {
        case LineComment:
        case LineLabel:
            continue;
        case LineRaw:
        case LineData:
            return false;
        case LineBranch:
            {
                size_t target = LabelIndex(line.target);
                if (target == string::npos)
                    return false;
                if (visited.insert(target).second && !IsRegDead(target, reg, visited))
                    return false;
                if (line.opcode == "BR")
                    return true;
                continue;
            }
        default:
            break;
        }
        if (line.opcode == "RETURN")
            return reg != 0;  // R0 is a function result
        if (line.opcode == "CALL" || line.opcode == "JMP" || !line.text.empty())
            return false;
        int use = RegisterUse(line, reg);
        if (use < 0 || use == 1)
            return false;
        if (use == 2)
            return true;
    }
    return false;
}

bool Peephole::IsRegDeadAfter(size_t i, int reg) const
{
    std::set<size_t> visited;
    return IsRegDead(i + 1, reg, visited);
}

// Some of the flags in the mask are read before an instruction determines them, on some path
bool Peephole::FlagsRead(size_t start, int mask, std::set<size_t>& visited) const
{
    for (size_t i = start; i < m_lines.size(); i++)
    {
        const CodeLine& line = m_lines[i];
        switch (line.kind)
        {
        case LineComment:
        case LineLabel:
            continue;
        case LineRaw:
        case LineData:
            return true;
        case LineBranch:
            {
                if (BranchReadsFlags(line.opcode) & mask)
                    return true;
                size_t target = LabelIndex(line.target);
                if (target == string::npos)
                    return true;
                if (visited.insert(target).second && FlagsRead(target, mask, visited))
                    return true;
                if (line.opcode == "BR")
                    return false;
                continue;
            }
        default:
            break;
        }
        if (line.opcode == "CALL")
            return false;  // The callee determines the flags
        if (line.opcode == "RETURN" || line.opcode == "JMP" || !line.text.empty())
            return true;
        const string& op = line.opcode;
        if (op == "MUL" || op == "DIV" || op == "ASH" || op == "ASHC" ||
            op == "FADD" || op == "FSUB" || op == "FMUL" || op == "FDIV")
            return false;  // All flags set
        const InstructionInfo* info = FindInstruction(op);
        if (info == nullptr)
            return true;
        if (info->readsflags & mask)
            return true;
        mask &= ~info->setsflags;
        if (mask == 0)
            return false;
    }
    return true;
}

bool Peephole::FlagsReadAfter(size_t i, int mask) const
{
    std::set<size_t> visited;
    return FlagsRead(i + 1, mask, visited);
}


//////////////////////////////////////////////////////////////////////
// Patterns: each gets an instruction line and returns true if it changed the list

// Operand that can be read again with the same value: not a device register at an absolute address
static bool IsPlainMemory(const Operand& operand)
{
    switch (operand.mode)
    {
    case OpLabel:
        return true;
    case OpIndex:
    case OpRegDeferred:
        return operand.reg != 6 && operand.reg != 7;
    default:
        return false;
    }
}

// Real stored and loaded back:
//   MOV (SP)+, A / MOV (SP)+, B / MOV B, -(SP) / MOV A, -(SP)  ->  MOV (SP), A / MOV 2(SP), B
bool Peephole::RealReload(size_t i)
{
    size_t j = Next(i), k = (j == string::npos) ? j : Next(j), l = (k == string::npos) ? k : Next(k);
    if (!IsInstruction(i, "MOV") || !IsInstruction(j, "MOV") || !IsInstruction(k, "MOV") || !IsInstruction(l, "MOV"))
        return false;
    const CodeLine& a = m_lines[i];
    const CodeLine& b = m_lines[j];
    const CodeLine& c = m_lines[k];
    const CodeLine& d = m_lines[l];
    Operand pop = Operand::AutoInc(6), push = Operand::AutoDec(6);
    if (!SameOperand(a.src, pop) || !SameOperand(b.src, pop) || !SameOperand(c.dst, push) || !SameOperand(d.dst, push))
        return false;
    if (!IsPlainMemory(a.dst) || !IsPlainMemory(b.dst) || !SameOperand(c.src, b.dst) || !SameOperand(d.src, a.dst))
        return false;
    if (FlagsReadAfter(l, FlagsNZV))
        return false;  // The last MOV set the flags by the high word
    m_lines[i].src = Operand::Deferred(6);
    m_lines[i].words = InstructionWords(m_lines[i]);
    m_lines[j].src = Operand::Index(6, 2);
    m_lines[j].words = InstructionWords(m_lines[j]);
    Remove(l);
    Remove(k);
    return true;
}

// MOV src, Rn / MOV Rn, dst  ->  MOV src, dst  when Rn is dead after
bool Peephole::MoveThroughRegister(size_t i)
{
    size_t j = Next(i);
    if (!IsInstruction(i, "MOV") || !IsInstruction(j, "MOV"))
        return false;
    const CodeLine& first = m_lines[i];
    const CodeLine& second = m_lines[j];
    if (first.dst.mode != OpReg || first.dst.reg > 4)
        return false;
    int reg = first.dst.reg;
    if (!IsReg(second.src, reg) || IsReg(second.dst, reg) || UsesRegAsAddress(second.dst, reg))
        return false;
    // MOV SP, -(SP) stores SP before or after the decrement depending on the processor model
    if ((first.src.mode == OpReg && first.src.reg >= 6))
        return false;
    if (!IsRegDeadAfter(j, reg))
        return false;
    CodeLine merged = first;
    merged.dst = second.dst;
    merged.words = InstructionWords(merged);
    if (merged.comment.empty())
        merged.comment = second.comment;
    m_lines[i] = merged;
    Remove(j);
    return true;
}

// MOV Rn, -(SP) / MOV (SP)+, dst  ->  MOV Rn, dst
bool Peephole::PushPop(size_t i)
{
    size_t j = Next(i);
    if (!IsInstruction(i, "MOV") || !IsInstruction(j, "MOV"))
        return false;
    const CodeLine& first = m_lines[i];
    const CodeLine& second = m_lines[j];
    if (first.src.mode != OpReg || first.src.reg >= 6 ||
        !SameOperand(first.dst, Operand::AutoDec(6)) || !SameOperand(second.src, Operand::AutoInc(6)))
        return false;
    if (UsesRegAsAddress(second.dst, 6))
        return false;
    if (IsReg(second.dst, first.src.reg))
    {
        if (FlagsReadAfter(j, FlagsNZV))
            return false;
        Remove(j);
        Remove(i);
        return true;
    }
    CodeLine merged = second;
    merged.src = first.src;
    merged.words = InstructionWords(merged);
    m_lines[i] = merged;
    Remove(j);
    return true;
}

// Instructions with an immediate operand: #0 and #1 forms
bool Peephole::Immediate(size_t i)
{
    if (m_lines[i].kind != LineInstruction)
        return false;
    CodeLine& line = m_lines[i];
    const string& op = line.opcode;
    if (line.src.mode != OpImmediate || !line.src.label.empty())
        return false;
    if (line.dst.mode == OpReg && line.dst.reg >= 6)
        return false;  // SP and PC stay as they are
    int64_t value = line.src.value;
    if ((op == "ADD" || op == "SUB") && (value == 1 || value == -1))
    {
        if (FlagsReadAfter(i, FlagC))
            return false;  // INC and DEC keep C
        bool up = (op == "ADD") == (value == 1);
        line.opcode = up ? "INC" : "DEC";
        line.src = line.dst;
        line.dst = Operand();
        line.words = InstructionWords(line);
        return true;
    }
    if ((op == "ADD" || op == "SUB" || op == "BIS" || op == "BIC" || op == "BISB" || op == "BICB") && value == 0)
    {
        if (FlagsReadAfter(i, FlagsAll))
            return false;
        Remove(i);
        return true;
    }
    if ((op == "MOV" || op == "MOVB") && value == 0)
    {
        if (FlagsReadAfter(i, FlagC))
            return false;  // CLR clears C, MOV keeps it
        line.opcode = (op == "MOV") ? "CLR" : "CLRB";
        line.src = line.dst;
        line.dst = Operand();
        line.words = InstructionWords(line);
        return true;
    }
    return false;
}

// Also the immediate as the second operand: CMP x, #0 -> TST x
static bool CompareWithZero(CodeLine& line)
{
    if ((line.opcode != "CMP" && line.opcode != "CMPB") || line.dst.mode != OpImmediate ||
        !line.dst.label.empty() || line.dst.value != 0)
        return false;
    line.opcode = (line.opcode == "CMP") ? "TST" : "TSTB";
    line.dst = Operand();
    line.words = InstructionWords(line);
    return true;
}

// TST Rn right after an instruction that set N and Z by the whole of Rn
bool Peephole::RedundantTest(size_t i)
{
    if (m_lines[i].kind == LineInstruction && CompareWithZero(m_lines[i]))
        return true;
    if (!IsInstruction(i, "TST") || m_lines[i].src.mode != OpReg)
        return false;
    int reg = m_lines[i].src.reg;
    // The previous line, comments skipped, must be an instruction (a label would let other paths in)
    size_t p = i;
    do
    {
        if (p == 0)
            return false;
        p--;
    } while (m_lines[p].kind == LineComment);
    const CodeLine& prev = m_lines[p];
    if (prev.kind != LineInstruction)
        return false;
    const string& op = prev.opcode;
    int mask;
    if (op == "MOV" || op == "BIS" || op == "BIC" || op == "XOR")
        mask = FlagC;                   // V is 0 as after TST, C is kept
    else if (op == "ADD" || op == "SUB")
        mask = FlagV | FlagC;
    else if (op == "INC" || op == "DEC" || op == "NEG" || op == "COM" || op == "ASL" || op == "ASR")
        mask = FlagV | FlagC;
    else if (op == "BISB")
        mask = FlagN | FlagC;           // N of the byte, not of the register
    else if (op == "CLR")
        mask = 0;                       // The same flags as TST
    else
        return false;
    const Operand& target = (op == "INC" || op == "DEC" || op == "NEG" || op == "COM" || op == "ASL" || op == "ASR" || op == "CLR") ?
        prev.src : prev.dst;
    bool movedfrom = (op == "MOV" && IsReg(prev.src, reg));  // MOV Rn, x sets N and Z by Rn too
    if (!IsReg(target, reg) && !movedfrom)
        return false;
    if (op == "BISB")
    {
        // Only after CLR Rn: the high byte is zero
        size_t q = p;
        do
        {
            if (q == 0)
                return false;
            q--;
        } while (m_lines[q].kind == LineComment);
        if (!IsInstruction(q, "CLR") || !IsReg(m_lines[q].src, reg))
            return false;
    }
    if (mask != 0 && FlagsReadAfter(i, mask))
        return false;
    Remove(i);
    return true;
}

// Bxx L / L:  ->  L:   (also BR)
bool Peephole::BranchToNext(size_t i)
{
    if (m_lines[i].kind != LineBranch)
        return false;
    const string& target = m_lines[i].target;
    for (size_t j = i + 1; j < m_lines.size(); j++)
    {
        const CodeLine& line = m_lines[j];
        if (line.kind == LineComment)
            continue;
        if (line.kind != LineLabel)
            return false;
        if (StripColons(line.label) == target)
        {
            Remove(i);
            return true;
        }
    }
    return false;
}

// Bxx L1 / BR L2 / L1:  ->  Byy L2 / L1:
bool Peephole::BranchOverBranch(size_t i)
{
    const CodeLine& cond = m_lines[i];
    if (cond.kind != LineBranch || cond.opcode == "BR")
        return false;
    const char* inverse = GetInverseBranch(cond.opcode);
    if (inverse == nullptr)
        return false;
    size_t j = Next(i);
    if (j == string::npos || m_lines[j].kind != LineBranch || m_lines[j].opcode != "BR")
        return false;
    for (size_t k = j + 1; k < m_lines.size(); k++)
    {
        const CodeLine& line = m_lines[k];
        if (line.kind == LineComment)
            continue;
        if (line.kind != LineLabel)
            return false;
        if (StripColons(line.label) == cond.target)
        {
            CodeLine replaced = m_lines[j];
            replaced.opcode = inverse;
            replaced.comment = cond.comment;
            m_lines[i] = replaced;
            Remove(j);
            return true;
        }
    }
    return false;
}

// A branch to BR M or JMP M goes straight to M
bool Peephole::BranchToBranch(size_t i)
{
    CodeLine& line = m_lines[i];
    if (line.kind != LineBranch)
        return false;
    size_t target = LabelIndex(line.target);
    if (target == string::npos)
        return false;
    size_t k = SkipLabels(target);
    if (k == string::npos || k == i)
        return false;
    const CodeLine& next = m_lines[k];
    string newtarget;
    if (next.kind == LineBranch && next.opcode == "BR")
        newtarget = next.target;
    else if (next.kind == LineInstruction && next.opcode == "JMP" && next.src.mode == OpLabel && next.src.value == 0)
        newtarget = next.src.label;
    else
        return false;
    if (newtarget == line.target || LabelIndex(newtarget) == string::npos)
        return false;
    line.target = newtarget;
    line.text = newtarget;
    return true;
}

// BR L where L: RETURN  ->  RETURN
bool Peephole::BranchToReturn(size_t i)
{
    CodeLine& line = m_lines[i];
    if (line.kind != LineBranch || line.opcode != "BR")
        return false;
    size_t target = LabelIndex(line.target);
    if (target == string::npos)
        return false;
    size_t k = SkipLabels(target);
    if (!IsInstruction(k, "RETURN"))
        return false;
    CodeLine ret = m_lines[k];
    ret.label.clear();
    ret.comment = line.comment;
    m_lines[i] = ret;
    return true;
}

// Code after BR, JMP or RETURN up to the next label cannot be reached
bool Peephole::DeadCode(size_t i)
{
    const CodeLine& line = m_lines[i];
    bool unconditional = (line.kind == LineBranch && line.opcode == "BR") ||
        (line.kind == LineInstruction && (line.opcode == "JMP" || line.opcode == "RETURN"));
    if (!unconditional)
        return false;
    size_t j = Next(i);
    if (j == string::npos)
        return false;
    const CodeLine& next = m_lines[j];
    if (next.kind != LineInstruction && next.kind != LineBranch)
        return false;
    Remove(j);
    return true;
}


//////////////////////////////////////////////////////////////////////

void Peephole::Run()
{
    for (int pass = 0; pass < 1000; pass++)
    {
        bool changed = false;
        CollectLabels();
        for (size_t i = 0; i < m_lines.size() && !changed; i++)
        {
            for (const Pattern& pattern : m_patterns)
            {
                if ((this->*pattern.method)(i))
                {
                    changed = true;
                    break;
                }
            }
        }
        if (!changed && !RemoveUnusedLabels())
            break;  // A removed label may open new windows: then one more round
    }
}

// Labels L00000 without references go away; true if any was removed
bool Peephole::RemoveUnusedLabels()
{
    std::set<string> used;
    for (const CodeLine& line : m_lines)
    {
        if (!line.target.empty())
            used.insert(line.target);
        for (const Operand* op : { &line.src, &line.dst })
        {
            if (!op->label.empty())
                used.insert(op->label);
        }
        // Text operands: data, raw lines, SOB
        const string& text = line.text;
        for (size_t pos = 0; (pos = text.find('L', pos)) != string::npos; pos++)
        {
            if (pos + 6 <= text.size())
                used.insert(text.substr(pos, 6));
        }
    }
    bool removed = false;
    for (size_t i = m_lines.size(); i-- > 0; )
    {
        const CodeLine& line = m_lines[i];
        if (line.kind != LineLabel)
            continue;
        string name = StripColons(line.label);
        // A label before an asm insert starts a new scope of local labels 1$: it stays
        size_t next = Next(i);
        if (next != string::npos && m_lines[next].kind == LineRaw)
            continue;
        if (name.size() == 6 && name[0] == 'L' && used.count(name) == 0)
        {
            Remove(i);
            removed = true;
        }
    }
    return removed;
}

void OptimizeCode(ProcedureCode& code)
{
    Peephole peephole(code.lines);
    peephole.Run();
}


//////////////////////////////////////////////////////////////////////
