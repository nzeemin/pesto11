
#include <cassert>
#include <cmath>

#include "main.h"

// Validator: expressions, type checks, implicit conversions, constant folding.


//////////////////////////////////////////////////////////////////////
// Helpers

// Integer arithmetic is 16-bit, overflow wraps around as at run time
static int64_t Wrap16(int64_t value)
{
    return (int16_t)(uint16_t)(value & 0xFFFF);
}

static double GetRealValue(const ExpressionNode& node)
{
    return Validator::IsRealType(node.type) ? node.constvalue.dvalue : (double)node.constvalue.ivalue;
}

// String value of a constant string-like node: a Char constant is a one-character string
static string GetStringValue(const ExpressionNode& node)
{
    if (Validator::IsCharType(node.type))
        return string(1, (char)node.constvalue.ivalue);
    return node.constvalue.svalue;
}

static int CompareStrings(const string& a, const string& b)
{
    size_t count = std::min(a.size(), b.size());
    for (size_t i = 0; i < count; i++)
    {
        unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
        if (ca != cb)
            return (ca < cb) ? -1 : 1;
    }
    if (a.size() == b.size())
        return 0;
    return (a.size() < b.size()) ? -1 : 1;
}

static string EmptySetBits()
{
    return string(32, '\0');
}

static bool SetBit(const string& bits, int64_t element)
{
    if (element < 0 || element > 255)
        return false;
    return (((unsigned char)bits[(size_t)(element / 8)]) >> (element % 8)) & 1;
}

static bool IsOperation(const Token& token, SymbolIndex symbol)
{
    return token.IsSymbol(symbol);
}

static bool IsOperation(const Token& token, KeywordIndex keyword)
{
    return token.IsKeyword(keyword);
}

static string GetOperationName(const Token& token)
{
    if (token.type == TokenTypeKeyword)
        return GetKeywordName(token.keyword);
    return GetSymbolText(token.symbol);
}


//////////////////////////////////////////////////////////////////////
// Entry points

int Validator::ValidateExpression(ExpressionModel& expr)
{
    return ValidateValue(expr, expr.root);
}

// Validate a node that must give a value: a procedure call is an error here
int Validator::ValidateValue(ExpressionModel& expr, int index, bool negated)
{
    int type = ValidateNode(expr, index, negated);
    if (type == g_typeVoid)
    {
        Error(expr.nodes[index].token.pos, 0, "Procedure call has no value: " + expr.nodes[index].token.text);
        expr.nodes[index].type = g_typeError;
        return g_typeError;
    }
    return type;
}

bool Validator::EvaluateConstant(ExpressionModel& expr, ConstValue& value, int& type)
{
    if (expr.root < 0)
        return false;
    type = ValidateValue(expr, expr.root);
    if (type == g_typeError)
        return false;
    const ExpressionNode& node = expr.nodes[expr.root];
    if (!node.isconst)
    {
        Error(node.token.pos, 0, "Constant expected");
        type = g_typeError;
        return false;
    }
    value = node.constvalue;
    return true;
}

int Validator::ValidateNode(ExpressionModel& expr, int index, bool negated)
{
    if (index < 0 || index >= (int)expr.nodes.size())
        return g_typeError;

    int type = g_typeError;
    ExpressionNode& node = expr.nodes[index];
    switch (node.kind)
    {
    case NodeInteger:
        {
            int64_t value = node.token.ivalue;
            if (node.token.ishex)
                value = Wrap16(value);
            else if (value > 32767 && !(negated && value == 32768))
            {
                Error(node.token.pos, 56, "Error in integer constant: " + node.token.text + " is out of range -32768..32767");
                break;
            }
            node.isconst = true;
            node.constvalue.ivalue = value;
            type = g_typeInteger;
        }
        break;
    case NodeReal:
        node.isconst = true;
        node.constvalue.dvalue = node.token.dvalue;
        type = g_typeReal;
        break;
    case NodeString:
        node.isconst = true;
        node.constvalue.svalue = node.token.svalue;
        if (node.token.svalue.size() == 1)
        {
            node.constvalue.ivalue = (unsigned char)node.token.svalue[0];
            type = g_typeChar;
        }
        else
            type = g_typeString;
        break;
    case NodeNil:
        node.isconst = true;
        type = g_typeNilPointer;
        break;
    case NodeIdent:
        type = ValidateIdent(expr, index);
        break;
    case NodeCall:
        type = ValidateCall(expr, index);
        break;
    case NodeIndex:
        type = ValidateIndex(expr, index);
        break;
    case NodeField:
        type = ValidateField(expr, index);
        break;
    case NodeDeref:
        type = ValidateDeref(expr, index);
        break;
    case NodeUnary:
        type = ValidateUnary(expr, index, negated);
        break;
    case NodeBinary:
        type = ValidateBinary(expr, index);
        break;
    case NodeSet:
        type = ValidateSet(expr, index);
        break;
    default:
        break;
    }
    expr.nodes[index].type = type;
    return type;
}


//////////////////////////////////////////////////////////////////////
// Variables and assignment

bool Validator::IsVariable(const ExpressionModel& expr, int index) const
{
    if (index < 0)
        return false;
    const ExpressionNode& node = expr.nodes[index];
    switch (node.kind)
    {
    case NodeIdent:
        if (node.fieldoffset >= 0)
            return true;  // Field inside with
        if (node.symbol < 0)
            return false;
        switch (g_program.symbols[node.symbol].kind)
        {
        case SymVar: case SymParam: case SymVarParam: case SymTypedConst: case SymStdVar:
            return true;
        default:
            return false;
        }
    case NodeIndex:
    case NodeField:
        return IsVariable(expr, node.left);
    case NodeDeref:
        return true;
    default:
        return false;
    }
}

void Validator::SetConvert(ExpressionModel& expr, int index, ConvertKind convert)
{
    ExpressionNode& node = expr.nodes[index];
    node.convert = convert;
    if (!node.isconst)
        return;
    if (convert == ConvertIntToReal)
        node.constvalue.dvalue = (double)node.constvalue.ivalue;
    else if (convert == ConvertCharToString)
        node.constvalue.svalue = string(1, (char)node.constvalue.ivalue);
}

// Can the value of the node be assigned to a variable of the type; sets the conversion
bool Validator::CheckAssignable(int desttype, ExpressionModel& expr, int index, const SourcePos& pos, bool report)
{
    if (index < 0)
        return false;
    int srctype = expr.nodes[index].type;
    if (Resolve(desttype) == g_typeError || Resolve(srctype) == g_typeError)
        return true;  // Already reported

    bool ok = false;
    if (Resolve(desttype) == Resolve(srctype) && !IsFileType(desttype))
        ok = true;
    else if (IsOrdinal(desttype) && OrdinalCompatible(desttype, srctype))
        ok = true;
    else if (IsRealType(desttype) && IsNumeric(srctype))
    {
        ok = true;
        if (IsIntegerType(srctype))
            SetConvert(expr, index, ConvertIntToReal);
    }
    else if (IsStringType(desttype) && IsStringLike(srctype))
    {
        ok = true;
        if (IsCharType(srctype))
            SetConvert(expr, index, ConvertCharToString);
        else if (IsCharArray(srctype))
            SetConvert(expr, index, ConvertArrayToString);
    }
    else if (IsCharType(desttype) && IsStringType(srctype))
        ok = true;  // Length 1 is checked at run time, error 10
    else if (IsCharArray(desttype) && expr.nodes[index].isconst && IsStringLike(srctype))
    {
        int64_t low = 0, high = -1;
        GetOrdinalRange(GetType(desttype).indextype, low, high);
        ok = ((int64_t)GetStringValue(expr.nodes[index]).size() == high - low + 1);
    }
    else if (IsSetType(desttype) && SetCompatible(desttype, srctype))
        ok = true;
    else if (IsPointerType(desttype) && PointerCompatible(desttype, srctype))
        ok = true;

    if (!ok && report)
        Error(pos, 44, "Type mismatch: " + g_program.TypeToString(srctype) + " is not compatible with " + g_program.TypeToString(desttype));
    return ok;
}

bool Validator::FindField(int recordtype, const string& name, int& offset, int& type) const
{
    if (!IsRecordType(recordtype))
        return false;
    string upper = name;
    for (char& c : upper)
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
    auto same = [&upper](const string& other)
    {
        if (other.size() != upper.size())
            return false;
        for (size_t i = 0; i < other.size(); i++)
        {
            char c = other[i];
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
            if (c != upper[i])
                return false;
        }
        return true;
    };
    std::vector<const RecordPartModel*> parts { &GetType(recordtype).record };
    while (!parts.empty())
    {
        const RecordPartModel* part = parts.back();
        parts.pop_back();
        for (const FieldModel& field : part->fields)
        {
            if (same(field.name.text))
            {
                offset = field.offset;
                type = field.type;
                return true;
            }
        }
        if (part->hasvariant && part->tagname.type != TokenTypeNone && same(part->tagname.text))
        {
            offset = part->tagoffset;
            type = part->tagtype;
            return true;
        }
        for (const VariantModel& variant : part->variants)
            parts.push_back(&variant.part);
    }
    return false;
}

void Validator::CheckNoFormat(ExpressionModel& expr, int index)
{
    for (const ArgModel& arg : expr.nodes[index].args)
    {
        if (arg.width >= 0)
        {
            Error(expr.nodes[arg.width].token.pos, 0, "Format is allowed only in Write, WriteLn and Str");
            return;
        }
    }
}


//////////////////////////////////////////////////////////////////////
// Identifiers and calls

int Validator::ValidateIdent(ExpressionModel& expr, int index)
{
    ExpressionNode& node = expr.nodes[index];
    const string& name = node.token.text;

    // Fields of the records opened by with, the innermost first
    for (int i = (int)m_withs.size() - 1; i >= 0; i--)
    {
        int offset, type;
        if (FindField(m_withs[i].type, name, offset, type))
        {
            node.fieldoffset = offset;
            node.withslot = m_withs[i].slot;
            return type;
        }
    }

    int symbol = FindSymbol(name);
    if (symbol < 0)
    {
        Error(node.token.pos, 41, "Unknown identifier: " + name);
        return g_typeError;
    }
    const SymbolModel& model = g_program.symbols[symbol];
    node.symbol = symbol;
    if (model.unsupported)
    {
        Error(node.token.pos, 200, "Not supported in Pesto/11: " + name);
        return g_typeError;
    }

    switch (model.kind)
    {
    case SymConst:
        node.isconst = true;
        node.constvalue = model.constvalue;
        if (IsCharType(model.type))
            node.constvalue.svalue = string(1, (char)model.constvalue.ivalue);
        return model.type;
    case SymVar:
    case SymParam:
    case SymVarParam:
    case SymTypedConst:
    case SymStdVar:
        return (model.type >= 0) ? model.type : g_typeError;
    case SymFuncResult:
        if (m_assigntarget && index == expr.root)
            return model.type;  // Assignment to the function result
        // In an expression the function name is a recursive call
        node.symbol = g_program.procedures[model.procindex].symbol;
        return ValidateUserCall(expr, index, node.symbol);
    case SymFunction:
    case SymProcedure:
        return ValidateUserCall(expr, index, symbol);
    case SymStdFunc:
    case SymStdProc:
        return ValidateStdCall(expr, index, symbol);
    case SymType:
        Error(node.token.pos, 0, "Type identifier is not allowed here: " + name);
        return g_typeError;
    case SymLabel:
        Error(node.token.pos, 41, "Label is not allowed here: " + name);
        return g_typeError;
    default:
        return g_typeError;
    }
}

int Validator::ValidateCall(ExpressionModel& expr, int index)
{
    ExpressionNode& node = expr.nodes[index];
    const string& name = node.token.text;
    int symbol = FindSymbol(name);
    if (symbol < 0)
    {
        Error(node.token.pos, 41, "Unknown identifier: " + name);
        for (const ArgModel& arg : node.args)
            ValidateNode(expr, arg.node);
        return g_typeError;
    }
    node.symbol = symbol;
    const SymbolModel& model = g_program.symbols[symbol];
    if (model.unsupported)
    {
        Error(node.token.pos, 200, "Not supported in Pesto/11: " + name);
        return g_typeError;
    }

    switch (model.kind)
    {
    case SymType:
        CheckNoFormat(expr, index);
        return ValidateTypeCast(expr, index, symbol);
    case SymProcedure:
    case SymFunction:
        CheckNoFormat(expr, index);
        return ValidateUserCall(expr, index, symbol);
    case SymFuncResult:
        CheckNoFormat(expr, index);
        expr.nodes[index].symbol = g_program.procedures[model.procindex].symbol;
        return ValidateUserCall(expr, index, expr.nodes[index].symbol);
    case SymStdProc:
    case SymStdFunc:
        return ValidateStdCall(expr, index, symbol);
    default:
        Error(node.token.pos, 0, "Procedure or function expected: " + name);
        return g_typeError;
    }
}

int Validator::ValidateUserCall(ExpressionModel& expr, int index, int symbol)
{
    const SymbolModel& model = g_program.symbols[symbol];
    const ProcedureModel& proc = g_program.procedures[model.procindex];
    struct Param { bool isvar; int type; };
    std::vector<Param> params;
    for (const ParamGroupModel& group : proc.params)
    {
        for (size_t i = 0; i < group.names.size(); i++)
            params.push_back({ group.isvar, group.paramtype });
    }
    int resulttype = (proc.kind == ProcKindFunction) ?
        ((proc.resulttypeindex >= 0) ? proc.resulttypeindex : g_typeError) : g_typeVoid;

    std::vector<ArgModel> args = expr.nodes[index].args;
    const Token token = expr.nodes[index].token;
    if (args.size() != params.size())
    {
        Error(token.pos, 0, "Wrong number of parameters for " + token.text + ": expected " +
                std::to_string(params.size()) + ", found " + std::to_string(args.size()));
        for (const ArgModel& arg : args)
            ValidateNode(expr, arg.node);
        return resulttype;
    }

    for (size_t i = 0; i < args.size(); i++)
    {
        int argnode = args[i].node;
        if (params[i].isvar)
        {
            int argtype = ValidateNode(expr, argnode);
            if (argtype == g_typeError)
                continue;
            if (!IsVariable(expr, argnode))
            {
                Error(expr.nodes[argnode].token.pos, 0, "Variable expected for a VAR parameter");
                continue;
            }
            if (params[i].type < 0)
                continue;  // Untyped var parameter takes any variable
            bool same = (Resolve(argtype) == Resolve(params[i].type));
            if (!same && IsStringType(argtype) && IsStringType(params[i].type) &&
                (token.directives & DirectiveV) == 0)
                same = true;  // {$V-}: string length is not checked
            if (!same)
                Error(expr.nodes[argnode].token.pos, 44, "Type mismatch: VAR parameter must be of the same type " +
                        g_program.TypeToString(params[i].type));
        }
        else
        {
            int argtype = ValidateValue(expr, argnode);
            if (argtype != g_typeError && params[i].type >= 0)
                CheckAssignable(params[i].type, expr, argnode, expr.nodes[argnode].token.pos, true);
        }
    }
    return resulttype;
}

// Type identifier as a function: Char(78), Day(3) - the same ordinal value in another type
int Validator::ValidateTypeCast(ExpressionModel& expr, int index, int symbol)
{
    int target = g_program.symbols[symbol].type;
    std::vector<ArgModel> args = expr.nodes[index].args;
    if (args.size() != 1)
    {
        Error(expr.nodes[index].token.pos, 0, "Type cast takes one argument");
        return g_typeError;
    }
    int argtype = ValidateValue(expr, args[0].node);
    if (argtype == g_typeError)
        return g_typeError;
    if (!IsOrdinal(target) || !IsOrdinal(argtype))
    {
        Error(expr.nodes[index].token.pos, 44, "Type cast is allowed only between scalar types except Real");
        return g_typeError;
    }
    const ExpressionNode& arg = expr.nodes[args[0].node];
    if (arg.isconst)
    {
        ExpressionNode& node = expr.nodes[index];
        node.isconst = true;
        node.constvalue.ivalue = arg.constvalue.ivalue;
        if (IsCharType(target))
            node.constvalue.svalue = string(1, (char)arg.constvalue.ivalue);
    }
    return target;
}

int Validator::ValidateIndex(ExpressionModel& expr, int index)
{
    int base = ValidateValue(expr, expr.nodes[index].left);
    std::vector<ArgModel> args = expr.nodes[index].args;
    for (const ArgModel& arg : args)
    {
        int argtype = ValidateValue(expr, arg.node);
        if (base == g_typeError)
            continue;
        if (IsArrayType(base))
        {
            int indextype = GetType(base).indextype;
            if (argtype != g_typeError && !OrdinalCompatible(indextype, argtype))
                Error(expr.nodes[arg.node].token.pos, 44, "Type mismatch: index must be of type " + g_program.TypeToString(indextype));
            base = GetType(base).elemtype;
        }
        else if (IsStringType(base))
        {
            if (argtype != g_typeError && !IsIntegerType(argtype))
                Error(expr.nodes[arg.node].token.pos, 23, "Integer expression expected");
            base = g_typeChar;
        }
        else
        {
            Error(expr.nodes[index].token.pos, 0, "Array or string expected before [");
            base = g_typeError;
        }
    }
    return base;
}

int Validator::ValidateField(ExpressionModel& expr, int index)
{
    int base = ValidateValue(expr, expr.nodes[index].left);
    if (base == g_typeError)
        return g_typeError;
    ExpressionNode& node = expr.nodes[index];
    if (!IsRecordType(base))
    {
        Error(node.token.pos, 29, "Record variable expected before ." + node.token.text);
        return g_typeError;
    }
    int offset, type;
    if (!FindField(base, node.token.text, offset, type))
    {
        Error(node.token.pos, 41, "Unknown field: " + node.token.text);
        return g_typeError;
    }
    node.fieldoffset = offset;
    return type;
}

int Validator::ValidateDeref(ExpressionModel& expr, int index)
{
    int base = ValidateValue(expr, expr.nodes[index].left);
    if (base == g_typeError)
        return g_typeError;
    if (!IsPointerType(base) || Resolve(base) == g_typeNilPointer)
    {
        Error(expr.nodes[index].token.pos, 28, "Pointer variable expected before ^");
        return g_typeError;
    }
    return Resolve(GetType(base).elemtype);
}


//////////////////////////////////////////////////////////////////////
// Operations

int Validator::ValidateUnary(ExpressionModel& expr, int index, bool negated)
{
    (void)negated;
    const Token op = expr.nodes[index].token;
    int left = expr.nodes[index].left;
    bool isminus = IsOperation(op, SymbolMinus);
    // -32768 is valid only as a negated literal
    bool negliteral = isminus && left >= 0 && expr.nodes[left].kind == NodeInteger;
    int type = ValidateValue(expr, left, negliteral);
    if (type == g_typeError)
        return g_typeError;
    const ExpressionNode& operand = expr.nodes[left];
    ExpressionNode result;  // Constant result
    result.isconst = operand.isconst;
    int resulttype = g_typeError;

    if (IsOperation(op, KeywordNOT))
    {
        if (IsIntegerType(type))
        {
            resulttype = g_typeInteger;
            result.constvalue.ivalue = Wrap16(~operand.constvalue.ivalue);
        }
        else if (IsBooleanType(type))
        {
            resulttype = g_typeBoolean;
            result.constvalue.ivalue = operand.constvalue.ivalue ? 0 : 1;
        }
    }
    else if (IsNumeric(type))
    {
        if (IsIntegerType(type))
        {
            resulttype = g_typeInteger;
            result.constvalue.ivalue = isminus ? Wrap16(-operand.constvalue.ivalue) : operand.constvalue.ivalue;
        }
        else
        {
            resulttype = g_typeReal;
            result.constvalue.dvalue = isminus ? -operand.constvalue.dvalue : operand.constvalue.dvalue;
        }
    }

    if (resulttype == g_typeError)
    {
        Error(op.pos, 47, "Operand type does not match operator " + GetOperationName(op));
        return g_typeError;
    }
    ExpressionNode& node = expr.nodes[index];
    node.isconst = result.isconst;
    if (result.isconst)
        node.constvalue = result.constvalue;
    return resulttype;
}

int Validator::ValidateBinary(ExpressionModel& expr, int index)
{
    const Token op = expr.nodes[index].token;
    int left = expr.nodes[index].left;
    int right = expr.nodes[index].right;
    int lt = ValidateValue(expr, left);
    int rt = ValidateValue(expr, right);
    if (lt == g_typeError || rt == g_typeError)
        return g_typeError;

    bool bothconst = expr.nodes[left].isconst && expr.nodes[right].isconst;
    ConstValue value;
    int resulttype = g_typeError;

    bool isplus = IsOperation(op, SymbolPlus), isminus = IsOperation(op, SymbolMinus), isstar = IsOperation(op, SymbolStar);
    bool iscompare = IsOperation(op, SymbolEqual) || IsOperation(op, SymbolNotEqual) || IsOperation(op, SymbolLess) ||
        IsOperation(op, SymbolGreater) || IsOperation(op, SymbolLessEqual) || IsOperation(op, SymbolGreaterEqual);

    if (isplus || isminus || isstar || IsOperation(op, SymbolSlash))
    {
        bool isslash = IsOperation(op, SymbolSlash);
        if (IsNumeric(lt) && IsNumeric(rt))
        {
            if (IsIntegerType(lt) && IsIntegerType(rt) && !isslash)
            {
                resulttype = g_typeInteger;
                if (bothconst)
                {
                    int64_t a = expr.nodes[left].constvalue.ivalue, b = expr.nodes[right].constvalue.ivalue;
                    value.ivalue = Wrap16(isplus ? a + b : isminus ? a - b : a * b);
                }
            }
            else
            {
                resulttype = g_typeReal;
                if (IsIntegerType(lt))
                    SetConvert(expr, left, ConvertIntToReal);
                if (IsIntegerType(rt))
                    SetConvert(expr, right, ConvertIntToReal);
                if (bothconst)
                {
                    double a = GetRealValue(expr.nodes[left]), b = GetRealValue(expr.nodes[right]);
                    if (isslash && b == 0.0)
                        bothconst = false;  // Division by zero: run-time error 02
                    else
                        value.dvalue = isplus ? a + b : isminus ? a - b : isstar ? a * b : a / b;
                }
            }
        }
        else if (isplus && IsStringLike(lt) && IsStringLike(rt))
        {
            resulttype = g_typeString;
            if (IsCharType(lt)) SetConvert(expr, left, ConvertCharToString);
            if (IsCharArray(lt)) SetConvert(expr, left, ConvertArrayToString);
            if (IsCharType(rt)) SetConvert(expr, right, ConvertCharToString);
            if (IsCharArray(rt)) SetConvert(expr, right, ConvertArrayToString);
            if (bothconst)
            {
                value.svalue = GetStringValue(expr.nodes[left]) + GetStringValue(expr.nodes[right]);
                if (value.svalue.size() > 255)
                {
                    Error(op.pos, 0, "String constant is longer than 255 characters");
                    value.svalue.resize(255);
                }
            }
        }
        else if (!isslash && SetCompatible(lt, rt))
        {
            resulttype = (Resolve(lt) != g_typeEmptySet) ? lt : rt;
            if (bothconst)
            {
                const string& a = expr.nodes[left].constvalue.svalue;
                const string& b = expr.nodes[right].constvalue.svalue;
                value.svalue = EmptySetBits();
                for (size_t i = 0; i < 32; i++)
                {
                    unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
                    value.svalue[i] = (char)(isplus ? (ca | cb) : isminus ? (ca & ~cb) : (ca & cb));
                }
            }
        }
    }
    else if (IsOperation(op, KeywordDIV) || IsOperation(op, KeywordMOD) ||
             IsOperation(op, KeywordSHL) || IsOperation(op, KeywordSHR))
    {
        if (IsIntegerType(lt) && IsIntegerType(rt))
        {
            resulttype = g_typeInteger;
            if (bothconst)
            {
                int64_t a = expr.nodes[left].constvalue.ivalue, b = expr.nodes[right].constvalue.ivalue;
                if (IsOperation(op, KeywordDIV) || IsOperation(op, KeywordMOD))
                {
                    if (b == 0)
                        bothconst = false;  // Division by zero: run-time error 02
                    else
                        value.ivalue = Wrap16(IsOperation(op, KeywordDIV) ? a / b : a % b);  // Truncation toward zero
                }
                else if (IsOperation(op, KeywordSHL))
                    value.ivalue = (b < 0 || b > 15) ? 0 : Wrap16(a << b);
                else
                    value.ivalue = (b < 0 || b > 15) ? 0 : Wrap16((a & 0xFFFF) >> b);  // Logical shift
            }
        }
    }
    else if (IsOperation(op, KeywordAND) || IsOperation(op, KeywordOR) || IsOperation(op, KeywordXOR))
    {
        bool isand = IsOperation(op, KeywordAND), isor = IsOperation(op, KeywordOR);
        if ((IsIntegerType(lt) && IsIntegerType(rt)) || (IsBooleanType(lt) && IsBooleanType(rt)))
        {
            resulttype = IsIntegerType(lt) ? g_typeInteger : g_typeBoolean;
            if (bothconst)
            {
                int64_t a = expr.nodes[left].constvalue.ivalue, b = expr.nodes[right].constvalue.ivalue;
                value.ivalue = Wrap16(isand ? (a & b) : isor ? (a | b) : (a ^ b));
            }
        }
    }
    else if (iscompare)
    {
        bool equality = IsOperation(op, SymbolEqual) || IsOperation(op, SymbolNotEqual);
        bool inclusion = IsOperation(op, SymbolLessEqual) || IsOperation(op, SymbolGreaterEqual);
        int compare = 0;  // Constant comparison result: -1, 0, 1
        bool comparable = false;
        if (IsNumeric(lt) && IsNumeric(rt))
        {
            comparable = true;
            if (IsRealType(lt) || IsRealType(rt))
            {
                if (IsIntegerType(lt)) SetConvert(expr, left, ConvertIntToReal);
                if (IsIntegerType(rt)) SetConvert(expr, right, ConvertIntToReal);
                if (bothconst)
                {
                    double a = GetRealValue(expr.nodes[left]), b = GetRealValue(expr.nodes[right]);
                    compare = (a < b) ? -1 : (a > b) ? 1 : 0;
                }
            }
            else if (bothconst)
            {
                int64_t a = expr.nodes[left].constvalue.ivalue, b = expr.nodes[right].constvalue.ivalue;
                compare = (a < b) ? -1 : (a > b) ? 1 : 0;
            }
        }
        else if (OrdinalCompatible(lt, rt))
        {
            comparable = true;
            if (bothconst)
            {
                int64_t a = expr.nodes[left].constvalue.ivalue, b = expr.nodes[right].constvalue.ivalue;
                compare = (a < b) ? -1 : (a > b) ? 1 : 0;
            }
        }
        else if (IsStringLike(lt) && IsStringLike(rt))
        {
            comparable = true;
            if (IsCharArray(lt)) SetConvert(expr, left, ConvertArrayToString);
            if (IsCharArray(rt)) SetConvert(expr, right, ConvertArrayToString);
            if (bothconst)
                compare = CompareStrings(GetStringValue(expr.nodes[left]), GetStringValue(expr.nodes[right]));
        }
        else if ((equality || inclusion) && SetCompatible(lt, rt))
        {
            comparable = true;
            if (bothconst)
            {
                const string& a = expr.nodes[left].constvalue.svalue;
                const string& b = expr.nodes[right].constvalue.svalue;
                bool aInB = true, bInA = true;
                for (size_t i = 0; i < 32; i++)
                {
                    unsigned char ca = (unsigned char)a[i], cb = (unsigned char)b[i];
                    if (ca & ~cb) aInB = false;
                    if (cb & ~ca) bInA = false;
                }
                // For sets: <= is "left included in right", >= is "right included in left"
                if (equality)
                    compare = (aInB && bInA) ? 0 : 1;
                else if (IsOperation(op, SymbolLessEqual))
                    compare = aInB ? 0 : 1;
                else
                    compare = bInA ? 0 : -1;
            }
        }
        else if (equality && PointerCompatible(lt, rt))
        {
            comparable = true;
            bothconst = false;
        }
        if (comparable)
        {
            resulttype = g_typeBoolean;
            if (bothconst)
            {
                bool result =
                    IsOperation(op, SymbolEqual) ? (compare == 0) :
                    IsOperation(op, SymbolNotEqual) ? (compare != 0) :
                    IsOperation(op, SymbolLess) ? (compare < 0) :
                    IsOperation(op, SymbolGreater) ? (compare > 0) :
                    IsOperation(op, SymbolLessEqual) ? (compare <= 0) : (compare >= 0);
                value.ivalue = result ? 1 : 0;
            }
        }
    }
    else if (IsOperation(op, KeywordIN))
    {
        if (IsOrdinal(lt) && IsSetType(rt) &&
            (Resolve(rt) == g_typeEmptySet || OrdinalCompatible(lt, GetType(rt).elemtype)))
        {
            resulttype = g_typeBoolean;
            if (bothconst)
                value.ivalue = SetBit(expr.nodes[right].constvalue.svalue, expr.nodes[left].constvalue.ivalue) ? 1 : 0;
        }
    }

    if (resulttype == g_typeError)
    {
        Error(op.pos, 47, "Operand types do not match operator " + GetOperationName(op) + ": " +
                g_program.TypeToString(lt) + ", " + g_program.TypeToString(rt));
        return g_typeError;
    }
    ExpressionNode& node = expr.nodes[index];
    node.isconst = bothconst;
    if (bothconst)
        node.constvalue = value;
    return resulttype;
}

int Validator::ValidateSet(ExpressionModel& expr, int index)
{
    std::vector<SetItemModel> items = expr.nodes[index].setitems;
    if (items.empty())
    {
        expr.nodes[index].isconst = true;
        expr.nodes[index].constvalue.svalue = EmptySetBits();
        return g_typeEmptySet;
    }

    int basetype = -1;
    bool allconst = true;
    bool error = false;
    string bits = EmptySetBits();
    for (const SetItemModel& item : items)
    {
        int nodes[2] = { item.from, item.to };
        int64_t values[2] = { 0, 0 };
        for (int i = 0; i < 2; i++)
        {
            if (nodes[i] < 0)
            {
                values[1] = values[0];
                continue;
            }
            int type = ValidateValue(expr, nodes[i]);
            if (type == g_typeError)
            {
                error = true;
                continue;
            }
            if (!IsOrdinal(type))
            {
                Error(expr.nodes[nodes[i]].token.pos, 30, "Simple type expected for a set element");
                error = true;
                continue;
            }
            if (basetype < 0)
                basetype = type;
            else if (!OrdinalCompatible(basetype, type))
            {
                Error(expr.nodes[nodes[i]].token.pos, 44, "Type mismatch in the set");
                error = true;
                continue;
            }
            if (!expr.nodes[nodes[i]].isconst)
                allconst = false;
            else
            {
                values[i] = expr.nodes[nodes[i]].constvalue.ivalue;
                if (values[i] < 0 || values[i] > 255)
                {
                    Error(expr.nodes[nodes[i]].token.pos, 70, "Set element out of range 0..255");
                    error = true;
                }
            }
        }
        if (allconst && !error)
        {
            for (int64_t v = values[0]; v <= values[1]; v++)
                bits[(size_t)(v / 8)] = (char)((unsigned char)bits[(size_t)(v / 8)] | (1 << (v % 8)));
        }
    }
    if (error || basetype < 0)
        return g_typeError;

    ExpressionNode& node = expr.nodes[index];
    node.isconst = allconst;
    if (allconst)
        node.constvalue.svalue = bits;
    return GetSetType(basetype);
}


//////////////////////////////////////////////////////////////////////
