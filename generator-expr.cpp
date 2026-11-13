
#include <cassert>

#include "main.h"

// Generator: expressions and variable addressing, see docs/DESIGN.md sections 7.2 and 7.5.


//////////////////////////////////////////////////////////////////////

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

// Load an ordinal value (Integer, Char, Boolean, enumeration) into a register.
// Returns false if the expression is not supported yet.
bool Generator::LoadOrdinal(const ExpressionModel& expr, int index, int reg)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.isconst)
    {
        int64_t value = node.constvalue.ivalue;
        if (Validator::IsOrdinal(node.type) && Validator::TypeSize(node.type) == 1)
            value &= 0xFF;
        if (value == 0)
            Emit("CLR", Operand::Reg(reg));
        else
            Emit("MOV", Operand::Imm((int16_t)(value & 0xFFFF)), Operand::Reg(reg));
        return true;
    }

    Operand operand;
    if (!GetVariableOperand(expr, index, operand))
        return false;
    string comment = node.token.text;
    if (Validator::TypeSize(node.type) == 1)
    {
        // Byte values are unsigned: clear the register, then BISB
        Emit("CLR", Operand::Reg(reg));
        Emit("BISB", operand, Operand::Reg(reg), comment);
    }
    else
        Emit("MOV", operand, Operand::Reg(reg), comment);
    return true;
}


//////////////////////////////////////////////////////////////////////
