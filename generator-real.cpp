
#include <cassert>

#include "main.h"

// Generator: Real, see docs/DESIGN.md section 7.2 and docs/RUNTIME.md section 5.
// A Real value is 4 bytes of DEC F_floating on the stack, the high word on top (as in memory:
// the high word at the lower address). Arithmetic is done by FIS: FADD SP, FSUB SP, FMUL SP, FDIV SP.


//////////////////////////////////////////////////////////////////////

// Real constant onto the stack: the low word first, then the high word
void Generator::PushRealConst(double value)
{
    uint16_t high, low;
    if (!RealToDecF(value, high, low))
        high = low = 0;  // The validator reports the constants out of range
    if (low == 0)
        Emit("CLR", Operand::AutoDec(6));
    else
        Emit("MOV", Operand::Imm((int16_t)low), Operand::AutoDec(6));
    if (high == 0)
        Emit("CLR", Operand::AutoDec(6));
    else
        Emit("MOV", Operand::Imm((int16_t)high), Operand::AutoDec(6));
}

void Generator::EmitFis(const char* opcode)
{
    Emit(opcode, Operand::Reg(6));
    m_usesreal = true;
}

// Real value onto the stack
bool Generator::GenerateReal(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    m_usesreal = true;
    if (node.convert == ConvertIntToReal)
    {
        if (node.isconst)
        {
            PushRealConst((double)node.constvalue.ivalue);
            return true;
        }
        if (!GenerateValue(expr, index, true))
            return false;
        EmitCall("ITOF", "Integer -> Real");
        return true;
    }
    if (!Validator::IsRealType(node.type) || node.convert != ConvertNone)
        return false;
    if (node.isconst)
    {
        PushRealConst(node.constvalue.dvalue);
        return true;
    }

    if ((node.kind == NodeIdent || node.kind == NodeCall) && node.symbol >= 0)
    {
        const SymbolModel& symbol = g_program.symbols[node.symbol];
        if (symbol.kind == SymFunction)
            return GenerateUserCall(expr, index);
        if (symbol.kind == SymStdFunc)
            return GenerateRealFunction(Validator::m_stdroutines[symbol.stdindex].name, expr, index);
        if (symbol.kind == SymType && node.args.size() == 1)
            return GenerateReal(expr, node.args[0].node);  // Real(X)
    }
    if (IsDesignator(expr, index))
    {
        Operand operand;
        if (!GenerateDesignator(expr, index, 3, operand))
            return false;
        if (!PrepareRealOperand(operand, 3))
            return false;
        Operand lowword = operand;
        AddOffset(lowword, 2, 3);
        Emit("MOV", lowword, Operand::AutoDec(6), node.token.text);
        Emit("MOV", operand, Operand::AutoDec(6));
        return true;
    }
    if (node.kind == NodeUnary)
    {
        if (!GenerateReal(expr, node.left))
            return false;
        if (node.token.IsSymbol(SymbolMinus))
        {
            // Change the sign, but keep a clean zero
            string skip = NewLabel();
            Emit("TST", Operand::Deferred(6));
            EmitBranch("BEQ", skip);
            Emit("ADD", Operand::Imm(-32768), Operand::Deferred(6), "change the sign");
            EmitLabel(skip);
        }
        return true;
    }
    if (node.kind == NodeBinary)
    {
        const Token& op = node.token;
        const char* opcode = op.IsSymbol(SymbolPlus) ? "FADD" : op.IsSymbol(SymbolMinus) ? "FSUB" :
            op.IsSymbol(SymbolStar) ? "FMUL" : op.IsSymbol(SymbolSlash) ? "FDIV" : nullptr;
        if (opcode == nullptr)
            return false;
        if (!GenerateReal(expr, node.left) || !GenerateReal(expr, node.right))
            return false;
        EmitFis(opcode);
        return true;
    }
    return false;
}

// An address held in memory (var parameter) is loaded into the register, so both words can be reached
bool Generator::PrepareRealOperand(Operand& operand, int reg)
{
    switch (operand.mode)
    {
    case OpIndexDeferred:
        Emit("MOV", Operand::Index(operand.reg, operand.value), Operand::Reg(reg));
        operand = Operand::Deferred(reg);
        return true;
    case OpLabel:
    case OpIndex:
    case OpRegDeferred:
    case OpAbsolute:
        return true;
    default:
        return false;
    }
}

// The Real on the stack into the variable
bool Generator::StoreReal(const ExpressionModel& target, int index)
{
    Operand operand;
    if (!GenerateDesignator(target, index, 3, operand))
        return false;
    if (!PrepareRealOperand(operand, 3))
        return false;
    Operand lowword = operand;
    AddOffset(lowword, 2, 3);
    Emit("MOV", Operand::AutoInc(6), operand, target.nodes[index].token.text);
    Emit("MOV", Operand::AutoInc(6), lowword);
    return true;
}

// Comparison of Real values: FCMP takes both, gives R0 = -1/0/1 and the flags of TST R0
bool Generator::GenerateRealCompare(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue)
{
    const ExpressionNode& node = expr.nodes[index];
    if (!GenerateReal(expr, node.left) || !GenerateReal(expr, node.right))
        return false;
    EmitCall("FCMP");
    EmitBranch(GetCompareBranch(node.token, false, jumpiftrue), label);
    return true;
}

// Standard functions with a Real result; the argument is replaced by the result on the stack
bool Generator::GenerateRealFunction(const string& name, const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    if (name == "Random" && node.args.empty())
    {
        PushRealConst(1.0);  // FRND: a positive parameter gives the next number
        EmitCall("FRND", "Random");
        return true;
    }
    if (node.args.size() != 1)
        return false;
    int arg = node.args[0].node;
    if (!GenerateReal(expr, arg))
        return false;
    struct { const char* name; const char* routine; } calls[] =
    {
        { "Sqrt", "FSQR" }, { "Sin", "FSIN" }, { "Cos", "FCOS" }, { "ArcTan", "FATN" },
        { "Exp", "FEXP" }, { "Ln", "FLOG" }, { "Int", "FINT" },
    };
    for (auto& item : calls)
    {
        if (name == item.name)
        {
            EmitCall(item.routine, name);
            return true;
        }
    }
    if (name == "Abs")
    {
        Emit("BIC", Operand::Imm(-32768), Operand::Deferred(6), "Abs: clear the sign");
        return true;
    }
    if (name == "Sqr")
    {
        Emit("MOV", Operand::Index(6, 2), Operand::AutoDec(6), "Sqr: a copy");
        Emit("MOV", Operand::Index(6, 2), Operand::AutoDec(6));
        EmitFis("FMUL");
        return true;
    }
    if (name == "Frac")
    {
        // X - Int(X)
        Emit("MOV", Operand::Index(6, 2), Operand::AutoDec(6), "Frac: a copy");
        Emit("MOV", Operand::Index(6, 2), Operand::AutoDec(6));
        EmitCall("FINT");
        EmitFis("FSUB");
        return true;
    }
    return false;
}

// Real results that become Integer: Trunc, Round
bool Generator::GenerateRealToInteger(const string& name, const ExpressionModel& expr, int arg)
{
    if (!GenerateReal(expr, arg))
        return false;
    EmitCall(name == "Round" ? "FROUND" : "FTOI", name);
    return true;
}


//////////////////////////////////////////////////////////////////////
