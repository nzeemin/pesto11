
#include <cassert>
#include <cstdio>
#include <algorithm>

#include "main.h"

// Validator: scopes, standard identifiers, declarations, types, memory layout, statements.
// See docs/DESIGN.md section 4.10. Expressions are in validator-expr.cpp,
// standard procedures and functions in validator-std.cpp.


//////////////////////////////////////////////////////////////////////
// Globals

int g_typeInteger = -1, g_typeByte = -1, g_typeBoolean = -1, g_typeChar = -1, g_typeReal = -1, g_typeText = -1;
int g_typeString = -1;
int g_typeUntypedFile = -1;
int g_typeNilPointer = -1;
int g_typeEmptySet = -1;
int g_typeError = -1;
int g_typeVoid = -1;

bool g_showvalidation = false;

const int FILE_BLOCK_SIZE = 528;    // File interface block: 512-byte buffer and the header; preliminary, DESIGN 6.4
const int STRING_TYPE_SIZE = 256;   // string[255]

static string ToUpperName(const string& name)
{
    string result = name;
    for (char& c : result)
    {
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
    }
    return result;
}

static string MakeLabel(char prefix, int number)
{
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%c%05d", prefix, number);
    return buffer;
}

string TempSlot::ToString() const
{
    if (!asmlabel.empty())
        return asmlabel;
    return std::to_string(offset) + "(R5)";
}


//////////////////////////////////////////////////////////////////////
// Validator: scopes and symbols

Validator::Validator() :
    m_procindex(0), m_scope(-1), m_localoffset(0), m_globalcount(0), m_constcount(0), m_proccount(0),
    m_assigntarget(false)
{
}

void Validator::Error(const SourcePos& pos, int code, const string& message)
{
    ReportError(pos, code, message);
}

int Validator::AddType(const TypeModel& type)
{
    int index = g_program.AddType(type);
    m_validatedtypes.insert(index);
    return index;
}

int Validator::AddSymbol(const SymbolModel& symbol, int scope)
{
    g_program.symbols.push_back(symbol);
    int index = (int)g_program.symbols.size() - 1;
    g_program.symbols[index].scope = scope;
    if (scope >= 0)
        g_program.scopes[scope].names[ToUpperName(symbol.name)] = index;
    return index;
}

// Declare a symbol in the current scope; reports a duplicate
int Validator::DeclareSymbol(const Token& name, SymbolKind kind, int type)
{
    if (FindSymbolInScope(name.text, m_scope) >= 0)
        Error(name.pos, 43, "Duplicate identifier or label: " + name.text);
    SymbolModel symbol;
    symbol.name = name.text;
    symbol.pos = name.pos;
    symbol.kind = kind;
    symbol.type = type;
    symbol.level = g_program.procedures[m_procindex].level;
    symbol.procindex = (kind == SymProcedure || kind == SymFunction || kind == SymFuncResult) ? -1 : m_procindex;
    return AddSymbol(symbol, m_scope);
}

int Validator::FindSymbolInScope(const string& name, int scope) const
{
    if (scope < 0)
        return -1;
    const ScopeModel& model = g_program.scopes[scope];
    auto it = model.names.find(ToUpperName(name));
    return (it == model.names.end()) ? -1 : it->second;
}

int Validator::FindSymbol(const string& name) const
{
    for (int scope = m_scope; scope >= 0; scope = g_program.scopes[scope].parent)
    {
        int symbol = FindSymbolInScope(name, scope);
        if (symbol >= 0)
            return symbol;
    }
    return -1;
}

TempSlot Validator::AllocateGlobal(int size, bool isconst)
{
    (void)size;
    TempSlot slot;
    slot.asmlabel = isconst ? MakeLabel('C', ++m_constcount) : MakeLabel('V', ++m_globalcount);
    return slot;
}

// Locals take whole words, the offsets go down from R5
TempSlot Validator::AllocateLocal(int size)
{
    if (size < 2)
        size = 2;
    size = (size + 1) & ~1;
    m_localoffset -= size;
    TempSlot slot;
    slot.offset = m_localoffset;
    return slot;
}

// Temporary for a statement: static in the main program, in the frame in a procedure
TempSlot Validator::AllocateTemp(int size)
{
    if (m_procindex == 0)
        return AllocateGlobal(size, false);
    return AllocateLocal(size);
}


//////////////////////////////////////////////////////////////////////
// Standard scope

void Validator::CreateStandardScope()
{
    g_program.scopes.clear();
    g_program.scopes.push_back(ScopeModel());   // [0] standard identifiers
    m_scope = 0;

    TypeModel type;
    type.kind = TypeKindInteger; type.low = -32768; type.high = 32767; type.size = 2; type.align = 2;
    g_typeInteger = AddType(type);
    type = TypeModel(); type.kind = TypeKindByte; type.low = 0; type.high = 255; type.size = 1;
    g_typeByte = AddType(type);
    type = TypeModel(); type.kind = TypeKindBoolean; type.low = 0; type.high = 1; type.size = 1;
    g_typeBoolean = AddType(type);
    type = TypeModel(); type.kind = TypeKindChar; type.low = 0; type.high = 255; type.size = 1;
    g_typeChar = AddType(type);
    type = TypeModel(); type.kind = TypeKindReal; type.size = 4; type.align = 2;
    g_typeReal = AddType(type);
    type = TypeModel(); type.kind = TypeKindText; type.size = FILE_BLOCK_SIZE; type.align = 2;
    g_typeText = AddType(type);
    type = TypeModel(); type.kind = TypeKindString; type.high = 255; type.size = STRING_TYPE_SIZE;
    g_typeString = AddType(type);
    type = TypeModel(); type.kind = TypeKindFile; type.size = FILE_BLOCK_SIZE; type.align = 2;
    g_typeUntypedFile = AddType(type);
    type = TypeModel(); type.kind = TypeKindPointer; type.size = 2; type.align = 2;
    g_typeNilPointer = AddType(type);
    type = TypeModel(); type.kind = TypeKindSet; type.low = 0; type.high = -1;
    g_typeEmptySet = AddType(type);
    type = TypeModel(); type.kind = TypeKindNone;
    g_typeError = AddType(type);
    g_typeVoid = AddType(type);

    // Mem: array[Integer] of Byte over the whole memory
    type = TypeModel(); type.kind = TypeKindArray; type.indextype = g_typeInteger; type.elemtype = g_typeByte;
    int memtype = AddType(type);
    // MemW: array[Integer] of Integer, words at even addresses (Pesto/11 extension)
    type.elemtype = g_typeInteger;
    int memwtype = AddType(type);

    struct { const char* name; int type; } stdtypes[] =
    {
        { "Integer", g_typeInteger }, { "Byte", g_typeByte }, { "Boolean", g_typeBoolean },
        { "Char", g_typeChar }, { "Real", g_typeReal }, { "Text", g_typeText },
    };
    for (auto& item : stdtypes)
    {
        SymbolModel symbol;
        symbol.name = item.name;
        symbol.kind = SymType;
        symbol.type = item.type;
        AddSymbol(symbol, 0);
    }

    struct { const char* name; int type; int64_t ivalue; double dvalue; } stdconsts[] =
    {
        { "False", g_typeBoolean, 0, 0.0 }, { "True", g_typeBoolean, 1, 0.0 },
        { "MaxInt", g_typeInteger, 32767, 0.0 }, { "Pi", g_typeReal, 0, 3.14159265358979 },
    };
    for (auto& item : stdconsts)
    {
        SymbolModel symbol;
        symbol.name = item.name;
        symbol.kind = SymConst;
        symbol.type = item.type;
        symbol.constvalue.ivalue = item.ivalue;
        symbol.constvalue.dvalue = item.dvalue;
        AddSymbol(symbol, 0);
    }

    struct { const char* name; int type; bool unsupported; } stdvars[] =
    {
        { "Input", g_typeText, false }, { "Output", g_typeText, false }, { "Con", g_typeText, false },
        { "Trm", g_typeText, false }, { "Kbd", g_typeText, false }, { "Lst", g_typeText, false },
        { "Mem", memtype, false }, { "HeapPtr", g_typeInteger, false }, { "BufLen", g_typeInteger, false },
        { "Aux", g_typeText, true }, { "Usr", g_typeText, true }, { "Port", memtype, true },
        { "RecurPtr", g_typeInteger, true }, { "StackPtr", g_typeInteger, true },
        { "ConStPtr", g_typeInteger, true }, { "ConInPtr", g_typeInteger, true }, { "ConOutPtr", g_typeInteger, true },
        { "LstOutPtr", g_typeInteger, true }, { "AuxInPtr", g_typeInteger, true }, { "AuxOutPtr", g_typeInteger, true },
        { "UsrInPtr", g_typeInteger, true }, { "UsrOutPtr", g_typeInteger, true },
        { "MemW", memwtype, false },
    };
    int stdvarindex = 0;
    for (auto& item : stdvars)
    {
        SymbolModel symbol;
        symbol.name = item.name;
        symbol.kind = SymStdVar;
        symbol.type = item.type;
        symbol.stdindex = stdvarindex++;
        symbol.unsupported = item.unsupported;
        AddSymbol(symbol, 0);
    }

    for (int i = 0; m_stdroutines[i].name != nullptr; i++)
    {
        const StdRoutineSpec& spec = m_stdroutines[i];
        SymbolModel symbol;
        symbol.name = spec.name;
        symbol.kind = (spec.kind == SymNone) ? SymStdProc : spec.kind;
        symbol.stdindex = i;
        symbol.unsupported = (spec.kind == SymNone);
        AddSymbol(symbol, 0);
    }
}


//////////////////////////////////////////////////////////////////////
// Program and blocks

void Validator::ValidateProgram()
{
    CreateStandardScope();

    ScopeModel programscope;
    programscope.parent = 0;
    programscope.procindex = 0;
    g_program.scopes.push_back(programscope);
    m_scope = 1;
    m_procindex = 0;
    m_localoffset = 0;

    ProcedureModel& program = g_program.procedures[0];
    program.scope = 1;
    program.asmlabel = "START";

    ValidateBlock(g_program.procedures[0].block);
}

void Validator::ValidateBlock(BlockModel& block)
{
    std::vector<GotoRef> savedgotos;
    savedgotos.swap(m_gotos);

    for (DeclarationModel& decl : block.declarations)
    {
        if (decl.kind != DeclType && !m_pendingpointers.empty())
            ResolvePendingPointers();  // End of a run of type definitions
        ValidateDeclaration(decl);
    }
    ResolvePendingPointers();

    // Every forward procedure of this block must have its body
    for (const auto& item : g_program.scopes[m_scope].names)
    {
        const SymbolModel& symbol = g_program.symbols[item.second];
        if (symbol.forwarded)
            Error(symbol.pos, 73, "Undefined FORWARD procedure: " + symbol.name);
    }

    ValidateStatement(block.body);

    // Every goto target must be placed in this block
    for (const GotoRef& ref : m_gotos)
    {
        if (!g_program.symbols[ref.symbol].labeldefined)
            Error(ref.pos, 40, "Undefined label: " + g_program.symbols[ref.symbol].name);
    }
    m_gotos.swap(savedgotos);
}

void Validator::ValidateDeclaration(DeclarationModel& decl)
{
    switch (decl.kind)
    {
    case DeclLabel: ValidateLabelDecl(decl); break;
    case DeclConst: ValidateConstDecl(decl); break;
    case DeclTypedConst: ValidateTypedConstDecl(decl); break;
    case DeclType: ValidateTypeDecl(decl); break;
    case DeclVar: ValidateVarDecl(decl); break;
    case DeclProcedure: ValidateProcedureDecl(decl); break;
    default: break;
    }
}

void Validator::ValidateLabelDecl(DeclarationModel& decl)
{
    for (const Token& name : decl.names)
        decl.symbols.push_back(DeclareSymbol(name, SymLabel, -1));
}

void Validator::ValidateConstDecl(DeclarationModel& decl)
{
    ConstValue value;
    int type = g_typeError;
    if (!EvaluateConstant(decl.value, value, type))
        type = g_typeError;
    int symbol = DeclareSymbol(decl.names[0], SymConst, type);
    g_program.symbols[symbol].constvalue = value;
    decl.symbols.push_back(symbol);
}

void Validator::ValidateTypedConstDecl(DeclarationModel& decl)
{
    ValidateType(decl.type);
    if (ContainsFile(decl.type) || IsPointerType(decl.type))
        Error(decl.pos, 61, "Files and pointers are not allowed here");
    else
        CheckStructConst(decl.initvalue, decl.type);
    int symbol = DeclareSymbol(decl.names[0], SymTypedConst, decl.type);
    g_program.symbols[symbol].location = AllocateGlobal(TypeSize(decl.type), true);  // Typed constants are static
    decl.symbols.push_back(symbol);
}

void Validator::ValidateTypeDecl(DeclarationModel& decl)
{
    ValidateType(decl.type);
    decl.symbols.push_back(DeclareSymbol(decl.names[0], SymType, decl.type));
}

void Validator::ValidateVarDecl(DeclarationModel& decl)
{
    ValidateType(decl.type);
    int size = TypeSize(decl.type);

    int64_t absaddr = -1;
    int abssymbol = -1;
    if (!decl.absolute.IsEmpty())
    {
        if (decl.names.size() != 1)
            Error(decl.pos, 75, "Illegal use of ABSOLUTE: only one identifier is allowed");
        const ExpressionNode& root = decl.absolute.nodes[decl.absolute.root];
        if (root.kind == NodeIdent && FindSymbol(root.token.text) >= 0 &&
            g_program.symbols[FindSymbol(root.token.text)].kind != SymConst)
        {
            abssymbol = FindSymbol(root.token.text);
            SymbolKind kind = g_program.symbols[abssymbol].kind;
            if (kind != SymVar && kind != SymParam && kind != SymVarParam && kind != SymTypedConst)
            {
                Error(root.token.pos, 75, "Illegal use of ABSOLUTE: variable expected");
                abssymbol = -1;
            }
        }
        else
        {
            ConstValue value;
            int type;
            if (EvaluateConstant(decl.absolute, value, type))
            {
                if (!IsIntegerType(type))
                    Error(root.token.pos, 22, "Integer constant expected");
                else
                    absaddr = value.ivalue & 0xFFFF;
            }
        }
    }

    for (const Token& name : decl.names)
    {
        int symbol = DeclareSymbol(name, SymVar, decl.type);
        SymbolModel& model = g_program.symbols[symbol];
        model.absolutesym = abssymbol;
        model.absoluteaddr = absaddr;
        if (decl.absolute.IsEmpty())
            model.location = (m_procindex == 0) ? AllocateGlobal(size, false) : AllocateLocal(size);
        decl.symbols.push_back(symbol);
    }
}

void Validator::ValidateProcedureDecl(DeclarationModel& decl)
{
    int procindex = decl.procindex;
    ProcedureModel& proc = g_program.procedures[procindex];
    proc.nested = (proc.level >= 2);

    // Body of a forward procedure: the heading comes from the forward declaration
    int existing = FindSymbolInScope(proc.name.text, m_scope);
    if (existing >= 0 && g_program.symbols[existing].forwarded)
    {
        SymbolModel& fwdsymbol = g_program.symbols[existing];
        int fwdindex = fwdsymbol.procindex;
        ProcedureModel& fwd = g_program.procedures[fwdindex];
        bool samekind = (fwd.kind == proc.kind);
        if (!samekind)
            Error(proc.name.pos, 43, "Duplicate identifier: " + proc.name.text);
        if (!proc.params.empty() || proc.resulttype.type != TokenTypeNone)
            Error(proc.name.pos, 0, "The body of a FORWARD procedure must not repeat the parameters");
        proc.params = fwd.params;
        proc.resulttype = fwd.resulttype;
        proc.resulttypeindex = fwd.resulttypeindex;
        proc.forwardof = fwdindex;
        proc.symbol = existing;
        proc.asmlabel = fwd.asmlabel;
        fwd.bodyproc = procindex;
        fwdsymbol.forwarded = false;
        fwdsymbol.procindex = procindex;
        decl.symbols.push_back(existing);
    }
    else
    {
        if (proc.directive == ProcDirOverlay)
            Error(proc.name.pos, 200, "Not supported in Pesto/11: OVERLAY");

        // Heading: parameter types and the result type, resolved in the declaring scope
        for (ParamGroupModel& group : proc.params)
        {
            if (group.typename_.type == TokenTypeNone)
                continue;  // Untyped var parameter
            group.paramtype = ResolveTypeName(group.typename_);
            if (group.paramtype != g_typeError && !group.isvar && ContainsFile(group.paramtype))
                Error(group.typename_.pos, 67, "Files must be VAR parameters");
        }
        if (proc.kind == ProcKindFunction)
        {
            if (proc.resulttype.type == TokenTypeNone)
                Error(proc.name.pos, 0, "Function result type expected");
            else
            {
                proc.resulttypeindex = ResolveTypeName(proc.resulttype);
                int rt = proc.resulttypeindex;
                if (rt != g_typeError && !IsOrdinal(rt) && !IsRealType(rt) && !IsStringType(rt) && !IsPointerType(rt))
                    Error(proc.resulttype.pos, 48, "Invalid result type");
            }
        }

        int symbol = DeclareSymbol(proc.name, (proc.kind == ProcKindFunction) ? SymFunction : SymProcedure,
                (proc.kind == ProcKindFunction) ? proc.resulttypeindex : -1);
        g_program.symbols[symbol].procindex = procindex;
        g_program.symbols[symbol].forwarded = (proc.directive == ProcDirForward);
        proc.symbol = symbol;
        proc.asmlabel = MakeLabel('P', ++m_proccount);
        decl.symbols.push_back(symbol);

        if (proc.directive == ProcDirExternal)
        {
            ConstValue value;
            int type;
            if (EvaluateConstant(proc.external, value, type) && !IsIntegerType(type))
                Error(proc.name.pos, 22, "Integer constant expected");
        }
    }

    if (proc.directive == ProcDirNone || proc.directive == ProcDirOverlay)
        ValidateProcedureBody(procindex);
}

// Parameters, result and block of a procedure with a body
void Validator::ValidateProcedureBody(int procindex)
{
    int savedprocindex = m_procindex;
    int savedscope = m_scope;
    int savedlocaloffset = m_localoffset;
    std::vector<WithEntry> savedwiths;
    savedwiths.swap(m_withs);

    ScopeModel scope;
    scope.parent = m_scope;
    scope.procindex = procindex;
    g_program.scopes.push_back(scope);
    m_scope = (int)g_program.scopes.size() - 1;
    m_procindex = procindex;
    g_program.procedures[procindex].scope = m_scope;
    m_localoffset = g_program.procedures[procindex].nested ? -2 : 0;  // -2(R5): static link

    // Parameter layout: pushed left to right, the last one at 4(R5)
    struct ParamInfo { const ParamGroupModel* group; const Token* name; int size; };
    std::vector<ParamInfo> params;
    int total = 0;
    for (const ParamGroupModel& group : g_program.procedures[procindex].params)
    {
        for (const Token& name : group.names)
        {
            int size = 2;
            if (!group.isvar && group.paramtype >= 0 && IsRealType(group.paramtype))
                size = 4;
            params.push_back({ &group, &name, size });
            total += size;
        }
    }
    int offset = 4 + total;
    for (const ParamInfo& info : params)
    {
        offset -= info.size;
        int type = (info.group->paramtype >= 0) ? info.group->paramtype : g_typeError;
        int symbol = DeclareSymbol(*info.name, info.group->isvar ? SymVarParam : SymParam, type);
        SymbolModel& model = g_program.symbols[symbol];
        bool structured = !info.group->isvar && type != g_typeError &&
            (IsStringType(type) || IsSetType(type) || IsArrayType(type) || IsRecordType(type));
        if (structured)
        {
            // The caller passes the address, the procedure copies the value into its frame
            model.paramoffset = offset;
            model.location = AllocateLocal(TypeSize(type));
        }
        else
        {
            model.location.offset = offset;
            model.indirect = info.group->isvar;
        }
        if (info.group->paramtype < 0)
            model.type = -1;  // Untyped var parameter
    }

    ProcedureModel& proc = g_program.procedures[procindex];
    proc.paramsize = total;
    if (proc.kind == ProcKindFunction)
    {
        int rt = (proc.resulttypeindex >= 0) ? proc.resulttypeindex : g_typeError;
        int symbol = DeclareSymbol(proc.name, SymFuncResult, rt);
        SymbolModel& model = g_program.symbols[symbol];
        model.procindex = procindex;
        if (rt != g_typeError && IsRealType(rt))
            model.location.offset = 4 + total;  // Slot reserved by the caller above the parameters
        else if (rt != g_typeError && IsStringType(rt))
        {
            model.location.offset = 4 + total;  // Hidden address of the result buffer
            model.indirect = true;
            g_program.procedures[procindex].paramsize += 2;
        }
        else
            model.location = AllocateLocal(2);
        g_program.procedures[procindex].resultsymbol = symbol;
    }

    ValidateBlock(g_program.procedures[procindex].block);
    g_program.procedures[procindex].localsize = -m_localoffset;

    m_withs.swap(savedwiths);
    m_procindex = savedprocindex;
    m_scope = savedscope;
    m_localoffset = savedlocaloffset;
}


//////////////////////////////////////////////////////////////////////
// Types

int Validator::Resolve(int type)
{
    while (type >= 0 && type < (int)g_program.types.size() && g_program.types[type].kind == TypeKindName)
    {
        int resolved = g_program.types[type].resolved;
        if (resolved < 0)
            return g_typeError;
        type = resolved;
    }
    if (type < 0)
        return g_typeError;
    return type;
}

int Validator::OrdinalBase(int type)
{
    type = Resolve(type);
    const TypeModel& model = g_program.types[type];
    if (model.kind == TypeKindSubrange)
        return (model.elemtype >= 0) ? OrdinalBase(model.elemtype) : g_typeError;
    if (model.kind == TypeKindByte)
        return g_typeInteger;
    return type;
}

bool Validator::IsOrdinal(int type)
{
    TypeKind kind = GetType(OrdinalBase(type)).kind;
    return kind == TypeKindInteger || kind == TypeKindChar || kind == TypeKindBoolean || kind == TypeKindEnum;
}

bool Validator::IsScalar(int type)
{
    return IsOrdinal(type) || IsPointerType(type);
}

bool Validator::IsIntegerType(int type) { return OrdinalBase(type) == g_typeInteger; }
bool Validator::IsRealType(int type) { return GetType(type).kind == TypeKindReal; }
bool Validator::IsCharType(int type) { return OrdinalBase(type) == g_typeChar; }
bool Validator::IsBooleanType(int type) { return OrdinalBase(type) == g_typeBoolean; }
bool Validator::IsStringType(int type) { return GetType(type).kind == TypeKindString; }
bool Validator::IsSetType(int type) { return GetType(type).kind == TypeKindSet; }
bool Validator::IsPointerType(int type) { return GetType(type).kind == TypeKindPointer; }
bool Validator::IsTextType(int type) { return GetType(type).kind == TypeKindText; }
bool Validator::IsRecordType(int type) { return GetType(type).kind == TypeKindRecord; }
bool Validator::IsArrayType(int type) { return GetType(type).kind == TypeKindArray; }

bool Validator::IsFileType(int type)
{
    TypeKind kind = GetType(type).kind;
    return kind == TypeKindFile || kind == TypeKindText;
}

// Array of Char with one index: usable in string expressions
bool Validator::IsCharArray(int type)
{
    const TypeModel& model = GetType(type);
    return model.kind == TypeKindArray && model.elemtype >= 0 && GetType(model.elemtype).kind == TypeKindChar;
}

bool Validator::IsStringLike(int type)
{
    return IsStringType(type) || IsCharType(type) || IsCharArray(type);
}

bool Validator::OrdinalCompatible(int a, int b)
{
    if (!IsOrdinal(a) || !IsOrdinal(b))
        return false;
    return OrdinalBase(a) == OrdinalBase(b);
}

bool Validator::SetCompatible(int a, int b)
{
    if (!IsSetType(a) || !IsSetType(b))
        return false;
    a = Resolve(a);
    b = Resolve(b);
    if (a == g_typeEmptySet || b == g_typeEmptySet)
        return true;
    return OrdinalCompatible(g_program.types[a].elemtype, g_program.types[b].elemtype);
}

bool Validator::PointerCompatible(int a, int b)
{
    if (!IsPointerType(a) || !IsPointerType(b))
        return false;
    a = Resolve(a);
    b = Resolve(b);
    if (a == g_typeNilPointer || b == g_typeNilPointer)
        return true;
    return Resolve(g_program.types[a].elemtype) == Resolve(g_program.types[b].elemtype);
}

int Validator::TypeSize(int type)
{
    return GetType(type).size;
}

bool Validator::ContainsFile(int type)
{
    const TypeModel& model = GetType(type);
    switch (model.kind)
    {
    case TypeKindFile:
    case TypeKindText:
        return true;
    case TypeKindArray:
        return ContainsFile(model.elemtype);
    case TypeKindRecord:
        {
            std::vector<const RecordPartModel*> parts { &model.record };
            while (!parts.empty())
            {
                const RecordPartModel* part = parts.back();
                parts.pop_back();
                for (const FieldModel& field : part->fields)
                {
                    if (ContainsFile(field.type))
                        return true;
                }
                for (const VariantModel& variant : part->variants)
                    parts.push_back(&variant.part);
            }
        }
        return false;
    default:
        return false;
    }
}

bool Validator::GetOrdinalRange(int type, int64_t& low, int64_t& high)
{
    if (!IsOrdinal(type))
        return false;
    const TypeModel& model = GetType(type);
    low = model.low;
    high = model.high;
    return true;
}

// Find a type by its name; reports an error and returns g_typeError if it is not a type
int Validator::ResolveTypeName(const Token& name)
{
    int symbol = FindSymbol(name.text);
    if (symbol < 0)
    {
        Error(name.pos, 41, "Unknown identifier: " + name.text);
        return g_typeError;
    }
    const SymbolModel& model = g_program.symbols[symbol];
    if (model.kind != SymType)
    {
        Error(name.pos, 36, "Type identifier expected: " + name.text);
        return g_typeError;
    }
    return model.type;
}

void Validator::ResolvePendingPointers()
{
    std::vector<int> pending;
    pending.swap(m_pendingpointers);
    for (int nametype : pending)
    {
        TypeModel& model = g_program.types[nametype];
        int symbol = FindSymbol(model.name.text);
        if (symbol < 0 || g_program.symbols[symbol].kind != SymType)
            Error(model.name.pos, 42, "Undefined pointer type in preceding type definitions: " + model.name.text);
        else
            g_program.types[nametype].resolved = g_program.symbols[symbol].type;
    }
}

int Validator::GetSetType(int basetype)
{
    int base = OrdinalBase(basetype);
    auto it = m_settypes.find(base);
    if (it != m_settypes.end())
        return it->second;
    TypeModel type;
    type.kind = TypeKindSet;
    type.elemtype = base;
    type.low = 0;
    type.high = 255;
    type.size = 32;
    int index = AddType(type);
    m_settypes[base] = index;
    return index;
}

void Validator::ValidateType(int index)
{
    if (index < 0 || m_validatedtypes.count(index) > 0)
        return;
    m_validatedtypes.insert(index);

    TypeModel& type = g_program.types[index];
    switch (type.kind)
    {
    case TypeKindName:
        g_program.types[index].resolved = ResolveTypeName(g_program.types[index].name);
        break;
    case TypeKindEnum:
        for (size_t i = 0; i < g_program.types[index].enumnames.size(); i++)
        {
            Token name = g_program.types[index].enumnames[i];
            int symbol = DeclareSymbol(name, SymConst, index);
            g_program.symbols[symbol].constvalue.ivalue = (int64_t)i;
        }
        g_program.types[index].low = 0;
        g_program.types[index].high = (int64_t)g_program.types[index].enumnames.size() - 1;
        break;
    case TypeKindSubrange:
        {
            ConstValue low, high;
            int lowtype = g_typeError, hightype = g_typeError;
            // Both bounds are evaluated, so that errors in both are reported
            bool lowok = EvaluateConstant(g_program.types[index].lowexpr, low, lowtype);
            bool highok = EvaluateConstant(g_program.types[index].highexpr, high, hightype);
            bool ok = lowok && highok;
            TypeModel& model = g_program.types[index];
            if (!ok)
            {
                model.elemtype = g_typeError;
                break;
            }
            if (!IsOrdinal(lowtype) || !IsOrdinal(hightype))
            {
                Error(model.pos, 51, "Invalid subrange base type");
                model.elemtype = g_typeError;
                break;
            }
            if (!OrdinalCompatible(lowtype, hightype))
            {
                Error(model.pos, 44, "Type mismatch");
                model.elemtype = g_typeError;
                break;
            }
            model.elemtype = OrdinalBase(lowtype);
            model.low = low.ivalue;
            model.high = high.ivalue;
            if (model.low > model.high)
                Error(model.pos, 52, "Lower bound > upper bound");
        }
        break;
    case TypeKindString:
        {
            ConstValue length;
            int lengthtype;
            if (EvaluateConstant(g_program.types[index].highexpr, length, lengthtype))
            {
                TypeModel& model = g_program.types[index];
                if (!IsIntegerType(lengthtype) || length.ivalue < 1 || length.ivalue > 255)
                    Error(model.pos, 49, "Invalid string length, must be 1..255");
                else
                    model.high = length.ivalue;
            }
        }
        break;
    case TypeKindArray:
        {
            int indextype = g_program.types[index].indextype;
            int elemtype = g_program.types[index].elemtype;
            ValidateType(indextype);
            ValidateType(elemtype);
            if (Resolve(indextype) != g_typeError && (!IsOrdinal(indextype)))
                Error(g_program.types[indextype].pos, 30, "Simple type expected for the array index");
        }
        break;
    case TypeKindRecord:
        {
            int offset = 0, align = 1;
            ValidateRecordPart(g_program.types[index].record, offset, align);
            // Duplicate field names in the whole record
            std::set<string> names;
            std::vector<const RecordPartModel*> parts { &g_program.types[index].record };
            while (!parts.empty())
            {
                const RecordPartModel* part = parts.back();
                parts.pop_back();
                std::vector<Token> fieldnames;
                for (const FieldModel& field : part->fields)
                    fieldnames.push_back(field.name);
                if (part->tagname.type != TokenTypeNone)
                    fieldnames.push_back(part->tagname);
                for (const Token& name : fieldnames)
                {
                    if (!names.insert(ToUpperName(name.text)).second)
                        Error(name.pos, 43, "Duplicate identifier: " + name.text);
                }
                for (const VariantModel& variant : part->variants)
                    parts.push_back(&variant.part);
            }
        }
        break;
    case TypeKindSet:
        {
            int elemtype = g_program.types[index].elemtype;
            ValidateType(elemtype);
            int64_t low = 0, high = 0;
            if (Resolve(elemtype) == g_typeError)
                break;
            if (!GetOrdinalRange(elemtype, low, high) || low < 0 || high > 255)
                Error(g_program.types[index].pos, 70, "Set base type out of range, must be within 0..255");
            g_program.types[index].low = low;
            g_program.types[index].high = high;
        }
        break;
    case TypeKindPointer:
        {
            // The target may be declared later in the same type section
            int target = g_program.types[index].elemtype;
            m_validatedtypes.insert(target);
            TypeModel& targetmodel = g_program.types[target];
            int symbol = FindSymbol(targetmodel.name.text);
            if (symbol >= 0 && g_program.symbols[symbol].kind == SymType)
                g_program.types[target].resolved = g_program.symbols[symbol].type;
            else
                m_pendingpointers.push_back(target);
        }
        break;
    case TypeKindFile:
        {
            int elemtype = g_program.types[index].elemtype;
            if (elemtype >= 0)
            {
                ValidateType(elemtype);
                if (ContainsFile(elemtype))
                    Error(g_program.types[index].pos, 68, "File components may not be files");
            }
        }
        break;
    default:
        break;
    }
    ComputeTypeLayout(g_program.types[index]);
}

void Validator::ValidateRecordPart(RecordPartModel& part, int& offset, int& align)
{
    for (FieldModel& field : part.fields)
    {
        ValidateType(field.type);
        const TypeModel& fieldtype = GetType(field.type);
        if (fieldtype.align == 2)
            offset = (offset + 1) & ~1;
        field.offset = offset;
        offset += fieldtype.size;
        if (fieldtype.align > align)
            align = fieldtype.align;
    }
    if (part.hasvariant)
    {
        ValidateType(part.tagtype);
        if (Resolve(part.tagtype) != g_typeError && !IsOrdinal(part.tagtype))
            Error(g_program.types[part.tagtype].pos, 30, "Simple type expected for the tag field");
        if (part.tagname.type != TokenTypeNone)
        {
            const TypeModel& tagtype = GetType(part.tagtype);
            if (tagtype.align == 2)
                offset = (offset + 1) & ~1;
            part.tagoffset = offset;
            offset += tagtype.size;
            if (tagtype.align > align)
                align = tagtype.align;
        }
        int maxoffset = offset;
        for (VariantModel& variant : part.variants)
        {
            for (CaseLabelModel& label : variant.labels)
            {
                ConstValue value;
                int type;
                if (EvaluateConstant(label.from, value, type) && Resolve(part.tagtype) != g_typeError &&
                    !OrdinalCompatible(type, part.tagtype))
                    Error(label.from.nodes[label.from.root].token.pos, 46, "Constant and tag field type do not match");
                if (!label.to.IsEmpty())
                    EvaluateConstant(label.to, value, type);
            }
            int variantoffset = offset;
            ValidateRecordPart(variant.part, variantoffset, align);
            if (variantoffset > maxoffset)
                maxoffset = variantoffset;
        }
        offset = maxoffset;
    }
    part.size = offset;
}

void Validator::ComputeTypeLayout(TypeModel& type)
{
    switch (type.kind)
    {
    case TypeKindName:
        {
            const TypeModel& resolved = GetType(type.resolved);
            type.size = resolved.size;
            type.align = resolved.align;
        }
        break;
    case TypeKindEnum:
        type.size = (type.high < 256) ? 1 : 2;
        type.align = type.size;
        break;
    case TypeKindSubrange:
        if (type.elemtype < 0 || Resolve(type.elemtype) == g_typeError)
            type.size = 2;
        else if (OrdinalBase(type.elemtype) == g_typeInteger)
            type.size = (type.low >= 0 && type.high <= 255) ? 1 : 2;
        else
            type.size = GetType(type.elemtype).size;
        type.align = type.size;
        break;
    case TypeKindString:
        type.size = (int)type.high + 1;
        type.align = 1;
        break;
    case TypeKindArray:
        {
            int64_t low = 0, high = -1;
            GetOrdinalRange(type.indextype, low, high);
            const TypeModel& elem = GetType(type.elemtype);
            int64_t size = (high - low + 1) * elem.size;
            type.align = elem.align;
            if (size > 0xFFFF)
            {
                Error(type.pos, 98, "Memory overflow: the array is too big");
                size = 0;
            }
            type.size = (int)size;
        }
        break;
    case TypeKindRecord:
        {
            int align = 1;
            for (const FieldModel& field : type.record.fields)
                align = std::max(align, GetType(field.type).align);
            std::vector<const RecordPartModel*> parts { &type.record };
            while (!parts.empty())
            {
                const RecordPartModel* part = parts.back();
                parts.pop_back();
                for (const FieldModel& field : part->fields)
                    align = std::max(align, GetType(field.type).align);
                if (part->hasvariant && part->tagname.type != TokenTypeNone)
                    align = std::max(align, GetType(part->tagtype).align);
                for (const VariantModel& variant : part->variants)
                    parts.push_back(&variant.part);
            }
            type.align = align;
            type.size = type.record.size;
            if (align == 2)
                type.size = (type.size + 1) & ~1;
        }
        break;
    case TypeKindSet:
        type.size = (int)((type.high / 8) - (type.low / 8) + 1);
        if (type.size < 1)
            type.size = 1;
        type.align = 1;
        break;
    case TypeKindPointer:
        type.size = 2;
        type.align = 2;
        break;
    case TypeKindFile:
        type.size = FILE_BLOCK_SIZE;
        type.align = 2;
        break;
    default:
        break;
    }
}

// Typed constant value against its type
void Validator::CheckStructConst(StructConstModel& value, int type)
{
    if (Resolve(type) == g_typeError)
        return;
    const TypeModel model = GetType(type);  // A copy: nested checks may add types and move the vector
    switch (value.kind)
    {
    case StructConstModel::ConstSimple:
        {
            ConstValue cv;
            int cvtype;
            if (!EvaluateConstant(value.value, cv, cvtype))
                return;
            if (IsCharArray(type) && IsStringLike(cvtype))
            {
                int64_t low = 0, high = -1;
                GetOrdinalRange(model.indextype, low, high);
                size_t length = IsCharType(cvtype) ? 1 : cv.svalue.size();
                if ((int64_t)length != high - low + 1)
                    Error(value.pos, 50, "String constant length does not match type");
                return;
            }
            CheckAssignable(type, value.value, value.value.root, value.pos, true);
        }
        break;
    case StructConstModel::ConstList:
        if (model.kind == TypeKindArray)
        {
            int64_t low = 0, high = -1;
            GetOrdinalRange(model.indextype, low, high);
            if ((int64_t)value.items.size() != high - low + 1)
                Error(value.pos, 0, "Wrong number of elements in the array constant, expected " + std::to_string(high - low + 1));
            for (StructConstModel& item : value.items)
            {
                if (item.fieldname.type != TokenTypeNone)
                    Error(item.fieldname.pos, 44, "Type mismatch: array constant expected");
                else
                    CheckStructConst(item, model.elemtype);
            }
        }
        else if (model.kind == TypeKindRecord)
        {
            for (StructConstModel& item : value.items)
            {
                int offset, fieldtype;
                if (item.fieldname.type == TokenTypeNone)
                    Error(item.pos, 44, "Type mismatch: record constant expected");
                else if (!FindField(type, item.fieldname.text, offset, fieldtype))
                    Error(item.fieldname.pos, 41, "Unknown field: " + item.fieldname.text);
                else
                    CheckStructConst(item, fieldtype);
            }
        }
        else
            Error(value.pos, 44, "Type mismatch");
        break;
    case StructConstModel::ConstSet:
        if (model.kind != TypeKindSet)
        {
            Error(value.pos, 44, "Type mismatch");
            break;
        }
        for (StructConstModel& item : value.items)
        {
            ConstValue cv;
            int cvtype;
            if (EvaluateConstant(item.value, cv, cvtype) && !OrdinalCompatible(cvtype, model.elemtype))
                Error(item.pos, 44, "Type mismatch in the set constant");
            if (!item.rangeto.IsEmpty() && EvaluateConstant(item.rangeto, cv, cvtype) && !OrdinalCompatible(cvtype, model.elemtype))
                Error(item.pos, 44, "Type mismatch in the set constant");
        }
        break;
    }
}


//////////////////////////////////////////////////////////////////////
// Statements

void Validator::ValidateStatements(std::vector<StatementModel>& statements)
{
    for (StatementModel& statement : statements)
        ValidateStatement(statement);
}

void Validator::ValidateLabelPlacement(StatementModel& statement)
{
    const Token& label = statement.label;
    int symbol = FindSymbolInScope(label.text, m_scope);
    if (symbol < 0 || g_program.symbols[symbol].kind != SymLabel)
    {
        Error(label.pos, 40, "Undefined label: " + label.text + " is not declared in this block");
        return;
    }
    SymbolModel& model = g_program.symbols[symbol];
    if (model.labeldefined)
        Error(label.pos, 43, "Duplicate label: " + label.text);
    model.labeldefined = true;
    statement.labelsymbol = symbol;
}

void Validator::ValidateStatement(StatementModel& statement)
{
    if (statement.label.type != TokenTypeNone)
        ValidateLabelPlacement(statement);

    switch (statement.kind)
    {
    case StmtEmpty:
        break;
    case StmtCompound:
        ValidateStatements(statement.statements);
        break;
    case StmtAssign:
        ValidateAssign(statement);
        break;
    case StmtCall:
        ValidateCallStatement(statement);
        break;
    case StmtGoto:
        ValidateGoto(statement);
        break;
    case StmtInline:
        ValidateInline(statement);
        break;
    case StmtIf:
        ValidateCondition(statement.expr, statement.token.pos);
        ValidateStatements(statement.statements);
        break;
    case StmtCase:
        ValidateCase(statement);
        break;
    case StmtWhile:
        ValidateCondition(statement.expr, statement.token.pos);
        ValidateStatements(statement.statements);
        break;
    case StmtRepeat:
        ValidateStatements(statement.statements);
        ValidateCondition(statement.expr, statement.token.pos);
        break;
    case StmtFor:
        ValidateFor(statement);
        break;
    case StmtWith:
        ValidateWith(statement);
        break;
    }
}

void Validator::ValidateCondition(ExpressionModel& expr, const SourcePos& pos)
{
    int type = ValidateValue(expr, expr.root);
    if (type != g_typeError && !IsBooleanType(type))
    {
        const SourcePos& exprpos = (expr.root >= 0) ? expr.nodes[expr.root].token.pos : pos;
        Error(exprpos, 20, "Boolean expression expected");
    }
}

void Validator::ValidateAssign(StatementModel& statement)
{
    ExpressionModel& target = statement.target;
    m_assigntarget = true;
    int targettype = ValidateNode(target, target.root);
    m_assigntarget = false;
    int valuetype = ValidateValue(statement.expr, statement.expr.root);
    if (targettype == g_typeError || valuetype == g_typeError)
        return;

    const ExpressionNode& node = target.nodes[target.root];
    bool isresult = (node.kind == NodeIdent && node.symbol >= 0 && g_program.symbols[node.symbol].kind == SymFuncResult);
    if (!isresult && !IsVariable(target, target.root))
    {
        Error(node.token.pos, 54, "Illegal assignment: variable expected");
        return;
    }
    if (isresult)
        targettype = g_program.symbols[node.symbol].type;
    if (IsFileType(targettype))
    {
        Error(node.token.pos, 54, "Illegal assignment to a file variable");
        return;
    }
    CheckAssignable(targettype, statement.expr, statement.expr.root, node.token.pos, true);
}

void Validator::ValidateCallStatement(StatementModel& statement)
{
    ExpressionModel& target = statement.target;
    int type = ValidateNode(target, target.root);
    if (type == g_typeError || type == g_typeVoid)
        return;
    const ExpressionNode& node = target.nodes[target.root];
    Error(node.token.pos, 0, "Procedure expected: " + node.token.text + " does not name a procedure");
}

void Validator::ValidateFor(StatementModel& statement)
{
    ExpressionModel& var = statement.target;
    if (var.root < 0)
        return;
    int vartype = ValidateNode(var, var.root);
    const ExpressionNode& node = var.nodes[var.root];
    bool simplevar = node.symbol >= 0 && node.fieldoffset < 0 &&
        (g_program.symbols[node.symbol].kind == SymVar || g_program.symbols[node.symbol].kind == SymParam ||
         g_program.symbols[node.symbol].kind == SymVarParam || g_program.symbols[node.symbol].kind == SymTypedConst);
    if (vartype != g_typeError)
    {
        if (!simplevar)
            Error(node.token.pos, 0, "Control variable expected: a simple variable");
        else if (!IsOrdinal(vartype))
            Error(node.token.pos, 30, "Simple type expected for the control variable");
    }
    int t1 = ValidateValue(statement.expr, statement.expr.root);
    int t2 = ValidateValue(statement.expr2, statement.expr2.root);
    if (vartype != g_typeError && simplevar && IsOrdinal(vartype))
    {
        if (t1 != g_typeError && !OrdinalCompatible(vartype, t1))
            Error(statement.expr.nodes[statement.expr.root].token.pos, 44, "Type mismatch");
        if (t2 != g_typeError && !OrdinalCompatible(vartype, t2))
            Error(statement.expr2.nodes[statement.expr2.root].token.pos, 44, "Type mismatch");
    }
    statement.forlimit = AllocateTemp(2);
    ValidateStatements(statement.statements);
}

void Validator::ValidateCase(StatementModel& statement)
{
    int selector = ValidateValue(statement.expr, statement.expr.root);
    bool selectorok = (selector != g_typeError);
    if (selectorok && !IsOrdinal(selector))
    {
        Error(statement.expr.nodes[statement.expr.root].token.pos, 30, "Simple type expected for the case selector");
        selectorok = false;
    }

    std::vector<std::pair<int64_t, int64_t>> ranges;
    for (CaseElementModel& element : statement.caseelements)
    {
        for (CaseLabelModel& label : element.labels)
        {
            ConstValue from, to;
            int fromtype, totype = g_typeError;
            if (!EvaluateConstant(label.from, from, fromtype))
                continue;
            to = from;
            totype = fromtype;
            if (!label.to.IsEmpty() && !EvaluateConstant(label.to, to, totype))
                continue;
            const SourcePos& pos = label.from.nodes[label.from.root].token.pos;
            if (selectorok && (!OrdinalCompatible(fromtype, selector) || !OrdinalCompatible(totype, selector)))
            {
                Error(pos, 46, "Constant and CASE selector type does not match");
                continue;
            }
            if (from.ivalue > to.ivalue)
            {
                Error(pos, 52, "Lower bound > upper bound");
                continue;
            }
            for (const auto& range : ranges)
            {
                if (from.ivalue <= range.second && to.ivalue >= range.first)
                {
                    Error(pos, 0, "Duplicate case label");
                    break;
                }
            }
            ranges.push_back(std::make_pair(from.ivalue, to.ivalue));
        }
        ValidateStatements(element.statement);
    }
    ValidateStatements(statement.statements);
}

void Validator::ValidateWith(StatementModel& statement)
{
    size_t savedcount = m_withs.size();
    for (ExpressionModel& var : statement.withvars)
    {
        int type = ValidateNode(var, var.root);
        TempSlot slot = AllocateTemp(2);
        statement.withslots.push_back(slot);
        if (type == g_typeError)
            continue;
        const ExpressionNode& node = var.nodes[var.root];
        if (!IsRecordType(type) || !IsVariable(var, var.root))
        {
            Error(node.token.pos, 29, "Record variable expected");
            continue;
        }
        WithEntry entry;
        entry.type = type;
        entry.slot = slot;
        m_withs.push_back(entry);
    }
    ValidateStatements(statement.statements);
    m_withs.resize(savedcount);
}

void Validator::ValidateGoto(StatementModel& statement)
{
    const Token& label = statement.gotolabel;
    int symbol = FindSymbol(label.text);
    if (symbol < 0 || g_program.symbols[symbol].kind != SymLabel)
    {
        Error(label.pos, 40, "Undefined label: " + label.text);
        return;
    }
    if (g_program.symbols[symbol].scope != m_scope)
    {
        Error(label.pos, 72, "Label not within current block: " + label.text);
        return;
    }
    statement.gotosymbol = symbol;
    m_gotos.push_back({ symbol, label.pos });
}

void Validator::ValidateInline(StatementModel& statement)
{
    for (InlineItemModel& item : statement.inlineitems)
    {
        for (InlineTermModel& term : item.terms)
        {
            if (term.token.type != TokenTypeIdentifier)
                continue;
            int symbol = FindSymbol(term.token.text);
            if (symbol < 0)
            {
                Error(term.token.pos, 41, "Unknown identifier: " + term.token.text);
                continue;
            }
            SymbolKind kind = g_program.symbols[symbol].kind;
            if (kind != SymConst && kind != SymVar && kind != SymParam && kind != SymVarParam &&
                kind != SymTypedConst && kind != SymProcedure && kind != SymFunction)
                Error(term.token.pos, 74, "INLINE error: constant, variable or procedure expected");
        }
    }
}


//////////////////////////////////////////////////////////////////////
