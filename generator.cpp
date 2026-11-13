
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
    m_labelcount(0), m_gotocount(0), m_stringcount(0), m_lastfile(-1), m_lastline(0), m_setrangetype(-1)
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


// Raw data (set images) are kept once each, word-aligned
string Generator::GetDataLabel(const string& bytes)
{
    auto it = m_datas.find(bytes);
    if (it != m_datas.end())
        return it->second;
    string label = MakeLabel('K', ++m_stringcount);
    m_datas[bytes] = label;
    m_datalist.push_back(std::make_pair(label, bytes));
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

    for (const ParamGroupModel& group : proc.params)
    {
        if (!group.isvar && group.paramtype >= 0 && !Validator::IsScalar(group.paramtype) &&
            !Validator::IsArrayType(group.paramtype) && !Validator::IsRecordType(group.paramtype) &&
            !Validator::IsStringType(group.paramtype) && !Validator::IsSetType(group.paramtype))
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

    // Array and record value parameters: the caller passed the address, the copy goes into the frame
    for (const SymbolModel& symbol : g_program.symbols)
    {
        if (symbol.kind != SymParam || symbol.scope != proc.scope || symbol.paramoffset == 0)
            continue;
        if (Validator::IsStringType(symbol.type))
        {
            Emit("MOV", Operand::Index(5, symbol.paramoffset), Operand::Reg(0), "copy of " + symbol.name);
            Emit("MOV", Operand::Reg(5), Operand::Reg(1));
            Emit("SUB", Operand::Imm(-symbol.location.offset), Operand::Reg(1));
            int64_t maxlength = Validator::GetType(symbol.type).high;
            Emit("MOV", Operand::Imm(maxlength > 0 && maxlength <= 255 ? maxlength : 255), Operand::Reg(2));
            EmitCall("STCP");
            continue;
        }
        if (Validator::IsSetType(symbol.type))
        {
            // The caller passed an expanded 32-byte set: compress it into the copy
            const TypeModel& model = Validator::GetType(symbol.type);
            Emit("MOV", Operand::Index(5, symbol.paramoffset), Operand::Reg(0), "copy of " + symbol.name);
            Emit("MOV", Operand::Reg(5), Operand::Reg(1));
            Emit("SUB", Operand::Imm(-symbol.location.offset), Operand::Reg(1));
            Emit("MOV", Operand::Imm(model.low / 8), Operand::Reg(2));
            Emit("MOV", Operand::Imm(Validator::TypeSize(symbol.type)), Operand::Reg(3));
            EmitCall("SETST");
            continue;
        }
        if (!Validator::IsArrayType(symbol.type) && !Validator::IsRecordType(symbol.type))
            continue;
        Emit("MOV", Operand::Index(5, symbol.paramoffset), Operand::Reg(3), "copy of " + symbol.name);
        Emit("MOV", Operand::Reg(5), Operand::Reg(4));
        Emit("SUB", Operand::Imm(-symbol.location.offset), Operand::Reg(4));
        EmitBlockCopy(Validator::TypeSize(symbol.type), Validator::GetType(symbol.type).align >= 2);
    }

    m_exitlabel = NewLabel();
    GenerateStatement(proc.block.body);
    EmitLabel(m_exitlabel);

    // Function result into R0
    if (proc.kind == ProcKindFunction && proc.resultsymbol >= 0)
    {
        const SymbolModel& result = g_program.symbols[proc.resultsymbol];
        if (Validator::IsStringType(result.type))
            ;  // Written by the function into the caller's buffer
        else if (!Validator::IsScalar(result.type))
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

    // Raw data: set images, each starts at an even address
    for (const auto& item : m_datalist)
    {
        AddByteLines(data, item.first, item.second, "");
        if (item.second.size() % 2 != 0)
            data.push_back(MakeDataLine("", ".EVEN", ""));
    }

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

// Identifiers are ASCII: compare them ignoring the case
static bool EqualNoCase(const string& a, const string& b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        char x = a[i], y = b[i];
        if (x >= 'a' && x <= 'z') x = (char)(x - 'a' + 'A');
        if (y >= 'a' && y <= 'z') y = (char)(y - 'a' + 'A');
        if (x != y)
            return false;
    }
    return true;
}

// Field of a record part by name, also inside the variants
static bool FindRecordField(const RecordPartModel& part, const string& name, int& offset, int& type)
{
    for (const FieldModel& field : part.fields)
    {
        if (EqualNoCase(field.name.text, name))
        {
            offset = field.offset;
            type = field.type;
            return true;
        }
    }
    for (const VariantModel& variant : part.variants)
    {
        if (FindRecordField(variant.part, name, offset, type))
            return true;
    }
    if (part.tagname.type != TokenTypeNone && EqualNoCase(part.tagname.text, name))
    {
        offset = part.tagoffset;
        type = part.tagtype;
        return true;
    }
    return false;
}

// Bytes of a typed constant value at the offset of the image; false for values not supported yet
static bool FillConstImage(std::vector<unsigned char>& image, int offset, int type, const StructConstModel& value)
{
    const TypeModel& model = Validator::GetType(type);
    int size = Validator::TypeSize(type);
    if (value.kind == StructConstModel::ConstList)
    {
        if (model.kind == TypeKindArray)
        {
            int elemsize = Validator::TypeSize(model.elemtype);
            for (size_t i = 0; i < value.items.size(); i++)
            {
                if (!FillConstImage(image, offset + (int)i * elemsize, model.elemtype, value.items[i]))
                    return false;
            }
            return true;
        }
        for (const StructConstModel& item : value.items)
        {
            int fieldoffset, fieldtype;
            if (!FindRecordField(model.record, item.fieldname.text, fieldoffset, fieldtype))
                return false;
            if (!FillConstImage(image, offset + fieldoffset, fieldtype, item))
                return false;
        }
        return true;
    }
    if (value.kind == StructConstModel::ConstSet)
    {
        int lowbyte = (int)(model.low / 8);
        for (const StructConstModel& item : value.items)
        {
            if (item.value.root < 0)
                return false;
            int64_t from = item.value.nodes[item.value.root].constvalue.ivalue;
            int64_t to = item.rangeto.IsEmpty() ? from : item.rangeto.nodes[item.rangeto.root].constvalue.ivalue;
            for (int64_t e = from; e <= to; e++)
                image[offset + (int)(e / 8) - lowbyte] |= (unsigned char)(1 << (e % 8));
        }
        return true;
    }

    if (value.value.root < 0)
        return false;
    const ExpressionNode& node = value.value.nodes[value.value.root];
    if (!node.isconst)
        return false;
    if (Validator::IsOrdinal(type))
    {
        int64_t v = node.constvalue.ivalue;
        image[offset] = (unsigned char)(v & 0xFF);
        if (size > 1)
            image[offset + 1] = (unsigned char)((v >> 8) & 0xFF);
        return true;
    }
    if (model.kind == TypeKindSet)
    {
        const string& bits = node.constvalue.svalue;
        for (int i = 0; i < size; i++)
            image[offset + i] = (unsigned char)bits[(size_t)(model.low / 8) + i];
        return true;
    }
    string bytes = Validator::IsCharType(node.type) ? string(1, (char)node.constvalue.ivalue) : node.constvalue.svalue;
    if (model.kind == TypeKindString)
    {
        if ((int64_t)bytes.size() > model.high)
            bytes.resize((size_t)model.high);
        image[offset] = (unsigned char)bytes.size();
        for (size_t i = 0; i < bytes.size(); i++)
            image[offset + 1 + i] = (unsigned char)bytes[i];
        return true;
    }
    if (Validator::IsCharArray(type))
    {
        for (size_t i = 0; i < bytes.size() && (int)i < size; i++)
            image[offset + i] = (unsigned char)bytes[i];
        return true;
    }
    return false;  // Real: step 7
}

// Typed constant: the value as an image of bytes, written as words if the type is word-aligned
void Generator::GenerateTypedConst(const SymbolModel& symbol, const StructConstModel& value)
{
    std::vector<CodeLine>& data = m_final->data;
    const string& label = symbol.location.asmlabel;
    string comment = symbol.name + ": " + g_program.TypeToString(symbol.type);
    int type = symbol.type;
    int size = Validator::TypeSize(type);

    std::vector<unsigned char> image((size_t)(size + 1), 0);
    if (!FillConstImage(image, 0, type, value))
    {
        data.push_back(MakeDataLine(label, ".BLKW", FormatNumber((size + 1) / 2), "TODO: typed constant " + comment));
        return;
    }
    if (Validator::GetType(type).align < 2)
    {
        // Trailing zeros, like the unused part of a string, go into .BLKB
        int used = size;
        while (used > 1 && image[used - 1] == 0)
            used--;
        if (size - used < 4)
            used = size;
        AddByteLines(data, label, string(image.begin(), image.begin() + used), comment);
        if (used < size)
            data.push_back(MakeDataLine("", ".BLKB", FormatNumber(size - used)));
        data.push_back(MakeDataLine("", ".EVEN", ""));
        return;
    }
    for (int i = 0; i < size; i += 16)
    {
        string text;
        for (int j = i; j < i + 16 && j < size; j += 2)
        {
            int word = image[j] | (image[j + 1] << 8);
            text += (text.empty() ? "" : ", ") + FormatNumber((int16_t)word);
        }
        data.push_back(MakeDataLine(i == 0 ? label : "", ".WORD", text, i == 0 ? comment : ""));
    }
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
