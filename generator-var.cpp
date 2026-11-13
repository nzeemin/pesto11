
#include <cassert>

#include "main.h"

// Generator: variables and their addresses, see docs/DESIGN.md sections 7.3 and 7.5.
// A variable designator (identifier, field, array element, pointer target, Mem[]) gives an operand.
// The code that computes a base address puts it into the register given by the caller (R3 by default);
// it may also use R1, and R0 only when DesignatorUsesR0() says so.


//////////////////////////////////////////////////////////////////////

// Variable symbol that has a location: variables, parameters, typed constants, function results
static bool IsVariableSymbol(const SymbolModel& symbol)
{
    switch (symbol.kind)
    {
    case SymVar: case SymTypedConst: case SymParam: case SymVarParam: case SymFuncResult:
        return true;
    default:
        return false;
    }
}

// Mem[] and MemW[]: arrays over the whole memory
static bool IsMemArray(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.kind != NodeIdent || node.symbol < 0)
        return false;
    const SymbolModel& symbol = g_program.symbols[node.symbol];
    return symbol.kind == SymStdVar && (symbol.name == "Mem" || symbol.name == "MemW");
}

static bool IsHeapPtr(const SymbolModel& symbol)
{
    return symbol.kind == SymStdVar && symbol.name == "HeapPtr";
}

// Operand that needs no register: a global, an absolute address, or a slot in the current frame
static bool IsStaticOperand(const Operand& operand)
{
    return operand.mode == OpLabel || operand.mode == OpAbsolute ||
        (operand.mode == OpIndex && operand.reg == 5);
}

bool Generator::IsDesignator(const ExpressionModel& expr, int index) const
{
    const ExpressionNode& node = expr.nodes[index];
    switch (node.kind)
    {
    case NodeIdent:
        if (node.fieldoffset >= 0)
            return true;  // Field inside with
        if (node.symbol < 0)
            return false;
        {
            const SymbolModel& symbol = g_program.symbols[node.symbol];
            return IsVariableSymbol(symbol) || IsHeapPtr(symbol) || IsMemArray(expr, index);
        }
    case NodeField:
    case NodeIndex:
        return true;
    case NodeDeref:
        return Validator::IsPointerType(expr.nodes[node.left].type);
    default:
        return false;
    }
}

// Does the address code of the designator need R0: an index that is computed by an expression
// or checked by {$R+}, or a pointer that comes from a function
bool Generator::DesignatorUsesR0(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    switch (node.kind)
    {
    case NodeField:
        return DesignatorUsesR0(expr, node.left);
    case NodeDeref:
        return !IsDesignator(expr, node.left) || DesignatorUsesR0(expr, node.left);
    case NodeIndex:
        {
            bool check = (node.token.directives & DirectiveR) != 0 && !IsMemArray(expr, node.left);
            for (const ArgModel& arg : node.args)
            {
                Operand operand;
                bool isbyte;
                if (expr.nodes[arg.node].isconst)
                    continue;
                if (check || !GetSimpleOperand(expr, arg.node, operand, isbyte))
                    return true;
            }
            return DesignatorUsesR0(expr, node.left);
        }
    default:
        return false;
    }
}

// Operand of a variable symbol. A variable of an enclosing procedure is reached through the
// static chain in the register; with emit false such a variable gives false instead.
bool Generator::GetSymbolOperand(int symindex, int reg, bool emit, Operand& operand)
{
    const SymbolModel& symbol = g_program.symbols[symindex];
    if (IsHeapPtr(symbol))
    {
        operand = Operand::Label("HEAPPT");
        m_final->runtimeneeds.insert("HEAPPT");
        return true;
    }
    if (!IsVariableSymbol(symbol))
        return false;
    if (symbol.absoluteaddr >= 0)
    {
        operand = Operand::Absolute(symbol.absoluteaddr);
        return true;
    }
    if (symbol.absolutesym >= 0)
        return GetSymbolOperand(symbol.absolutesym, reg, emit, operand);
    if (!symbol.location.asmlabel.empty())
    {
        operand = Operand::Label(symbol.location.asmlabel);
        return true;
    }
    int base = 5;
    if (symbol.level != g_program.procedures[m_procindex].level)
    {
        if (!emit)
            return false;
        EmitStaticChain(symbol.level, reg);
        base = reg;
    }
    operand = symbol.indirect ? Operand::IndexDeferred(base, symbol.location.offset) : Operand::Index(base, symbol.location.offset);
    return true;
}

// Frame of the enclosing procedure of the given level into the register, by the static chain:
// -2(R5) is the frame of the enclosing procedure, -2 of that frame is the next one, and so on
void Generator::EmitStaticChain(int level, int reg)
{
    int current = g_program.procedures[m_procindex].level;
    assert(level < current);
    Emit("MOV", Operand::Index(5, -2), Operand::Reg(reg), "static link");
    for (int i = current - 1; i > level; i--)
        Emit("MOV", Operand::Index(reg, -2), Operand::Reg(reg));
}

// Operand for a variable that needs no code: a global, a local or a parameter of the current
// procedure, a field or a constant-index element of those, a field of a static with record
bool Generator::GetVariableOperand(const ExpressionModel& expr, int index, Operand& operand)
{
    const ExpressionNode& node = expr.nodes[index];
    switch (node.kind)
    {
    case NodeIdent:
        if (node.fieldoffset >= 0)
        {
            auto it = m_withstatic.find(node.withslot.ToString());
            if (it == m_withstatic.end())
                return false;
            operand = it->second;
            operand.value += node.fieldoffset;
            return true;
        }
        if (node.symbol < 0)
            return false;
        return GetSymbolOperand(node.symbol, -1, false, operand);
    case NodeField:
        if (!GetVariableOperand(expr, node.left, operand) || !IsStaticOperand(operand))
            return false;
        operand.value += node.fieldoffset;
        return true;
    case NodeIndex:
        {
            bool ismem = IsMemArray(expr, node.left);
            if (ismem)
                operand = Operand::Absolute(0);
            else if (!GetVariableOperand(expr, node.left, operand) || !IsStaticOperand(operand))
                return false;
            int type = expr.nodes[node.left].type;
            for (const ArgModel& arg : node.args)
            {
                const ExpressionNode& argnode = expr.nodes[arg.node];
                if (!argnode.isconst)
                    return false;
                const TypeModel& model = Validator::GetType(type);
                int64_t low = 0;
                int size = 1;
                if (ismem)
                    size = 1;  // The index is the byte address
                else if (model.kind == TypeKindArray)
                {
                    low = Validator::GetType(model.indextype).low;
                    type = model.elemtype;
                    size = Validator::TypeSize(type);
                }
                else
                    type = g_typeChar;  // string[n]: s[0] is the length
                operand.value += (argnode.constvalue.ivalue - low) * size;
            }
            if (operand.mode == OpAbsolute)
                operand.value &= 0xFFFF;
            return true;
        }
    default:
        return false;
    }
}


//////////////////////////////////////////////////////////////////////
// Address code

// Add a constant to the address of the operand; an address held in memory is loaded into reg first
bool Generator::AddOffset(Operand& operand, int64_t offset, int reg)
{
    if (offset == 0)
        return true;
    switch (operand.mode)
    {
    case OpLabel:
    case OpIndex:
        operand.value += offset;
        return true;
    case OpAbsolute:
        operand.value = (operand.value + offset) & 0xFFFF;
        return true;
    case OpRegDeferred:
        operand = Operand::Index(operand.reg, offset);
        return true;
    case OpIndexDeferred:
        Emit("MOV", Operand::Index(operand.reg, operand.value), Operand::Reg(reg));
        operand = Operand::Index(reg, offset);
        return true;
    default:
        return false;
    }
}

// Register times the element size: shifts for small powers of two, MUL otherwise
// (MUL with an odd register keeps the low word in it; an even one goes through R1)
void Generator::ScaleRegister(int reg, int size)
{
    if (size == 1)
        return;
    int shifts = -1;
    for (int i = 1; i <= 4; i++)
    {
        if (size == (1 << i))
            shifts = i;
    }
    if (shifts > 0)
    {
        for (int i = 0; i < shifts; i++)
            Emit("ASL", Operand::Reg(reg));
        return;
    }
    if (reg % 2 == 1)
    {
        Emit("MUL", Operand::Imm(size), Operand::Reg(reg), "element size");
        return;
    }
    Emit("MOV", Operand::Reg(reg), Operand::Reg(1));
    Emit("MUL", Operand::Imm(size), Operand::Reg(1), "element size");
    Emit("MOV", Operand::Reg(1), Operand::Reg(reg));
}

// Operand of a variable designator, the address code goes into reg (see the top of the file)
bool Generator::GenerateDesignator(const ExpressionModel& expr, int index, int reg, Operand& operand)
{
    if (GetVariableOperand(expr, index, operand))
        return true;
    const ExpressionNode& node = expr.nodes[index];
    switch (node.kind)
    {
    case NodeIdent:
        if (node.fieldoffset >= 0)
        {
            // Field inside with: the record address is in the with slot
            const TempSlot& slot = node.withslot;
            if (!slot.asmlabel.empty())
            {
                Emit("MOV", Operand::Label(slot.asmlabel), Operand::Reg(reg), "with record");
                operand = Operand::Deferred(reg);
            }
            else
                operand = Operand::IndexDeferred(5, slot.offset);
            return AddOffset(operand, node.fieldoffset, reg);
        }
        if (node.symbol < 0)
            return false;
        return GetSymbolOperand(node.symbol, reg, true, operand);
    case NodeField:
        if (!GenerateDesignator(expr, node.left, reg, operand))
            return false;
        return AddOffset(operand, node.fieldoffset, reg);
    case NodeIndex:
        return GenerateElement(expr, index, reg, operand);
    case NodeDeref:
        {
            if (!Validator::IsPointerType(expr.nodes[node.left].type))
                return false;  // File buffer variable: step 9
            Operand pointer;
            if (IsDesignator(expr, node.left))
            {
                if (!GenerateDesignator(expr, node.left, reg, pointer))
                    return false;
                Emit("MOV", pointer, Operand::Reg(reg), expr.nodes[node.left].token.text + "^");
            }
            else
            {
                if (!GenerateValue(expr, node.left))
                    return false;
                Emit("MOV", Operand::Reg(0), Operand::Reg(reg));
            }
            operand = Operand::Deferred(reg);
            return true;
        }
    default:
        return false;
    }
}

// Array or string element: base + (index - low) * size for every index
bool Generator::GenerateElement(const ExpressionModel& expr, int index, int reg, Operand& operand)
{
    const ExpressionNode& node = expr.nodes[index];
    bool ismem = IsMemArray(expr, node.left);
    if (ismem)
        operand = Operand::Absolute(0);
    else if (!GenerateDesignator(expr, node.left, reg, operand))
        return false;
    bool check = (node.token.directives & DirectiveR) != 0 && !ismem;

    int type = expr.nodes[node.left].type;
    for (const ArgModel& arg : node.args)
    {
        const TypeModel& model = Validator::GetType(type);
        int64_t low = 0, high = 0;
        int size = 1;
        if (ismem)
        {
            size = 1;  // The index is the byte address, also for MemW
            check = false;
        }
        else if (model.kind == TypeKindArray)
        {
            low = Validator::GetType(model.indextype).low;
            high = Validator::GetType(model.indextype).high;
            type = model.elemtype;
            size = Validator::TypeSize(type);
        }
        else if (model.kind == TypeKindString)
        {
            high = model.high;  // s[0] is the length byte
            type = g_typeChar;
        }
        else
            return false;

        const ExpressionNode& argnode = expr.nodes[arg.node];
        if (argnode.isconst)
        {
            if (!AddOffset(operand, (argnode.constvalue.ivalue - low) * size, reg))
                return false;
            continue;
        }
        if (!GenerateIndex(expr, arg.node, reg, low, high, size, check, operand))
            return false;
    }
    return true;
}

// Add a computed index to the base operand; the result is an index operand on reg
bool Generator::GenerateIndex(const ExpressionModel& expr, int argnode, int reg, int64_t low, int64_t high, int size, bool check, Operand& operand)
{
    Operand simple;
    bool isbyte;
    bool simplearg = !check && GetSimpleOperand(expr, argnode, simple, isbyte);
    int64_t offset = operand.value - low * size;
    string label = (operand.mode == OpLabel || operand.mode == OpIndex) ? operand.label : "";

    if (IsStaticOperand(operand))
    {
        // The base needs no register: the scaled index goes into reg
        if (simplearg)
            LoadOrdinal(expr, argnode, reg);
        else
        {
            if (!GenerateValue(expr, argnode))
                return false;
            if (check)
            {
                EmitCall("RNGCHK", "index check, error 90");
                EmitData(".WORD", FormatNumber(low) + ", " + FormatNumber(high), 2);
            }
            Emit("MOV", Operand::Reg(0), Operand::Reg(reg));
        }
        ScaleRegister(reg, size);
        if (operand.mode == OpIndex)
            Emit("ADD", Operand::Reg(5), Operand::Reg(reg), "frame");
        operand = Operand::Index(reg, operand.mode == OpAbsolute ? (offset & 0xFFFF) : offset);
        operand.label = label;
        if (operand.value == 0 && label.empty())
            operand = Operand::Deferred(reg);
        return true;
    }

    // The base address is computed: bring it into reg, then add the scaled index
    switch (operand.mode)
    {
    case OpIndexDeferred:
        Emit("MOV", Operand::Index(operand.reg, operand.value), Operand::Reg(reg));
        offset = -low * size;
        label.clear();
        break;
    case OpRegDeferred:
        if (operand.reg != reg)
            Emit("MOV", Operand::Reg(operand.reg), Operand::Reg(reg));
        offset = -low * size;
        break;
    case OpIndex:
        if (operand.reg != reg)
            Emit("MOV", Operand::Reg(operand.reg), Operand::Reg(reg));
        break;
    default:
        return false;
    }
    if (simplearg)
    {
        LoadOrdinal(expr, argnode, 1);
        ScaleRegister(1, size);
        Emit("ADD", Operand::Reg(1), Operand::Reg(reg));
    }
    else
    {
        Emit("MOV", Operand::Reg(reg), Operand::AutoDec(6), "base address");
        if (!GenerateValue(expr, argnode))
            return false;
        if (check)
        {
            EmitCall("RNGCHK", "index check, error 90");
            EmitData(".WORD", FormatNumber(low) + ", " + FormatNumber(high), 2);
        }
        ScaleRegister(0, size);
        Emit("ADD", Operand::AutoInc(6), Operand::Reg(0));
        Emit("MOV", Operand::Reg(0), Operand::Reg(reg));
    }
    operand = Operand::Index(reg, offset);
    operand.label = label;
    if (offset == 0 && label.empty())
        operand = Operand::Deferred(reg);
    return true;
}

// Address the operand refers to, into the register
void Generator::LoadAddress(const Operand& operand, int reg)
{
    switch (operand.mode)
    {
    case OpLabel:
        Emit("MOV", Operand::ImmLabel(operand.label, operand.value), Operand::Reg(reg));
        break;
    case OpAbsolute:
        Emit("MOV", Operand::Imm(operand.value), Operand::Reg(reg));
        break;
    case OpIndexDeferred:
        Emit("MOV", Operand::Index(operand.reg, operand.value), Operand::Reg(reg));
        break;
    case OpRegDeferred:
        if (operand.reg != reg)
            Emit("MOV", Operand::Reg(operand.reg), Operand::Reg(reg));
        break;
    case OpIndex:
        if (operand.reg != reg)
            Emit("MOV", Operand::Reg(operand.reg), Operand::Reg(reg));
        if (!operand.label.empty())
            Emit("ADD", Operand::ImmLabel(operand.label, operand.value), Operand::Reg(reg));
        else if (operand.value != 0)
            Emit("ADD", Operand::Imm(operand.value), Operand::Reg(reg));
        break;
    default:
        assert(false);
        break;
    }
}

// Address the operand refers to, onto the stack
void Generator::PushAddress(const Operand& operand, const string& comment)
{
    switch (operand.mode)
    {
    case OpLabel:
        Emit("MOV", Operand::ImmLabel(operand.label, operand.value), Operand::AutoDec(6), comment);
        break;
    case OpAbsolute:
        Emit("MOV", Operand::Imm(operand.value), Operand::AutoDec(6), comment);
        break;
    case OpIndexDeferred:
        Emit("MOV", Operand::Index(operand.reg, operand.value), Operand::AutoDec(6), comment);
        break;
    case OpRegDeferred:
        Emit("MOV", Operand::Reg(operand.reg), Operand::AutoDec(6), comment);
        break;
    case OpIndex:
        Emit("MOV", Operand::Reg(operand.reg), Operand::AutoDec(6), comment);
        if (!operand.label.empty())
            Emit("ADD", Operand::ImmLabel(operand.label, operand.value), Operand::Deferred(6));
        else if (operand.value != 0)
            Emit("ADD", Operand::Imm(operand.value), Operand::Deferred(6));
        break;
    default:
        assert(false);
        break;
    }
}

// Address of a variable designator into the register
bool Generator::GenerateAddress(const ExpressionModel& expr, int index, int reg)
{
    Operand operand;
    if (!GenerateDesignator(expr, index, reg, operand))
        return false;
    LoadAddress(operand, reg);
    return true;
}

// Copy a block from (R3) to (R4); words when the type is word-aligned, bytes otherwise.
// Uses R1, R3, R4.
void Generator::EmitBlockCopy(int size, bool wordaligned)
{
    int count = wordaligned ? (size + 1) / 2 : size;
    const char* move = wordaligned ? "MOV" : "MOVB";
    if (count <= 4)
    {
        for (int i = 0; i < count; i++)
            Emit(move, Operand::AutoInc(3), Operand::AutoInc(4));
        return;
    }
    Emit("MOV", Operand::Imm(count), Operand::Reg(1), wordaligned ? "words" : "bytes");
    string loop = NewLabel();
    EmitLabel(loop);
    Emit(move, Operand::AutoInc(3), Operand::AutoInc(4));
    EmitBranch("SOB", loop);
    m_code->lines.back().text = "R1, " + loop;
}


//////////////////////////////////////////////////////////////////////
