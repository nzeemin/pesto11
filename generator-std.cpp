
#include <cassert>

#include "main.h"

// Generator: standard procedures and functions, see docs/DESIGN.md section 7.7 and docs/RUNTIME.md.


//////////////////////////////////////////////////////////////////////
// Write and WriteLn

void Generator::GenerateWrite(const ExpressionModel& expr, int index, bool isln)
{
    const ExpressionNode& node = expr.nodes[index];
    const std::vector<ArgModel>& args = node.args;
    if (!args.empty())
    {
        int firsttype = expr.nodes[args[0].node].type;
        if (Validator::IsFileType(firsttype))
        {
            EmitTodo("Write to a file");
            return;
        }
    }
    for (const ArgModel& arg : args)
    {
        if (!GenerateWriteItem(expr, arg))
            EmitTodo("Write item " + expr.NodeToString(arg.node));
    }
    if (isln)
        EmitCall("WREOL");
}

// One item of Write: value in R0, width in R1 when given; see docs/RUNTIME.md section 8
bool Generator::GenerateWriteItem(const ExpressionModel& expr, const ArgModel& arg)
{
    const ExpressionNode& node = expr.nodes[arg.node];
    int type = node.type;
    bool haswidth = (arg.width >= 0);
    if (node.convert != ConvertNone)
        return false;  // Array of Char as a string: not supported yet

    if (Validator::IsStringType(type))
    {
        if (node.isconst)
        {
            string label = GetStringLabel(node.constvalue.svalue);
            if (!haswidth)
            {
                EmitCall("WRSTRI", "Write " + expr.NodeToString(arg.node));
                EmitData(".WORD", label, 1);
                return true;
            }
            Emit("MOV", Operand::ImmLabel(label), Operand::Reg(0));
        }
        else
        {
            Operand address;
            if (!GetVariableAddress(expr, arg.node, address))
                return false;
            Emit("MOV", address, Operand::Reg(0), node.token.text);
        }
        if (haswidth)
        {
            if (!LoadOrdinal(expr, arg.width, 1))
                return false;
            EmitCall("WRSTRW");
        }
        else
            EmitCall("WRSTR");
        return true;
    }

    if (Validator::IsCharType(type))
    {
        if (!LoadOrdinal(expr, arg.node, 0))
            return false;
        if (haswidth)
        {
            if (!LoadOrdinal(expr, arg.width, 1))
                return false;
            EmitCall("WRCHW");
        }
        else
            EmitCall("WRCH");
        return true;
    }

    if (Validator::IsBooleanType(type))
    {
        if (!LoadOrdinal(expr, arg.node, 0))
            return false;
        if (haswidth)
        {
            if (!LoadOrdinal(expr, arg.width, 1))
                return false;
        }
        else
            Emit("MOV", Operand::Imm(-1), Operand::Reg(1), "no width");
        EmitCall("WRBOOL");
        return true;
    }

    if (Validator::IsIntegerType(type))
    {
        if (!LoadOrdinal(expr, arg.node, 0))
            return false;
        if (haswidth)
        {
            if (!LoadOrdinal(expr, arg.width, 1))
                return false;
            EmitCall("WRINTW");
        }
        else
            EmitCall("WRINT");
        return true;
    }

    return false;  // Real and the rest: later steps
}


//////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////
// Ordinal functions computed in place, and type casts

bool Generator::GenerateStdFunction(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.symbol < 0)
        return false;
    const SymbolModel& symbol = g_program.symbols[node.symbol];

    // Type cast like Char(65): the ordinal value stays the same
    if (symbol.kind == SymType && node.args.size() == 1)
        return GenerateValue(expr, node.args[0].node);

    if (symbol.kind != SymStdFunc || node.args.size() != 1)
        return false;  // User functions: step 3
    string name = Validator::m_stdroutines[symbol.stdindex].name;
    int arg = node.args[0].node;
    if (!Validator::IsOrdinal(expr.nodes[arg].type))
        return false;  // Real argument (Trunc, Round...): step 7
    if (!GenerateValue(expr, arg))
        return false;

    if (name == "Ord" || name == "Chr")
        return true;
    if (name == "Succ")
        Emit("INC", Operand::Reg(0));
    else if (name == "Pred")
        Emit("DEC", Operand::Reg(0));
    else if (name == "Odd")
        Emit("BIC", Operand::Imm(-2), Operand::Reg(0), "keep bit 0");
    else if (name == "Abs")
    {
        string positive = NewLabel();
        Emit("TST", Operand::Reg(0));
        EmitBranch("BPL", positive);
        Emit("NEG", Operand::Reg(0));
        EmitLabel(positive);
    }
    else if (name == "Sqr")
    {
        Emit("MOV", Operand::Reg(0), Operand::Reg(1));
        Emit("MUL", Operand::Reg(0), Operand::Reg(1));
        Emit("MOV", Operand::Reg(1), Operand::Reg(0));
    }
    else if (name == "Hi")
    {
        Emit("SWAB", Operand::Reg(0));
        Emit("BIC", Operand::Imm(-256), Operand::Reg(0));
    }
    else if (name == "Lo")
        Emit("BIC", Operand::Imm(-256), Operand::Reg(0));
    else if (name == "Swap")
        Emit("SWAB", Operand::Reg(0));
    else
        return false;
    return true;
}
