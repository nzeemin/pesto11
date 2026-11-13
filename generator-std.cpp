
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
            if (!LoadOrdinal(expr, arg.width, 1))
                return false;
            Emit("MOV", Operand::ImmLabel(label), Operand::Reg(0));
            EmitCall("WRSTRW");
            return true;
        }
        if (!IsDesignator(expr, arg.node) || !LoadWithWidth(expr, arg, true))
            return false;  // String expressions: step 5
        EmitCall(haswidth ? "WRSTRW" : "WRSTR");
        return true;
    }

    if (Validator::IsCharType(type))
    {
        if (!LoadWithWidth(expr, arg, false))
            return false;
        EmitCall(haswidth ? "WRCHW" : "WRCH");
        return true;
    }

    if (Validator::IsBooleanType(type))
    {
        if (!LoadWithWidth(expr, arg, false))
            return false;
        if (!haswidth)
            Emit("MOV", Operand::Imm(-1), Operand::Reg(1), "no width");
        EmitCall("WRBOOL");
        return true;
    }

    if (Validator::IsIntegerType(type))
    {
        if (!LoadWithWidth(expr, arg, false))
            return false;
        EmitCall(haswidth ? "WRINTW" : "WRINT");
        return true;
    }

    return false;  // Real and the rest: later steps
}


// Value (or the address of a variable) into R0, the width into R1 when given.
// A width that needs code is computed first and kept on the stack.
bool Generator::LoadWithWidth(const ExpressionModel& expr, const ArgModel& arg, bool isaddress)
{
    Operand operand;
    bool isbyte;
    bool pushwidth = arg.width >= 0 && !GetSimpleOperand(expr, arg.width, operand, isbyte);
    if (pushwidth)
    {
        if (!GenerateValue(expr, arg.width))
            return false;
        Emit("MOV", Operand::Reg(0), Operand::AutoDec(6), "width");
    }
    if (isaddress ? !GenerateAddress(expr, arg.node, 0) : !LoadOrdinal(expr, arg.node, 0))
        return false;
    if (pushwidth)
        Emit("MOV", Operand::AutoInc(6), Operand::Reg(1));
    else if (arg.width >= 0)
        LoadOrdinal(expr, arg.width, 1);
    return true;
}


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

    if (symbol.kind != SymStdFunc)
        return false;
    string name = Validator::m_stdroutines[symbol.stdindex].name;
    if (node.args.empty() && (name == "MemAvail" || name == "MaxAvail"))
    {
        EmitCall(name == "MemAvail" ? "MEMAVL" : "MAXAVL", name);
        return true;
    }
    if (node.args.size() != 1)
        return false;
    int arg = node.args[0].node;
    if (name == "Addr")
    {
        const ExpressionNode& argnode = expr.nodes[arg];
        if (argnode.symbol >= 0 && (g_program.symbols[argnode.symbol].kind == SymProcedure ||
            g_program.symbols[argnode.symbol].kind == SymFunction))
        {
            const ProcedureModel& proc = g_program.procedures[g_program.symbols[argnode.symbol].procindex];
            if (proc.directive == ProcDirExternal)
            {
                int64_t address = proc.external.IsEmpty() ? 0 : proc.external.nodes[proc.external.root].constvalue.ivalue;
                Emit("MOV", Operand::Imm(address & 0xFFFF), Operand::Reg(0));
            }
            else
                Emit("MOV", Operand::ImmLabel(proc.asmlabel), Operand::Reg(0), "Addr(" + proc.name.text + ")");
            return true;
        }
        return IsDesignator(expr, arg) && GenerateAddress(expr, arg, 0);
    }
    if (!Validator::IsScalar(expr.nodes[arg].type))
        return false;  // Real argument (Trunc, Round...): step 7
    if (!GenerateValue(expr, arg))
        return false;

    if (name == "Ord" || name == "Chr" || name == "Ptr")
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


//////////////////////////////////////////////////////////////////////
// Heap and memory procedures, see docs/RUNTIME.md sections 11 and 12

// Size of the variable a pointer points to
static int PointerTargetSize(int pointertype)
{
    return Validator::TypeSize(Validator::GetType(pointertype).elemtype);
}

bool Generator::GenerateStdProcedure(const string& name, const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    const std::vector<ArgModel>& args = node.args;
    Operand operand;
    bool isbyte;

    if ((name == "New" || name == "Mark") && args.size() == 1)
    {
        int pointer = args[0].node;
        if (name == "New")
        {
            Emit("MOV", Operand::Imm(PointerTargetSize(expr.nodes[pointer].type)), Operand::Reg(0), "size");
            EmitCall("NEW");
        }
        else
        {
            Emit("MOV", Operand::Label("HEAPPT"), Operand::Reg(0), "HeapPtr");
            m_final->runtimeneeds.insert("HEAPPT");
        }
        return StoreOrdinal(expr, pointer, node.token.pos);
    }
    if (name == "GetMem" && args.size() == 2)
    {
        if (!LoadOrdinal(expr, args[1].node, 0))
            return false;
        EmitCall("NEW");
        return StoreOrdinal(expr, args[0].node, node.token.pos);
    }
    if (name == "Dispose" && args.size() == 1)
    {
        if (!LoadOrdinal(expr, args[0].node, 0))
            return false;
        Emit("MOV", Operand::Imm(PointerTargetSize(expr.nodes[args[0].node].type)), Operand::Reg(1), "size");
        EmitCall("DISPOS");
        return true;
    }
    if (name == "FreeMem" && args.size() == 2)
    {
        bool simplesize = GetSimpleOperand(expr, args[1].node, operand, isbyte);
        if (!simplesize)
        {
            if (!GenerateValue(expr, args[1].node))
                return false;
            Emit("MOV", Operand::Reg(0), Operand::AutoDec(6), "size");
        }
        if (!LoadOrdinal(expr, args[0].node, 0))
            return false;
        if (simplesize)
            LoadOrdinal(expr, args[1].node, 1);
        else
            Emit("MOV", Operand::AutoInc(6), Operand::Reg(1));
        EmitCall("DISPOS");
        return true;
    }
    if (name == "Release" && args.size() == 1)
    {
        if (!LoadOrdinal(expr, args[0].node, 0))
            return false;
        EmitCall("RELEAS");
        return true;
    }

    // FillChar(var X; Count: Integer; Value): R0 = address, R1 = count, R2 = value
    // Move(var Source, Dest; Count: Integer): R0 = from, R1 = to, R2 = count
    // Arguments that need code are computed first and kept on the stack.
    if ((name == "FillChar" || name == "Move") && args.size() == 3)
    {
        bool isfill = (name == "FillChar");
        int third = args[2].node;
        bool simplethird = GetSimpleOperand(expr, third, operand, isbyte);
        if (!simplethird)
        {
            if (!GenerateValue(expr, third))
                return false;
            Emit("MOV", Operand::Reg(0), Operand::AutoDec(6));
        }
        if (isfill)
        {
            int count = args[1].node;
            bool simplecount = GetSimpleOperand(expr, count, operand, isbyte);
            if (!simplecount)
            {
                if (!GenerateValue(expr, count))
                    return false;
                Emit("MOV", Operand::Reg(0), Operand::AutoDec(6), "count");
            }
            if (!GenerateAddress(expr, args[0].node, 0))
                return false;
            if (simplecount)
                LoadOrdinal(expr, count, 1);
            else
                Emit("MOV", Operand::AutoInc(6), Operand::Reg(1));
        }
        else
        {
            if (!GenerateAddress(expr, args[0].node, 3))
                return false;
            Emit("MOV", Operand::Reg(3), Operand::AutoDec(6), "source");
            if (!GenerateAddress(expr, args[1].node, 3))
                return false;
            Emit("MOV", Operand::Reg(3), Operand::Reg(1), "destination");
            Emit("MOV", Operand::AutoInc(6), Operand::Reg(0));
        }
        if (simplethird)
            LoadOrdinal(expr, third, 2);
        else
            Emit("MOV", Operand::AutoInc(6), Operand::Reg(2));
        EmitCall(isfill ? "FILLCH" : "MOVEM");
        return true;
    }
    return false;
}


//////////////////////////////////////////////////////////////////////
