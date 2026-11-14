
#include <cassert>
#include <cstdio>

#include "main.h"


ProgramModel g_program;


//////////////////////////////////////////////////////////////////////
// Helpers

static string ToLower(string text)
{
    for (char& c : text)
    {
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
    }
    return text;
}

// Operation text for dumps: symbols as is, keywords in lower case
static string GetOperationText(const Token& token)
{
    if (token.type == TokenTypeKeyword)
        return ToLower(GetKeywordName(token.keyword));
    return GetSymbolText(token.symbol);
}

static string JoinTokens(const std::vector<Token>& tokens)
{
    string result;
    for (size_t i = 0; i < tokens.size(); i++)
    {
        if (i > 0)
            result += ", ";
        result += tokens[i].text;
    }
    return result;
}

// Directives that differ from the defaults, like " {$R+,I-}"; empty if none
static string GetDirectivesDiffFromDefault(unsigned directives)
{
    static const char letters[] = "ABCIRUVX";
    static const unsigned flags[] =
    {
        DirectiveA, DirectiveB, DirectiveC, DirectiveI, DirectiveR, DirectiveU, DirectiveV, DirectiveX
    };
    string result;
    for (int i = 0; i < 8; i++)
    {
        if ((directives & flags[i]) == (DIRECTIVES_DEFAULT & flags[i]))
            continue;
        result += result.empty() ? " {$" : ",";
        result += letters[i];
        result += (directives & flags[i]) ? '+' : '-';
    }
    if (!result.empty())
        result += "}";
    else
        result = " {$defaults}";
    return result;
}

// Directives of the last dumped statement: the dump shows them only where they change
static unsigned g_dumpdirectives = DIRECTIVES_DEFAULT;

static string GetDirectivesDiff(unsigned directives)
{
    if (directives == g_dumpdirectives)
        return "";
    g_dumpdirectives = directives;
    return GetDirectivesDiffFromDefault(directives);
}


//////////////////////////////////////////////////////////////////////
// ExpressionModel

int ExpressionModel::AddNode(const ExpressionNode& node)
{
    nodes.push_back(node);
    return (int)nodes.size() - 1;
}

string ExpressionModel::ToString() const
{
    if (root < 0)
        return "";
    return NodeToString(root);
}

// Folded constant value for -v dumps
static string ConstValueToString(const ConstValue& value, int type);

static string ConstToString(const ExpressionNode& node)
{
    return ConstValueToString(node.constvalue, node.type);
}

static string ConstValueToString(const ConstValue& constvalue, int type)
{
    struct { ConstValue constvalue; } node { constvalue };
    if (Validator::IsRealType(type))
    {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%g", node.constvalue.dvalue);
        string result = buffer;
        if (result.find_first_of(".en") == string::npos)
            result += ".0";
        return result;
    }
    if (Validator::IsCharType(type))
    {
        Token token;
        token.type = TokenTypeString;
        token.svalue = string(1, (char)node.constvalue.ivalue);
        return token.GetDisplayValue();
    }
    if (Validator::IsStringType(type))
    {
        Token token;
        token.type = TokenTypeString;
        token.svalue = node.constvalue.svalue;
        return token.GetDisplayValue();
    }
    if (Validator::IsSetType(type))
    {
        string result = "[";
        const string& bits = node.constvalue.svalue;
        auto isset = [&bits](int i) { return bits.size() == 32 && (((unsigned char)bits[i / 8] >> (i % 8)) & 1); };
        bool first = true;
        for (int i = 0; i < 256; i++)
        {
            if (!isset(i))
                continue;
            int j = i;
            while (j + 1 < 256 && isset(j + 1))
                j++;
            result += first ? "" : ", ";
            result += std::to_string(i);
            if (j > i)
                result += ".." + std::to_string(j);
            first = false;
            i = j;
        }
        return result + "]";
    }
    if (Validator::IsPointerType(type))
        return "nil";
    if (Validator::IsBooleanType(type))
        return node.constvalue.ivalue ? "True" : "False";
    const TypeModel& model = Validator::GetType(Validator::OrdinalBase(type));
    if (model.kind == TypeKindEnum && node.constvalue.ivalue >= 0 && node.constvalue.ivalue < (int64_t)model.enumnames.size())
        return model.enumnames[(size_t)node.constvalue.ivalue].text;
    return std::to_string(node.constvalue.ivalue);
}

string ExpressionModel::NodeToString(int index) const
{
    if (index < 0 || index >= (int)nodes.size())
        return "?";
    const ExpressionNode& node = nodes[index];
    if (g_showvalidation)
    {
        string text;
        bool literal = (node.kind == NodeInteger || node.kind == NodeReal || node.kind == NodeString || node.kind == NodeNil);
        if (node.isconst && !literal && node.type >= 0)
            text = ConstToString(node);
        else
            text = NodeToStringPlain(index);
        switch (node.convert)
        {
        case ConvertIntToReal: return "ToReal(" + text + ")";
        case ConvertCharToString:
        case ConvertArrayToString: return "ToString(" + text + ")";
        default: return text;
        }
    }
    return NodeToStringPlain(index);
}

string ExpressionModel::NodeToStringPlain(int index) const
{
    const ExpressionNode& node = nodes[index];
    string result;
    switch (node.kind)
    {
    case NodeInteger:
    case NodeReal:
    case NodeIdent:
        return node.token.text;
    case NodeString:
        return node.token.GetDisplayValue();
    case NodeNil:
        return "nil";
    case NodeCall:
    case NodeIndex:
        result = (node.kind == NodeCall) ? node.token.text : NodeToString(node.left);
        result += (node.kind == NodeCall) ? "(" : "[";
        for (size_t i = 0; i < node.args.size(); i++)
        {
            const ArgModel& arg = node.args[i];
            if (i > 0)
                result += ", ";
            result += NodeToString(arg.node);
            if (arg.width >= 0)
                result += ":" + NodeToString(arg.width);
            if (arg.decimals >= 0)
                result += ":" + NodeToString(arg.decimals);
        }
        result += (node.kind == NodeCall) ? ")" : "]";
        return result;
    case NodeField:
        return NodeToString(node.left) + "." + node.token.text;
    case NodeDeref:
        return NodeToString(node.left) + "^";
    case NodeUnary:
        if (node.token.IsKeyword(KeywordNOT))
            return "(not " + NodeToString(node.left) + ")";
        return "(" + GetOperationText(node.token) + NodeToString(node.left) + ")";
    case NodeBinary:
        return "(" + NodeToString(node.left) + " " + GetOperationText(node.token) + " " + NodeToString(node.right) + ")";
    case NodeSet:
        result = "[";
        for (size_t i = 0; i < node.setitems.size(); i++)
        {
            if (i > 0)
                result += ", ";
            result += NodeToString(node.setitems[i].from);
            if (node.setitems[i].to >= 0)
                result += ".." + NodeToString(node.setitems[i].to);
        }
        return result + "]";
    default:
        return "?";
    }
}


//////////////////////////////////////////////////////////////////////
// Constants and types

static string CaseLabelsToString(const std::vector<CaseLabelModel>& labels)
{
    string result;
    for (size_t i = 0; i < labels.size(); i++)
    {
        if (i > 0)
            result += ", ";
        result += labels[i].from.ToString();
        if (!labels[i].to.IsEmpty())
            result += ".." + labels[i].to.ToString();
    }
    return result;
}

string StructConstModel::ToString() const
{
    string result;
    switch (kind)
    {
    case ConstSimple:
        return value.ToString();
    case ConstList:
        result = "(";
        for (size_t i = 0; i < items.size(); i++)
        {
            const StructConstModel& item = items[i];
            if (i > 0)
                result += (item.fieldname.type != TokenTypeNone) ? "; " : ", ";
            if (item.fieldname.type != TokenTypeNone)
                result += item.fieldname.text + ": ";
            result += item.ToString();
        }
        return result + ")";
    case ConstSet:
        result = "[";
        for (size_t i = 0; i < items.size(); i++)
        {
            if (i > 0)
                result += ", ";
            result += items[i].value.ToString();
            if (!items[i].rangeto.IsEmpty())
                result += ".." + items[i].rangeto.ToString();
        }
        return result + "]";
    }
    return "?";
}

string RecordPartModel::ToString() const
{
    string result;
    // Group the fields of one record section "A, B: T": they share the type index
    for (size_t i = 0; i < fields.size(); )
    {
        if (i > 0)
            result += "; ";
        size_t j = i;
        while (j < fields.size() && fields[j].type == fields[i].type)
        {
            if (j > i)
                result += ", ";
            result += fields[j].name.text;
            j++;
        }
        result += ": " + g_program.TypeToString(fields[i].type);
        i = j;
    }
    if (hasvariant)
    {
        if (!fields.empty())
            result += "; ";
        result += "case ";
        if (tagname.type != TokenTypeNone)
            result += tagname.text + ": ";
        result += g_program.TypeToString(tagtype) + " of ";
        for (size_t i = 0; i < variants.size(); i++)
        {
            if (i > 0)
                result += "; ";
            result += CaseLabelsToString(variants[i].labels) + ": (" + variants[i].part.ToString() + ")";
        }
    }
    return result;
}

int ProgramModel::AddType(const TypeModel& type)
{
    types.push_back(type);
    return (int)types.size() - 1;
}

string ProgramModel::TypeToString(int index) const
{
    if (index < 0 || index >= (int)types.size())
        return "?";
    const TypeModel& type = types[index];
    string result = type.ispacked ? "packed " : "";
    switch (type.kind)
    {
    case TypeKindName:
        return type.name.text;
    case TypeKindEnum:
        return "(" + JoinTokens(type.enumnames) + ")";
    case TypeKindSubrange:
        return type.lowexpr.ToString() + ".." + type.highexpr.ToString();
    case TypeKindString:
        if (type.highexpr.IsEmpty())
            return "string[" + std::to_string(type.high) + "]";
        return "string[" + type.highexpr.ToString() + "]";
    case TypeKindArray:
        return result + "array[" + TypeToString(type.indextype) + "] of " + TypeToString(type.elemtype);
    case TypeKindRecord:
        if (type.record.fields.empty() && !type.record.hasvariant)
            return result + "record end";
        return result + "record " + type.record.ToString() + " end";
    case TypeKindSet:
        return result + "set of " + TypeToString(type.elemtype);
    case TypeKindPointer:
        return "^" + TypeToString(type.elemtype);
    case TypeKindFile:
        if (type.elemtype < 0)
            return result + "file";
        return result + "file of " + TypeToString(type.elemtype);
    case TypeKindInteger: return "Integer";
    case TypeKindByte: return "Byte";
    case TypeKindBoolean: return "Boolean";
    case TypeKindChar: return "Char";
    case TypeKindReal: return "Real";
    case TypeKindText: return "Text";
    default:
        return "?";
    }
}


//////////////////////////////////////////////////////////////////////
// Dump for -p

static void DumpStatement(std::ostream& out, const StatementModel& statement, int indent);

static void DumpStatements(std::ostream& out, const std::vector<StatementModel>& statements, int indent)
{
    for (const StatementModel& statement : statements)
        DumpStatement(out, statement, indent);
}

static void DumpStatement(std::ostream& out, const StatementModel& statement, int indent)
{
    string pad(indent, ' ');
    out << pad;
    if (statement.label.type != TokenTypeNone)
        out << statement.label.text << ": ";

    string directives = GetDirectivesDiff(statement.token.directives);
    switch (statement.kind)
    {
    case StmtEmpty:
        out << "EMPTY" << directives << std::endl;
        break;
    case StmtCompound:
        out << "BEGIN" << directives << std::endl;
        DumpStatements(out, statement.statements, indent + 2);
        break;
    case StmtAssign:
        out << "ASSIGN " << statement.target.ToString() << " := " << statement.expr.ToString() << directives << std::endl;
        break;
    case StmtCall:
        out << "CALL " << statement.target.ToString() << directives << std::endl;
        break;
    case StmtGoto:
        out << "GOTO " << statement.gotolabel.text << directives << std::endl;
        break;
    case StmtInline:
        out << "INLINE (";
        for (size_t i = 0; i < statement.inlineitems.size(); i++)
        {
            const InlineItemModel& item = statement.inlineitems[i];
            if (i > 0)
                out << "/";
            if (item.sizeprefix != 0)
                out << item.sizeprefix;
            for (size_t j = 0; j < item.terms.size(); j++)
            {
                const InlineTermModel& term = item.terms[j];
                if (j > 0 || term.sign == '-')
                    out << term.sign;
                out << term.token.text;
            }
        }
        out << ")" << directives << std::endl;
        break;
    case StmtAsm:
        out << "ASM" << directives << std::endl;
        for (const AsmLineModel& line : statement.asmlines)
            out << pad << "  | " << AsmLineToString(line, line.operands) << std::endl;
        out << pad << "END" << std::endl;
        break;
    case StmtIf:
        out << "IF " << statement.expr.ToString() << directives << std::endl;
        out << pad << "  THEN" << std::endl;
        DumpStatement(out, statement.statements[0], indent + 4);
        if (statement.statements.size() > 1)
        {
            out << pad << "  ELSE" << std::endl;
            DumpStatement(out, statement.statements[1], indent + 4);
        }
        break;
    case StmtCase:
        out << "CASE " << statement.expr.ToString() << directives << std::endl;
        for (const CaseElementModel& element : statement.caseelements)
        {
            out << pad << "  " << CaseLabelsToString(element.labels) << ":" << std::endl;
            DumpStatements(out, element.statement, indent + 4);
        }
        if (!statement.statements.empty())
        {
            out << pad << "  ELSE" << std::endl;
            DumpStatements(out, statement.statements, indent + 4);
        }
        break;
    case StmtWhile:
        out << "WHILE " << statement.expr.ToString() << directives << std::endl;
        DumpStatements(out, statement.statements, indent + 2);
        break;
    case StmtRepeat:
        out << "REPEAT" << directives << std::endl;
        DumpStatements(out, statement.statements, indent + 2);
        out << pad << "UNTIL " << statement.expr.ToString() << std::endl;
        break;
    case StmtFor:
        out << "FOR " << statement.target.ToString() << " := " << statement.expr.ToString()
            << (statement.downto ? " DOWNTO " : " TO ") << statement.expr2.ToString() << directives << std::endl;
        DumpStatements(out, statement.statements, indent + 2);
        break;
    case StmtWith:
        out << "WITH ";
        for (size_t i = 0; i < statement.withvars.size(); i++)
        {
            if (i > 0)
                out << ", ";
            out << statement.withvars[i].ToString();
        }
        out << directives << std::endl;
        DumpStatements(out, statement.statements, indent + 2);
        break;
    }
}

static void DumpBlock(std::ostream& out, const BlockModel& block, int indent);

static void DumpProcedureHeading(std::ostream& out, const ProcedureModel& proc, int indent, const string& info)
{
    out << string(indent, ' ') << ((proc.kind == ProcKindFunction) ? "FUNCTION " : "PROCEDURE ") << proc.name.text;
    if (!proc.params.empty())
    {
        out << "(";
        for (size_t i = 0; i < proc.params.size(); i++)
        {
            const ParamGroupModel& group = proc.params[i];
            if (i > 0)
                out << "; ";
            if (group.isvar)
                out << "var ";
            out << JoinTokens(group.names);
            if (group.typename_.type != TokenTypeNone)
                out << ": " << group.typename_.text;
        }
        out << ")";
    }
    if (proc.resulttype.type != TokenTypeNone)
        out << ": " << proc.resulttype.text;
    switch (proc.directive)
    {
    case ProcDirForward: out << " FORWARD"; break;
    case ProcDirExternal: out << " EXTERNAL " << proc.external.ToString(); break;
    case ProcDirOverlay: out << " OVERLAY"; break;
    default: break;
    }
    out << info << std::endl;
}

// Validation info for a declaration line in -v mode, like " {V00001, size 2}"
static string GetDeclarationInfo(const DeclarationModel& decl)
{
    if (!g_showvalidation)
        return "";
    string result;
    switch (decl.kind)
    {
    case DeclConst:
        if (!decl.symbols.empty())
        {
            const SymbolModel& symbol = g_program.symbols[decl.symbols[0]];
            result = g_program.TypeToString(symbol.type);
            if (symbol.type != g_typeError)
                result += " " + ConstValueToString(symbol.constvalue, symbol.type);
        }
        break;
    case DeclTypedConst:
    case DeclVar:
        for (size_t i = 0; i < decl.symbols.size(); i++)
        {
            const SymbolModel& symbol = g_program.symbols[decl.symbols[i]];
            if (i > 0)
                result += ", ";
            if (symbol.absolutesym >= 0)
                result += "absolute " + g_program.symbols[symbol.absolutesym].name;
            else if (symbol.absoluteaddr >= 0)
                result += "absolute " + std::to_string(symbol.absoluteaddr);
            else
                result += symbol.location.ToString();
        }
        result += ", size " + std::to_string(Validator::TypeSize(decl.type));
        break;
    case DeclType:
        result = "size " + std::to_string(Validator::TypeSize(decl.type));
        break;
    case DeclProcedure:
        {
            const ProcedureModel& proc = g_program.procedures[decl.procindex];
            result = proc.asmlabel;
            if (proc.directive == ProcDirNone || proc.directive == ProcDirOverlay)
            {
                result += ", params " + std::to_string(proc.paramsize) + ", locals " + std::to_string(proc.localsize);
                if (proc.nested)
                    result += ", static link";
            }
        }
        break;
    default:
        return "";
    }
    return " {" + result + "}";
}

static void DumpDeclaration(std::ostream& out, const DeclarationModel& decl, int indent)
{
    string pad(indent, ' ');
    string info = GetDeclarationInfo(decl);
    switch (decl.kind)
    {
    case DeclLabel:
        out << pad << "LABEL " << JoinTokens(decl.names) << std::endl;
        break;
    case DeclConst:
        out << pad << "CONST " << JoinTokens(decl.names) << " = " << decl.value.ToString() << info << std::endl;
        break;
    case DeclTypedConst:
        out << pad << "CONST " << JoinTokens(decl.names) << ": " << g_program.TypeToString(decl.type)
            << " = " << decl.initvalue.ToString() << info << std::endl;
        break;
    case DeclType:
        out << pad << "TYPE " << JoinTokens(decl.names) << " = " << g_program.TypeToString(decl.type) << info << std::endl;
        break;
    case DeclVar:
        out << pad << "VAR " << JoinTokens(decl.names) << ": " << g_program.TypeToString(decl.type);
        if (!decl.absolute.IsEmpty())
            out << " ABSOLUTE " << decl.absolute.ToString();
        out << info << std::endl;
        break;
    case DeclProcedure:
        {
            const ProcedureModel& proc = g_program.procedures[decl.procindex];
            DumpProcedureHeading(out, proc, indent, info);
            if (proc.directive == ProcDirNone || proc.directive == ProcDirOverlay)
                DumpBlock(out, proc.block, indent + 2);
        }
        break;
    default:
        break;
    }
}

static void DumpBlock(std::ostream& out, const BlockModel& block, int indent)
{
    for (const DeclarationModel& decl : block.declarations)
        DumpDeclaration(out, decl, indent);
    DumpStatement(out, block.body, indent);
}

void ProgramModel::Dump(std::ostream& out) const
{
    g_dumpdirectives = DIRECTIVES_DEFAULT;
    out << "PROGRAM";
    if (name.type != TokenTypeNone)
        out << " " << name.text;
    if (!params.empty())
        out << " (" << JoinTokens(params) << ")";
    out << std::endl;
    if (!procedures.empty())
        DumpBlock(out, procedures[0].block, 2);
}


//////////////////////////////////////////////////////////////////////
