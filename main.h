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
