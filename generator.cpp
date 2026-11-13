
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "main.h"

// Generator: program and procedures, data, output files. See docs/DESIGN.md section 4.11.
// Statements are in generator-stmt.cpp, expressions in generator-expr.cpp,
// standard procedures in generator-std.cpp.


//////////////////////////////////////////////////////////////////////

static string MakeLabel(char prefix, int number)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%c%05d", prefix, number);
    return buffer;
}

Generator::Generator(FinalModel* final, const RuntimeGenerator* runtime) :
    m_final(final), m_runtime(runtime), m_code(nullptr), m_procindex(0),
    m_labelcount(0), m_gotocount(0), m_stringcount(0), m_lastfile(-1), m_lastline(0)
{
    assert(final != nullptr);
    assert(runtime != nullptr);
}


//////////////////////////////////////////////////////////////////////
// Emitting lines

void Generator::Emit(const string& opcode, const Operand& src, const Operand& dst, const string& comment)
{
    CodeLine line;
    line.kind = LineInstruction;
    line.opcode = opcode;
    line.src = src;
    line.dst = dst;
    line.comment = comment;
    line.words = 1 + src.ExtraWords() + dst.ExtraWords();
    m_code->lines.push_back(line);
}

void Generator::EmitLabel(const string& label)
{
    CodeLine line;
    line.kind = LineLabel;
    line.label = label;
    m_code->lines.push_back(line);
}

void Generator::EmitComment(const string& comment)
{
    CodeLine line;
    line.kind = LineComment;
    line.comment = comment;
    m_code->lines.push_back(line);
}

void Generator::EmitData(const string& opcode, const string& text, int words, const string& comment)
{
    CodeLine line;
    line.kind = LineData;
    line.opcode = opcode;
    line.text = text;
    line.words = words;
    line.comment = comment;
    m_code->lines.push_back(line);
}

void Generator::EmitCall(const string& runtimename, const string& comment)
{
    Emit("CALL", Operand::Label(runtimename), Operand(), comment);
    m_final->runtimeneeds.insert(runtimename);
}

// A construct the generator cannot handle yet: the TODO comment makes the test fail
void Generator::EmitTodo(const string& what)
{
    EmitComment("TODO: " + what);
}

// Source line as a comment before the code of a statement
void Generator::EmitSourceLine(const SourcePos& pos)
{
    if (pos.file < 0 || pos.line <= 0)
        return;
    if (pos.file == m_lastfile && pos.line == m_lastline)
        return;
    m_lastfile = pos.file;
    m_lastline = pos.line;
    if (pos.file >= (int)g_sourcelines.size() || pos.line > (int)g_sourcelines[pos.file].size())
        return;
    string text = g_sourcelines[pos.file][pos.line - 1];
    size_t first = text.find_first_not_of(" \t");
    text = (first == string::npos) ? "" : text.substr(first);
    EmitComment(text);
}

// Lines of a runtime block copied into the program, like INIT and TERM
void Generator::EmitTemplateBlock(const string& name)
{
    const RuntimeBlock* block = m_runtime->FindBlock(name);
    if (block == nullptr)
    {
        ReportError("runtime block not found in the template: " + name);
        return;
    }
    for (const string& text : block->lines)
    {
        CodeLine line;
        line.kind = LineRaw;
        line.text = text;
        line.words = (text.empty() || text[0] == ';') ? 0 : 3;  // Estimate: no branch crosses these blocks
        m_code->lines.push_back(line);
    }
    for (const string& need : block->needs)
        m_final->runtimeneeds.insert(need);
}

void Generator::EmitBranch(const string& opcode, const string& label, const string& comment)
{
    CodeLine line;
    line.kind = LineBranch;
    line.opcode = opcode;
    line.text = label;
    line.target = label;
    line.comment = comment;
    line.words = 1;
    m_code->lines.push_back(line);
}

string Generator::NewLabel()
{
    return MakeLabel('L', ++m_labelcount);
}

string Generator::GetGotoLabel(int symbol)
{
    auto it = m_gotolabels.find(symbol);
    if (it != m_gotolabels.end())
        return it->second;
    string label = MakeLabel('G', ++m_gotocount);
    m_gotolabels[symbol] = label;
    return label;
}

// String constants are kept once each, in TP3 format: length byte and characters
string Generator::GetStringLabel(const string& bytes)
{
    auto it = m_strings.find(bytes);
    if (it != m_strings.end())
        return it->second;
    string label = MakeLabel('K', ++m_stringcount);
    m_strings[bytes] = label;
    m_stringlist.push_back(std::make_pair(label, bytes));
    return label;
}


//////////////////////////////////////////////////////////////////////
// Program and procedures

void Generator::GenerateProgram()
{
    GenerateProcedure(0);
    for (int i = 1; i < (int)g_program.procedures.size(); i++)
    {
        const ProcedureModel& proc = g_program.procedures[i];
        if (proc.directive == ProcDirNone)
            GenerateProcedure(i);
    }
    GenerateData();
}

void Generator::GenerateProcedure(int procindex)
{
    m_final->procedures.push_back(ProcedureCode());
    m_code = &m_final->procedures.back();
    m_code->procindex = procindex;
    m_procindex = procindex;
    ProcedureModel& proc = g_program.procedures[procindex];

    if (procindex == 0)
    {
        if (g_program.name.type != TokenTypeNone)
            EmitSourceLine(g_program.name.pos);
        EmitLabel("START:");
        EmitTemplateBlock("INIT");
        GenerateStatement(proc.block.body);
        EmitTemplateBlock("TERM");
        LayoutBranches(*m_code);
        return;
    }

    EmitSourceLine(proc.name.pos);
    EmitLabel(proc.asmlabel);

    // Value parameters of strings, arrays, records and sets are copied into the frame: steps 4-6
    for (const ParamGroupModel& group : proc.params)
    {
        if (!group.isvar && group.paramtype >= 0 && !Validator::IsOrdinal(group.paramtype))
            EmitTodo("value parameter of type " + g_program.TypeToString(group.paramtype));
    }

    // The frame is needed for parameters, locals, the static link, or for nested procedures
    bool haschildren = false;
    for (const ProcedureModel& other : g_program.procedures)
    {
        if (other.parent == procindex)
            haschildren = true;
    }
    bool needframe = proc.paramsize > 0 || proc.localsize > 0 || proc.nested || haschildren;

    if (needframe)
    {
        Emit("MOV", Operand::Reg(5), Operand::AutoDec(6), "save the caller frame");
        Emit("MOV", Operand::Reg(6), Operand::Reg(5), "new frame");
        int locals = proc.localsize;
        if (proc.nested)
        {
            Emit("MOV", Operand::Reg(4), Operand::AutoDec(6), "static link -> -2(R5)");
            locals -= 2;
        }
        if (locals == 2)
            Emit("TST", Operand::AutoDec(6), Operand(), "locals");
        else if (locals == 4)
            Emit("CMP", Operand::AutoDec(6), Operand::AutoDec(6), "locals");
        else if (locals > 0)
            Emit("SUB", Operand::Imm(locals), Operand::Reg(6), "locals");
    }

    m_exitlabel = NewLabel();
    GenerateStatement(proc.block.body);
    EmitLabel(m_exitlabel);

    // Function result into R0
    if (proc.kind == ProcKindFunction && proc.resultsymbol >= 0)
    {
        const SymbolModel& result = g_program.symbols[proc.resultsymbol];
        if (!Validator::IsOrdinal(result.type))
            EmitTodo("function result of type " + g_program.TypeToString(result.type));
        else if (Validator::TypeSize(result.type) == 1)
        {
            Emit("CLR", Operand::Reg(0));
            Emit("BISB", Operand::Index(5, result.location.offset), Operand::Reg(0), "result");
        }
        else
            Emit("MOV", Operand::Index(5, result.location.offset), Operand::Reg(0), "result");
    }

    // Epilogue: restore the caller frame, pop the parameters (DESIGN 7.3)
    if (needframe)
    {
        Emit("MOV", Operand::Reg(5), Operand::Reg(6));
        Emit("MOV", Operand::AutoInc(6), Operand::Reg(5));
    }
    int params = proc.paramsize;
    if (params == 2)
        Emit("MOV", Operand::AutoInc(6), Operand::Deferred(6), "return address over the parameter");
    else if (params > 0)
    {
        Emit("MOV", Operand::Deferred(6), Operand::Index(6, params), "return address over the parameters");
        Emit("ADD", Operand::Imm(params), Operand::Reg(6));
    }
    Emit("RETURN");
    LayoutBranches(*m_code);
}


//////////////////////////////////////////////////////////////////////
// Data

static string FormatBytes(const string& bytes, size_t from, size_t count)
{
    string result;
    for (size_t i = from; i < from + count && i < bytes.size(); i++)
    {
        if (i > from)
            result += ", ";
        result += FormatNumber((unsigned char)bytes[i]);
    }
    return result;
}

// .BYTE lines, 16 values per line
static void AddByteLines(std::vector<CodeLine>& lines, const string& label, const string& bytes, const string& comment)
{
    for (size_t i = 0; i < bytes.size(); i += 16)
    {
        CodeLine line;
        line.kind = LineData;
        line.label = (i == 0) ? label : "";
        line.opcode = ".BYTE";
        line.text = FormatBytes(bytes, i, 16);
        line.comment = (i == 0) ? comment : "";
        lines.push_back(line);
    }
}

static CodeLine MakeDataLine(const string& label, const string& opcode, const string& text, const string& comment = "")
{
    CodeLine line;
    line.kind = LineData;
    line.label = label;
    line.opcode = opcode;
    line.text = text;
    line.comment = comment;
    return line;
}

void Generator::CollectTempSlots(const StatementModel& statement, std::vector<TempSlot>& slots)
{
    if (!statement.forlimit.asmlabel.empty())
        slots.push_back(statement.forlimit);
    for (const TempSlot& slot : statement.withslots)
    {
        if (!slot.asmlabel.empty())
            slots.push_back(slot);
    }
    for (const StatementModel& child : statement.statements)
        CollectTempSlots(child, slots);
    for (const CaseElementModel& element : statement.caseelements)
    {
        for (const StatementModel& child : element.statement)
            CollectTempSlots(child, slots);
    }
}

void Generator::GenerateData()
{
    std::vector<CodeLine>& data = m_final->data;

    // String constants
    for (const auto& item : m_stringlist)
    {
        string bytes = string(1, (char)item.second.size()) + item.second;
        AddByteLines(data, item.first, bytes, "");
    }
    if (!m_stringlist.empty())
        data.push_back(MakeDataLine("", ".EVEN", ""));

    // Typed constants, from the declarations of all blocks
    for (const ProcedureModel& proc : g_program.procedures)
    {
        for (const DeclarationModel& decl : proc.block.declarations)
        {
            if (decl.kind != DeclTypedConst || decl.symbols.empty())
                continue;
            GenerateTypedConst(g_program.symbols[decl.symbols[0]], decl.initvalue);
        }
    }

    // Global variables: each takes whole words
    for (const SymbolModel& symbol : g_program.symbols)
    {
        if (symbol.kind != SymVar || symbol.location.asmlabel.empty())
            continue;
        int size = Validator::TypeSize(symbol.type);
        int words = (size + 1) / 2;
        if (words < 1)
            words = 1;
        data.push_back(MakeDataLine(symbol.location.asmlabel, ".BLKW", FormatNumber(words),
                symbol.name + ": " + g_program.TypeToString(symbol.type)));
    }

    // Temporaries of the main program statements
    std::vector<TempSlot> slots;
    CollectTempSlots(g_program.procedures[0].block.body, slots);
    for (const TempSlot& slot : slots)
        data.push_back(MakeDataLine(slot.asmlabel, ".BLKW", "1", "temporary"));
}

void Generator::GenerateTypedConst(const SymbolModel& symbol, const StructConstModel& value)
{
    std::vector<CodeLine>& data = m_final->data;
    const string& label = symbol.location.asmlabel;
    string comment = symbol.name + ": " + g_program.TypeToString(symbol.type);
    int type = symbol.type;
    int size = Validator::TypeSize(type);

    if (value.kind != StructConstModel::ConstSimple || value.value.root < 0)
    {
        data.push_back(MakeDataLine(label, ".BLKW", FormatNumber((size + 1) / 2), "TODO: structured typed constant " + comment));
        return;
    }
    const ExpressionNode& node = value.value.nodes[value.value.root];
    if (!node.isconst)
        return;

    if (Validator::IsOrdinal(type))
    {
        int64_t v = node.constvalue.ivalue;
        if (size == 1)
        {
            data.push_back(MakeDataLine(label, ".BYTE", FormatNumber(v & 0xFF), comment));
            data.push_back(MakeDataLine("", ".EVEN", ""));
        }
        else
            data.push_back(MakeDataLine(label, ".WORD", FormatNumber((int16_t)(v & 0xFFFF)), comment));
        return;
    }

    string bytes = Validator::IsCharType(node.type) ? string(1, (char)node.constvalue.ivalue) : node.constvalue.svalue;
    if (Validator::IsStringType(type))
    {
        int64_t maxlength = Validator::GetType(type).high;
        if ((int64_t)bytes.size() > maxlength)
            bytes.resize((size_t)maxlength);
        AddByteLines(data, label, string(1, (char)bytes.size()) + bytes, comment);
        int64_t rest = maxlength - (int64_t)bytes.size();
        if (rest > 0)
            data.push_back(MakeDataLine("", ".BLKB", FormatNumber(rest)));
        data.push_back(MakeDataLine("", ".EVEN", ""));
        return;
    }
    if (Validator::IsCharArray(type))
    {
        AddByteLines(data, label, bytes, comment);
        data.push_back(MakeDataLine("", ".EVEN", ""));
        return;
    }
    data.push_back(MakeDataLine(label, ".BLKW", FormatNumber((size + 1) / 2), "TODO: typed constant " + comment));
}


//////////////////////////////////////////////////////////////////////
// Output files

static void WriteLine(std::ostream& out, const string& line)
{
    out << line << "\r\n";  // .MAC files always have CR/LF line ends
}

static void WriteRuntimeBlocks(std::ostream& out, const std::vector<const RuntimeBlock*>& blocks)
{
    for (const RuntimeBlock* block : blocks)
    {
        if (block->name == "INIT" || block->name == "TERM")
            continue;
        WriteLine(out, "");
        WriteLine(out, ";------------------------------------------------------------------------------");
        WriteLine(out, "\t.GLOBL\t" + block->name);
        for (const string& line : block->lines)
            WriteLine(out, line);
    }
}

bool WriteAssemblyFiles(const FinalModel& final, const RuntimeGenerator& runtime,
        const string& outfilename, const string& rtfilename, const string& title)
{
    std::vector<const RuntimeBlock*> blocks = runtime.CollectBlocks(final.runtimeneeds);
    bool hasruntime = false;
    for (const RuntimeBlock* block : blocks)
    {
        if (block->name != "INIT" && block->name != "TERM")
            hasruntime = true;
    }

    std::ofstream out(outfilename, std::ios::out | std::ios::binary);
    if (!out)
    {
        ReportError("cannot write file " + outfilename);
        return false;
    }

    WriteLine(out, "; Generated by Pesto/11 " + string(PESTO11_VERSION) + " from " + g_sourcefiles[0]);
    WriteLine(out, "\t.TITLE\t" + title);
    if (!g_onefile && !final.runtimeneeds.empty())
    {
        string globals;
        for (const string& name : final.runtimeneeds)
            globals += (globals.empty() ? "" : ", ") + name;
        WriteLine(out, "\t.GLOBL\t" + globals);
    }
    for (const ProcedureCode& code : final.procedures)
    {
        WriteLine(out, "");
        for (const CodeLine& line : code.lines)
        {
            WriteLine(out, line.ToString());
            if (g_showgeneration)
                std::cout << line.ToString() << std::endl;
        }
    }
    WriteLine(out, "");
    for (const CodeLine& line : final.data)
    {
        WriteLine(out, line.ToString());
        if (g_showgeneration)
            std::cout << line.ToString() << std::endl;
    }
    // PEND marks the end of the whole program and the start of the heap: it goes
    // after the runtime, so with a separate runtime file it is at the end of that file
    bool separateruntime = !g_onefile && hasruntime;
    if (g_onefile)
        WriteRuntimeBlocks(out, blocks);
    WriteLine(out, "");
    if (!separateruntime)
        WriteLine(out, "PEND::\t\t\t\t; end of the program, start of the heap");
    WriteLine(out, "\t.END\tSTART");
    out.close();

    if (!separateruntime)
        return true;

    std::ofstream rt(rtfilename, std::ios::out | std::ios::binary);
    if (!rt)
    {
        ReportError("cannot write file " + rtfilename);
        return false;
    }
    WriteLine(rt, "; Pesto/11 runtime for " + g_sourcefiles[0]);
    WriteLine(rt, "\t.TITLE\tPESTO");
    WriteRuntimeBlocks(rt, blocks);
    WriteLine(rt, "");
    WriteLine(rt, "PEND::\t\t\t\t; end of the program, start of the heap");
    WriteLine(rt, "\t.END");
    return true;
}


//////////////////////////////////////////////////////////////////////
