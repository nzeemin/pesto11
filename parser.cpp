
#include <cassert>

#include "main.h"

// Recursive descent parser, see docs/TP3-dialect.md section 2 for the grammar.
// The parser looks ahead one token at most: m_token is the current token, already read.
// After a syntax error m_panic suppresses further messages until the parser
// recovers at a statement or declaration boundary.


//////////////////////////////////////////////////////////////////////

const ParserStatementSpec Parser::m_statementspecs[] =
{
    { KeywordBEGIN,     &Parser::ParseCompound },
    { KeywordIF,        &Parser::ParseIf },
    { KeywordCASE,      &Parser::ParseCase },
    { KeywordWHILE,     &Parser::ParseWhile },
    { KeywordREPEAT,    &Parser::ParseRepeat },
    { KeywordFOR,       &Parser::ParseFor },
    { KeywordWITH,      &Parser::ParseWith },
    { KeywordGOTO,      &Parser::ParseGoto },
    { KeywordINLINE,    &Parser::ParseInline },
};

static int NewNode(ExpressionModel& expr, NodeKind kind, const Token& token, int left = -1, int right = -1)
{
    ExpressionNode node;
    node.kind = kind;
    node.token = token;
    node.left = left;
    node.right = right;
    return expr.AddNode(node);
}


//////////////////////////////////////////////////////////////////////
// Tokens and errors

Parser::Parser(Tokenizer* tokenizer) :
    m_tokenizer(tokenizer), m_panic(false), m_procindex(0)
{
    assert(tokenizer != nullptr);
    Next();
}

void Parser::Next()
{
    m_token = m_tokenizer->GetNextToken();
}

void Parser::Error(const Token& token, int code, const string& message)
{
    if (m_panic)
        return;
    ReportError(token.pos, code, message);
    m_panic = true;
    m_errorpos = token.pos;
}

bool Parser::Expect(SymbolIndex symbol, int code)
{
    if (IsSymbol(symbol))
    {
        Next();
        return true;
    }
    Error(m_token, code, string("'") + GetSymbolText(symbol) + "' expected");
    return false;
}

bool Parser::ExpectKeyword(KeywordIndex keyword, int code)
{
    if (IsKeyword(keyword))
    {
        Next();
        return true;
    }
    Error(m_token, code, string(GetKeywordName(keyword)) + " expected");
    return false;
}

bool Parser::ExpectIdentifier(Token& token)
{
    if (m_token.type == TokenTypeIdentifier)
    {
        token = m_token;
        Next();
        return true;
    }
    if (m_token.type == TokenTypeKeyword)
        Error(m_token, 53, string("Reserved word ") + GetKeywordName(m_token.keyword) + " cannot be used as an identifier");
    else
        Error(m_token, 41, "Identifier expected");
    return false;
}

// Skip to ';' or a terminator of the statement list, without consuming them
void Parser::RecoverStatement(KeywordIndex terminator1, KeywordIndex terminator2)
{
    while (!m_token.IsEOF() && !IsSymbol(SymbolSemicolon) &&
        !IsKeyword(terminator1) && !(terminator2 != KeywordNone && IsKeyword(terminator2)))
        Next();
    m_panic = false;
}

// Skip to the end of the declaration (';' is consumed) or to the start of a section
void Parser::RecoverDeclaration()
{
    // A section keyword that caused the error itself, like in "const begin = 1;", is skipped
    if (m_token.pos.file == m_errorpos.file && m_token.pos.line == m_errorpos.line &&
        m_token.pos.col == m_errorpos.col && !m_token.IsEOF() && !IsSymbol(SymbolSemicolon))
        Next();
    for (;;)
    {
        if (m_token.IsEOF())
            break;
        if (IsSymbol(SymbolSemicolon))
        {
            Next();
            break;
        }
        if (IsKeyword(KeywordBEGIN) || IsKeyword(KeywordCONST) || IsKeyword(KeywordTYPE) ||
            IsKeyword(KeywordVAR) || IsKeyword(KeywordLABEL) || IsKeyword(KeywordPROCEDURE) ||
            IsKeyword(KeywordFUNCTION) || IsKeyword(KeywordOVERLAY))
            break;
        Next();
    }
    m_panic = false;
}


//////////////////////////////////////////////////////////////////////
// Program, blocks and declarations

void Parser::ParseProgram()
{
    ProcedureModel program;
    program.kind = ProcKindProgram;
    program.level = 0;
    g_program.procedures.push_back(program);
    m_procindex = 0;

    if (IsKeyword(KeywordPROGRAM))
    {
        Next();
        ExpectIdentifier(g_program.name);
        if (IsSymbol(SymbolLParen))
        {
            Next();
            ParseIdentifierList(g_program.params);
            Expect(SymbolRParen, 5);
        }
        Expect(SymbolSemicolon, 1);
        if (m_panic)
            RecoverDeclaration();
    }

    BlockModel block;
    ParseBlock(block);
    g_program.procedures[0].block = std::move(block);

    Expect(SymbolDot, 10);  // The text after the final '.' is ignored
}

void Parser::ParseBlock(BlockModel& block)
{
    ParseDeclarations(block);

    if (!IsKeyword(KeywordBEGIN))
    {
        Error(m_token, 12, "BEGIN expected");
        while (!m_token.IsEOF() && !IsKeyword(KeywordBEGIN))
            Next();
        m_panic = false;
    }
    if (IsKeyword(KeywordBEGIN))
        block.body = ParseCompound();
    else
        block.body.kind = StmtCompound;
}

void Parser::ParseDeclarations(BlockModel& block)
{
    for (;;)
    {
        if (IsKeyword(KeywordLABEL))
            ParseLabelPart(block);
        else if (IsKeyword(KeywordCONST))
            ParseConstPart(block);
        else if (IsKeyword(KeywordTYPE))
            ParseTypePart(block);
        else if (IsKeyword(KeywordVAR))
            ParseVarPart(block);
        else if (IsKeyword(KeywordPROCEDURE) || IsKeyword(KeywordFUNCTION) || IsKeyword(KeywordOVERLAY))
            ParseProcedure(block);
        else
            break;
        if (m_panic)
            RecoverDeclaration();
    }
}

void Parser::ParseLabelPart(BlockModel& block)
{
    DeclarationModel decl;
    decl.kind = DeclLabel;
    decl.pos = m_token.pos;
    Next();  // label
    for (;;)
    {
        if (m_token.type == TokenTypeIdentifier || m_token.type == TokenTypeInteger)
        {
            decl.names.push_back(m_token);
            Next();
        }
        else
        {
            Error(m_token, 41, "Label expected");
            return;
        }
        if (!IsSymbol(SymbolComma))
            break;
        Next();
    }
    Expect(SymbolSemicolon, 1);
    block.declarations.push_back(decl);
}

void Parser::ParseConstPart(BlockModel& block)
{
    Next();  // const
    do
    {
        DeclarationModel decl;
        decl.pos = m_token.pos;
        Token name;
        if (ExpectIdentifier(name))
        {
            decl.names.push_back(name);
            if (IsSymbol(SymbolEqual))
            {
                Next();
                decl.kind = DeclConst;
                decl.value = ParseConstant();
            }
            else if (IsSymbol(SymbolColon))
            {
                Next();
                decl.kind = DeclTypedConst;
                decl.type = ParseType();
                Expect(SymbolEqual, 6);
                ParseStructConst(decl.initvalue);
            }
            else
                Error(m_token, 6, "'=' expected");
            Expect(SymbolSemicolon, 1);
        }
        if (decl.kind != DeclNone)
            block.declarations.push_back(decl);
        if (m_panic)
            RecoverDeclaration();
    }
    while (m_token.type == TokenTypeIdentifier);
}

void Parser::ParseTypePart(BlockModel& block)
{
    Next();  // type
    do
    {
        DeclarationModel decl;
        decl.kind = DeclType;
        decl.pos = m_token.pos;
        Token name;
        if (ExpectIdentifier(name))
        {
            decl.names.push_back(name);
            Expect(SymbolEqual, 6);
            decl.type = ParseType();
            Expect(SymbolSemicolon, 1);
            block.declarations.push_back(decl);
        }
        if (m_panic)
            RecoverDeclaration();
    }
    while (m_token.type == TokenTypeIdentifier);
}

void Parser::ParseVarPart(BlockModel& block)
{
    Next();  // var
    do
    {
        DeclarationModel decl;
        decl.kind = DeclVar;
        decl.pos = m_token.pos;
        ParseIdentifierList(decl.names);
        Expect(SymbolColon, 2);
        decl.type = ParseType();
        if (IsKeyword(KeywordABSOLUTE))
        {
            Next();
            decl.absolute = ParseConstant();
        }
        Expect(SymbolSemicolon, 1);
        if (!decl.names.empty())
            block.declarations.push_back(decl);
        if (m_panic)
            RecoverDeclaration();
    }
    while (m_token.type == TokenTypeIdentifier);
}

void Parser::ParseProcedure(BlockModel& block)
{
    bool overlay = false;
    if (IsKeyword(KeywordOVERLAY))
    {
        overlay = true;
        Next();
        if (!IsKeyword(KeywordPROCEDURE) && !IsKeyword(KeywordFUNCTION))
        {
            Error(m_token, 16, "PROCEDURE or FUNCTION expected");
            return;
        }
    }

    ProcedureModel proc;
    proc.kind = IsKeyword(KeywordFUNCTION) ? ProcKindFunction : ProcKindProcedure;
    proc.parent = m_procindex;
    proc.level = g_program.procedures[m_procindex].level + 1;
    proc.directive = overlay ? ProcDirOverlay : ProcDirNone;
    Next();  // procedure or function

    ExpectIdentifier(proc.name);
    if (IsSymbol(SymbolLParen))
        ParseParams(proc);
    if (proc.kind == ProcKindFunction && IsSymbol(SymbolColon))
    {
        Next();
        ExpectIdentifier(proc.resulttype);
    }
    Expect(SymbolSemicolon, 1);
    if (m_panic)
        RecoverDeclaration();  // Error in the heading: recover before the block

    int procindex = (int)g_program.procedures.size();
    g_program.procedures.push_back(proc);
    DeclarationModel decl;
    decl.kind = DeclProcedure;
    decl.pos = proc.name.pos;
    decl.names.push_back(proc.name);
    decl.procindex = procindex;
    block.declarations.push_back(decl);

    if (IsKeyword(KeywordFORWARD))
    {
        Next();
        g_program.procedures[procindex].directive = ProcDirForward;
        Expect(SymbolSemicolon, 1);
    }
    else if (IsKeyword(KeywordEXTERNAL))
    {
        Next();
        g_program.procedures[procindex].directive = ProcDirExternal;
        g_program.procedures[procindex].external = ParseConstant();
        Expect(SymbolSemicolon, 1);
    }
    else
    {
        int savedprocindex = m_procindex;
        m_procindex = procindex;
        BlockModel procblock;
        ParseBlock(procblock);
        m_procindex = savedprocindex;
        g_program.procedures[procindex].block = std::move(procblock);
        Expect(SymbolSemicolon, 1);
    }
}

void Parser::ParseParams(ProcedureModel& proc)
{
    Next();  // (
    for (;;)
    {
        ParamGroupModel group;
        if (IsKeyword(KeywordVAR))
        {
            group.isvar = true;
            Next();
        }
        ParseIdentifierList(group.names);
        if (IsSymbol(SymbolColon))
        {
            Next();
            ExpectIdentifier(group.typename_);
        }
        else if (!group.isvar)
            Error(m_token, 2, "':' expected");
        proc.params.push_back(group);
        if (!IsSymbol(SymbolSemicolon))
            break;
        Next();
    }
    Expect(SymbolRParen, 5);
}

void Parser::ParseIdentifierList(std::vector<Token>& names)
{
    Token name;
    if (ExpectIdentifier(name))
        names.push_back(name);
    while (IsSymbol(SymbolComma))
    {
        Next();
        if (ExpectIdentifier(name))
            names.push_back(name);
    }
}


//////////////////////////////////////////////////////////////////////
// Types

int Parser::ParseType()
{
    TypeModel type;
    type.pos = m_token.pos;
    if (IsKeyword(KeywordPACKED))
    {
        type.ispacked = true;
        Next();
        if (!IsKeyword(KeywordARRAY) && !IsKeyword(KeywordRECORD) && !IsKeyword(KeywordSET) && !IsKeyword(KeywordFILE))
            Error(m_token, 0, "ARRAY, RECORD, SET or FILE expected after PACKED");
    }

    if (IsSymbol(SymbolCaret))  // At the start of a type '^' is always a pointer
    {
        Next();
        TypeModel target;
        target.kind = TypeKindName;
        target.pos = m_token.pos;
        ExpectIdentifier(target.name);
        type.kind = TypeKindPointer;
        type.elemtype = g_program.AddType(target);
    }
    else if (IsSymbol(SymbolLParen))
    {
        Next();
        type.kind = TypeKindEnum;
        ParseIdentifierList(type.enumnames);
        Expect(SymbolRParen, 5);
    }
    else if (IsKeyword(KeywordARRAY))
    {
        Next();
        Expect(SymbolLBracket, 8);
        std::vector<int> indextypes;
        indextypes.push_back(ParseType());
        while (IsSymbol(SymbolComma))
        {
            Next();
            indextypes.push_back(ParseType());
        }
        Expect(SymbolRBracket, 9);
        ExpectKeyword(KeywordOF, 15);
        int elemtype = ParseType();
        // array[A, B] of T is array[A] of array[B] of T
        for (size_t i = indextypes.size() - 1; i > 0; i--)
        {
            TypeModel inner;
            inner.kind = TypeKindArray;
            inner.pos = type.pos;
            inner.ispacked = type.ispacked;
            inner.indextype = indextypes[i];
            inner.elemtype = elemtype;
            elemtype = g_program.AddType(inner);
        }
        type.kind = TypeKindArray;
        type.indextype = indextypes[0];
        type.elemtype = elemtype;
    }
    else if (IsKeyword(KeywordSTRING))
    {
        Next();
        type.kind = TypeKindString;
        Expect(SymbolLBracket, 8);
        type.highexpr = ParseConstant();
        Expect(SymbolRBracket, 9);
    }
    else if (IsKeyword(KeywordSET))
    {
        Next();
        ExpectKeyword(KeywordOF, 15);
        type.kind = TypeKindSet;
        type.elemtype = ParseType();
    }
    else if (IsKeyword(KeywordFILE))
    {
        Next();
        type.kind = TypeKindFile;
        if (IsKeyword(KeywordOF))
        {
            Next();
            type.elemtype = ParseType();
        }
    }
    else if (IsKeyword(KeywordRECORD))
    {
        Next();
        type.kind = TypeKindRecord;
        ParseRecordPart(type.record);
        ExpectKeyword(KeywordEND, 14);
    }
    else if (m_token.type == TokenTypeIdentifier)
    {
        Token ident = m_token;
        Next();
        if (IsSymbol(SymbolDotDot))  // Subrange starting with a constant identifier
        {
            Next();
            type.kind = TypeKindSubrange;
            type.lowexpr.root = ParseConstantRest(type.lowexpr, ident);
            type.highexpr = ParseConstant();
        }
        else
        {
            type.kind = TypeKindName;
            type.name = ident;
        }
    }
    else  // Subrange starting with a number, a string or a sign
    {
        type.kind = TypeKindSubrange;
        type.lowexpr = ParseConstant();
        Expect(SymbolDotDot, 11);
        type.highexpr = ParseConstant();
    }
    return g_program.AddType(type);
}

void Parser::ParseRecordPart(RecordPartModel& part)
{
    // Fixed part: record sections "A, B: T" separated by ';'
    while (m_token.type == TokenTypeIdentifier)
    {
        std::vector<Token> names;
        ParseIdentifierList(names);
        Expect(SymbolColon, 2);
        int type = ParseType();
        for (const Token& name : names)
        {
            FieldModel field;
            field.name = name;
            field.type = type;
            part.fields.push_back(field);
        }
        if (!IsSymbol(SymbolSemicolon))
            break;
        Next();
    }

    // Variant part: case [Tag:] Type of labels: (fields); ...
    if (IsKeyword(KeywordCASE))
    {
        Next();
        part.hasvariant = true;
        Token first;
        ExpectIdentifier(first);
        TypeModel tagtype;
        tagtype.kind = TypeKindName;
        if (IsSymbol(SymbolColon))
        {
            Next();
            part.tagname = first;
            tagtype.pos = m_token.pos;
            ExpectIdentifier(tagtype.name);
        }
        else
        {
            tagtype.pos = first.pos;
            tagtype.name = first;
        }
        part.tagtype = g_program.AddType(tagtype);
        ExpectKeyword(KeywordOF, 15);

        while (!IsKeyword(KeywordEND) && !IsSymbol(SymbolRParen) && !m_token.IsEOF())
        {
            VariantModel variant;
            ParseCaseLabels(variant.labels);
            Expect(SymbolColon, 2);
            Expect(SymbolLParen, 4);
            ParseRecordPart(variant.part);
            Expect(SymbolRParen, 5);
            part.variants.push_back(variant);
            if (m_panic || !IsSymbol(SymbolSemicolon))
                break;
            Next();
        }
    }
}


//////////////////////////////////////////////////////////////////////
// Constants

ExpressionModel Parser::ParseConstant()
{
    ExpressionModel expr;
    expr.root = ParseConstantNode(expr);
    return expr;
}

// constant ::= [ "+" | "-" ] ( unsigned-number | constant-identifier ) | string-literal
int Parser::ParseConstantNode(ExpressionModel& expr)
{
    if (IsSymbol(SymbolPlus) || IsSymbol(SymbolMinus))
    {
        Token sign = m_token;
        Next();
        int operand = -1;
        if (m_token.type == TokenTypeInteger)
            operand = NewNode(expr, NodeInteger, m_token);
        else if (m_token.type == TokenTypeReal)
            operand = NewNode(expr, NodeReal, m_token);
        else if (m_token.type == TokenTypeIdentifier)
            operand = NewNode(expr, NodeIdent, m_token);
        else
        {
            Error(m_token, 25, "Integer or real constant expected");
            return -1;
        }
        Next();
        return NewNode(expr, NodeUnary, sign, operand);
    }
    if (m_token.type == TokenTypeInteger || m_token.type == TokenTypeReal)
    {
        int node = NewNode(expr, (m_token.type == TokenTypeInteger) ? NodeInteger : NodeReal, m_token);
        Next();
        return node;
    }
    if (m_token.type == TokenTypeString || IsSymbol(SymbolCaret))
        return ParseStringLiteral(expr);
    if (m_token.type == TokenTypeIdentifier)
    {
        int node = NewNode(expr, NodeIdent, m_token);
        Next();
        return node;
    }
    Error(m_token, 41, "Constant expected");
    return -1;
}

// The constant identifier is already taken from the input
int Parser::ParseConstantRest(ExpressionModel& expr, const Token& ident)
{
    return NewNode(expr, NodeIdent, ident);
}

void Parser::ParseStructConst(StructConstModel& value)
{
    value.pos = m_token.pos;
    if (IsSymbol(SymbolLParen))  // Array or record constant
    {
        Next();
        value.kind = StructConstModel::ConstList;
        if (m_token.type == TokenTypeIdentifier)
        {
            Token ident = m_token;
            Next();
            if (IsSymbol(SymbolColon))  // Record constant: (Field: value; ...)
            {
                for (;;)
                {
                    Next();  // :
                    StructConstModel item;
                    item.fieldname = ident;
                    ParseStructConst(item);
                    value.items.push_back(item);
                    if (!IsSymbol(SymbolSemicolon))
                        break;
                    Next();
                    if (IsSymbol(SymbolRParen))
                        break;
                    if (!ExpectIdentifier(ident))
                        break;
                    if (!IsSymbol(SymbolColon))
                    {
                        Error(m_token, 2, "':' expected");
                        break;
                    }
                }
                Expect(SymbolRParen, 5);
                return;
            }
            // Array constant starting with a constant identifier
            StructConstModel item;
            item.pos = ident.pos;
            item.value.root = ParseConstantRest(item.value, ident);
            value.items.push_back(item);
            if (!IsSymbol(SymbolComma))
            {
                Expect(SymbolRParen, 5);
                return;
            }
            Next();
        }
        for (;;)
        {
            StructConstModel item;
            ParseStructConst(item);
            value.items.push_back(item);
            if (!IsSymbol(SymbolComma))
                break;
            Next();
        }
        Expect(SymbolRParen, 5);
    }
    else if (IsSymbol(SymbolLBracket))  // Set constant
    {
        Next();
        value.kind = StructConstModel::ConstSet;
        if (!IsSymbol(SymbolRBracket))
        {
            for (;;)
            {
                StructConstModel item;
                item.pos = m_token.pos;
                item.value = ParseConstant();
                if (IsSymbol(SymbolDotDot))
                {
                    Next();
                    item.rangeto = ParseConstant();
                }
                value.items.push_back(item);
                if (!IsSymbol(SymbolComma))
                    break;
                Next();
            }
        }
        Expect(SymbolRBracket, 9);
    }
    else
    {
        value.kind = StructConstModel::ConstSimple;
        value.value = ParseConstant();
    }
}

void Parser::ParseCaseLabels(std::vector<CaseLabelModel>& labels)
{
    for (;;)
    {
        CaseLabelModel label;
        label.from = ParseConstant();
        if (IsSymbol(SymbolDotDot))
        {
            Next();
            label.to = ParseConstant();
        }
        labels.push_back(label);
        if (!IsSymbol(SymbolComma))
            break;
        Next();
    }
}


//////////////////////////////////////////////////////////////////////
// Statements

StatementModel Parser::ParseStatement()
{
    Token label;
    if (m_token.type == TokenTypeInteger)  // Numeric label
    {
        label = m_token;
        Next();
        Expect(SymbolColon, 2);
    }
    else if (m_token.type == TokenTypeIdentifier)
    {
        Token ident = m_token;
        Next();
        if (!IsSymbol(SymbolColon))
        {
            StatementModel statement;
            statement.token = ident;
            ParseAssignOrCall(statement, ident);
            return statement;
        }
        label = ident;  // Identifier label
        Next();
    }

    StatementModel statement;
    if (m_token.type == TokenTypeIdentifier)
    {
        Token ident = m_token;
        Next();
        statement.token = ident;
        ParseAssignOrCall(statement, ident);
    }
    else
    {
        statement.kind = StmtEmpty;
        statement.token = m_token;
        if (m_token.type == TokenTypeKeyword)
        {
            for (const ParserStatementSpec& spec : m_statementspecs)
            {
                if (spec.keyword == m_token.keyword)
                {
                    statement = (this->*spec.methodref)();
                    break;
                }
            }
        }
    }
    statement.label = label;
    return statement;
}

void Parser::ParseStatementList(std::vector<StatementModel>& statements, KeywordIndex terminator1, KeywordIndex terminator2)
{
    for (;;)
    {
        statements.push_back(ParseStatement());
        if (m_panic)
            RecoverStatement(terminator1, terminator2);
        if (IsSymbol(SymbolSemicolon))
        {
            Next();
            continue;
        }
        if (IsKeyword(terminator1) || (terminator2 != KeywordNone && IsKeyword(terminator2)) || m_token.IsEOF())
            break;
        Error(m_token, 1, "';' expected");
        RecoverStatement(terminator1, terminator2);
        if (!IsSymbol(SymbolSemicolon))
            break;
        Next();
    }
}

void Parser::ParseAssignOrCall(StatementModel& statement, const Token& ident)
{
    statement.target.root = ParseDesignator(statement.target, ident);
    if (IsSymbol(SymbolAssign))
    {
        Next();
        statement.kind = StmtAssign;
        statement.expr = ParseExpression();
    }
    else
        statement.kind = StmtCall;
}

StatementModel Parser::ParseCompound()
{
    StatementModel statement;
    statement.kind = StmtCompound;
    statement.token = m_token;
    Next();  // begin
    ParseStatementList(statement.statements, KeywordEND, KeywordNone);
    ExpectKeyword(KeywordEND, 14);
    return statement;
}

StatementModel Parser::ParseIf()
{
    StatementModel statement;
    statement.kind = StmtIf;
    statement.token = m_token;
    Next();  // if
    statement.expr = ParseExpression();
    ExpectKeyword(KeywordTHEN, 17);
    statement.statements.push_back(ParseStatement());
    if (IsKeyword(KeywordELSE))
    {
        Next();
        statement.statements.push_back(ParseStatement());
    }
    return statement;
}

StatementModel Parser::ParseCase()
{
    StatementModel statement;
    statement.kind = StmtCase;
    statement.token = m_token;
    Next();  // case
    statement.expr = ParseExpression();
    ExpectKeyword(KeywordOF, 15);
    while (!IsKeyword(KeywordELSE) && !IsKeyword(KeywordEND) && !m_token.IsEOF())
    {
        CaseElementModel element;
        ParseCaseLabels(element.labels);
        Expect(SymbolColon, 2);
        element.statement.push_back(ParseStatement());
        statement.caseelements.push_back(element);
        if (m_panic)
            RecoverStatement(KeywordEND, KeywordELSE);
        if (!IsSymbol(SymbolSemicolon))
            break;
        Next();
    }
    if (IsKeyword(KeywordELSE))
    {
        Next();
        ParseStatementList(statement.statements, KeywordEND, KeywordNone);
    }
    ExpectKeyword(KeywordEND, 14);
    return statement;
}

StatementModel Parser::ParseWhile()
{
    StatementModel statement;
    statement.kind = StmtWhile;
    statement.token = m_token;
    Next();  // while
    statement.expr = ParseExpression();
    ExpectKeyword(KeywordDO, 13);
    statement.statements.push_back(ParseStatement());
    return statement;
}

StatementModel Parser::ParseRepeat()
{
    StatementModel statement;
    statement.kind = StmtRepeat;
    statement.token = m_token;
    Next();  // repeat
    ParseStatementList(statement.statements, KeywordUNTIL, KeywordNone);
    ExpectKeyword(KeywordUNTIL, 0);
    statement.expr = ParseExpression();
    return statement;
}

StatementModel Parser::ParseFor()
{
    StatementModel statement;
    statement.kind = StmtFor;
    statement.token = m_token;
    Next();  // for
    Token ident;
    if (ExpectIdentifier(ident))
        statement.target.root = NewNode(statement.target, NodeIdent, ident);
    Expect(SymbolAssign, 7);
    statement.expr = ParseExpression();
    if (IsKeyword(KeywordTO) || IsKeyword(KeywordDOWNTO))
    {
        statement.downto = IsKeyword(KeywordDOWNTO);
        Next();
    }
    else
        Error(m_token, 18, "TO or DOWNTO expected");
    statement.expr2 = ParseExpression();
    ExpectKeyword(KeywordDO, 13);
    statement.statements.push_back(ParseStatement());
    return statement;
}

StatementModel Parser::ParseWith()
{
    StatementModel statement;
    statement.kind = StmtWith;
    statement.token = m_token;
    Next();  // with
    for (;;)
    {
        Token ident;
        if (!ExpectIdentifier(ident))
            break;
        ExpressionModel var;
        var.root = ParseDesignator(var, ident);
        statement.withvars.push_back(var);
        if (!IsSymbol(SymbolComma))
            break;
        Next();
    }
    ExpectKeyword(KeywordDO, 13);
    statement.statements.push_back(ParseStatement());
    return statement;
}

StatementModel Parser::ParseGoto()
{
    StatementModel statement;
    statement.kind = StmtGoto;
    statement.token = m_token;
    Next();  // goto
    if (m_token.type == TokenTypeIdentifier || m_token.type == TokenTypeInteger)
    {
        statement.gotolabel = m_token;
        Next();
    }
    else
        Error(m_token, 41, "Label expected");
    return statement;
}

// inline ( element / element ... ), element ::= [<|>] term { (+|-) term }
StatementModel Parser::ParseInline()
{
    StatementModel statement;
    statement.kind = StmtInline;
    statement.token = m_token;
    Next();  // inline
    Expect(SymbolLParen, 4);
    for (;;)
    {
        InlineItemModel item;
        if (IsSymbol(SymbolLess) || IsSymbol(SymbolGreater))
        {
            item.sizeprefix = IsSymbol(SymbolLess) ? '<' : '>';
            Next();
        }
        char sign = '+';
        if (IsSymbol(SymbolPlus) || IsSymbol(SymbolMinus))
        {
            sign = IsSymbol(SymbolMinus) ? '-' : '+';
            Next();
        }
        for (;;)
        {
            if (m_token.type != TokenTypeInteger && m_token.type != TokenTypeIdentifier && !IsSymbol(SymbolStar))
            {
                Error(m_token, 74, "INLINE error: number, identifier or * expected");
                return statement;
            }
            InlineTermModel term;
            term.sign = sign;
            term.token = m_token;
            item.terms.push_back(term);
            Next();
            if (!IsSymbol(SymbolPlus) && !IsSymbol(SymbolMinus))
                break;
            sign = IsSymbol(SymbolMinus) ? '-' : '+';
            Next();
        }
        statement.inlineitems.push_back(item);
        if (!IsSymbol(SymbolSlash))
            break;
        Next();
    }
    Expect(SymbolRParen, 5);
    return statement;
}


//////////////////////////////////////////////////////////////////////
// Expressions

ExpressionModel Parser::ParseExpression()
{
    ExpressionModel expr;
    expr.root = ParseExpressionNode(expr);
    return expr;
}

// expression ::= simple-expression [ relational-op simple-expression ]
int Parser::ParseExpressionNode(ExpressionModel& expr)
{
    int left = ParseSimpleExpression(expr);
    if (IsSymbol(SymbolEqual) || IsSymbol(SymbolNotEqual) || IsSymbol(SymbolLess) || IsSymbol(SymbolGreater) ||
        IsSymbol(SymbolLessEqual) || IsSymbol(SymbolGreaterEqual) || IsKeyword(KeywordIN))
    {
        Token op = m_token;
        Next();
        int right = ParseSimpleExpression(expr);
        left = NewNode(expr, NodeBinary, op, left, right);
    }
    return left;
}

// simple-expression ::= term { adding-op term }
int Parser::ParseSimpleExpression(ExpressionModel& expr)
{
    int left = ParseTerm(expr);
    while (IsSymbol(SymbolPlus) || IsSymbol(SymbolMinus) || IsKeyword(KeywordOR) || IsKeyword(KeywordXOR))
    {
        Token op = m_token;
        Next();
        int right = ParseTerm(expr);
        left = NewNode(expr, NodeBinary, op, left, right);
    }
    return left;
}

// term ::= complemented-factor { multiplying-op complemented-factor }
int Parser::ParseTerm(ExpressionModel& expr)
{
    int left = ParseComplementedFactor(expr);
    while (IsSymbol(SymbolStar) || IsSymbol(SymbolSlash) || IsKeyword(KeywordDIV) || IsKeyword(KeywordMOD) ||
        IsKeyword(KeywordAND) || IsKeyword(KeywordSHL) || IsKeyword(KeywordSHR))
    {
        Token op = m_token;
        Next();
        int right = ParseComplementedFactor(expr);
        left = NewNode(expr, NodeBinary, op, left, right);
    }
    return left;
}

// complemented-factor ::= [ "not" ] signed-factor
int Parser::ParseComplementedFactor(ExpressionModel& expr)
{
    if (IsKeyword(KeywordNOT))
    {
        Token op = m_token;
        Next();
        int operand = ParseSignedFactor(expr);
        return NewNode(expr, NodeUnary, op, operand);
    }
    return ParseSignedFactor(expr);
}

// signed-factor ::= [ "+" | "-" ] factor
int Parser::ParseSignedFactor(ExpressionModel& expr)
{
    if (IsSymbol(SymbolPlus) || IsSymbol(SymbolMinus))
    {
        Token op = m_token;
        Next();
        int operand = ParseFactor(expr);
        return NewNode(expr, NodeUnary, op, operand);
    }
    return ParseFactor(expr);
}

int Parser::ParseFactor(ExpressionModel& expr)
{
    if (m_token.type == TokenTypeInteger || m_token.type == TokenTypeReal)
    {
        int node = NewNode(expr, (m_token.type == TokenTypeInteger) ? NodeInteger : NodeReal, m_token);
        Next();
        return node;
    }
    if (m_token.type == TokenTypeString || IsSymbol(SymbolCaret))  // '^' at the start of a factor: control char
        return ParseStringLiteral(expr);
    if (IsKeyword(KeywordNIL))
    {
        int node = NewNode(expr, NodeNil, m_token);
        Next();
        return node;
    }
    if (IsSymbol(SymbolLParen))
    {
        Next();
        int node = ParseExpressionNode(expr);
        Expect(SymbolRParen, 5);
        return node;
    }
    if (IsSymbol(SymbolLBracket))
        return ParseSetConstructor(expr);
    if (m_token.type == TokenTypeIdentifier)
    {
        Token ident = m_token;
        Next();
        return ParseDesignator(expr, ident);
    }
    Error(m_token, 41, "Syntax error: operand expected");
    return -1;
}

// Identifier already taken; then optional (arguments) and selectors [i], .field, ^
int Parser::ParseDesignator(ExpressionModel& expr, const Token& ident)
{
    int node = NewNode(expr, NodeIdent, ident);
    if (IsSymbol(SymbolLParen))
    {
        expr.nodes[node].kind = NodeCall;
        ParseArguments(expr, node, true);
    }
    for (;;)
    {
        if (IsSymbol(SymbolLBracket))
        {
            node = NewNode(expr, NodeIndex, m_token, node);
            ParseArguments(expr, node, false);
        }
        else if (IsSymbol(SymbolDot))
        {
            Next();
            Token field;
            if (!ExpectIdentifier(field))
                break;
            node = NewNode(expr, NodeField, field, node);
        }
        else if (IsSymbol(SymbolCaret))  // After a variable '^' is a dereference
        {
            node = NewNode(expr, NodeDeref, m_token, node);
            Next();
        }
        else
            break;
    }
    return node;
}

// Arguments in ( ) for a call, with optional formats x:w:d; or indices in [ ]
void Parser::ParseArguments(ExpressionModel& expr, int node, bool iscall)
{
    Next();  // ( or [
    std::vector<ArgModel> args;
    for (;;)
    {
        ArgModel arg;
        arg.node = ParseExpressionNode(expr);
        if (iscall && IsSymbol(SymbolColon))
        {
            Next();
            arg.width = ParseExpressionNode(expr);
            if (IsSymbol(SymbolColon))
            {
                Next();
                arg.decimals = ParseExpressionNode(expr);
            }
        }
        args.push_back(arg);
        if (!IsSymbol(SymbolComma))
            break;
        Next();
    }
    expr.nodes[node].args = args;
    if (iscall)
        Expect(SymbolRParen, 5);
    else
        Expect(SymbolRBracket, 9);
}

// String literal: 'text', #n and ^X parts written together without separators
int Parser::ParseStringLiteral(ExpressionModel& expr)
{
    Token token = m_token;
    token.type = TokenTypeString;
    token.text.clear();
    token.svalue.clear();
    for (;;)
    {
        if (m_token.type == TokenTypeString)
        {
            token.text += m_token.text;
            token.svalue += m_token.svalue;
            Next();
        }
        else  // '^': the character right after it gives the control character
        {
            Token caret = m_token;
            int ch = m_tokenizer->ReadControlChar(caret);
            if (ch < 0)
                m_panic = true;  // The tokenizer reported the error
            else
            {
                token.text += '^';
                token.text += (char)(ch ^ 0x40);
                token.svalue += (char)ch;
            }
            Next();
        }
        if ((m_token.type != TokenTypeString && !IsSymbol(SymbolCaret)) || m_token.separated)
            break;
    }
    return NewNode(expr, NodeString, token);
}

int Parser::ParseSetConstructor(ExpressionModel& expr)
{
    int node = NewNode(expr, NodeSet, m_token);
    Next();  // [
    std::vector<SetItemModel> items;
    if (!IsSymbol(SymbolRBracket))
    {
        for (;;)
        {
            SetItemModel item;
            item.from = ParseExpressionNode(expr);
            if (IsSymbol(SymbolDotDot))
            {
                Next();
                item.to = ParseExpressionNode(expr);
            }
            items.push_back(item);
            if (!IsSymbol(SymbolComma))
                break;
            Next();
        }
    }
    expr.nodes[node].setitems = items;
    Expect(SymbolRBracket, 9);
    return node;
}


//////////////////////////////////////////////////////////////////////
