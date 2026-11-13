
#include <cassert>

#include "main.h"

// Generator: files, see docs/DESIGN.md section 6.4 and docs/RUNTIME.md section 10.
// A file variable is its FIB; the runtime gets its address in R0. With {$I+} every I/O
// operation is followed by CALL IOCHK; with {$I-} the code stays in IORESV for IOresult.


//////////////////////////////////////////////////////////////////////

// Standard file variable name (Input, Output, Con, Trm, Kbd, Lst), empty for others
static string StdFileName(const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    if (node.symbol < 0 || g_program.symbols[node.symbol].kind != SymStdVar)
        return "";
    return g_program.symbols[node.symbol].name;
}

static bool IsConsoleFile(const string& name)
{
    return name == "Input" || name == "Output" || name == "Con" || name == "Trm";
}

// Kind and record length of a file type for FRESET/FREWRT: text 0/1, typed 1/size, untyped 2/128
static void GetFileKind(int type, int& kind, int& reclen)
{
    const TypeModel& model = Validator::GetType(type);
    if (Validator::IsTextType(type))
    {
        kind = 0; reclen = 1;
    }
    else if (model.elemtype < 0)
    {
        kind = 2; reclen = 128;
    }
    else
    {
        kind = 1; reclen = Validator::TypeSize(model.elemtype);
    }
}

void Generator::EmitIOCheck(const ExpressionNode& node)
{
    if ((node.token.directives & DirectiveI) != 0)
        EmitCall("IOCHK", "{$I+}");
}

// Assign, Reset, Rewrite, Close, Erase, Rename, Seek, Flush, BlockRead, BlockWrite
bool Generator::GenerateFileProcedure(const string& name, const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    const std::vector<ArgModel>& a = node.args;
    if (a.empty() || !Validator::IsFileType(expr.nodes[a[0].node].type))
        return false;
    if (!StdFileName(expr, a[0].node).empty())
        return false;  // Standard files are not assigned or opened
    std::vector<RuntimeArg> args;
    args.push_back(RuntimeArg(RuntimeArg::Address, a[0].node, 0));
    const char* routine = nullptr;
    if (name == "Assign" && a.size() == 2)
    {
        args.push_back(RuntimeArg(RuntimeArg::StringValue, a[1].node, 1));
        routine = "FASSGN";
    }
    else if ((name == "Reset" || name == "Rewrite") && a.size() == 1)
    {
        int kind, reclen;
        GetFileKind(expr.nodes[a[0].node].type, kind, reclen);
        args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 1, reclen));
        args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 2, kind));
        routine = (name == "Reset") ? "FRESET" : "FREWRT";
    }
    else if (name == "Close" && a.size() == 1)
        routine = "FCLOSE";
    else if (name == "Erase" && a.size() == 1)
        routine = "FERASE";
    else if (name == "Flush" && a.size() == 1)
        routine = "FFLUSH";
    else if (name == "Rename" && a.size() == 2)
    {
        args.push_back(RuntimeArg(RuntimeArg::StringValue, a[1].node, 1));
        routine = "FRENAM";
    }
    else if (name == "Seek" && a.size() == 2)
    {
        args.push_back(RuntimeArg(RuntimeArg::Value, a[1].node, 1));
        routine = "FSEEK";
    }
    else if ((name == "BlockRead" || name == "BlockWrite") && (a.size() == 3 || a.size() == 4))
    {
        args.push_back(RuntimeArg(RuntimeArg::Address, a[1].node, 1));
        args.push_back(RuntimeArg(RuntimeArg::Value, a[2].node, 2));
        if (a.size() == 4)
            args.push_back(RuntimeArg(RuntimeArg::Address, a[3].node, 3));
        else
            args.push_back(RuntimeArg(RuntimeArg::Immediate, -1, 3, 0));
        routine = (name == "BlockRead") ? "FBLKRD" : "FBLKWR";
    }
    if (routine == nullptr)
        return false;
    int buffers;
    if (!LoadRuntimeArgs(expr, args, buffers))
        return false;
    EmitCall(routine, name);
    FreeBuffers(buffers);
    EmitIOCheck(node);
    return true;
}

// FilePos, FileSize, IOresult, and Eof/Eoln/SeekEof/SeekEoln of a file; result in R0
bool Generator::GenerateFileFunction(const string& name, const ExpressionModel& expr, int index)
{
    const ExpressionNode& node = expr.nodes[index];
    if (name == "IOresult")
    {
        Emit("MOV", Operand::Label("IORESV"), Operand::Reg(0), "IOresult");
        Emit("CLR", Operand::Label("IORESV"));
        m_final->runtimeneeds.insert("IORESV");
        return true;
    }
    if (node.args.empty())
        return false;
    int file = node.args[0].node;
    if (!Validator::IsFileType(expr.nodes[file].type))
        return false;
    bool text = Validator::IsTextType(expr.nodes[file].type);
    string stdname = StdFileName(expr, file);
    if (name == "FilePos" || name == "FileSize")
    {
        if (!GenerateAddress(expr, file, 0))
            return false;
        EmitCall(name == "FilePos" ? "FPOS" : "FSIZE", name);
        return true;
    }
    if (!text)
    {
        if (name != "Eof")
            return false;
        if (!GenerateAddress(expr, file, 0))
            return false;
        EmitCall("FEOFT", "Eof");
        return true;
    }
    const char* routine = (name == "Eoln") ? "EOLN" : (name == "Eof") ? "EOFTX" :
        (name == "SeekEof") ? "SEEKEO" : (name == "SeekEoln") ? "SEEKEL" : nullptr;
    if (routine == nullptr)
        return false;
    if (IsConsoleFile(stdname))
    {
        EmitCall(name == "Eof" ? "EOLN" : routine, name);  // Console: the end of the input line
        return true;
    }
    if (!stdname.empty())
        return false;
    if (!GenerateAddress(expr, file, 0))
        return false;
    EmitCall("RDSETF");
    EmitCall(routine, name);
    EmitCall("RDREST");
    return true;
}

// Write and WriteLn to a file: a text file through WRSETF/WRREST, a typed file through FWRITE
bool Generator::GenerateWriteFile(const ExpressionModel& expr, int index, bool isln)
{
    const ExpressionNode& node = expr.nodes[index];
    const std::vector<ArgModel>& args = node.args;
    int file = args[0].node;
    string stdname = StdFileName(expr, file);
    if (IsConsoleFile(stdname))
    {
        for (size_t i = 1; i < args.size(); i++)
        {
            if (!GenerateWriteItem(expr, args[i]))
                EmitTodo("Write item " + expr.NodeToString(args[i].node));
        }
        if (isln)
            EmitCall("WREOL");
        return true;
    }
    if (!stdname.empty())
        return false;  // Lst and Kbd: later

    if (!Validator::IsTextType(expr.nodes[file].type))
    {
        // Typed file: every item is written as a component
        for (size_t i = 1; i < args.size(); i++)
        {
            int item = args[i].node;
            int type = expr.nodes[item].type;
            if (IsDesignator(expr, item) && expr.nodes[item].convert == ConvertNone)
            {
                std::vector<RuntimeArg> rtargs;
                rtargs.push_back(RuntimeArg(RuntimeArg::Address, file, 0));
                rtargs.push_back(RuntimeArg(RuntimeArg::Address, item, 1));
                int buffers;
                if (!LoadRuntimeArgs(expr, rtargs, buffers))
                    return false;
            }
            else if (Validator::IsRealType(type) || expr.nodes[item].convert == ConvertIntToReal)
            {
                if (!GenerateReal(expr, item))
                    return false;
                Emit("MOV", Operand::Reg(6), Operand::Reg(1), "the value on the stack");
                if (!GenerateAddress(expr, file, 0))
                    return false;
                EmitCall("FWRITE");
                Emit("CMP", Operand::AutoInc(6), Operand::AutoInc(6));
                continue;
            }
            else if (Validator::IsScalar(type))
            {
                if (!GenerateValue(expr, item))
                    return false;
                Emit("MOV", Operand::Reg(0), Operand::AutoDec(6), "the value on the stack");
                Emit("MOV", Operand::Reg(6), Operand::Reg(1));
                if (!GenerateAddress(expr, file, 0))
                    return false;
                EmitCall("FWRITE");
                Emit("TST", Operand::AutoInc(6));
                continue;
            }
            else
                return false;
            EmitCall("FWRITE");
        }
        EmitIOCheck(node);
        return true;
    }

    if (!GenerateAddress(expr, file, 0))
        return false;
    EmitCall("WRSETF", "Write to the file");
    for (size_t i = 1; i < args.size(); i++)
    {
        if (!GenerateWriteItem(expr, args[i]))
            EmitTodo("Write item " + expr.NodeToString(args[i].node));
    }
    if (isln)
        EmitCall("WREOL");
    EmitCall("WRREST");
    EmitIOCheck(node);
    return true;
}

// Read and ReadLn from a file: a text file through RDSETF/RDREST, a typed file through FREAD
bool Generator::GenerateReadFile(const ExpressionModel& expr, int index, bool isln)
{
    const ExpressionNode& node = expr.nodes[index];
    const std::vector<ArgModel>& args = node.args;
    int file = args[0].node;
    if (!Validator::IsTextType(expr.nodes[file].type))
    {
        for (size_t i = 1; i < args.size(); i++)
        {
            std::vector<RuntimeArg> rtargs;
            rtargs.push_back(RuntimeArg(RuntimeArg::Address, file, 0));
            rtargs.push_back(RuntimeArg(RuntimeArg::Address, args[i].node, 1));
            int buffers;
            if (!LoadRuntimeArgs(expr, rtargs, buffers))
                return false;
            EmitCall("FREAD");
        }
        EmitIOCheck(node);
        return true;
    }
    if (!GenerateAddress(expr, file, 0))
        return false;
    EmitCall("RDSETF", "Read from the file");
    if (!GenerateReadItems(expr, index, 1))
        return false;
    if (isln)
        EmitCall("RDLN", "ReadLn: to the next line");
    EmitCall("RDREST");
    EmitIOCheck(node);
    return true;
}


//////////////////////////////////////////////////////////////////////
