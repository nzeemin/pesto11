
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
