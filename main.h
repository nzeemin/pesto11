#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <iostream>
#include <filesystem>

typedef std::string string;

const char* const PESTO11_VERSION = "0.0.1";


//////////////////////////////////////////////////////////////////////
// Enums

enum TargetPlatform
{
    PlatformNone        = 0,
    PlatformUKNC        = 1,
    PlatformBK0010      = 2,
};


//////////////////////////////////////////////////////////////////////
// Globals, see main.cpp

extern bool g_quiet;            // Be quiet
extern TargetPlatform g_platform;
extern bool g_onefile;          // Generate single output file including the program and the runtime
extern bool g_noopt;            // Disable peephole optimization
extern bool g_tokenizeonly;     // Show tokenization and quit
extern bool g_parsingonly;      // Show parsing result and quit
extern bool g_validationonly;   // Show validation result and quit
extern bool g_showgeneration;   // Echo generated lines to stdout

const char* GetPlatformName(TargetPlatform platform);
TargetPlatform FindPlatformByName(const string& name);


//////////////////////////////////////////////////////////////////////
// Source positions and diagnostics, see main.cpp

// Position in the source text; file is an index in the source file list
struct SourcePos
{
    int file;   // Index in g_sourcefiles, -1 if unknown
    int line;   // 1-based, 0 if unknown
    int col;    // 1-based, 0 if unknown
public:
    SourcePos() : file(-1), line(0), col(0) {}
    SourcePos(int f, int l, int c) : file(f), line(l), col(c) {}
};

extern std::vector<string> g_sourcefiles;  // Main source file first, then include files

// Register a source file name, returns its index for SourcePos::file
int RegisterSourceFile(const string& filename);

// Error/warning output in the form "FILE.PAS(12,5): error 44: Type mismatch"
// code <= 0 means the message has no TP3 error number
void ReportError(const SourcePos& pos, int code, const string& message);
void ReportWarning(const SourcePos& pos, const string& message);
// Error/warning not tied to a source position, like "pesto11: error: ..."
void ReportError(const string& message);
void ReportWarning(const string& message);

int  GetErrorCount();
void ResetErrorCount();


//////////////////////////////////////////////////////////////////////
// Utilities, see utility.cpp

// Full path of the running executable, to find the runtime templates next to it
std::filesystem::path getexepath();

// Read whole file in binary mode; returns false if the file cannot be read
bool ReadFileBinary(const std::filesystem::path& path, string& content);

// Find the file ignoring letter case in the file name; returns empty path if not found
std::filesystem::path FindFileIgnoreCase(const std::filesystem::path& path);


//////////////////////////////////////////////////////////////////////
// Tokens, see tokenizer.cpp

//NOTE: This enum should be in the same order as the Keywords array in tokenizer.cpp
enum KeywordIndex
{
    KeywordNone = 0,
    KeywordABSOLUTE, KeywordAND, KeywordARRAY, KeywordBEGIN, KeywordCASE, KeywordCONST,
    KeywordDIV, KeywordDO, KeywordDOWNTO, KeywordELSE, KeywordEND, KeywordEXTERNAL,
    KeywordFILE, KeywordFOR, KeywordFORWARD, KeywordFUNCTION, KeywordGOTO, KeywordIF,
    KeywordIN, KeywordINLINE, KeywordLABEL, KeywordMOD, KeywordNIL, KeywordNOT,
    KeywordOF, KeywordOR, KeywordOVERLAY, KeywordPACKED, KeywordPROCEDURE, KeywordPROGRAM,
    KeywordRECORD, KeywordREPEAT, KeywordSET, KeywordSHL, KeywordSHR, KeywordSTRING,
    KeywordTHEN, KeywordTO, KeywordTYPE, KeywordUNTIL, KeywordVAR, KeywordWHILE,
    KeywordWITH, KeywordXOR,
    __KeywordIndex_SIZE__
};

//NOTE: This enum should be in the same order as the Symbols array in tokenizer.cpp
enum SymbolIndex
{
    SymbolNone = 0,
    SymbolPlus,         // +
    SymbolMinus,        // -
    SymbolStar,         // *
    SymbolSlash,        // /
    SymbolEqual,        // =
    SymbolNotEqual,     // <>
    SymbolLess,         // <
    SymbolGreater,      // >
    SymbolLessEqual,    // <=
    SymbolGreaterEqual, // >=
    SymbolAssign,       // :=
    SymbolColon,        // :
    SymbolSemicolon,    // ;
    SymbolComma,        // ,
    SymbolDot,          // .
    SymbolDotDot,       // ..
    SymbolLParen,       // (
    SymbolRParen,       // )
    SymbolLBracket,     // [ or (.
    SymbolRBracket,     // ] or .)
    SymbolCaret,        // ^
    __SymbolIndex_SIZE__
};

enum TokenType
{
    TokenTypeNone       = 0,
    TokenTypeIdentifier = 1,
    TokenTypeKeyword    = 2,
    TokenTypeInteger    = 3,
    TokenTypeReal       = 4,
    TokenTypeString     = 5,
    TokenTypeSymbol     = 6,
    TokenTypeEOF        = 100,
};

// Compiler directive switches, a bit mask kept in every token
enum DirectiveFlag
{
    DirectiveA = 0x01,  // Absolute (non-recursive) code; accepted and ignored
    DirectiveB = 0x02,  // Input/Output on CON: (+) or TRM: (-); global
    DirectiveC = 0x04,  // Ctrl-C and Ctrl-S during console input; global
    DirectiveI = 0x08,  // I/O error checking
    DirectiveR = 0x10,  // Index and range checking
    DirectiveU = 0x20,  // User interrupt by Ctrl-C
    DirectiveV = 0x40,  // Strict string length check for var parameters
    DirectiveX = 0x80,  // Array optimization; accepted and ignored
};
const unsigned DIRECTIVES_DEFAULT = DirectiveA | DirectiveB | DirectiveC | DirectiveI | DirectiveV | DirectiveX;
const unsigned DIRECTIVES_GLOBAL = DirectiveB | DirectiveC;

const size_t MAX_IDENTIFIER_LENGTH = 127;

const char* GetKeywordName(KeywordIndex keyword);
const char* GetSymbolText(SymbolIndex symbol);
string GetDirectivesString(unsigned directives);

struct Token
{
    TokenType   type;
    KeywordIndex keyword;
    SymbolIndex symbol;
    string      text;       // Source text of the token
    int64_t     ivalue;     // Integer value
    bool        ishex;      // Integer written in hex, like $FF
    double      dvalue;     // Real value
    string      svalue;     // String bytes, as is (KOI8-R)
    SourcePos   pos;
    unsigned    directives; // DirectiveFlag mask at this token
    bool        separated;  // Blank, line end or comment before the token
public:
    Token() :
        type(TokenTypeNone), keyword(KeywordNone), symbol(SymbolNone),
        ivalue(0), ishex(false), dvalue(0.0), directives(DIRECTIVES_DEFAULT), separated(true) {}
public:
    bool IsKeyword(KeywordIndex index) const { return type == TokenTypeKeyword && keyword == index; }
    bool IsSymbol(SymbolIndex index) const { return type == TokenTypeSymbol && symbol == index; }
    bool IsEOF() const { return type == TokenTypeEOF; }
    string GetTokenTypeStr() const;
    string GetDisplayValue() const;  // Value for -t output and messages, ASCII only
};


//////////////////////////////////////////////////////////////////////
// Tokenizer, see tokenizer.cpp
// Lazy: produces tokens one by one on the parser request.
// It never reads past the current token, so after a '^' token the parser
// may call ReadControlChar() to take the raw character that follows.

class Tokenizer
{
    struct SourceFile
    {
        int     fileindex;  // Index in g_sourcefiles
        std::filesystem::path path;
        string  text;
        size_t  pos;        // Current position in text
        int     line, col;  // Position of text[pos], 1-based
        unsigned savedirectives;  // Directives of the including file, restored on return
    };

    std::vector<SourceFile> m_files;  // Stack: main file, then the include file if any
    unsigned m_directives;            // Current directives
    bool    m_separated;              // Separator seen since the last token
public:
    Tokenizer();
public:
    bool OpenMainFile(const string& filename);
    Token GetNextToken();
    // Read the character right after a '^' token and convert it to a control character.
    // Returns the character code 0..31, or -1 on error (the error is reported).
    int ReadControlChar(const Token& caret);
private:
    SourceFile& Current() { return m_files.back(); }
    int  PeekChar(size_t offset = 0);
    int  GetChar();
    SourcePos GetPos();
    void SkipBlanksAndComments();
    void SkipComment(bool parencomment, const SourcePos& startpos);
    void ProcessDirective(const string& body, const SourcePos& startpos);
    void ProcessInclude(const string& name, const SourcePos& startpos);
    void TokenizeIdentifierOrKeyword(Token& token);
    void TokenizeNumber(Token& token);
    void TokenizeHexNumber(Token& token);
    void TokenizeString(Token& token);
    void TokenizeSymbol(Token& token);
};


//////////////////////////////////////////////////////////////////////
// Program model, see model.cpp and docs/DESIGN.md section 4.
// Models are kept by value in vectors; links between them are indices, -1 means none.
// The parser fills the syntax; fields marked (V) are filled later by the validator.

struct ConstValue
{
    int64_t ivalue;         // Ordinal value: integer, char, boolean, enumeration
    double  dvalue;         // Real value
    string  svalue;         // String value; for a set, the 32-byte bit map
public:
    ConstValue() : ivalue(0), dvalue(0.0) {}
};

enum NodeKind
{
    NodeNone = 0,
    NodeInteger,        // Integer literal
    NodeReal,           // Real literal
    NodeString,         // String literal with parts and ^X glued, bytes in token.svalue
    NodeNil,            // nil
    NodeIdent,          // Identifier: variable, constant, function without arguments, field inside with
    NodeCall,           // Identifier(arguments): function or procedure call, type cast
    NodeIndex,          // left[args]
    NodeField,          // left.name, the field name is in token
    NodeDeref,          // left^
    NodeUnary,          // Operation in token: + - not; operand in left
    NodeBinary,         // Operation in token: + - * / div mod and or xor shl shr = <> < > <= >= in
    NodeSet,            // [a, b..c]
};

struct ArgModel             // Argument of a call or an index
{
    int     node;           // Expression node
    int     width;          // Write(x:width:decimals) format nodes, -1 if none
    int     decimals;
public:
    ArgModel() : node(-1), width(-1), decimals(-1) {}
};

struct SetItemModel         // Element of a set constructor
{
    int     from;           // Expression node
    int     to;             // Expression node for a..b, -1 if not a range
public:
    SetItemModel() : from(-1), to(-1) {}
};

// Implicit conversion of a node result, set by the validator
enum ConvertKind
{
    ConvertNone = 0,
    ConvertIntToReal,       // Integer -> Real
    ConvertCharToString,    // Char -> String
    ConvertArrayToString,   // array of Char -> String
};

// Location of a variable or a temporary: a global label or an offset from R5
struct TempSlot
{
    string  asmlabel;       // Non-empty for a global location
    int     offset;         // Offset from R5 otherwise
public:
    TempSlot() : offset(0) {}
    bool IsValid() const { return !asmlabel.empty() || offset != 0; }
    string ToString() const;
};

struct ExpressionNode
{
    NodeKind kind;
    Token   token;          // Name, literal or operation; gives the position and directives
    int     left;
    int     right;
    std::vector<ArgModel> args;
    std::vector<SetItemModel> setitems;
    int     type;           // (V) TypeModel index of the result
    int     symbol;         // (V) Symbol found for Ident/Call/Field
    bool    isconst;        // (V)
    ConstValue constvalue;  // (V)
    ConvertKind convert;    // (V) Implicit conversion of the result
    int     fieldoffset;    // (V) NodeField, or NodeIdent that is a field inside with
    TempSlot withslot;      // (V) NodeIdent that is a field inside with: slot with the record address
public:
    ExpressionNode() : kind(NodeNone), left(-1), right(-1), type(-1), symbol(-1), isconst(false),
        convert(ConvertNone), fieldoffset(-1) {}
};

struct ExpressionModel
{
    std::vector<ExpressionNode> nodes;
    int     root;           // -1 for an empty expression
public:
    ExpressionModel() : root(-1) {}
    bool IsEmpty() const { return root < 0; }
    int  AddNode(const ExpressionNode& node);
    string ToString() const;  // Fully parenthesized text, for dumps
    string NodeToString(int index) const;
    string NodeToStringPlain(int index) const;
};

struct CaseLabelModel       // Case label or variant label: from or from..to
{
    ExpressionModel from;
    ExpressionModel to;     // Empty if not a range
};

struct StructConstModel     // Value of a typed constant
{
    enum Kind { ConstSimple, ConstList, ConstSet };
    Kind    kind;
    SourcePos pos;
    ExpressionModel value;      // ConstSimple; for a ConstSet item the lower bound
    ExpressionModel rangeto;    // ConstSet item upper bound, empty if not a range
    Token   fieldname;          // Record element `Name: value`, TokenTypeNone if none
    std::vector<StructConstModel> items;  // ConstList elements, ConstSet items
public:
    StructConstModel() : kind(ConstSimple) {}
    string ToString() const;
};

struct FieldModel
{
    Token   name;
    int     type;           // TypeModel index
    int     offset;         // (V)
public:
    FieldModel() : type(-1), offset(0) {}
};

struct VariantModel;

struct RecordPartModel
{
    std::vector<FieldModel> fields;     // Fixed part
    bool    hasvariant;
    Token   tagname;        // TokenTypeNone if there is no tag field
    int     tagtype;        // TypeModel index of the tag
    std::vector<VariantModel> variants;
    int     tagoffset;      // (V) Offset of the tag field
    int     size;           // (V) Size of this part including the variants
public:
    RecordPartModel() : hasvariant(false), tagtype(-1), tagoffset(0), size(0) {}
    string ToString() const;
};

struct VariantModel
{
    std::vector<CaseLabelModel> labels;
    RecordPartModel part;
};

enum TypeKind
{
    TypeKindNone = 0,
    TypeKindName,           // Reference by name, resolved by the validator
    TypeKindEnum,
    TypeKindSubrange,
    TypeKindString,         // string[n], n in highexpr
    TypeKindArray,
    TypeKindRecord,
    TypeKindSet,
    TypeKindPointer,
    TypeKindFile,           // file of T; elemtype -1 for an untyped file
    // Standard types, created by the validator
    TypeKindInteger, TypeKindByte, TypeKindBoolean, TypeKindChar, TypeKindReal, TypeKindText,
};

struct TypeModel
{
    TypeKind kind;
    SourcePos pos;
    Token   name;           // TypeKindName, pointer target name
    bool    ispacked;
    std::vector<Token> enumnames;
    ExpressionModel lowexpr;    // Subrange
    ExpressionModel highexpr;   // Subrange, string length
    int     indextype;      // Array index type
    int     elemtype;       // Array element, set base, file component; (V) subrange base type
    RecordPartModel record;
    int     resolved;       // (V) TypeKindName: the real type
    int64_t low, high;      // (V) Bounds, string length
    int     size, align;    // (V)
public:
    TypeModel() : kind(TypeKindNone), ispacked(false), indextype(-1), elemtype(-1),
        resolved(-1), low(0), high(0), size(0), align(1) {}
};

enum StatementKind
{
    StmtEmpty = 0, StmtCompound, StmtAssign, StmtCall, StmtGoto, StmtInline,
    StmtIf, StmtCase, StmtWhile, StmtRepeat, StmtFor, StmtWith,
};

struct InlineTermModel
{
    char    sign;           // '+' or '-'
    Token   token;          // Integer, identifier or '*'
public:
    InlineTermModel() : sign('+') {}
};

struct InlineItemModel      // One code element between '/'
{
    char    sizeprefix;     // 0, '<' or '>'
    std::vector<InlineTermModel> terms;
public:
    InlineItemModel() : sizeprefix(0) {}
};

struct StatementModel;

struct CaseElementModel
{
    std::vector<CaseLabelModel> labels;
    std::vector<StatementModel> statement;  // Exactly one element
};

struct StatementModel
{
    StatementKind kind;
    Token   token;          // First token: position and directives
    Token   label;          // Label before the statement, TokenTypeNone if none
    ExpressionModel target; // Assign: left side; Call: the call; For: control variable
    ExpressionModel expr;   // Assign: value; If/While/Repeat: condition; Case: selector; For: initial
    ExpressionModel expr2;  // For: final value
    bool    downto;
    std::vector<StatementModel> statements;  // See DESIGN.md 4.6
    std::vector<CaseElementModel> caseelements;
    std::vector<ExpressionModel> withvars;
    Token   gotolabel;
    std::vector<InlineItemModel> inlineitems;
    TempSlot forlimit;      // (V) For: slot for the final value
    std::vector<TempSlot> withslots;  // (V) With: slots for the record addresses
    int     labelsymbol;    // (V) Symbol of the label before the statement
    int     gotosymbol;     // (V) Goto: symbol of the label
public:
    StatementModel() : kind(StmtEmpty), downto(false), labelsymbol(-1), gotosymbol(-1) {}
};

enum DeclarationKind
{
    DeclNone = 0, DeclLabel, DeclConst, DeclTypedConst, DeclType, DeclVar, DeclProcedure,
};

struct DeclarationModel
{
    DeclarationKind kind;
    SourcePos pos;
    std::vector<Token> names;
    int     type;           // TypeModel index
    ExpressionModel value;  // DeclConst
    StructConstModel initvalue;   // DeclTypedConst
    ExpressionModel absolute;     // DeclVar with absolute
    int     procindex;      // DeclProcedure
    std::vector<int> symbols;   // (V) Symbols declared here
public:
    DeclarationModel() : kind(DeclNone), type(-1), procindex(-1) {}
};

struct BlockModel
{
    std::vector<DeclarationModel> declarations;   // In source order
    StatementModel body;    // Compound statement
};

enum ProcedureKind { ProcKindProgram = 0, ProcKindProcedure, ProcKindFunction };
enum ProcedureDirective { ProcDirNone = 0, ProcDirForward, ProcDirExternal, ProcDirOverlay };

struct ParamGroupModel      // One group like "var A, B: T"
{
    bool    isvar;
    std::vector<Token> names;
    Token   typename_;      // TokenTypeNone for an untyped var parameter
    int     paramtype;      // (V) TypeModel index, -1 for an untyped var parameter
public:
    ParamGroupModel() : isvar(false), paramtype(-1) {}
};

struct ProcedureModel
{
    ProcedureKind kind;
    Token   name;
    int     parent;         // Enclosing procedure, -1 for the program
    int     level;          // 0 program, 1 top level, 2+ nested
    std::vector<ParamGroupModel> params;
    Token   resulttype;     // Function result type name, TokenTypeNone if omitted
    ProcedureDirective directive;
    ExpressionModel external;   // external address
    BlockModel block;
    int     symbol;         // (V)
    int     scope;          // (V) Scope with the parameters and the locals
    int     forwardof;      // (V) For the body of a forward procedure: its forward declaration
    int     bodyproc;       // (V) For a forward declaration: the procedure with the body
    int     resulttypeindex;    // (V) Function result type
    int     resultsymbol;   // (V) Function result variable
    int     paramsize;      // (V) Bytes the procedure pops on return
    int     localsize;      // (V) Bytes for the locals and temporaries
    bool    nested;         // (V) Gets the static link: level 2 and deeper
    string  asmlabel;       // (V) "P00012"
public:
    ProcedureModel() : kind(ProcKindProcedure), parent(-1), level(0), directive(ProcDirNone),
        symbol(-1), scope(-1), forwardof(-1), bodyproc(-1), resulttypeindex(-1), resultsymbol(-1),
        paramsize(0), localsize(0), nested(false) {}
};

enum SymbolKind
{
    SymNone = 0,
    SymConst,               // Constant, also enumeration value
    SymTypedConst,          // Typed constant: initialized static variable
    SymType,
    SymVar,
    SymParam,               // Value parameter
    SymVarParam,            // var parameter
    SymFuncResult,          // Result variable of a function
    SymProcedure,
    SymFunction,
    SymLabel,
    SymStdProc,             // Standard procedure, stdindex is StdRoutine
    SymStdFunc,             // Standard function
    SymStdVar,              // Standard variable: Input, Mem, HeapPtr...
};

struct SymbolModel
{
    string  name;           // Source spelling
    SourcePos pos;
    SymbolKind kind;
    int     type;           // TypeModel index
    int     scope;
    int     level;          // Procedure level where declared
    ConstValue constvalue;  // SymConst
    int     procindex;      // SymProcedure, SymFunction, SymFuncResult
    int     stdindex;       // SymStd*: StdRoutine or StdVariable
    bool    unsupported;    // Standard identifier not supported in Pesto/11
    TempSlot location;      // Variables and parameters
    int     paramoffset;    // Structured value parameter: offset of the passed address
    bool    indirect;       // The location holds the address: var parameter, string function result
    int     absolutesym;    // absolute over this variable symbol, -1 if none
    int64_t absoluteaddr;   // absolute address, -1 if none
    bool    labeldefined;   // SymLabel: placed before a statement
    bool    forwarded;      // SymProcedure/SymFunction: declared forward, body not seen yet
public:
    SymbolModel() : kind(SymNone), type(-1), scope(-1), level(0), procindex(-1), stdindex(-1),
        unsupported(false), paramoffset(0), indirect(false), absolutesym(-1), absoluteaddr(-1),
        labeldefined(false), forwarded(false) {}
};

struct ScopeModel
{
    int     parent;         // Enclosing scope, -1 for the standard scope
    int     procindex;      // Owner procedure, -1 for the standard scope
    std::map<string, int> names;  // Upper-case name -> symbol index
public:
    ScopeModel() : parent(-1), procindex(-1) {}
};

struct ProgramModel
{
    Token   name;           // Program name, TokenTypeNone if no heading
    std::vector<Token> params;  // Program parameters, ignored
    std::vector<TypeModel> types;
    std::vector<ProcedureModel> procedures;  // [0] is the program itself
    std::vector<SymbolModel> symbols;   // (V)
    std::vector<ScopeModel> scopes;     // (V) [0] standard identifiers, [1] the program
public:
    int  AddType(const TypeModel& type);
    string TypeToString(int index) const;  // Type text, for dumps
    void Dump(std::ostream& out) const;     // -p output
};

extern ProgramModel g_program;


//////////////////////////////////////////////////////////////////////
// Parser, see parser.cpp

class Parser;
typedef StatementModel (Parser::* ParseStatementRef)();
struct ParserStatementSpec
{
    KeywordIndex keyword;
    ParseStatementRef methodref;
};

class Parser
{
    Tokenizer* m_tokenizer;
    Token   m_token;        // Current token
    bool    m_panic;        // Error reported: suppress more errors until recovery
    SourcePos m_errorpos;   // Position of the last reported error
    int     m_procindex;    // Procedure being parsed
public:
    Parser(Tokenizer* tokenizer);
public:
    void ParseProgram();
private:
    static const ParserStatementSpec m_statementspecs[];
private:
    void Next();
    void Error(const Token& token, int code, const string& message);
    bool IsSymbol(SymbolIndex symbol) const { return m_token.IsSymbol(symbol); }
    bool IsKeyword(KeywordIndex keyword) const { return m_token.IsKeyword(keyword); }
    bool Expect(SymbolIndex symbol, int code);
    bool ExpectKeyword(KeywordIndex keyword, int code);
    bool ExpectIdentifier(Token& token);
    void RecoverStatement(KeywordIndex terminator1, KeywordIndex terminator2);
    void RecoverDeclaration();
private:
    void ParseBlock(BlockModel& block);
    void ParseDeclarations(BlockModel& block);
    void ParseLabelPart(BlockModel& block);
    void ParseConstPart(BlockModel& block);
    void ParseTypePart(BlockModel& block);
    void ParseVarPart(BlockModel& block);
    void ParseProcedure(BlockModel& block);
    void ParseParams(ProcedureModel& proc);
    int  ParseType();
    void ParseRecordPart(RecordPartModel& part);
    void ParseIdentifierList(std::vector<Token>& names);
    ExpressionModel ParseConstant();
    int  ParseConstantNode(ExpressionModel& expr);
    int  ParseConstantRest(ExpressionModel& expr, const Token& ident);
    void ParseStructConst(StructConstModel& value);
    void ParseCaseLabels(std::vector<CaseLabelModel>& labels);
private:
    StatementModel ParseStatement();
    void ParseStatementList(std::vector<StatementModel>& statements, KeywordIndex terminator1, KeywordIndex terminator2);
    StatementModel ParseCompound();
    StatementModel ParseIf();
    StatementModel ParseCase();
    StatementModel ParseWhile();
    StatementModel ParseRepeat();
    StatementModel ParseFor();
    StatementModel ParseWith();
    StatementModel ParseGoto();
    StatementModel ParseInline();
    void ParseAssignOrCall(StatementModel& statement, const Token& ident);
private:
    ExpressionModel ParseExpression();
    int  ParseExpressionNode(ExpressionModel& expr);
    int  ParseSimpleExpression(ExpressionModel& expr);
    int  ParseTerm(ExpressionModel& expr);
    int  ParseComplementedFactor(ExpressionModel& expr);
    int  ParseSignedFactor(ExpressionModel& expr);
    int  ParseFactor(ExpressionModel& expr);
    int  ParseDesignator(ExpressionModel& expr, const Token& ident);
    int  ParseStringLiteral(ExpressionModel& expr);
    int  ParseSetConstructor(ExpressionModel& expr);
    void ParseArguments(ExpressionModel& expr, int node, bool allowformat);
};


//////////////////////////////////////////////////////////////////////
// Validator, see validator*.cpp and docs/DESIGN.md section 4.10

// Standard types, created by the validator; indices in g_program.types
extern int g_typeInteger, g_typeByte, g_typeBoolean, g_typeChar, g_typeReal, g_typeText;
extern int g_typeString;    // String expression result, string[255]
extern int g_typeUntypedFile;
extern int g_typeNilPointer;    // nil and Ptr(): compatible with all pointer types
extern int g_typeEmptySet;      // []: compatible with all set types
extern int g_typeError;         // Result of an expression with an error, checks are skipped
extern int g_typeVoid;          // "Result" of a procedure call

extern bool g_showvalidation;   // -v mode: dumps show the validation results

class Validator;
struct StdRoutineSpec;
typedef int (Validator::* StdCheckMethodRef)(ExpressionModel&, int, const StdRoutineSpec&);
struct StdRoutineSpec
{
    const char* name;
    SymbolKind  kind;       // SymStdProc or SymStdFunc; SymNone for an unsupported one
    const char* params;     // Parameter pattern, see validator-std.cpp
    char        result;     // Result type code, see validator-std.cpp
    StdCheckMethodRef method;   // Special check, nullptr for the pattern check
};

struct WithEntry            // Record opened by a with statement
{
    int     type;           // Record type
    TempSlot slot;          // Slot with the record address
};

class Validator
{
    int     m_procindex;    // Procedure being validated
    int     m_scope;        // Current scope
    int     m_localoffset;  // Next free negative offset in the current frame
    int     m_globalcount;  // Counter for V labels
    int     m_constcount;   // Counter for C labels
    int     m_proccount;    // Counter for P labels
    std::vector<WithEntry> m_withs;
    bool    m_assigntarget; // Validating the left side of an assignment: function name is the result variable
    std::vector<int> m_pendingpointers;  // Pointer types waiting for their target type
    std::set<int> m_validatedtypes;
    std::map<int, int> m_settypes;       // Ordinal base type -> set type for set constructors
    struct GotoRef { int symbol; SourcePos pos; };
    std::vector<GotoRef> m_gotos;        // Gotos of the current block, checked at its end
public:
    Validator();
public:
    void ValidateProgram();
public:
    static const StdRoutineSpec m_stdroutines[];
private:
    // Scopes and symbols, validator.cpp
    void CreateStandardScope();
    int  AddType(const TypeModel& type);
    int  AddSymbol(const SymbolModel& symbol, int scope);
    int  DeclareSymbol(const Token& name, SymbolKind kind, int type);
    int  FindSymbol(const string& name) const;
    int  FindSymbolInScope(const string& name, int scope) const;
    void Error(const SourcePos& pos, int code, const string& message);
    // Blocks and declarations
    void ValidateBlock(BlockModel& block);
    void ValidateDeclaration(DeclarationModel& decl);
    void ValidateConstDecl(DeclarationModel& decl);
    void ValidateTypedConstDecl(DeclarationModel& decl);
    void ValidateTypeDecl(DeclarationModel& decl);
    void ValidateVarDecl(DeclarationModel& decl);
    void ValidateLabelDecl(DeclarationModel& decl);
    void ValidateProcedureDecl(DeclarationModel& decl);
    void ValidateProcedureBody(int procindex);
    void ResolvePendingPointers();
    void CheckStructConst(StructConstModel& value, int type);
    TempSlot AllocateGlobal(int size, bool isconst);
    TempSlot AllocateLocal(int size);
    TempSlot AllocateTemp(int size);
    // Types
public:
    static int  Resolve(int type);
    static const TypeModel& GetType(int type) { return g_program.types[Resolve(type)]; }
    static bool IsOrdinal(int type);
    static bool IsScalar(int type);     // Ordinal or pointer: a value in one register
    static bool IsIntegerType(int type);
    static bool IsRealType(int type);
    static bool IsNumeric(int type) { return IsIntegerType(type) || IsRealType(type); }
    static bool IsCharType(int type);
    static bool IsBooleanType(int type);
    static bool IsStringType(int type);
    static bool IsCharArray(int type);
    static bool IsStringLike(int type);
    static bool IsSetType(int type);
    static bool IsPointerType(int type);
    static bool IsFileType(int type);
    static bool IsTextType(int type);
    static bool IsRecordType(int type);
    static bool IsArrayType(int type);
    static int  OrdinalBase(int type);
    static bool OrdinalCompatible(int a, int b);
    static bool SetCompatible(int a, int b);
    static bool PointerCompatible(int a, int b);
    static int  TypeSize(int type);
    static bool ContainsFile(int type);
private:
    void ValidateType(int type);
    void ValidateRecordPart(RecordPartModel& part, int& offset, int& align);
    bool GetOrdinalRange(int type, int64_t& low, int64_t& high);
    void ComputeTypeLayout(TypeModel& type);
    int  ResolveTypeName(const Token& name);
    // Expressions, validator-expr.cpp
public:
    int  ValidateExpression(ExpressionModel& expr);
    int  ValidateNode(ExpressionModel& expr, int index, bool negated = false);
    bool EvaluateConstant(ExpressionModel& expr, ConstValue& value, int& type);
    bool IsVariable(const ExpressionModel& expr, int index) const;
    bool CheckAssignable(int desttype, ExpressionModel& expr, int index, const SourcePos& pos, bool report);
    void SetConvert(ExpressionModel& expr, int index, ConvertKind convert);
private:
    int  ValidateIdent(ExpressionModel& expr, int index);
    int  ValidateCall(ExpressionModel& expr, int index);
    int  ValidateUserCall(ExpressionModel& expr, int index, int symbol);
    int  ValidateTypeCast(ExpressionModel& expr, int index, int symbol);
    int  ValidateIndex(ExpressionModel& expr, int index);
    int  ValidateField(ExpressionModel& expr, int index);
    int  ValidateDeref(ExpressionModel& expr, int index);
    int  ValidateUnary(ExpressionModel& expr, int index, bool negated);
    int  ValidateBinary(ExpressionModel& expr, int index);
    int  ValidateSet(ExpressionModel& expr, int index);
    bool FindField(int recordtype, const string& name, int& offset, int& type) const;
    int  ValidateValue(ExpressionModel& expr, int index, bool negated = false);
    int  GetSetType(int basetype);
    void CheckNoFormat(ExpressionModel& expr, int index);
    // Standard routines, validator-std.cpp
    int  ValidateStdCall(ExpressionModel& expr, int index, int symbol);
    int  CheckStdPattern(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    bool CheckStdArg(ExpressionModel& expr, const ArgModel& arg, char code);
    int  StdResultType(char code, ExpressionModel& expr, int index);
    int  CheckStdWrite(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdRead(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdStr(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdVal(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdConcat(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdSizeOf(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdAddr(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdOrd(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdSuccPred(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdAbsSqr(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdRandom(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdFileFunc(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    int  CheckStdChr(ExpressionModel& expr, int index, const StdRoutineSpec& spec);
    // Statements, validator.cpp
    void ValidateStatement(StatementModel& statement);
    void ValidateStatements(std::vector<StatementModel>& statements);
    void ValidateAssign(StatementModel& statement);
    void ValidateCallStatement(StatementModel& statement);
    void ValidateCondition(ExpressionModel& expr, const SourcePos& pos);
    void ValidateFor(StatementModel& statement);
    void ValidateCase(StatementModel& statement);
    void ValidateWith(StatementModel& statement);
    void ValidateGoto(StatementModel& statement);
    void ValidateInline(StatementModel& statement);
    void ValidateLabelPlacement(StatementModel& statement);
};


//////////////////////////////////////////////////////////////////////
// Generator output model, see codeline.cpp and docs/DESIGN.md section 4.11

enum OperandMode
{
    OpNone = 0,
    OpReg,              // Rn
    OpRegDeferred,      // (Rn)
    OpAutoInc,          // (Rn)+
    OpAutoDec,          // -(Rn)
    OpIndex,            // n(Rn)
    OpIndexDeferred,    // @n(Rn)
    OpImmediate,        // #n or #label
    OpLabel,            // label or label+n (PC relative)
    OpAbsolute,         // @#n
};

struct Operand
{
    OperandMode mode;
    int     reg;            // 0..7
    int64_t value;          // Number, offset
    string  label;          // Label for OpImmediate/OpLabel/OpIndex
public:
    Operand() : mode(OpNone), reg(0), value(0) {}
    static Operand Reg(int reg) { Operand op; op.mode = OpReg; op.reg = reg; return op; }
    static Operand Imm(int64_t value) { Operand op; op.mode = OpImmediate; op.value = value; return op; }
    static Operand ImmLabel(const string& label, int64_t offset = 0) { Operand op; op.mode = OpImmediate; op.label = label; op.value = offset; return op; }
    static Operand Absolute(int64_t address) { Operand op; op.mode = OpAbsolute; op.value = address & 0xFFFF; return op; }
    static Operand Label(const string& label, int64_t offset = 0) { Operand op; op.mode = OpLabel; op.label = label; op.value = offset; return op; }
    static Operand Index(int reg, int64_t offset) { Operand op; op.mode = OpIndex; op.reg = reg; op.value = offset; return op; }
    static Operand IndexDeferred(int reg, int64_t offset) { Operand op; op.mode = OpIndexDeferred; op.reg = reg; op.value = offset; return op; }
    static Operand Deferred(int reg) { Operand op; op.mode = OpRegDeferred; op.reg = reg; return op; }
    static Operand AutoInc(int reg) { Operand op; op.mode = OpAutoInc; op.reg = reg; return op; }
    static Operand AutoDec(int reg) { Operand op; op.mode = OpAutoDec; op.reg = reg; return op; }
    bool IsNone() const { return mode == OpNone; }
    int  ExtraWords() const;    // Words this operand adds to the instruction
    string ToString() const;
};

enum CodeLineKind
{
    LineInstruction = 0,    // Machine instruction
    LineBranch,             // Bxx, BR, SOB: target in CodeLine::target
    LineData,               // .WORD, .BYTE, .BLKW, .EVEN...: size in words
    LineLabel,              // Label only
    LineComment,            // Comment only
    LineRaw,                // Line copied as is from the runtime template; words is an estimate
};

struct CodeLine
{
    CodeLineKind kind;
    string  label;
    string  opcode;
    Operand src, dst;
    string  text;           // Operands as text for directives
    string  comment;
    int     words;          // Size in words, for the branch layout
    string  target;         // Branch target label
public:
    CodeLine() : kind(LineInstruction), words(0) {}
    string ToString() const;  // Assembly text line, tabs between the fields
};

struct ProcedureCode
{
    int     procindex;
    std::vector<CodeLine> lines;
};

struct FinalModel
{
    std::vector<ProcedureCode> procedures;  // The program first
    std::vector<CodeLine> data;             // Constants, typed constants, global variables
    std::set<string> runtimeneeds;          // Runtime blocks called by the code
};

extern std::vector<std::vector<string>> g_sourcelines;  // Source lines by file index, for comments

string FormatNumber(int64_t value);     // Decimal number for macro11: "12." or "-4."
bool RealToDecF(double value, uint16_t& high, uint16_t& low);  // DEC F_floating words, see codeline.cpp


//////////////////////////////////////////////////////////////////////
// Runtime templates, see runtime.cpp and docs/RUNTIME.md

struct RuntimeBlock
{
    string  name;
    std::vector<string> needs;
    std::vector<string> lines;
};

class RuntimeGenerator
{
    std::vector<RuntimeBlock> m_blocks;
public:
    bool LoadTemplate(const std::filesystem::path& path);
    const RuntimeBlock* FindBlock(const string& name) const;
    // Closure of the needed blocks over their dependencies, in template order
    std::vector<const RuntimeBlock*> CollectBlocks(const std::set<string>& needs) const;
};


//////////////////////////////////////////////////////////////////////
// Generator, see generator*.cpp and docs/DESIGN.md section 4.11

// Argument of a runtime call, loaded into a register by Generator::LoadRuntimeArgs.
// A set value is always passed expanded to 32 bytes: a constant image or a buffer.
struct RuntimeArg
{
    enum Kind { StringValue, SetValue, Address, Value, Immediate };
    Kind    kind;
    int     node;           // Expression node; for Value of a width -1 means "no width" (-1 loaded)
    int     reg;
    int64_t imm;            // Immediate
    int     buffer;         // String or set value in a temporary buffer: its number, -1 if none
    const ExpressionModel* model;  // Expression of the node, if not the one given to LoadRuntimeArgs
public:
    RuntimeArg(Kind k, int n, int r, int64_t i = 0) : kind(k), node(n), reg(r), imm(i), buffer(-1), model(nullptr) {}
};

class Generator
{
    FinalModel* m_final;
    const RuntimeGenerator* m_runtime;
    ProcedureCode* m_code;      // Procedure being generated
    int     m_procindex;
    int     m_labelcount;       // Counter for L labels
    int     m_gotocount;        // Counter for G labels
    std::map<int, string> m_gotolabels;     // Label symbol -> G label
    std::map<string, string> m_strings;     // String constant bytes -> K label
    std::vector<std::pair<string, string>> m_stringlist;  // K label and bytes, in creation order
    std::map<string, string> m_datas;       // Raw data bytes (set images) -> K label
    std::vector<std::pair<string, string>> m_datalist;    // K label and raw bytes, in creation order
    int     m_stringcount;
    int     m_lastfile, m_lastline;         // Last source line put into a comment
    string  m_exitlabel;                    // Epilogue of the procedure being generated, for Exit
    std::map<string, Operand> m_withstatic; // With slot -> record operand, when the record address is static
    int     m_setrangetype;                 // Set type whose range checks constructor elements, -1: their own type
    bool    m_usesreal;                     // Real code was generated: the FIS trap handler is needed
    size_t  m_initend;                      // Line after the INIT block in the program code
public:
    Generator(FinalModel* final, const RuntimeGenerator* runtime);
public:
    void GenerateProgram();
private:
    // Output, generator.cpp
    void Emit(const string& opcode, const Operand& src = Operand(), const Operand& dst = Operand(), const string& comment = "");
    void EmitLabel(const string& label);
    void EmitComment(const string& comment);
    void EmitData(const string& opcode, const string& text, int words, const string& comment = "");
    void EmitCall(const string& runtimename, const string& comment = "");
    void EmitTodo(const string& what);
    void EmitSourceLine(const SourcePos& pos);
    void EmitTemplateBlock(const string& name);
    string NewLabel();
    string GetGotoLabel(int symbol);
    string GetStringLabel(const string& bytes);
    void GenerateProcedure(int procindex);
    void GenerateData();
    void GenerateTypedConst(const SymbolModel& symbol, const StructConstModel& value);
    void CollectTempSlots(const StatementModel& statement, std::vector<TempSlot>& slots);
    void EmitBranch(const string& opcode, const string& label, const string& comment = "");
    // Statements, generator-stmt.cpp
    void GenerateStatement(StatementModel& statement);
    void GenerateCallStatement(StatementModel& statement);
    bool GenerateAssign(StatementModel& statement);
    bool GenerateIf(StatementModel& statement);
    bool GenerateWhile(StatementModel& statement);
    bool GenerateRepeat(StatementModel& statement);
    bool GenerateFor(StatementModel& statement);
    bool GenerateCase(StatementModel& statement);
    bool GenerateGoto(StatementModel& statement);
    bool GenerateWith(StatementModel& statement);
    bool GenerateStructAssign(StatementModel& statement);
    // Variables, generator-var.cpp
    bool IsDesignator(const ExpressionModel& expr, int index) const;
    bool DesignatorUsesR0(const ExpressionModel& expr, int index);
    bool GetSymbolOperand(int symbol, int reg, bool emit, Operand& operand);
    bool GetVariableOperand(const ExpressionModel& expr, int index, Operand& operand);
    bool GenerateDesignator(const ExpressionModel& expr, int index, int reg, Operand& operand);
    bool GenerateElement(const ExpressionModel& expr, int index, int reg, Operand& operand);
    bool GenerateIndex(const ExpressionModel& expr, int argnode, int reg, int64_t low, int64_t high, int size, bool check, Operand& operand);
    bool AddOffset(Operand& operand, int64_t offset, int reg);
    void ScaleRegister(int reg, int size);
    void LoadAddress(const Operand& operand, int reg);
    void PushAddress(const Operand& operand, const string& comment);
    bool GenerateAddress(const ExpressionModel& expr, int index, int reg);
    void EmitBlockCopy(int size, bool wordaligned);
    void EmitStaticChain(int level, int reg);
    bool GenerateUserCall(const ExpressionModel& expr, int index);
    bool PushArgument(const ExpressionModel& expr, int argnode, bool isvar);
    bool LoadOrdinal(const ExpressionModel& expr, int index, int reg);
    bool GenerateValue(const ExpressionModel& expr, int index, bool ignoreconvert = false);
    bool GetSimpleOperand(const ExpressionModel& expr, int index, Operand& operand, bool& isbyte);
    bool GetRightOperand(const ExpressionModel& expr, int index, Operand& operand);
    bool GenerateUnaryValue(const ExpressionModel& expr, int index);
    bool GenerateBinaryValue(const ExpressionModel& expr, int index);
    bool GenerateBooleanValue(const ExpressionModel& expr, int index);
    bool GenerateCondition(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue);
    bool GenerateCompare(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue);
    bool GenerateInConst(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue);
    bool HasSideEffects(const ExpressionModel& expr, int index) const;
    bool StoreOrdinal(const ExpressionModel& target, int index, const SourcePos& pos);
    // Standard procedures and functions, generator-std.cpp
    void GenerateWrite(const ExpressionModel& expr, int index, bool isln);
    bool GenerateWriteItem(const ExpressionModel& expr, const ArgModel& arg);
    bool GenerateStdFunction(const ExpressionModel& expr, int index);
    bool GenerateStdProcedure(const string& name, const ExpressionModel& expr, int index);
    bool GenerateRead(const ExpressionModel& expr, int index);
    bool GenerateReadItems(const ExpressionModel& expr, int index, size_t first);
    // Files, generator-file.cpp
    void EmitIOCheck(const ExpressionNode& node);
    bool GenerateFileProcedure(const string& name, const ExpressionModel& expr, int index);
    bool GenerateFileFunction(const string& name, const ExpressionModel& expr, int index);
    bool GenerateWriteFile(const ExpressionModel& expr, int index, bool isln);
    bool GenerateReadFile(const ExpressionModel& expr, int index, bool isln);
    // Strings, generator-str.cpp
    bool IsSimpleString(const ExpressionModel& expr, int index);
    bool LoadStringAddress(const ExpressionModel& expr, int index, int reg);
    bool GenerateStringBuffer(const ExpressionModel& expr, int index);
    bool AppendString(const ExpressionModel& expr, int index);
    bool LoadRuntimeArgs(const ExpressionModel& expr, std::vector<RuntimeArg>& args, int& buffers);
    void FreeBuffers(int bytes);
    bool GenerateStringAssign(StatementModel& statement);
    bool GenerateStringCompare(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue);
    bool GenerateStringFunction(const string& name, const ExpressionModel& expr, int index);
    bool GenerateStringProcedure(const string& name, const ExpressionModel& expr, int index);
    string GetDataLabel(const string& bytes);
    // Real, generator-real.cpp
    void PushRealConst(double value);
    void EmitFis(const char* opcode);
    bool GenerateReal(const ExpressionModel& expr, int index);
    bool PrepareRealOperand(Operand& operand, int reg);
    bool StoreReal(const ExpressionModel& target, int index);
    bool GenerateRealCompare(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue);
    bool GenerateRealFunction(const string& name, const ExpressionModel& expr, int index);
    bool GenerateRealToInteger(const string& name, const ExpressionModel& expr, int arg);
    // Sets, generator-set.cpp
    bool GenerateSetBuffer(const ExpressionModel& expr, int index);
    bool GenerateSetAssign(StatementModel& statement);
    bool GenerateSetCompare(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue);
    bool GenerateSetIn(const ExpressionModel& expr, int index, const string& label, bool jumpiftrue);
    bool LoadWithWidth(const ExpressionModel& expr, const ArgModel& arg, bool isaddress);
};

// Branch opcode for a comparison operation, see generator-expr.cpp
const char* GetCompareBranch(const Token& op, bool isunsigned, bool jumpiftrue);

// Branch layout: replace short branches that cannot reach their target, see branches.cpp
void LayoutBranches(ProcedureCode& code);

// Write PROGRAM.MAC and the runtime PESTO.MAC (or one file with --onefile), see generator.cpp
bool WriteAssemblyFiles(const FinalModel& final, const RuntimeGenerator& runtime,
        const string& outfilename, const string& rtfilename, const string& title);


//////////////////////////////////////////////////////////////////////
