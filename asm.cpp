
#include <cassert>

#include "main.h"

// Assembler inserts asm ... end: macro11 line fields, operand items, operand substitution, size estimate.
// See docs/DESIGN.md section 7.9.


//////////////////////////////////////////////////////////////////////
// Character classes of macro11

static bool IsAsmBlank(char ch) { return ch == ' ' || ch == '\t'; }
static bool IsAsmLetter(char ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_'; }
static bool IsAsmDigit(char ch) { return ch >= '0' && ch <= '9'; }
static bool IsAsmSymbolChar(char ch) { return IsAsmLetter(ch) || IsAsmDigit(ch) || ch == '$' || ch == '.'; }

static string ToUpperText(const string& text)
{
    string result = text;
    for (char& ch : result)
    {
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');
    }
    return result;
}

static string TrimText(const string& text)
{
    size_t first = 0;
    while (first < text.size() && IsAsmBlank(text[first]))
        first++;
    size_t last = text.size();
    while (last > first && IsAsmBlank(text[last - 1]))
        last--;
    return text.substr(first, last - first);
}

bool IsBlankAsmText(const string& text)
{
    for (char ch : text)
    {
        if (!IsAsmBlank(ch))
            return false;
    }
    return true;
}

// Directives with a text operand: /text/ or <code>, the operand is not split and has no names
static bool IsTextDirective(const string& upperopcode)
{
    return upperopcode == ".ASCII" || upperopcode == ".ASCIZ" || upperopcode == ".RAD50";
}

// Directives with names of their own (modes, macros, globals): no substitution in the operands
static bool IsNameDirective(const string& upperopcode)
{
    return upperopcode == ".ENABL" || upperopcode == ".DSABL" || upperopcode == ".MCALL" ||
        upperopcode == ".GLOBL" || upperopcode == ".LIST" || upperopcode == ".NLIST";
}

bool IsForbiddenAsmDirective(const string& opcode)
{
    string upper = ToUpperText(opcode);
    return upper == ".PSECT" || upper == ".CSECT" || upper == ".ASECT" || upper == ".END" ||
        upper == ".TITLE" || upper == ".IDENT";
}

bool IsAsmRegisterName(const string& name)
{
    string upper = ToUpperText(name);
    if (upper == "SP" || upper == "PC")
        return true;
    return upper.size() == 2 && upper[0] == 'R' && upper[1] >= '0' && upper[1] <= '7';
}


//////////////////////////////////////////////////////////////////////
// Line fields

// Symbol at the position: letters, digits, '_', '$', '.'
static size_t ScanSymbol(const string& text, size_t pos)
{
    while (pos < text.size() && IsAsmSymbolChar(text[pos]))
        pos++;
    return pos;
}

static size_t SkipBlanks(const string& text, size_t pos)
{
    while (pos < text.size() && IsAsmBlank(text[pos]))
        pos++;
    return pos;
}

// Position of the opcode field after the labels, or npos for a comment-only line or an assignment
static size_t FindOpcodeField(const string& text, std::vector<string>* labels, size_t* assignend, size_t* labelsend = nullptr)
{
    size_t pos = SkipBlanks(text, 0);
    if (labelsend != nullptr)
        *labelsend = pos;
    for (;;)
    {
        if (pos >= text.size() || text[pos] == ';')
            return string::npos;
        size_t end = ScanSymbol(text, pos);
        if (end == pos)
            return pos;  // Not a symbol: the field starts here
        size_t next = SkipBlanks(text, end);
        if (next < text.size() && text[next] == ':')
        {
            next++;
            bool global = (next < text.size() && text[next] == ':');
            if (global)
                next++;  // Global label NAME::
            if (labels != nullptr)
                labels->push_back(text.substr(pos, end - pos) + (global ? "::" : ":"));
            pos = SkipBlanks(text, next);
            if (labelsend != nullptr)
                *labelsend = pos;
            continue;
        }
        if (next < text.size() && text[next] == '=')
        {
            if (assignend != nullptr)
                *assignend = end;
            return string::npos;
        }
        return pos;
    }
}

// The word END at the position: letters E, N, D, not followed by a symbol character
static bool IsEndWord(const string& text, size_t pos)
{
    if (pos + 3 > text.size() || ToUpperText(text.substr(pos, 3)) != "END")
        return false;
    return pos + 3 == text.size() || !IsAsmSymbolChar(text[pos + 3]) || text[pos + 3] == '.';
}

static size_t ScanTextOperand(const string& text, size_t pos);

// Offset of END closing the asm statement, or npos: the word END in the opcode field,
// or after a blank in the operands (asm CLR R0 end), outside comments and .ASCII text
size_t FindAsmEnd(const string& text)
{
    size_t pos = FindOpcodeField(text, nullptr, nullptr);
    if (pos == string::npos)
    {
        // Comment-only line or NAME = expression: END can only follow the expression
        size_t assignend = string::npos;
        FindOpcodeField(text, nullptr, &assignend);
        if (assignend == string::npos)
            return string::npos;
        pos = assignend;
    }
    else
    {
        if (IsEndWord(text, pos))
            return pos;
        size_t end = ScanSymbol(text, pos);
        string upper = ToUpperText(text.substr(pos, end - pos));
        pos = end;
        if (IsTextDirective(upper))
        {
            pos = ScanTextOperand(text, pos);
            size_t next = SkipBlanks(text, pos);
            return (next > pos && IsEndWord(text, next)) ? next : string::npos;
        }
    }
    while (pos < text.size())
    {
        char ch = text[pos];
        if (ch == ';')
            return string::npos;
        if (ch == '\'')
            pos += 2;
        else if (ch == '"')
            pos += 3;
        else if (IsAsmBlank(ch))
        {
            pos++;
            if (pos < text.size() && IsEndWord(text, pos))
                return pos;
        }
        else if (IsAsmSymbolChar(ch))
            pos = ScanSymbol(text, pos);
        else
            pos++;
    }
    return string::npos;
}

// Text operand of .ASCII and the like: /text/ and <code> parts; returns the position after it
static size_t ScanTextOperand(const string& text, size_t pos)
{
    for (;;)
    {
        pos = SkipBlanks(text, pos);
        if (pos >= text.size() || text[pos] == ';')
            return pos;
        char delimiter = (text[pos] == '<') ? '>' : text[pos];
        pos++;
        while (pos < text.size() && text[pos] != delimiter)
            pos++;
        if (pos < text.size())
            pos++;
    }
}

// Operands separated by commas, up to a comment
static size_t SplitOperands(const string& text, size_t pos, AsmLineModel& line)
{
    size_t start = pos;
    int depth = 0;
    for (;;)
    {
        bool atend = (pos >= text.size() || text[pos] == ';');
        if (atend || (text[pos] == ',' && depth == 0))
        {
            string operand = text.substr(start, pos - start);
            size_t lead = SkipBlanks(operand, 0);
            string trimmed = TrimText(operand);
            if (!trimmed.empty() || !line.operands.empty() || (!atend))
            {
                line.operands.push_back(trimmed);
                line.operandcols.push_back((int)(start + lead));
            }
            if (atend)
                return pos;
            start = ++pos;
            continue;
        }
        char ch = text[pos];
        if (ch == '\'')
            pos += 2;  // 'c: one character
        else if (ch == '"')
            pos += 3;  // "cc: two characters
        else
        {
            if (ch == '<')
                depth++;
            else if (ch == '>' && depth > 0)
                depth--;
            pos++;
        }
        if (pos > text.size())
            pos = text.size();
    }
}

void ParseAsmLine(const string& text, AsmLineModel& line)
{
    line.text = text;
    size_t assignend = string::npos;
    size_t labelsend = 0;
    size_t pos = FindOpcodeField(text, &line.labels, &assignend, &labelsend);
    if (assignend != string::npos)
    {
        // NAME = expression
        size_t namepos = SkipBlanks(text, 0);
        line.assignname = text.substr(namepos, assignend - namepos);
        pos = SkipBlanks(text, assignend) + 1;
        line.opcode = "=";
        if (pos < text.size() && text[pos] == '=')
        {
            line.opcode = "==";
            pos++;
        }
        pos = SplitOperands(text, pos, line);
    }
    else if (pos == string::npos)
    {
        // Labels and a comment only
        pos = labelsend;
    }
    else
    {
        size_t end = ScanSymbol(text, pos);
        line.opcode = text.substr(pos, end - pos);
        string upper = ToUpperText(line.opcode);
        pos = SkipBlanks(text, end);
        if (IsTextDirective(upper))
        {
            size_t textend = ScanTextOperand(text, pos);
            string operand = TrimText(text.substr(pos, textend - pos));
            if (!operand.empty())
            {
                line.operands.push_back(operand);
                line.operandcols.push_back((int)pos);
            }
            line.rawoperands = true;
            pos = textend;
        }
        else
        {
            line.rawoperands = IsNameDirective(upper);
            pos = SplitOperands(text, pos, line);
        }
    }
    if (pos < text.size() && text[pos] == ';')
    {
        line.hascomment = true;
        line.comment = text.substr(pos + 1);
    }
}

// The line in standard columns: labels, tab, opcode, tab, operands, tab, comment
string AsmLineToString(const AsmLineModel& line, const std::vector<string>& operands)
{
    string result;
    for (const string& label : line.labels)
        result += label;
    string joined;
    for (size_t i = 0; i < operands.size(); i++)
        joined += (i > 0 ? ", " : "") + operands[i];
    if (!line.assignname.empty())
        result += "\t" + line.assignname + " " + line.opcode + " " + joined;
    else if (!line.opcode.empty())
    {
        result += "\t" + line.opcode;
        if (!joined.empty())
            result += "\t" + joined;
    }
    if (line.hascomment)
        result += "\t;" + line.comment;
    return result;
}


//////////////////////////////////////////////////////////////////////
// Operand items

std::vector<AsmItem> ScanAsmOperand(const string& text)
{
    std::vector<AsmItem> items;
    size_t pos = 0;
    while (pos < text.size())
    {
        AsmItem item;
        item.start = pos;
        char ch = text[pos];
        if (IsAsmBlank(ch))
        {
            pos++;
            continue;
        }
        if (IsAsmLetter(ch))
        {
            size_t end = ScanSymbol(text, pos);
            item.text = text.substr(pos, end - pos);
            item.kind = (item.text.find('$') != string::npos) ? AsmItemOther : AsmItemName;
            if (item.kind == AsmItemName && IsAsmRegisterName(item.text))
                item.kind = AsmItemRegister;
            pos = end;
        }
        else if (IsAsmDigit(ch))
        {
            size_t end = ScanSymbol(text, pos);
            item.text = text.substr(pos, end - pos);
            item.kind = AsmItemNumber;
            for (char c : item.text)
            {
                if (!IsAsmDigit(c) && c != '.')
                    item.kind = AsmItemOther;  // Local label 1$ and the like
            }
            pos = end;
        }
        else if (ch == '.')
        {
            size_t end = ScanSymbol(text, pos);
            item.text = text.substr(pos, end - pos);
            item.kind = AsmItemOther;
            pos = end;
        }
        else if (ch == '\'' || ch == '"')
        {
            size_t end = pos + (ch == '\'' ? 2 : 3);
            if (end > text.size())
                end = text.size();
            item.text = text.substr(pos, end - pos);
            item.kind = AsmItemOther;
            pos = end;
        }
        else if (ch == '%' && pos + 1 < text.size() && text[pos + 1] >= '0' && text[pos + 1] <= '7')
        {
            item.text = text.substr(pos, 2);
            item.kind = AsmItemRegister;
            pos += 2;
        }
        else
        {
            item.text = string(1, ch);
            item.kind = AsmItemChar;
            pos++;
        }
        item.length = pos - item.start;
        items.push_back(item);
    }
    return items;
}

// macro11 number: digits with a trailing dot are decimal, without it octal
bool ParseAsmNumber(const string& text, int64_t& value)
{
    if (text.empty())
        return false;
    bool decimal = (text.back() == '.');
    string digits = decimal ? text.substr(0, text.size() - 1) : text;
    if (digits.empty())
        return false;
    value = 0;
    for (char ch : digits)
    {
        if (!IsAsmDigit(ch) || (!decimal && ch > '7'))
            return false;
        value = value * (decimal ? 10 : 8) + (ch - '0');
        if (value > 0xFFFF)
            return false;
    }
    return true;
}


//////////////////////////////////////////////////////////////////////
// Substitution

// Constant value as an operand item: negative values after an operator go in <>
string FormatAsmConst(int64_t value, const string& operand, size_t start)
{
    string number = FormatNumber(value);
    if (value >= 0)
        return number;
    size_t pos = start;
    while (pos > 0 && IsAsmBlank(operand[pos - 1]))
        pos--;
    if (pos == 0 || operand[pos - 1] == '#' || operand[pos - 1] == '@')
        return number;
    return "<" + number + ">";
}

// Operand for a Pascal variable, procedure or label: prefix, base, offset, index register.
// Returns false with the reason if the form cannot be written.
bool FormatAsmOperand(const Operand& base, const AsmNameModel& name, string& result, string& error)
{
    string index = name.indexreg.empty() ? "" : "(" + name.indexreg + ")";
    const string& prefix = name.prefix;
    int64_t offset = base.value + name.offset;
    switch (base.mode)
    {
    case OpLabel:
        result = prefix + LabelWithOffset(base.label, offset) + index;
        return true;
    case OpAbsolute:
        {
            string address = FormatNumber(offset & 0xFFFF);
            if (prefix == "#" && index.empty())
                result = "#" + address;
            else if ((prefix.empty() || prefix == "@#") && index.empty())
                result = "@#" + address;
            else if ((prefix.empty() || prefix == "@") && !index.empty())
                result = prefix + address + index;
            else
            {
                error = "absolute variable: X, X+n, X(Rn), #X or @X(Rn) expected";
                return false;
            }
            return true;
        }
    case OpIndex:
        if ((!prefix.empty() && prefix != "@") || !index.empty())
        {
            error = "local variable or parameter: X, X+n or @X expected";
            return false;
        }
        result = prefix + FormatNumber(offset) + "(R5)";
        return true;
    case OpIndexDeferred:
        if (!prefix.empty() || !index.empty() || name.offset != 0)
        {
            error = "var parameter: only X is allowed";
            return false;
        }
        result = "@" + FormatNumber(base.value) + "(R5)";
        return true;
    default:
        error = "cannot be used in an asm operand";
        return false;
    }
}

// Upper estimate of the line size in words, for the branch layout; -1 if .BLKW/.BLKB count is unknown
int EstimateAsmWords(const AsmLineModel& line, const std::vector<string>& operands)
{
    if (line.opcode.empty() || !line.assignname.empty())
        return 0;
    if (line.opcode[0] != '.')
        return 3;  // Instruction: at most 3 words on PDP-11
    string upper = ToUpperText(line.opcode);
    int count = (int)operands.size();
    if (upper == ".WORD")
        return count > 0 ? count : 1;
    if (upper == ".BYTE")
        return count > 0 ? (count + 1) / 2 : 1;
    if (upper == ".EVEN" || upper == ".ODD")
        return 1;
    if (upper == ".FLT2")
        return 2 * (count > 0 ? count : 1);
    if (upper == ".FLT4")
        return 4 * (count > 0 ? count : 1);
    if (upper == ".BLKW" || upper == ".BLKB")
    {
        int64_t n = 1;
        if (count > 0 && !ParseAsmNumber(TrimText(operands[0]), n))
            return -1;
        return (int)(upper == ".BLKW" ? n : (n + 1) / 2);
    }
    if (IsTextDirective(upper))
    {
        // Bytes of /text/ parts and <code> parts
        int bytes = 0;
        string text = operands.empty() ? "" : operands[0];
        size_t pos = 0;
        for (;;)
        {
            pos = SkipBlanks(text, pos);
            if (pos >= text.size())
                break;
            if (text[pos] == '<')
            {
                size_t end = text.find('>', pos);
                pos = (end == string::npos) ? text.size() : end + 1;
                bytes++;
                continue;
            }
            char delimiter = text[pos];
            size_t end = text.find(delimiter, pos + 1);
            if (end == string::npos)
                end = text.size();
            bytes += (int)(end - pos - 1);
            pos = end + 1;
        }
        if (upper == ".ASCIZ")
            bytes++;
        if (upper == ".RAD50")
            return (bytes + 2) / 3;
        return (bytes + 1) / 2;
    }
    if (IsNameDirective(upper) || upper == ".PAGE" || upper == ".SBTTL")
        return 0;
    return 16;  // Macro call, like .TTYOUT
}


//////////////////////////////////////////////////////////////////////
// Validator

static SourcePos AsmItemPos(const AsmLineModel& line, int operandindex, size_t offset)
{
    SourcePos pos = line.pos;
    pos.col += line.operandcols[operandindex] + (int)offset;
    return pos;
}

static bool IsStandardSymbol(const SymbolModel& symbol)
{
    return symbol.scope >= 0 && g_program.scopes[symbol.scope].parent < 0;
}

// Ordinal value of a constant symbol; false for Real, string and set constants
static bool GetAsmConstValue(const SymbolModel& symbol, int64_t& value)
{
    if (Validator::IsOrdinal(symbol.type))
    {
        value = symbol.constvalue.ivalue;
        return true;
    }
    if (Validator::IsStringType(symbol.type) && symbol.constvalue.svalue.size() == 1)
    {
        value = (unsigned char)symbol.constvalue.svalue[0];
        return true;
    }
    return false;
}

static void SplitDotted(const string& text, std::vector<string>& parts)
{
    size_t start = 0;
    for (;;)
    {
        size_t dot = text.find('.', start);
        parts.push_back(text.substr(start, dot == string::npos ? string::npos : dot - start));
        if (dot == string::npos)
            return;
        start = dot + 1;
    }
}

// Operand of a variable, procedure or label as the generator will write it; labels get a placeholder
bool Validator::GetAsmBaseOperand(int symindex, Operand& base, string& error)
{
    const SymbolModel& symbol = g_program.symbols[symindex];
    switch (symbol.kind)
    {
    case SymVar: case SymTypedConst: case SymParam: case SymVarParam: case SymFuncResult:
        if (symbol.absoluteaddr >= 0)
            base = Operand::Absolute((int)symbol.absoluteaddr);
        else if (symbol.absolutesym >= 0)
            return GetAsmBaseOperand(symbol.absolutesym, base, error);
        else if (!symbol.location.asmlabel.empty())
            base = Operand::Label(symbol.location.asmlabel);
        else if (symbol.level != g_program.procedures[m_procindex].level)
        {
            error = "variable of an enclosing procedure cannot be used, take its address in Pascal";
            return false;
        }
        else if (symbol.indirect)
            base = Operand::IndexDeferred(5, symbol.location.offset);
        else
            base = Operand::Index(5, symbol.location.offset);
        return true;
    case SymProcedure: case SymFunction:
        {
            const ProcedureModel& proc = g_program.procedures[symbol.procindex];
            if (!proc.external.nodes.empty() || proc.asmlabel.empty())
            {
                error = "external procedure has no label";
                return false;
            }
            base = Operand::Label(proc.asmlabel);
            return true;
        }
    case SymLabel:
        base = Operand::Label("G00000");
        return true;
    default:
        error = "only constants, variables, procedures and labels can be used";
        return false;
    }
}

// +n-m... part after a name: macro11 numbers and Pascal constants
bool Validator::EvaluateAsmOffset(const std::vector<AsmItem>& items, size_t& i, int64_t& value, string& error)
{
    value = 0;
    while (i < items.size() && items[i].kind == AsmItemChar && (items[i].text == "+" || items[i].text == "-"))
    {
        bool minus = (items[i].text == "-");
        i++;
        int64_t term = 0;
        if (i < items.size() && items[i].kind == AsmItemNumber)
        {
            if (!ParseAsmNumber(items[i].text, term))
            {
                error = "bad number " + items[i].text;
                return false;
            }
        }
        else if (i < items.size() && items[i].kind == AsmItemName)
        {
            int symbol = FindSymbol(items[i].text);
            if (symbol < 0 || g_program.symbols[symbol].kind != SymConst ||
                !GetAsmConstValue(g_program.symbols[symbol], term))
            {
                error = "number or ordinal constant expected after +/-: " + items[i].text;
                return false;
            }
        }
        else
        {
            error = "number or constant expected after +/-";
            return false;
        }
        value += minus ? -term : term;
        i++;
    }
    return true;
}

void Validator::ValidateAsmOperand(AsmLineModel& line, int operandindex)
{
    const string& operand = line.operands[operandindex];
    std::vector<AsmItem> items = ScanAsmOperand(operand);
    for (size_t i = 0; i < items.size(); i++)
    {
        const AsmItem& item = items[i];
        if (item.kind != AsmItemName)
            continue;
        std::vector<string> parts;
        SplitDotted(item.text, parts);
        int symindex = FindSymbol(parts[0]);
        if (symindex < 0 || (IsStandardSymbol(g_program.symbols[symindex]) && g_program.symbols[symindex].kind != SymConst))
        {
            if (parts.size() == 1)
                line.othernames.push_back(ToUpperText(item.text));
            continue;
        }
        const SymbolModel& symbol = g_program.symbols[symindex];
        SourcePos pos = AsmItemPos(line, operandindex, item.start);
        AsmNameModel name;
        name.operand = operandindex;
        name.symbol = symindex;
        if (symbol.kind == SymConst)
        {
            if (parts.size() > 1 || !GetAsmConstValue(symbol, name.value))
            {
                Error(pos, 74, "Error in asm: only ordinal constants can be used: " + item.text);
                continue;
            }
            name.isconst = true;
            name.start = item.start;
            name.length = item.length;
            line.names.push_back(name);
            continue;
        }

        // A variable, procedure or label takes the whole operand: [#|@|@#] name[.field...][+n][(Rn)]
        string prefix;
        for (size_t k = 0; k < i; k++)
            prefix += items[k].text;
        if (prefix != "" && prefix != "#" && prefix != "@" && prefix != "@#")
        {
            Error(pos, 74, "Error in asm: Pascal name must start the operand (X, X+n, X(Rn), #X, @X): " + item.text);
            return;
        }
        string error;
        int type = symbol.type;
        int64_t fieldoffset = 0;
        for (size_t k = 1; k < parts.size(); k++)
        {
            int offset = 0, fieldtype = -1;
            if (!FindField(type, parts[k], offset, fieldtype))
            {
                Error(pos, 74, "Error in asm: unknown field " + parts[k] + " in " + item.text);
                return;
            }
            fieldoffset += offset;
            type = fieldtype;
        }
        size_t j = i + 1;
        int64_t delta = 0;
        if (!EvaluateAsmOffset(items, j, delta, error))
        {
            Error(pos, 74, "Error in asm: " + error);
            return;
        }
        if (j + 2 < items.size() && items[j].text == "(" && items[j + 1].kind == AsmItemRegister && items[j + 2].text == ")")
        {
            name.indexreg = ToUpperText(items[j + 1].text);
            j += 3;
        }
        if (j != items.size())
        {
            Error(pos, 74, "Error in asm: unexpected text after the Pascal name " + item.text);
            return;
        }
        name.prefix = prefix;
        name.offset = fieldoffset + delta;
        name.start = items[0].start;
        name.length = operand.size() - name.start;
        Operand base;
        string result;
        if (!GetAsmBaseOperand(symindex, base, error) || !FormatAsmOperand(base, name, result, error))
        {
            Error(pos, 74, "Error in asm: " + item.text + ": " + error);
            return;
        }
        line.names.push_back(name);
        return;
    }
}

void Validator::ValidateAsm(StatementModel& statement)
{
    for (AsmLineModel& line : statement.asmlines)
    {
        if (IsForbiddenAsmDirective(line.opcode))
        {
            Error(line.pos, 74, "Error in asm: directive " + line.opcode + " is not allowed");
            continue;
        }
        std::vector<string> defined;
        for (const string& label : line.labels)
            defined.push_back(label.substr(0, label.find(':')));
        if (!line.assignname.empty())
            defined.push_back(line.assignname);
        for (const string& name : defined)
        {
            int symbol = FindSymbol(name);
            if (symbol >= 0 && !IsStandardSymbol(g_program.symbols[symbol]))
                ReportWarning(line.pos, "asm symbol " + name + " has the name of a Pascal identifier, references to it are replaced");
        }
        if (!line.rawoperands)
        {
            for (size_t i = 0; i < line.operands.size(); i++)
                ValidateAsmOperand(line, (int)i);
        }

        // Size: constants substituted, so that .BLKW Count is known
        std::vector<string> operands = line.operands;
        for (size_t k = line.names.size(); k-- > 0; )
        {
            const AsmNameModel& name = line.names[k];
            if (name.isconst)
                operands[name.operand].replace(name.start, name.length, FormatNumber(name.value));
        }
        line.words = EstimateAsmWords(line, operands);
        if (line.words < 0)
        {
            Error(line.pos, 74, "Error in asm: " + line.opcode + " needs a number or a constant");
            line.words = 0;
        }
    }
}


//////////////////////////////////////////////////////////////////////
// Generator

bool Generator::GenerateAsm(StatementModel& statement)
{
    EmitLabel(NewLabel());  // Starts a new scope of local labels 1$
    for (const AsmLineModel& line : statement.asmlines)
    {
        std::vector<string> operands = line.operands;
        for (size_t k = line.names.size(); k-- > 0; )
        {
            const AsmNameModel& name = line.names[k];
            string& operand = operands[name.operand];
            string result;
            if (name.isconst)
                result = FormatAsmConst(name.value, operand, name.start);
            else
            {
                const SymbolModel& symbol = g_program.symbols[name.symbol];
                Operand base;
                if (symbol.kind == SymProcedure || symbol.kind == SymFunction)
                    base = Operand::Label(g_program.procedures[symbol.procindex].asmlabel);
                else if (symbol.kind == SymLabel)
                    base = Operand::Label(GetGotoLabel(name.symbol));
                else if (!GetSymbolOperand(name.symbol, 0, false, base))
                    return false;
                string error;
                if (!FormatAsmOperand(base, name, result, error))
                    return false;
            }
            operand.replace(name.start, name.length, result);
        }
        for (const string& other : line.othernames)
        {
            if (m_runtime->FindBlock(other) != nullptr)
                m_final->runtimeneeds.insert(other);
        }
        CodeLine code;
        code.kind = LineRaw;
        code.text = AsmLineToString(line, operands);
        code.words = line.words;
        m_code->lines.push_back(code);
    }
    return true;
}


//////////////////////////////////////////////////////////////////////
