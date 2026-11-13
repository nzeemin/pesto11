
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

    bool ok = true;
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
    case StmtAssign:
        EmitSourceLine(statement.token.pos);
        ok = GenerateAssign(statement);
        break;
    case StmtIf:
        EmitSourceLine(statement.token.pos);
        ok = GenerateIf(statement);
        break;
    case StmtWhile:
        EmitSourceLine(statement.token.pos);
        ok = GenerateWhile(statement);
        break;
    case StmtRepeat:
        EmitSourceLine(statement.token.pos);
        ok = GenerateRepeat(statement);
        break;
    case StmtFor:
        EmitSourceLine(statement.token.pos);
        ok = GenerateFor(statement);
        break;
    case StmtCase:
        EmitSourceLine(statement.token.pos);
        ok = GenerateCase(statement);
        break;
    case StmtGoto:
        EmitSourceLine(statement.token.pos);
        ok = GenerateGoto(statement);
        break;
    default:
        EmitSourceLine(statement.token.pos);
        ok = false;
        break;
    }
    if (!ok)
        EmitTodo(GetStatementName(statement.kind));
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
        if (strcmp(name, "Halt") == 0 || (strcmp(name, "Exit") == 0 && m_procindex == 0))
        {
            Emit("JMP", Operand::Label("HALT"), Operand(), name);  // Exit in the main program ends it
            return;
        }
        EmitTodo(string("standard procedure ") + name);
        return;
    }
    EmitTodo("call " + symbol.name);
}


//////////////////////////////////////////////////////////////////////

bool Generator::GenerateAssign(StatementModel& statement)
{
    const ExpressionModel& target = statement.target;
    const ExpressionModel& value = statement.expr;
    const ExpressionNode& targetnode = target.nodes[target.root];
    const ExpressionNode& valuenode = value.nodes[value.root];
    int type = targetnode.type;
    if (!Validator::IsOrdinal(type))
        return false;  // Real, strings, sets, structures: later steps

    Operand dest;
    if (!GetVariableOperand(target, target.root, dest))
        return false;  // Arrays, records, pointers, function results: later steps
    bool isbyte = (Validator::TypeSize(type) == 1);
    bool checked = (targetnode.token.directives & DirectiveR) != 0 &&
        (Validator::GetType(type).kind == TypeKindSubrange || Validator::GetType(type).kind == TypeKindEnum ||
         Validator::GetType(type).kind == TypeKindByte);

    // Constant or a variable of the same size: one instruction without R0
    if (!checked)
    {
        Operand src;
        bool srcbyte;
        if (valuenode.isconst && GetSimpleOperand(value, value.root, src, srcbyte))
        {
            int64_t v = src.value;
            if (isbyte)
                v &= 0xFF;
            if (v == 0)
                Emit(isbyte ? "CLRB" : "CLR", dest, Operand(), targetnode.token.text);
            else
                Emit(isbyte ? "MOVB" : "MOV", Operand::Imm(v), dest, targetnode.token.text);
            return true;
        }
        if (GetSimpleOperand(value, value.root, src, srcbyte) && srcbyte == isbyte)
        {
            Emit(isbyte ? "MOVB" : "MOV", src, dest, targetnode.token.text);
            return true;
        }
    }

    if (!GenerateValue(value, value.root))
        return false;
    return StoreOrdinal(target, target.root, statement.token.pos);
}

bool Generator::GenerateIf(StatementModel& statement)
{
    string elselabel = NewLabel();
    if (!GenerateCondition(statement.expr, statement.expr.root, elselabel, false))
        return false;
    GenerateStatement(statement.statements[0]);
    if (statement.statements.size() > 1)
    {
        string endlabel = NewLabel();
        EmitBranch("BR", endlabel);
        EmitLabel(elselabel);
        GenerateStatement(statement.statements[1]);
        EmitLabel(endlabel);
    }
    else
        EmitLabel(elselabel);
    return true;
}

bool Generator::GenerateWhile(StatementModel& statement)
{
    string looplabel = NewLabel();
    string endlabel = NewLabel();
    EmitLabel(looplabel);
    if (!GenerateCondition(statement.expr, statement.expr.root, endlabel, false))
        return false;
    GenerateStatement(statement.statements[0]);
    EmitBranch("BR", looplabel);
    EmitLabel(endlabel);
    return true;
}

bool Generator::GenerateRepeat(StatementModel& statement)
{
    string looplabel = NewLabel();
    EmitLabel(looplabel);
    for (StatementModel& child : statement.statements)
        GenerateStatement(child);
    EmitSourceLine(statement.expr.nodes[statement.expr.root].token.pos);
    return GenerateCondition(statement.expr, statement.expr.root, looplabel, false);
}

// for V := A to B do S: the final value is kept in a temporary, so a goto out of the loop
// does not break the stack; the equality check before INC keeps V within its type
bool Generator::GenerateFor(StatementModel& statement)
{
    const ExpressionModel& var = statement.target;
    const ExpressionNode& varnode = var.nodes[var.root];
    Operand varop;
    if (!GetVariableOperand(var, var.root, varop))
        return false;
    bool isbyte = (Validator::TypeSize(varnode.type) == 1);
    Operand limit = statement.forlimit.asmlabel.empty() ?
        Operand::Index(5, statement.forlimit.offset) : Operand::Label(statement.forlimit.asmlabel);

    if (!GenerateValue(statement.expr2, statement.expr2.root))
        return false;
    Emit("MOV", Operand::Reg(0), limit, "final value");
    if (!GenerateValue(statement.expr, statement.expr.root))
        return false;
    Emit(isbyte ? "MOVB" : "MOV", Operand::Reg(0), varop, varnode.token.text);

    string looplabel = NewLabel();
    string endlabel = NewLabel();
    Emit("CMP", Operand::Reg(0), limit);
    EmitBranch(statement.downto ? "BLT" : "BGT", endlabel, "not even once");
    EmitLabel(looplabel);
    GenerateStatement(statement.statements[0]);
    if (isbyte)
    {
        Emit("CLR", Operand::Reg(0));
        Emit("BISB", varop, Operand::Reg(0), varnode.token.text);
        Emit("CMP", Operand::Reg(0), limit);
    }
    else
        Emit("CMP", varop, limit, varnode.token.text);
    EmitBranch("BEQ", endlabel, "the variable keeps the final value");
    Emit(statement.downto ? (isbyte ? "DECB" : "DEC") : (isbyte ? "INCB" : "INC"), varop);
    EmitBranch("BR", looplabel);
    EmitLabel(endlabel);
    return true;
}

// case: a chain of comparisons with the selector in R0, then the branches
bool Generator::GenerateCase(StatementModel& statement)
{
    if (!GenerateValue(statement.expr, statement.expr.root))
        return false;

    std::vector<string> elementlabels;
    for (CaseElementModel& element : statement.caseelements)
    {
        string elementlabel = NewLabel();
        elementlabels.push_back(elementlabel);
        for (CaseLabelModel& label : element.labels)
        {
            int64_t from = label.from.nodes[label.from.root].constvalue.ivalue;
            if (label.to.IsEmpty())
            {
                Emit("CMP", Operand::Reg(0), Operand::Imm(from));
                EmitBranch("BEQ", elementlabel);
                continue;
            }
            int64_t to = label.to.nodes[label.to.root].constvalue.ivalue;
            string next = NewLabel();
            Emit("CMP", Operand::Reg(0), Operand::Imm(from));
            EmitBranch("BLT", next);
            Emit("CMP", Operand::Reg(0), Operand::Imm(to));
            EmitBranch("BLE", elementlabel);
            EmitLabel(next);
        }
    }

    string endlabel = NewLabel();
    for (StatementModel& child : statement.statements)  // else part
        GenerateStatement(child);
    EmitBranch("BR", endlabel);
    for (size_t i = 0; i < statement.caseelements.size(); i++)
    {
        EmitLabel(elementlabels[i]);
        GenerateStatement(statement.caseelements[i].statement[0]);
        if (i + 1 < statement.caseelements.size())
            EmitBranch("BR", endlabel);
    }
    EmitLabel(endlabel);
    return true;
}

bool Generator::GenerateGoto(StatementModel& statement)
{
    if (statement.gotosymbol < 0)
        return false;
    EmitBranch("BR", GetGotoLabel(statement.gotosymbol));
    return true;
}


//////////////////////////////////////////////////////////////////////
