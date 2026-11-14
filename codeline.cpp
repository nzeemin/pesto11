
#include <cmath>

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

// label, label+n or label-n
string LabelWithOffset(const string& label, int64_t value)
{
    if (value == 0)
        return label;
    return label + (value > 0 ? "+" : "-") + FormatNumber(value > 0 ? value : -value);
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
            string offset = label.empty() ? FormatNumber(value) : LabelWithOffset(label, value);
            return string(mode == OpIndexDeferred ? "@" : "") + offset + "(" + RegisterName(reg) + ")";
        }
    case OpImmediate:
        return "#" + (label.empty() ? FormatNumber(value) : LabelWithOffset(label, value));
    case OpLabel:
        return LabelWithOffset(label, value);
    case OpAbsolute:
        return "@#" + FormatNumber(value & 0xFFFF);
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

// Real value as DEC F_floating, rounded to nearest: sign, 8-bit exponent excess 128, 23-bit fraction
// with a hidden bit; the high word comes first in memory. False if out of the DEC F range.
bool RealToDecF(double value, uint16_t& high, uint16_t& low)
{
    high = low = 0;
    if (value == 0.0)
        return true;
    bool negative = value < 0.0;
    int exponent;
    double fraction = std::frexp(negative ? -value : value, &exponent);  // [0.5, 1)
    uint32_t mantissa = (uint32_t)std::floor(std::ldexp(fraction, 24) + 0.5);
    if (mantissa >= (1u << 24))
    {
        mantissa >>= 1;
        exponent++;
    }
    int biased = exponent + 128;
    if (biased <= 0 || biased > 255)
        return false;
    mantissa &= 0x7FFFFF;  // Hidden bit
    high = (uint16_t)((negative ? 0x8000 : 0) | (biased << 7) | (mantissa >> 16));
    low = (uint16_t)(mantissa & 0xFFFF);
    return true;
}


//////////////////////////////////////////////////////////////////////
