
#include <cassert>
#include <cstdlib>
#include <algorithm>

#include "main.h"


//////////////////////////////////////////////////////////////////////
// Keywords and symbols

//NOTE: This array should be in the same order as the KeywordIndex enum, it is sorted for the binary search
static const char* Keywords[] =
{
    "ABSOLUTE", "AND", "ARRAY", "BEGIN", "CASE", "CONST",
    "DIV", "DO", "DOWNTO", "ELSE", "END", "EXTERNAL",
    "FILE", "FOR", "FORWARD", "FUNCTION", "GOTO", "IF",
    "IN", "INLINE", "LABEL", "MOD", "NIL", "NOT",
    "OF", "OR", "OVERLAY", "PACKED", "PROCEDURE", "PROGRAM",
    "RECORD", "REPEAT", "SET", "SHL", "SHR", "STRING",
    "THEN", "TO", "TYPE", "UNTIL", "VAR", "WHILE",
    "WITH", "XOR",
};
static_assert(sizeof(Keywords) / sizeof(Keywords[0]) == __KeywordIndex_SIZE__ - 1, "Keywords array size mismatch");

//NOTE: This array should be in the same order as the SymbolIndex enum
static const char* Symbols[] =
{
    "+", "-", "*", "/", "=", "<>", "<", ">", "<=", ">=", ":=",
    ":", ";", ",", ".", "..", "(", ")", "[", "]", "^",
};
static_assert(sizeof(Symbols) / sizeof(Symbols[0]) == __SymbolIndex_SIZE__ - 1, "Symbols array size mismatch");

const char* GetKeywordName(KeywordIndex keyword)
{
    if (keyword <= KeywordNone || keyword >= __KeywordIndex_SIZE__)
        return "";
    return Keywords[keyword - 1];
}

const char* GetSymbolText(SymbolIndex symbol)
{
    if (symbol <= SymbolNone || symbol >= __SymbolIndex_SIZE__)
        return "";
    return Symbols[symbol - 1];
}

static KeywordIndex FindKeyword(const string& upperword)
{
    const char** begin = std::begin(Keywords);
    const char** end = std::end(Keywords);
    const char** found = std::lower_bound(begin, end, upperword,
            [](const char* a, const string& b) { return strcmp(a, b.c_str()) < 0; });
    if (found != end && upperword == *found)
        return (KeywordIndex)(found - begin + 1);
    return KeywordNone;
}

string GetDirectivesString(unsigned directives)
{
    static const char letters[] = "ABCIRUVX";
    static const unsigned flags[] =
    {
        DirectiveA, DirectiveB, DirectiveC, DirectiveI, DirectiveR, DirectiveU, DirectiveV, DirectiveX
    };
    string result;
    for (int i = 0; i < 8; i++)
    {
        if (i > 0)
            result += ' ';
        result += letters[i];
        result += (directives & flags[i]) ? '+' : '-';
    }
    return result;
}


//////////////////////////////////////////////////////////////////////
// Token

string Token::GetTokenTypeStr() const
{
    switch (type)
    {
    case TokenTypeIdentifier: return "IDENT";
    case TokenTypeKeyword: return "KEYWORD";
    case TokenTypeInteger: return "INTEGER";
    case TokenTypeReal: return "REAL";
    case TokenTypeString: return "STRING";
    case TokenTypeSymbol: return "SYMBOL";
    case TokenTypeEOF: return "EOF";
    default: return "NONE";
    }
}

// Printable form of string bytes: 'text' parts for printable ASCII, #nnn for the rest
static string GetStringDisplay(const string& bytes)
{
    if (bytes.empty())
        return "''";
    string result;
    bool inquotes = false;
    for (unsigned char ch : bytes)
    {
        if (ch >= 32 && ch < 127)
        {
            if (!inquotes)
            {
                result += '\'';
                inquotes = true;
            }
            result += (char)ch;
            if (ch == '\'')
                result += '\'';
        }
        else
        {
            if (inquotes)
            {
                result += '\'';
                inquotes = false;
            }
            result += '#';
            result += std::to_string((int)ch);
        }
    }
    if (inquotes)
        result += '\'';
    return result;
}

string Token::GetDisplayValue() const
{
    switch (type)
    {
    case TokenTypeIdentifier:
        return text;
    case TokenTypeKeyword:
        return GetKeywordName(keyword);
    case TokenTypeInteger:
        return ishex ? text + "=" + std::to_string(ivalue) : text;
    case TokenTypeReal:
        return text;
    case TokenTypeString:
        return GetStringDisplay(svalue);
    case TokenTypeSymbol:
        return GetSymbolText(symbol);
    default:
        return "";
    }
}


//////////////////////////////////////////////////////////////////////
// Character classes, ASCII only: KOI8-R letters are not allowed in identifiers

static bool IsLetter(int ch) { return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_'; }
static bool IsDigit(int ch) { return ch >= '0' && ch <= '9'; }
static bool IsHexDigit(int ch) { return IsDigit(ch) || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'); }
static bool IsBlank(int ch) { return ch >= 0 && ch <= ' '; }  // Space, tab, line ends and other control chars
static int  ToUpper(int ch) { return (ch >= 'a' && ch <= 'z') ? ch - 'a' + 'A' : ch; }
static int  HexDigitValue(int ch) { return IsDigit(ch) ? ch - '0' : ToUpper(ch) - 'A' + 10; }

const int CHAR_CTRLZ = 26;  // ^Z, end of text file


//////////////////////////////////////////////////////////////////////
// Tokenizer

Tokenizer::Tokenizer() :
    m_files(), m_directives(DIRECTIVES_DEFAULT), m_separated(true)
{
}

bool Tokenizer::OpenMainFile(const string& filename)
{
    assert(m_files.empty());
    SourceFile file;
    if (!ReadFileBinary(filename, file.text))
    {
        ReportError("cannot read file " + filename);
        return false;
    }
    file.fileindex = RegisterSourceFile(filename);
    file.path = filename;
    file.pos = 0;
    file.line = file.col = 1;
    file.savedirectives = m_directives;
    m_files.push_back(file);
    return true;
}

// Character at the current position plus offset, or -1 at the end of the current file
int Tokenizer::PeekChar(size_t offset)
{
    SourceFile& file = Current();
    size_t pos = file.pos + offset;
    if (pos >= file.text.size())
        return -1;
    int ch = (unsigned char)file.text[pos];
    if (ch == CHAR_CTRLZ)
        return -1;
    return ch;
}

// Take the current character; any line end (CR/LF, LF, CR) comes as '\n'
int Tokenizer::GetChar()
{
    int ch = PeekChar();
    if (ch < 0)
        return -1;
    SourceFile& file = Current();
    file.pos++;
    if (ch == '\r' || ch == '\n')
    {
        if (ch == '\r' && PeekChar() == '\n')
            file.pos++;
        file.line++;
        file.col = 1;
        return '\n';
    }
    file.col++;
    return ch;
}

SourcePos Tokenizer::GetPos()
{
    SourceFile& file = Current();
    return SourcePos(file.fileindex, file.line, file.col);
}

void Tokenizer::SkipBlanksAndComments()
{
    for (;;)
    {
        int ch = PeekChar();
        if (ch < 0)
        {
            if (m_files.size() <= 1)
                return;  // End of the main file
            // End of an include file: back to the including file, restore the local directives
            unsigned saved = Current().savedirectives;
            m_files.pop_back();
            m_directives = (saved & ~DIRECTIVES_GLOBAL) | (m_directives & DIRECTIVES_GLOBAL);
            m_separated = true;
            continue;
        }
        if (IsBlank(ch))
        {
            GetChar();
            m_separated = true;
            continue;
        }
        if (ch == '{' || (ch == '(' && PeekChar(1) == '*'))
        {
            SourcePos startpos = GetPos();
            SkipComment(ch == '(', startpos);
            m_separated = true;
            continue;
        }
        return;
    }
}

// Skip { ... } or (* ... *); a comment starting with $ is a compiler directive
void Tokenizer::SkipComment(bool parencomment, const SourcePos& startpos)
{
    GetChar();  // '{' or '('
    if (parencomment)
        GetChar();  // '*'

    bool isdirective = (PeekChar() == '$');
    string body;
    for (;;)
    {
        int ch = PeekChar();
        if (ch < 0)
        {
            ReportError(startpos, 91, "Unexpected end of source: comment is not closed");
            return;
        }
        if (!parencomment && ch == '}')
        {
            GetChar();
            break;
        }
        if (parencomment && ch == '*' && PeekChar(1) == ')')
        {
            GetChar();
            GetChar();
            break;
        }
        ch = GetChar();
        if (isdirective)
            body += (char)ch;
    }

    if (isdirective)
        ProcessDirective(body.substr(1), startpos);  // Skip '$'
}

// Directive body after '$': a list like "R+,I-" or an include "I FILE.PAS"
void Tokenizer::ProcessDirective(const string& body, const SourcePos& startpos)
{
    if (body.size() >= 1 && ToUpper((unsigned char)body[0]) == 'I' &&
        (body.size() == 1 || (body[1] != '+' && body[1] != '-')))
    {
        string name = body.substr(1);
        size_t first = name.find_first_not_of(" \t\n");
        size_t last = name.find_last_not_of(" \t\n");
        if (first == string::npos)
        {
            ReportError(startpos, 93, "Invalid compiler directive: file name expected");
            return;
        }
        ProcessInclude(name.substr(first, last - first + 1), startpos);
        return;
    }

    size_t i = 0;
    for (;;)
    {
        if (i >= body.size())
        {
            ReportError(startpos, 93, "Invalid compiler directive");
            return;
        }
        int letter = ToUpper((unsigned char)body[i++]);
        int arg = (i < body.size()) ? (unsigned char)body[i++] : -1;
        unsigned flag = 0;
        switch (letter)
        {
        case 'A': flag = DirectiveA; break;
        case 'B': flag = DirectiveB; break;
        case 'C': flag = DirectiveC; break;
        case 'I': flag = DirectiveI; break;
        case 'R': flag = DirectiveR; break;
        case 'U': flag = DirectiveU; break;
        case 'V': flag = DirectiveV; break;
        case 'X': flag = DirectiveX; break;
        case 'W':
            if (!IsDigit(arg))
            {
                ReportError(startpos, 93, "Invalid compiler directive: digit expected after W");
                return;
            }
            break;  // Nesting of WITH: accepted and ignored
        default:
            ReportError(startpos, 93, "Invalid compiler directive");
            return;
        }
        if (flag != 0)
        {
            if (arg == '+')
                m_directives |= flag;
            else if (arg == '-')
                m_directives &= ~flag;
            else
            {
                ReportError(startpos, 93, "Invalid compiler directive: + or - expected");
                return;
            }
        }
        if (i < body.size() && body[i] == ',')
        {
            i++;
            continue;
        }
        break;  // The rest of the comment is ignored
    }
}

void Tokenizer::ProcessInclude(const string& name, const SourcePos& startpos)
{
    if (m_files.size() > 1)
    {
        ReportError(startpos, 0, "Include files cannot be nested");
        return;
    }

    string normalized = name;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    std::filesystem::path path = Current().path.parent_path() / normalized;
    if (!path.has_extension())
        path += ".PAS";

    std::filesystem::path found = FindFileIgnoreCase(path);
    SourceFile file;
    if (found.empty() || !ReadFileBinary(found, file.text))
    {
        ReportError(startpos, 90, "File not found: " + name);
        return;
    }

    file.fileindex = RegisterSourceFile(found.generic_string());
    file.path = found;
    file.pos = 0;
    file.line = file.col = 1;
    file.savedirectives = m_directives;
    m_files.push_back(file);
}

Token Tokenizer::GetNextToken()
{
    for (;;)
    {
        SkipBlanksAndComments();

        Token token;
        token.pos = GetPos();
        token.directives = m_directives;
        token.separated = m_separated;

        int ch = PeekChar();
        if (ch < 0)
        {
            token.type = TokenTypeEOF;
            return token;
        }

        if (IsLetter(ch))
            TokenizeIdentifierOrKeyword(token);
        else if (IsDigit(ch))
            TokenizeNumber(token);
        else if (ch == '$')
            TokenizeHexNumber(token);
        else if (ch == '\'' || ch == '#')
            TokenizeString(token);
        else
            TokenizeSymbol(token);

        if (token.type == TokenTypeNone)
            continue;  // Illegal character, already reported and skipped

        m_separated = false;
        return token;
    }
}

int Tokenizer::ReadControlChar(const Token& caret)
{
    assert(caret.IsSymbol(SymbolCaret));
    (void)caret;
    SourcePos pos = GetPos();
    int ch = PeekChar();
    int upper = ToUpper(ch);
    if (upper < 0x40 || upper > 0x5F)
    {
        ReportError(pos, 58, "Illegal character after ^, control character expected");
        if (ch >= 0 && !IsBlank(ch))
            GetChar();
        return -1;
    }
    GetChar();
    return upper ^ 0x40;
}

void Tokenizer::TokenizeIdentifierOrKeyword(Token& token)
{
    string text;
    while (IsLetter(PeekChar()) || IsDigit(PeekChar()))
        text += (char)GetChar();
    token.text = text;

    if (text.size() > MAX_IDENTIFIER_LENGTH)
        ReportError(token.pos, 0, "Identifier is longer than 127 characters");

    string upper = text;
    for (char& c : upper)
        c = (char)ToUpper((unsigned char)c);
    KeywordIndex keyword = FindKeyword(upper);
    if (keyword != KeywordNone)
    {
        token.type = TokenTypeKeyword;
        token.keyword = keyword;
    }
    else
        token.type = TokenTypeIdentifier;
}

void Tokenizer::TokenizeNumber(Token& token)
{
    string text;
    while (IsDigit(PeekChar()))
        text += (char)GetChar();

    bool isreal = false;
    // Fraction: '.' must be followed by a digit, so that "1..10" and "1.)" stay integers
    if (PeekChar() == '.' && IsDigit(PeekChar(1)))
    {
        isreal = true;
        text += (char)GetChar();
        while (IsDigit(PeekChar()))
            text += (char)GetChar();
    }
    // Exponent: E, optional sign, digits
    int ch = PeekChar();
    if (ch == 'E' || ch == 'e')
    {
        int next = PeekChar(1);
        if (IsDigit(next) || ((next == '+' || next == '-') && IsDigit(PeekChar(2))))
        {
            isreal = true;
            text += (char)GetChar();
            if (!IsDigit(PeekChar()))
                text += (char)GetChar();  // Sign
            while (IsDigit(PeekChar()))
                text += (char)GetChar();
        }
    }
    token.text = text;

    if (isreal)
    {
        token.type = TokenTypeReal;
        token.dvalue = std::strtod(text.c_str(), nullptr);
        return;
    }

    token.type = TokenTypeInteger;
    int64_t value = 0;
    bool overflow = false;
    for (char c : text)
    {
        value = value * 10 + (c - '0');
        if (value > INT32_MAX)
        {
            overflow = true;
            value = INT32_MAX;
        }
    }
    if (overflow)
        ReportError(token.pos, 56, "Error in integer constant: " + text);
    token.ivalue = value;
}

void Tokenizer::TokenizeHexNumber(Token& token)
{
    string text;
    text += (char)GetChar();  // '$'
    int64_t value = 0;
    while (IsHexDigit(PeekChar()))
    {
        int ch = GetChar();
        text += (char)ch;
        if (value <= 0xFFFF)
            value = value * 16 + HexDigitValue(ch);
    }
    token.type = TokenTypeInteger;
    token.text = text;
    token.ishex = true;
    if (text.size() == 1)
        ReportError(token.pos, 56, "Error in integer constant: hex digits expected after $");
    else if (value > 0xFFFF)
    {
        ReportError(token.pos, 56, "Error in integer constant: " + text + " is out of range $0000..$FFFF");
        value = 0xFFFF;
    }
    token.ivalue = value;
}

// Quoted strings and #n control characters written together, like 'Hello'#13#10
void Tokenizer::TokenizeString(Token& token)
{
    token.type = TokenTypeString;
    string& text = token.text;
    string& bytes = token.svalue;
    for (;;)
    {
        int ch = PeekChar();
        if (ch == '\'')
        {
            text += (char)GetChar();
            for (;;)
            {
                ch = PeekChar();
                if (ch < 0 || ch == '\r' || ch == '\n')
                {
                    ReportError(token.pos, 55, "String constant exceeds line");
                    return;
                }
                GetChar();
                text += (char)ch;
                if (ch == '\'')
                {
                    if (PeekChar() != '\'')
                        break;  // Closing quote
                    text += (char)GetChar();  // Doubled quote inside the string
                }
                bytes += (char)ch;
            }
        }
        else if (ch == '#')
        {
            SourcePos pos = GetPos();
            text += (char)GetChar();
            int64_t value = 0;
            bool hasdigits = false;
            if (PeekChar() == '$')
            {
                text += (char)GetChar();
                while (IsHexDigit(PeekChar()))
                {
                    int digit = GetChar();
                    text += (char)digit;
                    if (value <= 255)
                        value = value * 16 + HexDigitValue(digit);
                    hasdigits = true;
                }
            }
            else
            {
                while (IsDigit(PeekChar()))
                {
                    int digit = GetChar();
                    text += (char)digit;
                    if (value <= 255)
                        value = value * 10 + (digit - '0');
                    hasdigits = true;
                }
            }
            if (!hasdigits)
                ReportError(pos, 56, "Error in integer constant: character code expected after #");
            else if (value > 255)
                ReportError(pos, 45, "Constant out of range: character code must be 0..255");
            else
                bytes += (char)value;
        }
        else
            break;
    }
}

void Tokenizer::TokenizeSymbol(Token& token)
{
    int ch = GetChar();
    SymbolIndex symbol = SymbolNone;
    switch (ch)
    {
    case '+': symbol = SymbolPlus; break;
    case '-': symbol = SymbolMinus; break;
    case '*': symbol = SymbolStar; break;
    case '/': symbol = SymbolSlash; break;
    case '=': symbol = SymbolEqual; break;
    case ',': symbol = SymbolComma; break;
    case ';': symbol = SymbolSemicolon; break;
    case ')': symbol = SymbolRParen; break;
    case '[': symbol = SymbolLBracket; break;
    case ']': symbol = SymbolRBracket; break;
    case '^': symbol = SymbolCaret; break;
    case '<':
        if (PeekChar() == '>') { GetChar(); symbol = SymbolNotEqual; }
        else if (PeekChar() == '=') { GetChar(); symbol = SymbolLessEqual; }
        else symbol = SymbolLess;
        break;
    case '>':
        if (PeekChar() == '=') { GetChar(); symbol = SymbolGreaterEqual; }
        else symbol = SymbolGreater;
        break;
    case ':':
        if (PeekChar() == '=') { GetChar(); symbol = SymbolAssign; }
        else symbol = SymbolColon;
        break;
    case '.':
        if (PeekChar() == '.') { GetChar(); symbol = SymbolDotDot; }
        else if (PeekChar() == ')') { GetChar(); symbol = SymbolRBracket; }
        else symbol = SymbolDot;
        break;
    case '(':
        if (PeekChar() == '.') { GetChar(); symbol = SymbolLBracket; }
        else symbol = SymbolLParen;
        break;
    default:
        {
            string display = (ch >= 32 && ch < 127) ? string("'") + (char)ch + "'" : "#" + std::to_string(ch);
            ReportError(token.pos, 58, "Illegal character " + display);
            return;  // token.type stays TokenTypeNone
        }
    }
    token.type = TokenTypeSymbol;
    token.symbol = symbol;
    token.text = GetSymbolText(symbol);
}


//////////////////////////////////////////////////////////////////////
