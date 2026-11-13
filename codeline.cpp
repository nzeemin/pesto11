
#include "main.h"

// Generator output model: operands and assembly lines.


//////////////////////////////////////////////////////////////////////

// macro11 default radix is octal: decimal numbers get a trailing dot
string FormatNumber(int64_t value)
{
    return std::to_string(value) + ".";
}

static string RegisterName(int reg)
{
    switch (reg)
    {
    case 6: return "SP";
    case 7: return "PC";
    default: return "R" + std::to_string(reg);
    }
}

int Operand::ExtraWords() const
{
    switch (mode)
    {
    case OpIndex:
    case OpIndexDeferred:
    case OpImmediate:
    case OpLabel:
    case OpAbsolute:
        return 1;
    default:
        return 0;
    }
}

string Operand::ToString() const
{
    switch (mode)
    {
    case OpReg:
        return RegisterName(reg);
    case OpRegDeferred:
        return "(" + RegisterName(reg) + ")";
    case OpAutoInc:
        return "(" + RegisterName(reg) + ")+";
    case OpAutoDec:
        return "-(" + RegisterName(reg) + ")";
    case OpIndex:
    case OpIndexDeferred:
        {
            string offset = label.empty() ? FormatNumber(value) :
                (value == 0 ? label : label + "+" + FormatNumber(value));
            return string(mode == OpIndexDeferred ? "@" : "") + offset + "(" + RegisterName(reg) + ")";
        }
    case OpImmediate:
        return "#" + (label.empty() ? FormatNumber(value) : label);
    case OpLabel:
        return (value == 0) ? label : label + "+" + FormatNumber(value);
    case OpAbsolute:
        return "@#" + FormatNumber(value);
    default:
        return "";
    }
}

string CodeLine::ToString() const
{
    if (kind == LineRaw)
        return text;
    if (kind == LineComment)
        return "; " + comment;

    string result;
    if (!label.empty())
        result = label + ":";
    if (kind == LineLabel)
        return result;

    result += "\t" + opcode;
    string operands = text;
    if (operands.empty() && !src.IsNone())
    {
        operands = src.ToString();
        if (!dst.IsNone())
            operands += ", " + dst.ToString();
    }
    if (!operands.empty())
        result += "\t" + operands;
    if (!comment.empty())
        result += "\t; " + comment;
    return result;
}


//////////////////////////////////////////////////////////////////////
