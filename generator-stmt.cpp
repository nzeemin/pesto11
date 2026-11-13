
#include <cassert>
#include <cstring>

#include "main.h"

// Generator: statements, see docs/DESIGN.md section 7.4.


//////////////////////////////////////////////////////////////////////

static const char* GetStatementName(StatementKind kind)
{
    switch (kind)
    {
    case StmtEmpty: return "empty";
    case StmtCompound: return "begin";
    case StmtAssign: return "assignment";
    case StmtCall: return "procedure call";
    case StmtGoto: return "goto";
    case StmtInline: return "inline";
    case StmtIf: return "if";
    case StmtCase: return "case";
    case StmtWhile: return "while";
    case StmtRepeat: return "repeat";
    case StmtFor: return "for";
    case StmtWith: return "with";
    default: return "statement";
    }
}

void Generator::GenerateStatement(StatementModel& statement)
{
    if (statement.labelsymbol >= 0)
        EmitLabel(GetGotoLabel(statement.labelsymbol));

    switch (statement.kind)
    {
    case StmtEmpty:
        break;
    case StmtCompound:
        for (StatementModel& child : statement.statements)
            GenerateStatement(child);
        break;
    case StmtCall:
        EmitSourceLine(statement.token.pos);
        GenerateCallStatement(statement);
        break;
    default:
        EmitSourceLine(statement.token.pos);
        EmitTodo(GetStatementName(statement.kind));
        break;
    }
}

void Generator::GenerateCallStatement(StatementModel& statement)
{
    const ExpressionModel& expr = statement.target;
    const ExpressionNode& node = expr.nodes[expr.root];
    if (node.symbol < 0)
    {
        EmitTodo("call");
        return;
    }
    const SymbolModel& symbol = g_program.symbols[node.symbol];
    if (symbol.kind == SymStdProc)
    {
        const char* name = Validator::m_stdroutines[symbol.stdindex].name;
        if (strcmp(name, "Write") == 0 || strcmp(name, "WriteLn") == 0)
        {
            GenerateWrite(expr, expr.root, strcmp(name, "WriteLn") == 0);
            return;
        }
        EmitTodo(string("standard procedure ") + name);
        return;
    }
    EmitTodo("call " + symbol.name);
}


//////////////////////////////////////////////////////////////////////
