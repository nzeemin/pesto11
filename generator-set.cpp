
#include <cassert>

#include "main.h"

// Generator: sets, see docs/DESIGN.md section 7.6 and docs/RUNTIME.md section 7.
// A set variable is stored compressed: bytes (Min div 8) .. (Max div 8) of the full 32-byte bitmap.
// A set value in an expression is expanded to 32 bytes: a constant image in the data,
// or a temporary buffer on the stack (SUB #32., SP, the set at (SP)).

static const int SET_BUFFER = 32;


//////////////////////////////////////////////////////////////////////

// Layout of a set type: first stored byte of the bitmap and the number of bytes
static void GetSetLayout(int type, int& firstbyte, int& size)
{
    const TypeModel& model = Validator::GetType(type);
    firstbyte = (int)(model.low / 8);
    size = Validator::TypeSize(type);
    if (size <= 0 || firstbyte < 0 || firstbyte + size > 32)
    {
        firstbyte = 0;
        size = 32;
    }
}

static bool SameSetLayout(int type1, int type2)
{
    int first1, size1, first2, size2;
    GetSetLayout(type1, first1, size1);
    GetSetLayout(type2, first2, size2);
    return first1 == first2 && size1 == size2;
}

static void SetBits(string& bits, int64_t from, int64_t to)
{
    if (from < 0)
        from = 0;
    if (to > 255)
        to = 255;
    for (int64_t e = from; e <= to; e++)
        bits[(size_t)(e / 8)] = (char)(bits[(size_t)(e / 8)] | (1 << (e % 8)));
}

// Build the set value expanded in a new 32-byte buffer on the stack
bool Generator::GenerateSetBuffer(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.isconst)
    {
        Emit("SUB", Operand::Imm(SET_BUFFER), Operand::Reg(6), "set buffer");
        if (node.constvalue.svalue == string(32, '\0'))
        {
            Emit("MOV", Operand::Reg(6), Operand::Reg(0));
            EmitCall("SETCLR", "[]");
            return true;
        }
        Emit("MOV", Operand::ImmLabel(GetDataLabel(node.constvalue.svalue)), Operand::Reg(3), "set constant");
        Emit("MOV", Operand::Reg(6), Operand::Reg(4));
        EmitBlockCopy(SET_BUFFER, true);
        return true;
    }
    if (IsDesignator(expr, index))
    {
        // Expand the compressed variable
        int firstbyte, size;
        GetSetLayout(node.type, firstbyte, size);
        Emit("SUB", Operand::Imm(SET_BUFFER), Operand::Reg(6), "set buffer");
        std::vector<RuntimeArg> args;
        args.push_back(RuntimeArg(RuntimeArg::Address, index, 0));
        args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 2, firstbyte));
        args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 3, size));
        int buffers;
        if (!LoadRuntimeArgs(expr, args, buffers))
            return false;
        Emit("MOV", Operand::Reg(6), Operand::Reg(1));
        EmitCall("SETLD");
        return true;
    }
    if (node.kind == NodeSet)
    {
        // The constant elements go into an image copied at once, the others are added one by one
        string bits(32, '\0');
        bool hasconst = false;
        for (const SetItemModel& item : node.setitems)
        {
            const ExpressionNode& from = expr.nodes[item.from];
            if (!from.isconst || (item.to >= 0 && !expr.nodes[item.to].isconst))
                continue;
            int64_t to = (item.to >= 0) ? expr.nodes[item.to].constvalue.ivalue : from.constvalue.ivalue;
            SetBits(bits, from.constvalue.ivalue, to);
            hasconst = true;
        }
        Emit("SUB", Operand::Imm(SET_BUFFER), Operand::Reg(6), "set buffer");
        if (hasconst && bits != string(32, '\0'))
        {
            Emit("MOV", Operand::ImmLabel(GetDataLabel(bits)), Operand::Reg(3), "constant elements");
            Emit("MOV", Operand::Reg(6), Operand::Reg(4));
            EmitBlockCopy(SET_BUFFER, true);
        }
        else
        {
            Emit("MOV", Operand::Reg(6), Operand::Reg(0));
            EmitCall("SETCLR");
        }

        // With {$R+} every element is checked against the base type of the set
        bool check = (node.token.directives & DirectiveR) != 0;
        int64_t low = 0, high = 255;
        const TypeModel& settype = Validator::GetType(m_setrangetype >= 0 ? m_setrangetype : node.type);
        if (settype.kind == TypeKindSet && settype.high >= settype.low)
        {
            low = settype.low;
            high = settype.high;
        }
        auto emitcheck = [&]()
        {
            if (!check)
                return;
            EmitCall("RNGSCL", "set element check, error 91");
            EmitData(".WORD", FormatNumber(low) + ", " + FormatNumber(high), 2);
        };
        for (const SetItemModel& item : node.setitems)
        {
            const ExpressionNode& from = expr.nodes[item.from];
            if (from.isconst && (item.to < 0 || expr.nodes[item.to].isconst))
                continue;
            if (!GenerateValue(expr, item.from))
                return false;
            emitcheck();
            if (item.to < 0)
            {
                Emit("MOV", Operand::Reg(6), Operand::Reg(1));
                EmitCall("SETADD");
                continue;
            }
            Emit("MOV", Operand::Reg(0), Operand::AutoDec(6));
            if (!GenerateValue(expr, item.to))
                return false;
            emitcheck();
            Emit("MOV", Operand::Reg(0), Operand::Reg(1));
            Emit("MOV", Operand::AutoInc(6), Operand::Reg(0));
            Emit("MOV", Operand::Reg(6), Operand::Reg(2));
            EmitCall("SETRNG");
        }
        return true;
    }
    if (node.kind == NodeBinary)
    {
        const char* routine = node.token.IsSymbol(SymbolPlus) ? "SETUN" :
            node.token.IsSymbol(SymbolMinus) ? "SETDF" : node.token.IsSymbol(SymbolStar) ? "SETIS" : nullptr;
        if (routine == nullptr)
            return false;
        if (!GenerateSetBuffer(expr, node.left))
            return false;
        const ExpressionNode& right = expr.nodes[node.right];
        if (right.isconst)
        {
            Emit("MOV", Operand::Reg(6), Operand::Reg(0));
            Emit("MOV", Operand::ImmLabel(GetDataLabel(right.constvalue.svalue)), Operand::Reg(1), "set constant");
            EmitCall(routine);
            return true;
        }
        if (!GenerateSetBuffer(expr, node.right))
            return false;
        Emit("MOV", Operand::Reg(6), Operand::Reg(1));
        Emit("MOV", Operand::Reg(6), Operand::Reg(0));
        Emit("ADD", Operand::Imm(SET_BUFFER), Operand::Reg(0));
        EmitCall(routine);
        FreeBuffers(SET_BUFFER);
        return true;
    }
    return false;
}


//////////////////////////////////////////////////////////////////////

// S := set: a constant image in the target layout or a variable of the same layout is copied
// as a block; anything else is expanded into a buffer and compressed by SETST
bool Generator::GenerateSetAssign(StatementModel& statement)
{
    const ExpressionModel& target = statement.target;
    const ExpressionModel& value = statement.expr;
    const ExpressionNode& valuenode = value.nodes[value.root];
    int type = target.nodes[target.root].type;
    int firstbyte, size;
    GetSetLayout(type, firstbyte, size);

    if (valuenode.isconst && valuenode.constvalue.svalue.size() == 32)
    {
        string image = valuenode.constvalue.svalue.substr((size_t)firstbyte, (size_t)size);
        if (!GenerateAddress(target, target.root, 3))
            return false;
        Emit("MOV", Operand::Reg(3), Operand::Reg(4));
        Emit("MOV", Operand::ImmLabel(GetDataLabel(image)), Operand::Reg(3), "set constant");
        EmitBlockCopy(size, false);
        return true;
    }
    if (IsDesignator(value, value.root) && SameSetLayout(valuenode.type, type))
        return GenerateStructAssign(statement);

    std::vector<RuntimeArg> args;
    args.push_back(RuntimeArg(RuntimeArg::SetValue, value.root, 0));
    args.push_back(RuntimeArg(RuntimeArg::Address, target.root, 1));
    args.back().model = &target;
    args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 2, firstbyte));
    args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 3, size));
    int buffers;
    m_setrangetype = type;  // {$R+}: constructor elements are checked against the target
    bool ok = LoadRuntimeArgs(value, args, buffers);
    m_setrangetype = -1;
    if (!ok)
        return false;
    EmitCall("SETST", "compress into the variable");
    FreeBuffers(buffers);
    return true;
}

// =, <>, <=, >= of sets; SETEQ/SETLE give R0 = 0 and Z = 1 when the answer is yes
bool Generator::GenerateSetCompare(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue)
{
    const ExpressionNode& node = expr.nodes[index];
    const Token& op = node.token;
    int left = node.left, right = node.right;
    if (op.IsSymbol(SymbolGreaterEqual))
        std::swap(left, right);  // A >= B is B <= A
    bool isequal = op.IsSymbol(SymbolEqual) || op.IsSymbol(SymbolNotEqual);
    if (!isequal && !op.IsSymbol(SymbolLessEqual) && !op.IsSymbol(SymbolGreaterEqual))
        return false;
    const char* routine = isequal ? "SETEQ" : "SETLE";

    std::vector<RuntimeArg> args;
    int buffers;
    const ExpressionNode& leftnode = expr.nodes[left];
    const ExpressionNode& rightnode = expr.nodes[right];
    if (IsDesignator(expr, left) && IsDesignator(expr, right) && SameSetLayout(leftnode.type, rightnode.type))
    {
        // The same layout: compare the stored bytes
        int firstbyte, size;
        GetSetLayout(leftnode.type, firstbyte, size);
        args.push_back(RuntimeArg(RuntimeArg::Address, left, 0));
        args.push_back(RuntimeArg(RuntimeArg::Address, right, 1));
        args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 2, size));
    }
    else
    {
        args.push_back(RuntimeArg(RuntimeArg::SetValue, left, 0));
        args.push_back(RuntimeArg(RuntimeArg::SetValue, right, 1));
        args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 2, SET_BUFFER));
    }
    if (!LoadRuntimeArgs(expr, args, buffers))
        return false;
    EmitCall(routine);
    if (buffers > 0)
    {
        FreeBuffers(buffers);
        Emit("TST", Operand::Reg(0));
    }
    bool jumpifyes = op.IsSymbol(SymbolNotEqual) ? !jumpiftrue : jumpiftrue;
    EmitBranch(jumpifyes ? "BEQ" : "BNE", label);
    return true;
}

// x in S for a set variable (SETINV, no expansion) or a set expression (SETIN)
bool Generator::GenerateSetIn(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue)
{
    const ExpressionNode& node = expr.nodes[index];
    const ExpressionNode& setnode = expr.nodes[node.right];
    std::vector<RuntimeArg> args;
    int buffers;
    args.push_back(RuntimeArg(RuntimeArg::Value, node.left, 0));
    const char* routine = "SETIN";
    if (!setnode.isconst && IsDesignator(expr, node.right))
    {
        int firstbyte, size;
        GetSetLayout(setnode.type, firstbyte, size);
        args.push_back(RuntimeArg(RuntimeArg::Address, node.right, 1));
        args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 2, firstbyte));
        args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 3, size));
        routine = "SETINV";
    }
    else
        args.push_back(RuntimeArg(RuntimeArg::SetValue, node.right, 1));
    if (!LoadRuntimeArgs(expr, args, buffers))
        return false;
    EmitCall(routine);
    if (buffers > 0)
    {
        FreeBuffers(buffers);
        Emit("TST", Operand::Reg(0));
    }
    EmitBranch(jumpiftrue ? "BEQ" : "BNE", label);
    return true;
}


//////////////////////////////////////////////////////////////////////
