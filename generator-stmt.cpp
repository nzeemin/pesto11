
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
    case StmtAsm: return "asm";
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
    case StmtAsm:
        EmitSourceLine(statement.token.pos);
        ok = GenerateAsm(statement);
        break;
    case StmtWith:
        EmitSourceLine(statement.token.pos);
        ok = GenerateWith(statement);
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
        if (strcmp(name, "Exit") == 0)
        {
            EmitBranch("BR", m_exitlabel, "Exit");
            return;
        }
        if (!GenerateStdProcedure(name, expr, expr.root))
            EmitTodo(string("standard procedure ") + name);
        return;
    }
    if (symbol.kind == SymProcedure)
    {
        if (!GenerateUserCall(expr, expr.root))
            EmitTodo("call " + symbol.name);
        return;
    }
    EmitTodo("call " + symbol.name);
}


//////////////////////////////////////////////////////////////////////

// X := X + Y, X - Y, Y + X, X or Y, X and c: one instruction on the variable, see docs/DESIGN.md section 7.10.
// X is a variable addressed without code, Y a constant or a word variable. Returns false if not applicable.
bool Generator::GenerateAssignInPlace(StatementModel& statement)
{
    const ExpressionModel& target = statement.target;
    const ExpressionModel& value = statement.expr;
    const ExpressionNode& targetnode = target.nodes[target.root];
    const ExpressionNode& valuenode = value.nodes[value.root];
    if (valuenode.kind != NodeBinary || valuenode.convert != ConvertNone || valuenode.isconst)
        return false;
    const Token& op = valuenode.token;
    bool isplus = op.IsSymbol(SymbolPlus), isminus = op.IsSymbol(SymbolMinus);
    bool isor = op.IsKeyword(KeywordOR), isand = op.IsKeyword(KeywordAND);
    if (!isplus && !isminus && !isor && !isand)
        return false;
    if (!Validator::IsIntegerType(valuenode.type) && !Validator::IsOrdinal(valuenode.type))
        return false;
    if (Validator::IsBooleanType(valuenode.type))
        return false;  // and/or of Booleans go through the conditions
    Operand dest;
    if (!GetVariableOperand(target, target.root, dest))
        return false;
    bool isbyte = (Validator::TypeSize(targetnode.type) == 1);

    // Which operand is the target itself
    Operand left, right;
    bool leftbyte = false, rightbyte = false;
    bool leftok = GetSimpleOperand(value, valuenode.left, left, leftbyte) && !value.nodes[valuenode.left].isconst;
    bool rightok = GetSimpleOperand(value, valuenode.right, right, rightbyte);
    Operand other;
    bool otherbyte;
    bool othervar;
    if (leftok && left.ToString() == dest.ToString() && leftbyte == isbyte && rightok)
    {
        other = right;
        otherbyte = rightbyte;
        othervar = !value.nodes[valuenode.right].isconst;
    }
    else if ((isplus || isor) && rightok && right.ToString() == dest.ToString() && rightbyte == isbyte &&
             !value.nodes[valuenode.right].isconst &&
             GetSimpleOperand(value, valuenode.left, left, leftbyte))
    {
        other = left;
        otherbyte = leftbyte;
        othervar = !value.nodes[valuenode.left].isconst;
    }
    else
        return false;
    if (otherbyte)
        return false;  // A byte variable cannot be a word source

    const string& name = targetnode.token.text;
    bool isconst = !othervar;
    int64_t c = isconst ? (int16_t)(other.value & 0xFFFF) : 0;
    if (isconst && (isplus || isminus) && (c == 1 || c == -1))
    {
        bool up = (c == 1) == isplus;
        Emit(up ? (isbyte ? "INCB" : "INC") : (isbyte ? "DECB" : "DEC"), dest, Operand(), name);
        return true;
    }
    if (isbyte)
    {
        // No ADDB/SUBB on PDP-11; BISB and BICB exist
        if (isor && isconst)
        {
            Emit("BISB", Operand::Imm(c & 0xFF), dest, name);
            return true;
        }
        if (isand && isconst)
        {
            Emit("BICB", Operand::Imm(~c & 0xFF), dest, name);
            return true;
        }
        return false;
    }
    if (isand)
    {
        if (!isconst)
            return false;  // BIC needs the complement
        Emit("BIC", Operand::Imm((int16_t)(~c & 0xFFFF)), dest, name);
        return true;
    }
    if (isconst && c == 0)
        return true;  // X := X + 0
    Emit(isplus ? "ADD" : isminus ? "SUB" : "BIS", other, dest, name);
    return true;
}

bool Generator::GenerateAssign(StatementModel& statement)
{
    const ExpressionModel& target = statement.target;
    const ExpressionModel& value = statement.expr;
    const ExpressionNode& targetnode = target.nodes[target.root];
    const ExpressionNode& valuenode = value.nodes[value.root];
    int type = targetnode.type;
    if (Validator::IsStringType(type) ||
        (Validator::IsCharType(type) && Validator::IsStringType(valuenode.type) && valuenode.convert == ConvertNone) ||
        (Validator::IsCharArray(type) && valuenode.isconst && !Validator::IsCharArray(valuenode.type)))
        return GenerateStringAssign(statement);
    if (Validator::IsSetType(type))
        return GenerateSetAssign(statement);
    if (Validator::IsRealType(type))
        return GenerateReal(value, value.root) && StoreReal(target, target.root);
    if (Validator::IsArrayType(type) || Validator::IsRecordType(type))
        return GenerateStructAssign(statement);
    if (!Validator::IsScalar(type))
        return false;  // Real, strings, sets: later steps

    bool isbyte = (Validator::TypeSize(type) == 1);
    bool checked = (targetnode.token.directives & DirectiveR) != 0 &&
        (Validator::GetType(type).kind == TypeKindSubrange || Validator::GetType(type).kind == TypeKindEnum ||
         Validator::GetType(type).kind == TypeKindByte);

    // Constant or a variable of the same size: one instruction without R0.
    // The target may need its address in R3 first (a field, an element, an outer variable).
    if (!checked)
    {
        if (GenerateAssignInPlace(statement))
            return true;
        Operand src, dest;
        bool srcbyte;
        if (valuenode.isconst && GetSimpleOperand(value, value.root, src, srcbyte))
        {
            if (!GenerateDesignator(target, target.root, 3, dest))
                return false;
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
            if (!GenerateDesignator(target, target.root, 3, dest))
                return false;
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
// for: the final value is a constant or a slot; the loop is rotated, see docs/DESIGN.md section 7.10:
//         <pretest: not even once -> E>
//         BR B
//   L:    INC var
//   B:    <body>
//         CMP var, limit
//         BNE L
//   E:
bool Generator::GenerateFor(StatementModel& statement)
{
    const ExpressionModel& var = statement.target;
    const ExpressionNode& varnode = var.nodes[var.root];
    // A variable of an enclosing procedure has no static operand: its address is found
    // through the static chain into R3 before each use, the body may change R3
    Operand varop;
    bool staticvar = GetVariableOperand(var, var.root, varop);
    auto getvar = [&]() -> bool
    {
        return staticvar || GenerateDesignator(var, var.root, 3, varop);
    };
    bool isbyte = (Validator::TypeSize(varnode.type) == 1);
    const char* movop = isbyte ? "MOVB" : "MOV";
    const char* cmpop = isbyte ? "CMPB" : "CMP";

    const ExpressionNode& initnode = statement.expr.nodes[statement.expr.root];
    const ExpressionNode& finalnode = statement.expr2.nodes[statement.expr2.root];
    Operand limit;
    if (finalnode.isconst)
        limit = Operand::Imm(ConstOrdinalValue(finalnode));
    else
    {
        limit = statement.forlimit.asmlabel.empty() ?
            Operand::Index(5, statement.forlimit.offset) : Operand::Label(statement.forlimit.asmlabel);
        Operand finalop;
        bool finalbyte;
        if (GetSimpleOperand(statement.expr2, statement.expr2.root, finalop, finalbyte) && !finalbyte)
            Emit("MOV", finalop, limit, "final value");
        else
        {
            if (!GenerateValue(statement.expr2, statement.expr2.root))
                return false;
            Emit("MOV", Operand::Reg(0), limit, "final value");
        }
    }

    string looplabel = NewLabel();
    string bodylabel = NewLabel();
    string endlabel = NewLabel();
    Operand initop;
    bool initbyte;
    bool initsimple = GetSimpleOperand(statement.expr, statement.expr.root, initop, initbyte);
    if (initnode.isconst && finalnode.isconst)
    {
        // Both known: the pretest is decided now
        int64_t from = ConstOrdinalValue(initnode), to = ConstOrdinalValue(finalnode);
        if (!getvar())
            return false;
        if (from == 0)
            Emit(isbyte ? "CLRB" : "CLR", varop, Operand(), varnode.token.text);
        else
            Emit(movop, initop, varop, varnode.token.text);
        if (statement.downto ? from < to : from > to)
            EmitBranch("BR", endlabel, "not even once");
    }
    else if (initsimple && (isbyte || !initbyte))
    {
        if (!getvar())
            return false;
        if (initnode.isconst && initop.value == 0)
            Emit(isbyte ? "CLRB" : "CLR", varop, Operand(), varnode.token.text);
        else
            Emit(movop, initop, varop, varnode.token.text);
        Emit(cmpop, varop, limit);  // The address in R3 is still there
        EmitBranch(statement.downto ? (isbyte ? "BLO" : "BLT") : (isbyte ? "BHI" : "BGT"), endlabel, "not even once");
    }
    else
    {
        if (!GenerateValue(statement.expr, statement.expr.root))
            return false;
        if (!getvar())
            return false;
        Emit(movop, Operand::Reg(0), varop, varnode.token.text);
        Emit("CMP", Operand::Reg(0), limit);  // Byte values are zero-extended: a signed comparison is right
        EmitBranch(statement.downto ? "BLT" : "BGT", endlabel, "not even once");
    }
    EmitBranch("BR", bodylabel);
    EmitLabel(looplabel);
    if (!getvar())
        return false;
    Emit(statement.downto ? (isbyte ? "DECB" : "DEC") : (isbyte ? "INCB" : "INC"), varop, Operand(), varnode.token.text);
    EmitLabel(bodylabel);
    GenerateStatement(statement.statements[0]);
    if (!getvar())
        return false;
    Emit(cmpop, varop, limit, varnode.token.text);
    EmitBranch("BNE", looplabel, "the variable keeps the final value");
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

// Array or record assignment: block copy from the source address in R3 to the target in R4
bool Generator::GenerateStructAssign(StatementModel& statement)
{
    const ExpressionModel& target = statement.target;
    const ExpressionModel& value = statement.expr;
    if (!IsDesignator(value, value.root) || value.nodes[value.root].convert != ConvertNone)
        return false;  // String constant into an array of Char: step 5
    int type = target.nodes[target.root].type;
    int size = Validator::TypeSize(type);
    bool wordaligned = Validator::GetType(type).align >= 2;

    // Both static and short: direct moves
    Operand src, dest;
    if (wordaligned && size <= 8 && GetVariableOperand(value, value.root, src) && GetVariableOperand(target, target.root, dest))
    {
        for (int i = 0; i < size; i += 2)
        {
            Operand s = src, d = dest;
            AddOffset(s, i, 3);
            AddOffset(d, i, 4);
            Emit("MOV", s, d, i == 0 ? target.nodes[target.root].token.text : "");
        }
        return true;
    }

    if (!DesignatorUsesR0(target, target.root))
    {
        // The target address code touches only R4 and R1: compute it after the source
        if (!GenerateAddress(value, value.root, 3))
            return false;
        if (!GenerateAddress(target, target.root, 4))
            return false;
    }
    else
    {
        if (!GenerateAddress(target, target.root, 3))
            return false;
        Emit("MOV", Operand::Reg(3), Operand::AutoDec(6), "target address");
        if (!GenerateAddress(value, value.root, 3))
            return false;
        Emit("MOV", Operand::AutoInc(6), Operand::Reg(4));
    }
    EmitBlockCopy(size, wordaligned);
    return true;
}

// with: the record address goes into the slot once; a static record needs no slot
bool Generator::GenerateWith(StatementModel& statement)
{
    std::vector<string> keys;
    for (size_t i = 0; i < statement.withvars.size(); i++)
    {
        const ExpressionModel& var = statement.withvars[i];
        const TempSlot& slot = statement.withslots[i];
        string key = slot.ToString();
        keys.push_back(key);
        m_withstatic.erase(key);
        Operand operand;
        if (GetVariableOperand(var, var.root, operand) && (operand.mode == OpLabel || operand.mode == OpAbsolute ||
            (operand.mode == OpIndex && operand.reg == 5)))
        {
            m_withstatic[key] = operand;
            continue;
        }
        if (!GenerateAddress(var, var.root, 3))
            return false;
        Operand slotop = slot.asmlabel.empty() ? Operand::Index(5, slot.offset) : Operand::Label(slot.asmlabel);
        Emit("MOV", Operand::Reg(3), slotop, "with " + var.nodes[var.root].token.text);
    }
    for (StatementModel& child : statement.statements)
        GenerateStatement(child);
    for (const string& key : keys)
        m_withstatic.erase(key);
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
