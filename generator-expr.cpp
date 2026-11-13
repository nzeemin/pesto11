
#include <cassert>

#include "main.h"

// Generator: expressions and conditions, see docs/DESIGN.md sections 7.2 and 7.5.
// An ordinal value (Integer, Char, Boolean, enumeration, Byte) is computed into R0 as a word;
// byte values are zero-extended. R1 and R2 are scratch: R1 for MUL/DIV, R2 for the right operand.


//////////////////////////////////////////////////////////////////////
// Variables

// Operand for a variable that needs no code to address: a global, a local, a parameter.
// Returns false if the variable needs address calculation (not supported yet).
bool Generator::GetVariableOperand(const ExpressionModel& expr, int index, Operand& operand)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.kind != NodeIdent || node.symbol < 0 || node.fieldoffset >= 0)
        return false;
    const SymbolModel& symbol = g_program.symbols[node.symbol];
    if (symbol.kind != SymVar && symbol.kind != SymTypedConst && symbol.kind != SymParam)
        return false;
    if (symbol.absolutesym >= 0 || symbol.absoluteaddr >= 0 || symbol.indirect)
        return false;
    if (!symbol.location.asmlabel.empty())
    {
        operand = Operand::Label(symbol.location.asmlabel);
        return true;
    }
    if (symbol.level != g_program.procedures[m_procindex].level)
        return false;  // Variable of an enclosing procedure: static chain, not supported yet
    operand = Operand::Index(5, symbol.location.offset);
    return true;
}

// Address of a variable as an operand: #label for a global; false otherwise for now
bool Generator::GetVariableAddress(const ExpressionModel& expr, int index, Operand& operand)
{
    Operand variable;
    if (!GetVariableOperand(expr, index, variable) || variable.mode != OpLabel)
        return false;
    operand = Operand::ImmLabel(variable.label);
    return true;
}

static bool IsByteSized(int type)
{
    return Validator::TypeSize(type) == 1;
}

static int64_t ConstOrdinal(const ExpressionNode& node)
{
    int64_t value = node.constvalue.ivalue;
    if (IsByteSized(node.type))
        value &= 0xFF;
    return (int16_t)(value & 0xFFFF);
}

// Constant or a variable addressed without code; isbyte for a one-byte variable
bool Generator::GetSimpleOperand(const ExpressionModel& expr, int index, Operand& operand, bool& isbyte)
{
    const ExpressionNode& node = expr.nodes[index];
    isbyte = false;
    if (node.isconst && Validator::IsOrdinal(node.type))
    {
        operand = Operand::Imm(ConstOrdinal(node));
        return true;
    }
    if (!Validator::IsOrdinal(node.type) || !GetVariableOperand(expr, index, operand))
        return false;
    isbyte = IsByteSized(node.type);
    return true;
}

// Load an ordinal value into a register; R0 is computed by GenerateValue
bool Generator::LoadOrdinal(const ExpressionModel& expr, int index, int reg)
{
    Operand operand;
    bool isbyte;
    if (GetSimpleOperand(expr, index, operand, isbyte))
    {
        const ExpressionNode& node = expr.nodes[index];
        string comment = node.isconst ? "" : node.token.text;
        if (operand.mode == OpImmediate && operand.value == 0)
            Emit("CLR", Operand::Reg(reg));
        else if (isbyte)
        {
            Emit("CLR", Operand::Reg(reg));
            Emit("BISB", operand, Operand::Reg(reg), comment);  // Byte values are unsigned
        }
        else
            Emit("MOV", operand, Operand::Reg(reg), comment);
        return true;
    }
    if (reg == 0)
        return GenerateValue(expr, index);
    if (!GenerateValue(expr, index))
        return false;
    Emit("MOV", Operand::Reg(0), Operand::Reg(reg));
    return true;
}

// Right operand of a word operation while the left one is in R0:
// a constant or a word variable directly, otherwise the value in R2
bool Generator::GetRightOperand(const ExpressionModel& expr, int index, Operand& operand)
{
    bool isbyte;
    if (GetSimpleOperand(expr, index, operand, isbyte) && !isbyte)
        return true;
    if (GetSimpleOperand(expr, index, operand, isbyte))  // Byte variable: zero-extend into R2
    {
        Emit("CLR", Operand::Reg(2));
        Emit("BISB", operand, Operand::Reg(2), expr.nodes[index].token.text);
        operand = Operand::Reg(2);
        return true;
    }
    Emit("MOV", Operand::Reg(0), Operand::AutoDec(6));
    if (!GenerateValue(expr, index))
        return false;
    Emit("MOV", Operand::Reg(0), Operand::Reg(2));
    Emit("MOV", Operand::AutoInc(6), Operand::Reg(0));
    operand = Operand::Reg(2);
    return true;
}

bool Generator::HasSideEffects(const ExpressionModel& expr, int index) const
{
    if (index < 0)
        return false;
    const ExpressionNode& node = expr.nodes[index];
    if ((node.kind == NodeCall || node.kind == NodeIdent) && node.symbol >= 0)
    {
        SymbolKind kind = g_program.symbols[node.symbol].kind;
        if (kind == SymFunction || kind == SymFuncResult)
            return true;
    }
    if (HasSideEffects(expr, node.left) || HasSideEffects(expr, node.right))
        return true;
    for (const ArgModel& arg : node.args)
    {
        if (HasSideEffects(expr, arg.node))
            return true;
    }
    for (const SetItemModel& item : node.setitems)
    {
        if (HasSideEffects(expr, item.from) || HasSideEffects(expr, item.to))
            return true;
    }
    return false;
}


//////////////////////////////////////////////////////////////////////
// Values

// Ordinal value into R0. Returns false if the expression is not supported yet.
bool Generator::GenerateValue(const ExpressionModel& expr, int index)
{
    if (index < 0)
        return false;
    const ExpressionNode& node = expr.nodes[index];
    if (node.convert != ConvertNone || !Validator::IsOrdinal(node.type))
        return false;  // Real, strings, sets: later steps

    Operand operand;
    bool isbyte;
    if (GetSimpleOperand(expr, index, operand, isbyte))
        return LoadOrdinal(expr, index, 0);

    switch (node.kind)
    {
    case NodeUnary:
        return GenerateUnaryValue(expr, index);
    case NodeBinary:
        return GenerateBinaryValue(expr, index);
    case NodeCall:
    case NodeIdent:
        return GenerateStdFunction(expr, index);
    default:
        return false;
    }
}

bool Generator::GenerateUnaryValue(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.token.IsKeyword(KeywordNOT) && Validator::IsBooleanType(node.type))
        return GenerateBooleanValue(expr, index);
    if (!GenerateValue(expr, node.left))
        return false;
    if (node.token.IsSymbol(SymbolMinus))
        Emit("NEG", Operand::Reg(0));
    else if (node.token.IsKeyword(KeywordNOT))
        Emit("COM", Operand::Reg(0));
    return true;
}

bool Generator::GenerateBinaryValue(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    const Token& op = node.token;
    int lefttype = expr.nodes[node.left].type;

    // Comparisons, in, Boolean and/or/xor give a Boolean through the condition code
    if (Validator::IsBooleanType(node.type))
        return GenerateBooleanValue(expr, index);
    if (!Validator::IsIntegerType(node.type) || !Validator::IsIntegerType(lefttype))
        return false;

    if (op.IsSymbol(SymbolStar))
    {
        if (!GenerateValue(expr, node.left))
            return false;
        Operand right;
        if (!GetRightOperand(expr, node.right, right))
            return false;
        Emit("MOV", Operand::Reg(0), Operand::Reg(1));
        Emit("MUL", right, Operand::Reg(1), "low word, overflow is not checked");
        Emit("MOV", Operand::Reg(1), Operand::Reg(0));
        return true;
    }
    if (op.IsKeyword(KeywordDIV) || op.IsKeyword(KeywordMOD))
    {
        if (!GenerateValue(expr, node.left))
            return false;
        Operand right;
        if (!GetRightOperand(expr, node.right, right))
            return false;
        Emit("MOV", Operand::Reg(0), Operand::Reg(1));
        Emit("SXT", Operand::Reg(0), Operand(), "R0:R1 = dividend");
        Emit("DIV", right, Operand::Reg(0));
        string ok = NewLabel();
        EmitBranch("BVC", ok);
        EmitCall("DIVZER", "division by zero, error 02");
        EmitLabel(ok);
        if (op.IsKeyword(KeywordMOD))
            Emit("MOV", Operand::Reg(1), Operand::Reg(0), "remainder");
        return true;
    }
    if (op.IsKeyword(KeywordSHL) || op.IsKeyword(KeywordSHR))
    {
        bool isshl = op.IsKeyword(KeywordSHL);
        if (!GenerateValue(expr, node.left))
            return false;
        const ExpressionNode& count = expr.nodes[node.right];
        if (count.isconst)
        {
            int64_t n = count.constvalue.ivalue;
            if (n <= 0)
                return true;
            if (n >= 16)
            {
                Emit("CLR", Operand::Reg(0));
                return true;
            }
            Emit("ASH", Operand::Imm(isshl ? n : -n), Operand::Reg(0));
            if (!isshl)  // Logical shift: clear the copies of the sign bit
                Emit("BIC", Operand::Imm((int16_t)(~(0xFFFF >> n) & 0xFFFF)), Operand::Reg(0));
            return true;
        }
        Emit("MOV", Operand::Reg(0), Operand::AutoDec(6));
        if (!GenerateValue(expr, node.right))
            return false;
        Emit("MOV", Operand::Reg(0), Operand::Reg(1));
        Emit("MOV", Operand::AutoInc(6), Operand::Reg(0));
        EmitCall(isshl ? "ISHL" : "ISHR");
        return true;
    }

    if (!GenerateValue(expr, node.left))
        return false;
    const ExpressionNode& rightnode = expr.nodes[node.right];
    if (op.IsKeyword(KeywordAND) && rightnode.isconst)
    {
        Emit("BIC", Operand::Imm((int16_t)(~rightnode.constvalue.ivalue & 0xFFFF)), Operand::Reg(0));
        return true;
    }
    Operand right;
    if (!GetRightOperand(expr, node.right, right))
        return false;
    if (op.IsSymbol(SymbolPlus))
        Emit("ADD", right, Operand::Reg(0));
    else if (op.IsSymbol(SymbolMinus))
        Emit("SUB", right, Operand::Reg(0));
    else if (op.IsKeyword(KeywordOR))
        Emit("BIS", right, Operand::Reg(0));
    else if (op.IsKeyword(KeywordAND) || op.IsKeyword(KeywordXOR))
    {
        // PDP-11 has no AND: BIC with the complement; XOR takes a register as the source
        if (right.mode != OpReg)
        {
            Emit("MOV", right, Operand::Reg(2));
            right = Operand::Reg(2);
        }
        if (op.IsKeyword(KeywordAND))
        {
            Emit("COM", right);
            Emit("BIC", right, Operand::Reg(0));
        }
        else
            Emit("XOR", right, Operand::Reg(0));
    }
    else
        return false;
    return true;
}

// Boolean value 0/1 into R0
bool Generator::GenerateBooleanValue(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    bool binarylogic = node.kind == NodeBinary &&
        (node.token.IsKeyword(KeywordAND) || node.token.IsKeyword(KeywordOR) || node.token.IsKeyword(KeywordXOR));

    // and/or/xor with side effects: both operands are evaluated, then combined
    if (binarylogic && (node.token.IsKeyword(KeywordXOR) || HasSideEffects(expr, index)))
    {
        if (!GenerateValue(expr, node.left))
            return false;
        Operand right;
        if (!GetRightOperand(expr, node.right, right))
            return false;
        if (node.token.IsKeyword(KeywordOR))
        {
            Emit("BIS", right, Operand::Reg(0));
            return true;
        }
        if (right.mode != OpReg)
        {
            Emit("MOV", right, Operand::Reg(2));
            right = Operand::Reg(2);
        }
        if (node.token.IsKeyword(KeywordAND))
        {
            Emit("COM", right);
            Emit("BIC", right, Operand::Reg(0));
        }
        else
            Emit("XOR", right, Operand::Reg(0));
        return true;
    }

    if (node.kind == NodeUnary && node.token.IsKeyword(KeywordNOT) && !HasSideEffects(expr, node.left))
    {
        // not on 0/1: DEC then NEG gives 1/0
        if (!GenerateValue(expr, node.left))
            return false;
        Emit("DEC", Operand::Reg(0));
        Emit("NEG", Operand::Reg(0));
        return true;
    }

    // Comparison, in, and/or: through the condition jumps
    string falselabel = NewLabel();
    string endlabel = NewLabel();
    if (!GenerateCondition(expr, index, falselabel, false))
        return false;
    Emit("MOV", Operand::Imm(1), Operand::Reg(0));
    EmitBranch("BR", endlabel);
    EmitLabel(falselabel);
    Emit("CLR", Operand::Reg(0));
    EmitLabel(endlabel);
    return true;
}


//////////////////////////////////////////////////////////////////////
// Conditions

// Jump to the label if the Boolean expression is true (jumpiftrue) or false; otherwise fall through
bool Generator::GenerateCondition(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.isconst)
    {
        if ((node.constvalue.ivalue != 0) == jumpiftrue)
            EmitBranch("BR", label);
        return true;
    }

    if (node.kind == NodeUnary && node.token.IsKeyword(KeywordNOT))
        return GenerateCondition(expr, node.left, label, !jumpiftrue);

    if (node.kind == NodeBinary)
    {
        const Token& op = node.token;
        bool isand = op.IsKeyword(KeywordAND), isor = op.IsKeyword(KeywordOR);
        if ((isand || isor) && !HasSideEffects(expr, index))
        {
            // Without side effects the order and completeness of evaluation do not matter
            if (isand != jumpiftrue)
            {
                // and, jump if false / or, jump if true: either operand decides alone
                return GenerateCondition(expr, node.left, label, jumpiftrue) &&
                       GenerateCondition(expr, node.right, label, jumpiftrue);
            }
            string skip = NewLabel();
            if (!GenerateCondition(expr, node.left, skip, !jumpiftrue))
                return false;
            if (!GenerateCondition(expr, node.right, label, jumpiftrue))
                return false;
            EmitLabel(skip);
            return true;
        }
        if (op.IsKeyword(KeywordIN))
            return GenerateInConst(expr, index, label, jumpiftrue);
        if (op.IsSymbol(SymbolEqual) || op.IsSymbol(SymbolNotEqual) || op.IsSymbol(SymbolLess) ||
            op.IsSymbol(SymbolGreater) || op.IsSymbol(SymbolLessEqual) || op.IsSymbol(SymbolGreaterEqual))
            return GenerateCompare(expr, index, label, jumpiftrue);
    }

    // Boolean variable: test the byte in place
    Operand operand;
    bool isbyte;
    if (GetSimpleOperand(expr, index, operand, isbyte) && !node.isconst)
        Emit(isbyte ? "TSTB" : "TST", operand, Operand(), node.token.text);
    else
    {
        if (!GenerateValue(expr, index))
            return false;
        Emit("TST", Operand::Reg(0));
    }
    EmitBranch(jumpiftrue ? "BNE" : "BEQ", label);
    return true;
}

// Branch opcodes for a comparison: [relation][signed/unsigned][jump if true/false]
static const char* GetCompareBranch(const Token& op, bool isunsigned, bool jumpiftrue)
{
    struct { SymbolIndex symbol; const char* signedtrue; const char* signedfalse; const char* unsignedtrue; const char* unsignedfalse; }
    table[] =
    {
        { SymbolEqual,        "BEQ", "BNE", "BEQ",  "BNE"  },
        { SymbolNotEqual,     "BNE", "BEQ", "BNE",  "BEQ"  },
        { SymbolLess,         "BLT", "BGE", "BLO",  "BHIS" },
        { SymbolGreater,      "BGT", "BLE", "BHI",  "BLOS" },
        { SymbolLessEqual,    "BLE", "BGT", "BLOS", "BHI"  },
        { SymbolGreaterEqual, "BGE", "BLT", "BHIS", "BLO"  },
    };
    for (auto& item : table)
    {
        if (op.IsSymbol(item.symbol))
        {
            if (isunsigned)
                return jumpiftrue ? item.unsignedtrue : item.unsignedfalse;
            return jumpiftrue ? item.signedtrue : item.signedfalse;
        }
    }
    return "BR";
}

// Comparison of ordinal values; CMP left, right sets the flags for left - right
bool Generator::GenerateCompare(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue)
{
    const ExpressionNode& node = expr.nodes[index];
    const ExpressionNode& left = expr.nodes[node.left];
    const ExpressionNode& right = expr.nodes[node.right];
    if (!Validator::IsOrdinal(left.type) || !Validator::IsOrdinal(right.type) ||
        left.convert != ConvertNone || right.convert != ConvertNone)
        return false;  // Real, strings, sets: later steps

    Operand leftop, rightop;
    bool leftbyte, rightbyte;
    bool leftsimple = GetSimpleOperand(expr, node.left, leftop, leftbyte) && !left.isconst;
    bool rightsimple = GetSimpleOperand(expr, node.right, rightop, rightbyte);
    if (leftsimple && rightsimple)
    {
        bool rightconstbyte = right.isconst && rightop.value >= 0 && rightop.value <= 255;
        if (leftbyte && (rightbyte || rightconstbyte))
        {
            // Both are bytes: CMPB, unsigned branches
            Emit("CMPB", leftop, rightop);
            EmitBranch(GetCompareBranch(node.token, true, jumpiftrue), label);
            return true;
        }
        if (!leftbyte && !rightbyte)
        {
            if (right.isconst && rightop.value == 0)
                Emit("TST", leftop);
            else
                Emit("CMP", leftop, rightop);
            EmitBranch(GetCompareBranch(node.token, false, jumpiftrue), label);
            return true;
        }
    }

    // General case: values as words, signed comparison (byte values are zero-extended)
    if (!GenerateValue(expr, node.left))
        return false;
    Operand right2;
    if (!GetRightOperand(expr, node.right, right2))
        return false;
    if (right2.mode == OpImmediate && right2.value == 0)
        Emit("TST", Operand::Reg(0));
    else
        Emit("CMP", Operand::Reg(0), right2);
    EmitBranch(GetCompareBranch(node.token, false, jumpiftrue), label);
    return true;
}

// x in [constant set]: comparisons with the elements and ranges of the set
bool Generator::GenerateInConst(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue)
{
    const ExpressionNode& node = expr.nodes[index];
    const ExpressionNode& setnode = expr.nodes[node.right];
    if (!setnode.isconst || setnode.constvalue.svalue.size() != 32)
        return false;  // Set variables: step 6

    // Ranges of consecutive elements of the constant set
    const string& bits = setnode.constvalue.svalue;
    std::vector<std::pair<int, int>> ranges;
    for (int i = 0; i < 256; i++)
    {
        if (!(((unsigned char)bits[i / 8] >> (i % 8)) & 1))
            continue;
        int j = i;
        while (j + 1 < 256 && (((unsigned char)bits[(j + 1) / 8] >> ((j + 1) % 8)) & 1))
            j++;
        ranges.push_back(std::make_pair(i, j));
        i = j;
    }
    if (ranges.size() > 6)
        return false;  // Bit table test: step 6

    if (!GenerateValue(expr, node.left))
        return false;
    string intrue = jumpiftrue ? label : NewLabel();
    for (const auto& range : ranges)
    {
        if (range.first == range.second)
        {
            Emit("CMP", Operand::Reg(0), Operand::Imm(range.first));
            EmitBranch("BEQ", intrue);
            continue;
        }
        string next = NewLabel();
        Emit("CMP", Operand::Reg(0), Operand::Imm(range.first));
        EmitBranch("BLT", next);
        Emit("CMP", Operand::Reg(0), Operand::Imm(range.second));
        EmitBranch("BLE", intrue);
        EmitLabel(next);
    }
    if (!jumpiftrue)
    {
        EmitBranch("BR", label);  // Not in the set
        EmitLabel(intrue);
    }
    return true;
}


//////////////////////////////////////////////////////////////////////
// Store

// R0 into an ordinal variable; with {$R+} a subrange or enumeration value is checked first
bool Generator::StoreOrdinal(const ExpressionModel& target, int index, const SourcePos& pos)
{
    (void)pos;
    const ExpressionNode& node = target.nodes[index];
    Operand operand;
    if (!GetVariableOperand(target, index, operand))
        return false;
    int type = node.type;
    if ((node.token.directives & DirectiveR) != 0)
    {
        const TypeModel& model = Validator::GetType(type);
        bool checked = model.kind == TypeKindSubrange || model.kind == TypeKindEnum || model.kind == TypeKindByte;
        if (checked)
        {
            EmitCall("RNGSCL", "range check, error 91");
            EmitData(".WORD", FormatNumber(model.low) + ", " + FormatNumber(model.high), 2);
        }
    }
    Emit(IsByteSized(type) ? "MOVB" : "MOV", Operand::Reg(0), operand, node.token.text);
    return true;
}


//////////////////////////////////////////////////////////////////////
