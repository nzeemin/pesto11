
#include <algorithm>
#include <cassert>

#include "main.h"

// Generator: strings, see docs/DESIGN.md section 7.6 and docs/RUNTIME.md section 6.
// A string value is either "simple" (a constant or a variable: its address is used as is), or it is
// built in a temporary 256-byte buffer on the stack: SUB #256., SP, the string at (SP).
// Buffers are freed in the order of nesting, by ADD #256.*n, SP after use.

static const int STRING_BUFFER = 256;


//////////////////////////////////////////////////////////////////////

// Bytes of a string constant, also of a Char constant used as a string
static string ConstStringBytes(const ExpressionNode& node)
{
    if (Validator::IsCharType(node.type))
        return string(1, (char)node.constvalue.ivalue);
    return node.constvalue.svalue;
}

// Maximum length of a string type
static int StringMaxLength(int type)
{
    const TypeModel& model = Validator::GetType(type);
    if (model.kind != TypeKindString || model.high <= 0 || model.high > 255)
        return 255;
    return (int)model.high;
}

// String expression with an address that needs no buffer: a constant or a string variable
bool Generator::IsSimpleString(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.isconst)
        return node.convert == ConvertNone || node.convert == ConvertCharToString;
    if (node.convert != ConvertNone)
        return false;
    return Validator::IsStringType(node.type) && IsDesignator(expr, index);
}

// Address of a simple string into the register
bool Generator::LoadStringAddress(const ExpressionModel& expr, int index, int reg)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.isconst)
    {
        Emit("MOV", Operand::ImmLabel(GetStringLabel(ConstStringBytes(node))), Operand::Reg(reg));
        return true;
    }
    Operand operand;
    if (GetVariableOperand(expr, index, operand))
    {
        LoadAddress(operand, reg);  // No code needed but the load itself
        return true;
    }
    if (reg == 1)
    {
        // The address code may use R1 itself
        if (!GenerateAddress(expr, index, 3))
            return false;
        Emit("MOV", Operand::Reg(3), Operand::Reg(1));
        return true;
    }
    return GenerateAddress(expr, index, reg);
}

void Generator::FreeBuffers(int buffers)
{
    if (buffers > 0)
        Emit("ADD", Operand::Imm(buffers * STRING_BUFFER), Operand::Reg(6), "free the string buffers");
}

// Load the arguments of a runtime call into their registers:
//  1. string values that are not simple are built in buffers, in order;
//  2. items that need code (addresses of computed variables, values of expressions) are computed and pushed;
//  3. they are popped into their registers;
//  4. the rest is loaded directly: constants, simple values, static addresses, buffer addresses.
// The caller frees the buffers after the call.
bool Generator::LoadRuntimeArgs(const ExpressionModel& mainexpr, std::vector<RuntimeArg>& args, int& buffers)
{
    buffers = 0;
    for (RuntimeArg& arg : args)
    {
        const ExpressionModel& expr = arg.model ? *arg.model : mainexpr;
        if (arg.kind == RuntimeArg::StringValue && !IsSimpleString(expr, arg.node))
        {
            if (!GenerateStringBuffer(expr, arg.node))
                return false;
            arg.buffer = buffers++;
        }
    }

    std::vector<size_t> pushed;
    for (size_t i = 0; i < args.size(); i++)
    {
        RuntimeArg& arg = args[i];
        const ExpressionModel& expr = arg.model ? *arg.model : mainexpr;
        Operand operand;
        bool isbyte;
        if (arg.buffer >= 0 || arg.kind == RuntimeArg::Immediate || arg.node < 0)
            continue;
        const ExpressionNode& node = expr.nodes[arg.node];
        if ((arg.kind == RuntimeArg::StringValue && !node.isconst) || arg.kind == RuntimeArg::Address)
        {
            if (GetVariableOperand(expr, arg.node, operand))
                continue;
            if (!GenerateAddress(expr, arg.node, 3))
                return false;
            Emit("MOV", Operand::Reg(3), Operand::AutoDec(6));
            pushed.push_back(i);
        }
        else if (arg.kind == RuntimeArg::Value && !GetSimpleOperand(expr, arg.node, operand, isbyte))
        {
            if (!GenerateValue(expr, arg.node))
                return false;
            Emit("MOV", Operand::Reg(0), Operand::AutoDec(6));
            pushed.push_back(i);
        }
    }
    for (size_t j = pushed.size(); j > 0; j--)
        Emit("MOV", Operand::AutoInc(6), Operand::Reg(args[pushed[j - 1]].reg));

    for (size_t i = 0; i < args.size(); i++)
    {
        RuntimeArg& arg = args[i];
        const ExpressionModel& expr = arg.model ? *arg.model : mainexpr;
        if (std::find(pushed.begin(), pushed.end(), i) != pushed.end())
            continue;
        Operand reg = Operand::Reg(arg.reg);
        if (arg.buffer >= 0)
        {
            Emit("MOV", Operand::Reg(6), reg, "string buffer");
            int offset = (buffers - 1 - arg.buffer) * STRING_BUFFER;
            if (offset != 0)
                Emit("ADD", Operand::Imm(offset), reg);
            continue;
        }
        switch (arg.kind)
        {
        case RuntimeArg::Immediate:
            if (arg.imm == 0)
                Emit("CLR", reg);
            else
                Emit("MOV", Operand::Imm(arg.imm), reg);
            break;
        case RuntimeArg::Value:
            if (arg.node < 0)
                Emit("MOV", Operand::Imm(-1), reg, "no width");
            else
                LoadOrdinal(expr, arg.node, arg.reg);
            break;
        case RuntimeArg::StringValue:
        case RuntimeArg::Address:
            {
                const ExpressionNode& node = expr.nodes[arg.node];
                if (node.isconst)
                {
                    Emit("MOV", Operand::ImmLabel(GetStringLabel(ConstStringBytes(node))), reg);
                    break;
                }
                Operand operand;
                if (!GetVariableOperand(expr, arg.node, operand))
                    return false;
                LoadAddress(operand, arg.reg);
            }
            break;
        }
    }
    return true;
}


//////////////////////////////////////////////////////////////////////
// String values in buffers

// Build the string value in a new buffer on the stack: SP goes down by 256, the string is at (SP)
bool Generator::GenerateStringBuffer(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.convert == ConvertCharToString)
    {
        // A string of one character
        if (!node.isconst && !GenerateValue(expr, index, true))
            return false;
        Emit("SUB", Operand::Imm(STRING_BUFFER), Operand::Reg(6), "string buffer");
        Emit("MOVB", Operand::Imm(1), Operand::Deferred(6), "length 1");
        if (node.isconst)
            Emit("MOVB", Operand::Imm(node.constvalue.ivalue & 0xFF), Operand::Index(6, 1));
        else
            Emit("MOVB", Operand::Reg(0), Operand::Index(6, 1));
        return true;
    }
    if (node.convert == ConvertArrayToString)
    {
        // Array of Char: its length is the size of the type
        int length = Validator::TypeSize(node.type);
        Emit("SUB", Operand::Imm(STRING_BUFFER), Operand::Reg(6), "string buffer");
        if (!GenerateAddress(expr, index, 3))
            return false;
        Emit("MOV", Operand::Reg(6), Operand::Reg(4));
        Emit("MOVB", Operand::Imm(length), Operand::AutoInc(4), "length");
        EmitBlockCopy(length, false);
        return true;
    }
    if (node.convert != ConvertNone || !Validator::IsStringType(node.type))
        return false;

    if (IsSimpleString(expr, index))
    {
        // A copy of a constant or a variable
        Emit("SUB", Operand::Imm(STRING_BUFFER), Operand::Reg(6), "string buffer");
        if (!LoadStringAddress(expr, index, 0))
            return false;
        Emit("MOV", Operand::Reg(6), Operand::Reg(1));
        Emit("MOV", Operand::Imm(255), Operand::Reg(2));
        EmitCall("STCP");
        return true;
    }
    if (node.kind == NodeBinary && node.token.IsSymbol(SymbolPlus))
    {
        if (!GenerateStringBuffer(expr, node.left))
            return false;
        return AppendString(expr, node.right);
    }
    if ((node.kind == NodeCall || node.kind == NodeIdent) && node.symbol >= 0)
    {
        const SymbolModel& symbol = g_program.symbols[node.symbol];
        if (symbol.kind == SymFunction)
            return GenerateUserCall(expr, index);
        if (symbol.kind == SymStdFunc)
        {
            string name = Validator::m_stdroutines[symbol.stdindex].name;
            if (name == "Concat" && node.args.size() >= 2)
            {
                if (!GenerateStringBuffer(expr, node.args[0].node))
                    return false;
                for (size_t i = 1; i < node.args.size(); i++)
                {
                    if (!AppendString(expr, node.args[i].node))
                        return false;
                }
                return true;
            }
            if (name == "Copy" && node.args.size() == 3)
            {
                // The source is copied into the buffer, COPY cuts it in place
                if (!GenerateStringBuffer(expr, node.args[0].node))
                    return false;
                std::vector<RuntimeArg> args;
                args.push_back(RuntimeArg(RuntimeArg::Value, node.args[1].node, 1));
                args.push_back(RuntimeArg(RuntimeArg::Value, node.args[2].node, 2));
                int buffers;
                if (!LoadRuntimeArgs(expr, args, buffers))
                    return false;
                Emit("MOV", Operand::Reg(6), Operand::Reg(0), "string buffer");
                EmitCall("COPY");
                return true;
            }
        }
    }
    return false;
}

// Append the string value to the buffer at (SP)
bool Generator::AppendString(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.convert == ConvertCharToString)
    {
        if (node.isconst)
            Emit("MOV", Operand::Imm(node.constvalue.ivalue & 0xFF), Operand::Reg(1));
        else
        {
            if (!GenerateValue(expr, index, true))
                return false;
            Emit("MOV", Operand::Reg(0), Operand::Reg(1));
        }
        Emit("MOV", Operand::Reg(6), Operand::Reg(0));
        EmitCall("STCATC", "append a character");
        return true;
    }
    if (IsSimpleString(expr, index))
    {
        if (!LoadStringAddress(expr, index, 1))
            return false;
        Emit("MOV", Operand::Reg(6), Operand::Reg(0));
        EmitCall("STCAT");
        return true;
    }
    if (node.kind == NodeBinary && node.token.IsSymbol(SymbolPlus) && node.convert == ConvertNone)
        return AppendString(expr, node.left) && AppendString(expr, node.right);

    // Any other value: build it in its own buffer, append, free
    if (!GenerateStringBuffer(expr, index))
        return false;
    Emit("MOV", Operand::Reg(6), Operand::Reg(1));
    Emit("MOV", Operand::Reg(6), Operand::Reg(0));
    Emit("ADD", Operand::Imm(STRING_BUFFER), Operand::Reg(0));
    EmitCall("STCAT");
    FreeBuffers(1);
    return true;
}


//////////////////////////////////////////////////////////////////////
// Statements and conditions

// S := string; C := string (length must be 1); array of Char := string constant
bool Generator::GenerateStringAssign(StatementModel& statement)
{
    const ExpressionModel& target = statement.target;
    const ExpressionModel& value = statement.expr;
    int type = target.nodes[target.root].type;
    int buffers;

    if (Validator::IsCharArray(type))
    {
        const ExpressionNode& valuenode = value.nodes[value.root];
        if (!valuenode.isconst)
            return false;
        int length = Validator::TypeSize(type);
        if (!GenerateAddress(target, target.root, 3))
            return false;
        Emit("MOV", Operand::Reg(3), Operand::Reg(4));
        Emit("MOV", Operand::ImmLabel(GetStringLabel(ConstStringBytes(valuenode)), 1), Operand::Reg(3));
        EmitBlockCopy(length, false);
        return true;
    }
    if (Validator::IsCharType(type))
    {
        std::vector<RuntimeArg> args;
        args.push_back(RuntimeArg(RuntimeArg::StringValue, value.root, 0));
        if (!LoadRuntimeArgs(value, args, buffers))
            return false;
        EmitCall("STRCH", "string of length 1, error 10 otherwise");
        FreeBuffers(buffers);
        return StoreOrdinal(target, target.root, statement.token.pos);
    }

    std::vector<RuntimeArg> args;
    args.push_back(RuntimeArg(RuntimeArg::StringValue, value.root, 0));
    args.push_back(RuntimeArg(RuntimeArg::Address, target.root, 1));
    args.back().model = &target;
    args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 2, StringMaxLength(type)));
    if (!LoadRuntimeArgs(value, args, buffers))
        return false;
    EmitCall("STCP", "copy with the length limit");
    FreeBuffers(buffers);
    return true;
}

// Comparison of strings: STCMP gives R0 = -1/0/1 and the flags of TST R0
bool Generator::GenerateStringCompare(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue)
{
    const ExpressionNode& node = expr.nodes[index];
    std::vector<RuntimeArg> args;
    args.push_back(RuntimeArg(RuntimeArg::StringValue, node.left, 0));
    args.push_back(RuntimeArg(RuntimeArg::StringValue, node.right, 1));
    int buffers;
    if (!LoadRuntimeArgs(expr, args, buffers))
        return false;
    EmitCall("STCMP");
    if (buffers > 0)
    {
        FreeBuffers(buffers);
        Emit("TST", Operand::Reg(0));
    }
    EmitBranch(GetCompareBranch(node.token, false, jumpiftrue), label);
    return true;
}


//////////////////////////////////////////////////////////////////////
// Standard functions and procedures

// Length, Pos: Integer result in R0
bool Generator::GenerateStringFunction(const string& name, const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    int buffers;
    if (name == "Length" && node.args.size() == 1)
    {
        int arg = node.args[0].node;
        const ExpressionNode& argnode = expr.nodes[arg];
        if (argnode.convert == ConvertNone && !argnode.isconst && IsSimpleString(expr, arg))
        {
            Operand operand;
            if (!GenerateDesignator(expr, arg, 3, operand))
                return false;
            Emit("CLR", Operand::Reg(0));
            Emit("BISB", operand, Operand::Reg(0), "length byte");
            return true;
        }
        if (!GenerateStringBuffer(expr, arg))
            return false;
        Emit("CLR", Operand::Reg(0));
        Emit("BISB", Operand::Deferred(6), Operand::Reg(0), "length byte");
        FreeBuffers(1);
        return true;
    }
    if (name == "Pos" && node.args.size() == 2)
    {
        std::vector<RuntimeArg> args;
        args.push_back(RuntimeArg(RuntimeArg::StringValue, node.args[0].node, 0));
        args.push_back(RuntimeArg(RuntimeArg::StringValue, node.args[1].node, 1));
        if (!LoadRuntimeArgs(expr, args, buffers))
            return false;
        EmitCall("POS");
        FreeBuffers(buffers);
        return true;
    }
    return false;
}

// Insert, Delete, Str, Val
bool Generator::GenerateStringProcedure(const string& name, const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    const std::vector<ArgModel>& a = node.args;
    std::vector<RuntimeArg> args;
    int buffers;
    if (name == "Insert" && a.size() == 3)
    {
        args.push_back(RuntimeArg(RuntimeArg::StringValue, a[0].node, 0));
        args.push_back(RuntimeArg(RuntimeArg::Address, a[1].node, 1));
        args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 2, StringMaxLength(expr.nodes[a[1].node].type)));
        args.push_back(RuntimeArg(RuntimeArg::Value, a[2].node, 3));
        if (!LoadRuntimeArgs(expr, args, buffers))
            return false;
        EmitCall("INSERT");
        FreeBuffers(buffers);
        return true;
    }
    if (name == "Delete" && a.size() == 3)
    {
        args.push_back(RuntimeArg(RuntimeArg::Address, a[0].node, 0));
        args.push_back(RuntimeArg(RuntimeArg::Value, a[1].node, 1));
        args.push_back(RuntimeArg(RuntimeArg::Value, a[2].node, 2));
        if (!LoadRuntimeArgs(expr, args, buffers))
            return false;
        EmitCall("DELETE");
        return true;
    }
    if (name == "Str" && a.size() == 2)
    {
        if (!Validator::IsIntegerType(expr.nodes[a[0].node].type))
            return false;  // Real: step 7
        args.push_back(RuntimeArg(RuntimeArg::Value, a[0].node, 0));
        args.push_back(RuntimeArg(RuntimeArg::Value, a[0].width, 1));
        args.push_back(RuntimeArg(RuntimeArg::Address, a[1].node, 2));
        args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 3, StringMaxLength(expr.nodes[a[1].node].type)));
        if (!LoadRuntimeArgs(expr, args, buffers))
            return false;
        EmitCall("STRI");
        return true;
    }
    if (name == "Val" && a.size() == 3)
    {
        if (!Validator::IsIntegerType(expr.nodes[a[1].node].type))
            return false;  // Real: step 7
        args.push_back(RuntimeArg(RuntimeArg::StringValue, a[0].node, 0));
        if (!LoadRuntimeArgs(expr, args, buffers))
            return false;
        EmitCall("VALI", "R0 = value, R1 = error position");
        FreeBuffers(buffers);
        Emit("MOV", Operand::Reg(1), Operand::AutoDec(6), "error position");
        if (!StoreOrdinal(expr, a[1].node, node.token.pos))
            return false;
        Emit("MOV", Operand::AutoInc(6), Operand::Reg(0));
        return StoreOrdinal(expr, a[2].node, node.token.pos);
    }
    return false;
}


//////////////////////////////////////////////////////////////////////
