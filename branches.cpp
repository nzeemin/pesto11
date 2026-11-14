
#include <map>

#include "main.h"

// Branch layout, see docs/DESIGN.md section 9.2.
// Bxx and BR reach -128..+127 words from the word after the branch. The generator emits
// all branches short; this pass replaces the ones that do not reach their target:
//   BR L   ->  JMP L
//   Bxx L  ->  Byy .+6 / JMP L     (Byy is the inverse condition)
// Sizes only grow, so the pass repeats until nothing changes.


//////////////////////////////////////////////////////////////////////

const char* GetInverseBranch(const string& opcode)
{
    static const char* pairs[][2] =
    {
        { "BEQ", "BNE" }, { "BNE", "BEQ" }, { "BLT", "BGE" }, { "BGE", "BLT" },
        { "BGT", "BLE" }, { "BLE", "BGT" }, { "BLO", "BHIS" }, { "BHIS", "BLO" },
        { "BHI", "BLOS" }, { "BLOS", "BHI" }, { "BMI", "BPL" }, { "BPL", "BMI" },
        { "BVS", "BVC" }, { "BVC", "BVS" }, { "BCS", "BCC" }, { "BCC", "BCS" },
    };
    for (auto& pair : pairs)
    {
        if (opcode == pair[0])
            return pair[1];
    }
    return nullptr;
}

static string StripLabelColons(const string& label)
{
    string result = label;
    while (!result.empty() && result.back() == ':')
        result.pop_back();
    return result;
}

void LayoutBranches(ProcedureCode& code)
{
    for (;;)
    {
        // Addresses in words of every line and every label
        std::vector<int> addresses(code.lines.size());
        std::map<string, int> labels;
        int address = 0;
        for (size_t i = 0; i < code.lines.size(); i++)
        {
            addresses[i] = address;
            const CodeLine& line = code.lines[i];
            if (!line.label.empty())
                labels[StripLabelColons(line.label)] = address;
            address += line.words;
        }

        bool changed = false;
        for (size_t i = 0; i < code.lines.size(); i++)
        {
            CodeLine& line = code.lines[i];
            if (line.kind != LineBranch)
                continue;
            auto it = labels.find(line.target);
            if (it == labels.end())
                continue;  // Target outside of this procedure: never a short branch in our code
            int offset = it->second - (addresses[i] + 1);
            if (offset >= -128 && offset <= 127)
                continue;

            if (line.opcode == "BR")
            {
                line.kind = LineInstruction;
                line.opcode = "JMP";
                line.src = Operand::Label(line.target);
                line.text.clear();
                line.words = 2;
            }
            else
            {
                const char* inverse = GetInverseBranch(line.opcode);
                if (inverse == nullptr)
                    continue;
                CodeLine jump;
                jump.kind = LineInstruction;
                jump.opcode = "JMP";
                jump.src = Operand::Label(line.target);
                jump.words = 2;
                jump.comment = line.comment;
                line.kind = LineInstruction;  // Not a branch to a label any more
                line.opcode = inverse;
                line.text = ".+6";
                line.target.clear();
                line.comment.clear();
                line.words = 1;
                code.lines.insert(code.lines.begin() + i + 1, jump);
            }
            changed = true;
            break;  // Addresses changed: recompute
        }
        if (!changed)
            break;
    }
}


//////////////////////////////////////////////////////////////////////
