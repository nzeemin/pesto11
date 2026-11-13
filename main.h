#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <set>
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
    int64_t ivalue;
    double  dvalue;
    string  svalue;
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
public:
    ExpressionNode() : kind(NodeNone), left(-1), right(-1), type(-1), symbol(-1), isconst(false) {}
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
public:
    RecordPartModel() : hasvariant(false), tagtype(-1) {}
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
    int     elemtype;       // Array element, set base, file component
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
public:
    StatementModel() : kind(StmtEmpty), downto(false) {}
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
public:
    ParamGroupModel() : isvar(false) {}
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
public:
    ProcedureModel() : kind(ProcKindProcedure), parent(-1), level(0), directive(ProcDirNone) {}
};

struct ProgramModel
{
    Token   name;           // Program name, TokenTypeNone if no heading
    std::vector<Token> params;  // Program parameters, ignored
    std::vector<TypeModel> types;
    std::vector<ProcedureModel> procedures;  // [0] is the program itself
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
