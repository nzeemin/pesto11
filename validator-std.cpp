
#include <cassert>
#include <cmath>
#include <cstring>

#include "main.h"

// Validator: standard procedures and functions, see docs/TP3-dialect.md section 7.
//
// Parameter pattern, one character per parameter; '[' marks the rest as optional:
//   i  Integer expression          r  Real expression (Integer converted)
//   n  Integer or Real expression  o  ordinal expression
//   c  Char expression             s  string expression (Char, array of Char converted)
//   S  string variable             L  Integer variable
//   V  any variable                P  pointer variable
//   F  file variable               T  text file variable
// Result type: N none (procedure), I Integer, R Real, C Char, S String, B Boolean, P pointer


//////////////////////////////////////////////////////////////////////

#define STDPROC(name, params) { name, SymStdProc, params, 'N', nullptr }
#define STDFUNC(name, params, result) { name, SymStdFunc, params, result, nullptr }
#define STDSPECIAL(name, kind, method) { name, kind, "", 'N', &Validator::method }
#define STDUNSUPPORTED(name) { name, SymNone, "", 'N', nullptr }

const StdRoutineSpec Validator::m_stdroutines[] =
{
    // Input and output
    STDSPECIAL("Write", SymStdProc, CheckStdWrite),
    STDSPECIAL("WriteLn", SymStdProc, CheckStdWrite),
    STDSPECIAL("Read", SymStdProc, CheckStdRead),
    STDSPECIAL("ReadLn", SymStdProc, CheckStdRead),
    // Arithmetic
    STDSPECIAL("Abs", SymStdFunc, CheckStdAbsSqr),
    STDSPECIAL("Sqr", SymStdFunc, CheckStdAbsSqr),
    STDFUNC("ArcTan", "r", 'R'),
    STDFUNC("Cos", "r", 'R'),
    STDFUNC("Exp", "r", 'R'),
    STDFUNC("Frac", "r", 'R'),
    STDFUNC("Int", "r", 'R'),
    STDFUNC("Ln", "r", 'R'),
    STDFUNC("Sin", "r", 'R'),
    STDFUNC("Sqrt", "r", 'R'),
    // Scalar and transfer functions
    STDFUNC("Odd", "i", 'B'),
    STDSPECIAL("Pred", SymStdFunc, CheckStdSuccPred),
    STDSPECIAL("Succ", SymStdFunc, CheckStdSuccPred),
    STDSPECIAL("Ord", SymStdFunc, CheckStdOrd),
    STDSPECIAL("Chr", SymStdFunc, CheckStdChr),
    STDFUNC("Round", "r", 'I'),
    STDFUNC("Trunc", "r", 'I'),
    // Strings
    STDPROC("Delete", "Sii"),
    STDPROC("Insert", "sSi"),
    STDSPECIAL("Str", SymStdProc, CheckStdStr),
    STDSPECIAL("Val", SymStdProc, CheckStdVal),
    STDSPECIAL("Concat", SymStdFunc, CheckStdConcat),
    STDFUNC("Copy", "sii", 'S'),
    STDFUNC("Length", "s", 'I'),
    STDFUNC("Pos", "ss", 'I'),
    STDFUNC("UpCase", "c", 'C'),
    // Files
    STDPROC("Assign", "Fs"),
    STDPROC("Reset", "F"),
    STDPROC("Rewrite", "F"),
    STDPROC("Close", "F"),
    STDPROC("Erase", "F"),
    STDPROC("Rename", "Fs"),
    STDPROC("Seek", "Fi"),
    STDPROC("Flush", "F"),
    STDPROC("BlockRead", "FVi[V"),
    STDPROC("BlockWrite", "FVi[V"),
    STDSPECIAL("Eof", SymStdFunc, CheckStdFileFunc),
    STDSPECIAL("Eoln", SymStdFunc, CheckStdFileFunc),
    STDSPECIAL("SeekEof", SymStdFunc, CheckStdFileFunc),
    STDSPECIAL("SeekEoln", SymStdFunc, CheckStdFileFunc),
    STDFUNC("FilePos", "F", 'I'),
    STDFUNC("FileSize", "F", 'I'),
    STDFUNC("IOresult", "", 'I'),
    // Heap
    STDPROC("New", "P"),
    STDPROC("Dispose", "P"),
    STDPROC("Mark", "P"),
    STDPROC("Release", "P"),
    STDPROC("GetMem", "Pi"),
    STDPROC("FreeMem", "Pi"),
    STDFUNC("MemAvail", "", 'I'),
    STDFUNC("MaxAvail", "", 'I'),
    STDFUNC("Ptr", "i", 'P'),
    // Screen and keyboard
    STDPROC("ClrScr", ""),
    STDPROC("ClrEol", ""),
    STDPROC("DelLine", ""),
    STDPROC("InsLine", ""),
    STDPROC("CrtInit", ""),
    STDPROC("CrtExit", ""),
    STDPROC("GotoXY", "ii"),
    STDFUNC("KeyPressed", "", 'B'),
    // Miscellaneous
    STDPROC("Delay", "i"),
    STDPROC("Randomize", ""),
    STDPROC("Halt", ""),
    STDPROC("Exit", ""),
    STDPROC("FillChar", "Vio"),
    STDPROC("Move", "VVi"),
    STDSPECIAL("Random", SymStdFunc, CheckStdRandom),
    STDSPECIAL("SizeOf", SymStdFunc, CheckStdSizeOf),
    STDSPECIAL("Addr", SymStdFunc, CheckStdAddr),
    STDFUNC("Hi", "i", 'I'),
    STDFUNC("Lo", "i", 'I'),
    STDFUNC("Swap", "i", 'I'),
    STDFUNC("ParamCount", "", 'I'),
    STDFUNC("ParamStr", "i", 'S'),
    // Not supported in Pesto/11
    STDUNSUPPORTED("Append"),
    STDUNSUPPORTED("LowVideo"),
    STDUNSUPPORTED("NormVideo"),
    STDUNSUPPORTED("Chain"),
    STDUNSUPPORTED("Execute"),
    STDUNSUPPORTED("Bdos"),
    STDUNSUPPORTED("BdosHL"),
    STDUNSUPPORTED("Bios"),
    STDUNSUPPORTED("BiosHL"),
    { nullptr, SymNone, nullptr, 'N', nullptr }
};


//////////////////////////////////////////////////////////////////////

int Validator::ValidateStdCall(ExpressionModel& expr, int index, int symbol)
{
    const SymbolModel& model = g_program.symbols[symbol];
    const StdRoutineSpec& spec = m_stdroutines[model.stdindex];
    expr.nodes[index].symbol = symbol;
    bool allowformat = (spec.method == &Validator::CheckStdWrite) || (spec.method == &Validator::CheckStdStr);
    if (!allowformat)
        CheckNoFormat(expr, index);
    if (spec.method != nullptr)
        return (this->*spec.method)(expr, index, spec);
    int type = CheckStdPattern(expr, index, spec);
    FoldStdFunction(expr, index, spec);
    return type;
}

// Standard functions of constants become constants: Hi, Lo, Swap, Odd, UpCase, Length
void Validator::FoldStdFunction(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    ExpressionNode& node = expr.nodes[index];
    if (node.args.size() != 1)
        return;
    const ExpressionNode& arg = expr.nodes[node.args[0].node];
    if (!arg.isconst)
        return;
    string name = spec.name;
    int64_t word = arg.constvalue.ivalue & 0xFFFF;
    int64_t value;
    if (name == "Hi")
        value = (word >> 8) & 0xFF;
    else if (name == "Lo")
        value = word & 0xFF;
    else if (name == "Swap")
        value = (int16_t)(((word >> 8) & 0xFF) | ((word & 0xFF) << 8));
    else if (name == "Odd")
        value = word & 1;
    else if (name == "UpCase" && IsCharType(arg.type))
    {
        value = arg.constvalue.ivalue & 0xFF;
        if (value >= 128)
            return;  // KOI8-R letters: the runtime decides
        if (value >= 'a' && value <= 'z')
            value -= 'a' - 'A';
    }
    else if (name == "Length" && IsStringType(arg.type))
        value = (int64_t)arg.constvalue.svalue.size();
    else if (name == "Length" && IsCharType(arg.type))
        value = 1;
    else
        return;
    node.isconst = true;
    node.constvalue.ivalue = value;
}

int Validator::StdResultType(char code, ExpressionModel& expr, int index)
{
    (void)expr;
    (void)index;
    switch (code)
    {
    case 'I': return g_typeInteger;
    case 'R': return g_typeReal;
    case 'C': return g_typeChar;
    case 'S': return g_typeString;
    case 'B': return g_typeBoolean;
    case 'P': return g_typeNilPointer;
    default: return g_typeVoid;
    }
}

int Validator::CheckStdPattern(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    string pattern = spec.params;
    size_t optional = pattern.find('[');
    size_t mincount = (optional == string::npos) ? pattern.size() : optional;
    if (optional != string::npos)
        pattern.erase(optional, 1);
    if (args.size() < mincount || args.size() > pattern.size())
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        for (const ArgModel& arg : args)
            ValidateNode(expr, arg.node);
        return StdResultType(spec.result, expr, index);
    }
    for (size_t i = 0; i < args.size(); i++)
        CheckStdArg(expr, args[i], pattern[i]);
    return StdResultType(spec.result, expr, index);
}

// Check one argument by its pattern code; reports an error and returns false if it does not fit
bool Validator::CheckStdArg(ExpressionModel& expr, const ArgModel& arg, char code)
{
    int node = arg.node;
    bool isvarcode = (code == 'S' || code == 'L' || code == 'V' || code == 'P' || code == 'F' || code == 'T');
    int type = isvarcode ? ValidateNode(expr, node) : ValidateValue(expr, node);
    if (type == g_typeError)
        return false;
    const SourcePos& pos = expr.nodes[node].token.pos;
    if (isvarcode && !IsVariable(expr, node))
    {
        Error(pos, 0, "Variable expected");
        return false;
    }
    switch (code)
    {
    case 'i':
        if (IsIntegerType(type)) return true;
        Error(pos, 23, "Integer expression expected");
        return false;
    case 'r':
        if (IsRealType(type)) return true;
        if (IsIntegerType(type))
        {
            SetConvert(expr, node, ConvertIntToReal);
            return true;
        }
        Error(pos, 26, "Integer or real expression expected");
        return false;
    case 'n':
        if (IsNumeric(type)) return true;
        Error(pos, 26, "Integer or real expression expected");
        return false;
    case 'o':
        if (IsOrdinal(type)) return true;
        Error(pos, 30, "Simple type expected");
        return false;
    case 'c':
        if (IsCharType(type)) return true;
        Error(pos, 44, "Type mismatch: Char expected");
        return false;
    case 's':
        if (IsStringType(type)) return true;
        if (IsCharType(type))
        {
            SetConvert(expr, node, ConvertCharToString);
            return true;
        }
        if (IsCharArray(type))
        {
            SetConvert(expr, node, ConvertArrayToString);
            return true;
        }
        Error(pos, 33, "String expression expected");
        return false;
    case 'S':
        if (IsStringType(type)) return true;
        Error(pos, 34, "String variable expected");
        return false;
    case 'L':
        if (IsIntegerType(type)) return true;
        Error(pos, 24, "Integer variable expected");
        return false;
    case 'V':
        return true;
    case 'P':
        if (IsPointerType(type)) return true;
        Error(pos, 28, "Pointer variable expected");
        return false;
    case 'F':
        if (IsFileType(type)) return true;
        Error(pos, 21, "File variable expected");
        return false;
    case 'T':
        if (IsTextType(type)) return true;
        Error(pos, 35, "Textfile expected");
        return false;
    default:
        return false;
    }
}


//////////////////////////////////////////////////////////////////////
// Special checks

// Write/WriteLn([file,] items); text items: Char, string, Boolean, Integer, Real with :width[:decimals]
int Validator::CheckStdWrite(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    bool isln = (strcmp(spec.name, "WriteLn") == 0);
    std::vector<ArgModel> args = expr.nodes[index].args;
    size_t first = 0;
    int filetype = g_typeText;
    if (!args.empty())
    {
        int type = ValidateNode(expr, args[0].node);
        if (type != g_typeError && IsFileType(type))
        {
            if (args[0].width >= 0)
                Error(expr.nodes[args[0].width].token.pos, 0, "Format is not allowed for a file");
            filetype = type;
            first = 1;
            if (!IsVariable(expr, args[0].node))
                Error(expr.nodes[args[0].node].token.pos, 21, "File variable expected");
        }
    }
    if (!isln && first >= args.size())
    {
        Error(expr.nodes[index].token.pos, 0, "Write needs at least one item");
        return g_typeVoid;
    }

    bool istext = IsTextType(filetype);
    if (!istext && isln)
        Error(expr.nodes[index].token.pos, 35, "Textfile expected for WriteLn");
    for (size_t i = first; i < args.size(); i++)
    {
        const ArgModel& arg = args[i];
        int type = (i == 0) ? expr.nodes[arg.node].type : ValidateValue(expr, arg.node);
        if (type == g_typeVoid)
        {
            Error(expr.nodes[arg.node].token.pos, 0, "Procedure call has no value");
            type = g_typeError;
        }
        if (type == g_typeError)
            continue;
        const SourcePos& pos = expr.nodes[arg.node].token.pos;
        if (!istext)
        {
            // Typed file: the item is a component variable
            if (!IsVariable(expr, arg.node))
                Error(pos, 0, "Variable expected");
            else if (Resolve(type) != Resolve(GetType(filetype).elemtype))
                Error(pos, 44, "Type mismatch: file component type expected");
            if (arg.width >= 0)
                Error(expr.nodes[arg.width].token.pos, 0, "Format is allowed only for a text file");
            continue;
        }
        if (IsCharArray(type))
            SetConvert(expr, arg.node, ConvertArrayToString);
        else if (!IsCharType(type) && !IsStringType(type) && !IsBooleanType(type) && !IsNumeric(type))
        {
            Error(pos, 66, "I/O not allowed here: " + g_program.TypeToString(type));
            continue;
        }
        if (arg.width >= 0)
        {
            int wtype = ValidateValue(expr, arg.width);
            if (wtype != g_typeError && !IsIntegerType(wtype))
                Error(expr.nodes[arg.width].token.pos, 23, "Integer expression expected");
        }
        if (arg.decimals >= 0)
        {
            int dtype = ValidateValue(expr, arg.decimals);
            if (dtype != g_typeError && !IsIntegerType(dtype))
                Error(expr.nodes[arg.decimals].token.pos, 23, "Integer expression expected");
            if (!IsRealType(type))
                Error(expr.nodes[arg.decimals].token.pos, 0, "Decimals are allowed only for a Real value");
        }
    }
    return g_typeVoid;
}

// Read/ReadLn([file,] variables); text items: Char, string, Integer, Real
int Validator::CheckStdRead(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    bool isln = (strcmp(spec.name, "ReadLn") == 0);
    std::vector<ArgModel> args = expr.nodes[index].args;
    size_t first = 0;
    int filetype = g_typeText;
    if (!args.empty())
    {
        int type = ValidateNode(expr, args[0].node);
        if (type != g_typeError && IsFileType(type))
        {
            filetype = type;
            first = 1;
            if (!IsVariable(expr, args[0].node))
                Error(expr.nodes[args[0].node].token.pos, 21, "File variable expected");
        }
    }
    if (!isln && first >= args.size())
    {
        Error(expr.nodes[index].token.pos, 0, "Read needs at least one variable");
        return g_typeVoid;
    }
    bool istext = IsTextType(filetype);
    if (!istext && isln)
        Error(expr.nodes[index].token.pos, 35, "Textfile expected for ReadLn");
    for (size_t i = first; i < args.size(); i++)
    {
        int node = args[i].node;
        int type = (i == 0) ? expr.nodes[node].type : ValidateNode(expr, node);
        if (type == g_typeError)
            continue;
        const SourcePos& pos = expr.nodes[node].token.pos;
        if (!IsVariable(expr, node))
        {
            Error(pos, 0, "Variable expected");
            continue;
        }
        if (istext)
        {
            if (!IsCharType(type) && !IsStringType(type) && !IsNumeric(type))
                Error(pos, 66, "I/O not allowed here: " + g_program.TypeToString(type));
        }
        else if (Resolve(type) != Resolve(GetType(filetype).elemtype))
            Error(pos, 44, "Type mismatch: file component type expected");
    }
    return g_typeVoid;
}

// Str(value[:width[:decimals]], S)
int Validator::CheckStdStr(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() != 2)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeVoid;
    }
    int type = ValidateValue(expr, args[0].node);
    if (type != g_typeError && !IsNumeric(type))
        Error(expr.nodes[args[0].node].token.pos, 26, "Integer or real expression expected");
    if (args[0].width >= 0)
    {
        int wtype = ValidateValue(expr, args[0].width);
        if (wtype != g_typeError && !IsIntegerType(wtype))
            Error(expr.nodes[args[0].width].token.pos, 23, "Integer expression expected");
    }
    if (args[0].decimals >= 0)
    {
        int dtype = ValidateValue(expr, args[0].decimals);
        if (dtype != g_typeError && !IsIntegerType(dtype))
            Error(expr.nodes[args[0].decimals].token.pos, 23, "Integer expression expected");
        if (type != g_typeError && !IsRealType(type))
            Error(expr.nodes[args[0].decimals].token.pos, 0, "Decimals are allowed only for a Real value");
    }
    if (args[1].width >= 0)
        Error(expr.nodes[args[1].width].token.pos, 0, "Format is allowed only for the value");
    CheckStdArg(expr, args[1], 'S');
    return g_typeVoid;
}

// Val(S, V, Code): V is an Integer or Real variable
int Validator::CheckStdVal(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() != 3)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeVoid;
    }
    CheckStdArg(expr, args[0], 's');
    int type = ValidateNode(expr, args[1].node);
    if (type != g_typeError)
    {
        if (!IsVariable(expr, args[1].node))
            Error(expr.nodes[args[1].node].token.pos, 0, "Variable expected");
        else if (!IsNumeric(type))
            Error(expr.nodes[args[1].node].token.pos, 27, "Integer or real variable expected");
    }
    CheckStdArg(expr, args[2], 'L');
    return g_typeVoid;
}

int Validator::CheckStdConcat(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() < 2)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeString;
    }
    bool allconst = true;
    string value;
    for (const ArgModel& arg : args)
    {
        if (!CheckStdArg(expr, arg, 's'))
        {
            allconst = false;
            continue;
        }
        const ExpressionNode& node = expr.nodes[arg.node];
        if (!node.isconst)
            allconst = false;
        else
            value += node.constvalue.svalue;
    }
    if (allconst)
    {
        ExpressionNode& node = expr.nodes[index];
        node.isconst = true;
        node.constvalue.svalue = value.substr(0, 255);
    }
    return g_typeString;
}

// SizeOf(variable) or SizeOf(type identifier)
int Validator::CheckStdSizeOf(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() != 1)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeInteger;
    }
    int node = args[0].node;
    int type = g_typeError;
    if (expr.nodes[node].kind == NodeIdent)
    {
        int symbol = FindSymbol(expr.nodes[node].token.text);
        if (symbol >= 0 && g_program.symbols[symbol].kind == SymType)
        {
            type = g_program.symbols[symbol].type;
            expr.nodes[node].symbol = symbol;
            expr.nodes[node].type = type;
        }
    }
    if (type == g_typeError)
    {
        type = ValidateNode(expr, node);
        if (type == g_typeError)
            return g_typeInteger;
        if (!IsVariable(expr, node))
        {
            Error(expr.nodes[node].token.pos, 0, "Variable or type identifier expected");
            return g_typeInteger;
        }
    }
    ExpressionNode& result = expr.nodes[index];
    result.isconst = true;
    result.constvalue.ivalue = TypeSize(type);
    return g_typeInteger;
}

// Addr(variable) or Addr(procedure or function)
int Validator::CheckStdAddr(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() != 1)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeInteger;
    }
    int node = args[0].node;
    if (expr.nodes[node].kind == NodeIdent)
    {
        int symbol = FindSymbol(expr.nodes[node].token.text);
        if (symbol >= 0 && (g_program.symbols[symbol].kind == SymProcedure || g_program.symbols[symbol].kind == SymFunction))
        {
            expr.nodes[node].symbol = symbol;
            expr.nodes[node].type = g_typeVoid;
            return g_typeInteger;
        }
    }
    int type = ValidateNode(expr, node);
    if (type != g_typeError && !IsVariable(expr, node))
        Error(expr.nodes[node].token.pos, 0, "Variable, procedure or function expected");
    return g_typeInteger;
}

int Validator::CheckStdOrd(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() != 1)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeInteger;
    }
    int type = ValidateValue(expr, args[0].node);
    if (type == g_typeError)
        return g_typeInteger;
    if (!IsOrdinal(type) && !IsPointerType(type))
    {
        Error(expr.nodes[args[0].node].token.pos, 30, "Simple type or pointer expected");
        return g_typeInteger;
    }
    const ExpressionNode& arg = expr.nodes[args[0].node];
    if (arg.isconst && IsOrdinal(type))
    {
        ExpressionNode& node = expr.nodes[index];
        node.isconst = true;
        node.constvalue.ivalue = arg.constvalue.ivalue;
    }
    return g_typeInteger;
}

int Validator::CheckStdChr(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() != 1)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeChar;
    }
    if (!CheckStdArg(expr, args[0], 'i'))
        return g_typeChar;
    const ExpressionNode& arg = expr.nodes[args[0].node];
    if (arg.isconst)
    {
        ExpressionNode& node = expr.nodes[index];
        node.isconst = true;
        node.constvalue.ivalue = arg.constvalue.ivalue & 0xFF;
        node.constvalue.svalue = string(1, (char)node.constvalue.ivalue);
    }
    return g_typeChar;
}

int Validator::CheckStdSuccPred(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() != 1)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeError;
    }
    int type = ValidateValue(expr, args[0].node);
    if (type == g_typeError)
        return g_typeError;
    if (!IsOrdinal(type))
    {
        Error(expr.nodes[args[0].node].token.pos, 30, "Simple type expected");
        return g_typeError;
    }
    int resulttype = IsIntegerType(type) ? g_typeInteger : type;
    const ExpressionNode& arg = expr.nodes[args[0].node];
    if (arg.isconst)
    {
        int64_t value = arg.constvalue.ivalue + ((strcmp(spec.name, "Succ") == 0) ? 1 : -1);
        ExpressionNode& node = expr.nodes[index];
        node.isconst = true;
        node.constvalue.ivalue = IsIntegerType(type) ? (int16_t)(uint16_t)(value & 0xFFFF) : value;
        if (IsCharType(type))
        {
            node.constvalue.ivalue &= 0xFF;
            node.constvalue.svalue = string(1, (char)node.constvalue.ivalue);
        }
    }
    return resulttype;
}

int Validator::CheckStdAbsSqr(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() != 1)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeError;
    }
    if (!CheckStdArg(expr, args[0], 'n'))
        return g_typeError;
    int type = expr.nodes[args[0].node].type;
    bool isabs = (strcmp(spec.name, "Abs") == 0);
    const ExpressionNode& arg = expr.nodes[args[0].node];
    ExpressionNode result;
    result.isconst = arg.isconst;
    if (IsIntegerType(type))
    {
        int64_t v = arg.constvalue.ivalue;
        result.constvalue.ivalue = (int16_t)(uint16_t)((isabs ? (v < 0 ? -v : v) : v * v) & 0xFFFF);
        type = g_typeInteger;
    }
    else
    {
        double v = arg.constvalue.dvalue;
        result.constvalue.dvalue = isabs ? std::fabs(v) : v * v;
    }
    if (result.isconst)
    {
        ExpressionNode& node = expr.nodes[index];
        node.isconst = true;
        node.constvalue = result.constvalue;
    }
    return type;
}

// Random: Real; Random(N): Integer
int Validator::CheckStdRandom(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.empty())
        return g_typeReal;
    if (args.size() > 1)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeInteger;
    }
    CheckStdArg(expr, args[0], 'i');
    return g_typeInteger;
}

// Eof/Eoln/SeekEof/SeekEoln: the file is optional, Input by default
int Validator::CheckStdFileFunc(ExpressionModel& expr, int index, const StdRoutineSpec& spec)
{
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() > 1)
    {
        Error(expr.nodes[index].token.pos, 0, "Wrong number of parameters for " + string(spec.name));
        return g_typeBoolean;
    }
    if (args.size() == 1)
        CheckStdArg(expr, args[0], (strcmp(spec.name, "Eof") == 0) ? 'F' : 'T');
    return g_typeBoolean;
}


//////////////////////////////////////////////////////////////////////
