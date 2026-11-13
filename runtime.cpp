
#include <algorithm>

#include "main.h"

// Runtime templates: blocks of assembly in runtime-*.tmac, see docs/RUNTIME.md.
//   ;#####...      starts a block
//   ;## NAME       name of the block, also its entry label
//   ;## Need NAME  the block calls another block


//////////////////////////////////////////////////////////////////////

bool RuntimeGenerator::LoadTemplate(const std::filesystem::path& path)
{
    string text;
    if (!ReadFileBinary(path, text))
        return false;

    std::vector<string> lines;
    string line;
    for (size_t i = 0; i < text.size(); i++)
    {
        char ch = text[i];
        if (ch == '\r')
            continue;
        if (ch == '\n')
        {
            lines.push_back(line);
            line.clear();
        }
        else
            line += ch;
    }
    if (!line.empty())
        lines.push_back(line);

    RuntimeBlock* block = nullptr;
    bool expectname = false;
    for (const string& item : lines)
    {
        if (item.compare(0, 5, ";####") == 0)
        {
            m_blocks.push_back(RuntimeBlock());
            block = &m_blocks.back();
            expectname = true;
            continue;
        }
        if (block == nullptr)
            continue;  // File header
        if (expectname && item.compare(0, 4, ";## ") == 0)
        {
            block->name = item.substr(4);
            while (!block->name.empty() && (block->name.back() == ' ' || block->name.back() == '\t'))
                block->name.pop_back();
            expectname = false;
            continue;
        }
        if (item.compare(0, 8, ";## Need") == 0)
        {
            string need = item.substr(8);
            need.erase(0, need.find_first_not_of(" \t"));
            while (!need.empty() && (need.back() == ' ' || need.back() == '\t'))
                need.pop_back();
            block->needs.push_back(need);
            continue;
        }
        block->lines.push_back(item);
    }

    // Trailing empty lines of each block are not needed
    for (RuntimeBlock& item : m_blocks)
    {
        while (!item.lines.empty() && item.lines.back().empty())
            item.lines.pop_back();
    }
    return true;
}

const RuntimeBlock* RuntimeGenerator::FindBlock(const string& name) const
{
    for (const RuntimeBlock& block : m_blocks)
    {
        if (block.name == name)
            return &block;
    }
    return nullptr;
}

std::vector<const RuntimeBlock*> RuntimeGenerator::CollectBlocks(const std::set<string>& needs) const
{
    std::set<string> collected;
    std::vector<string> queue(needs.begin(), needs.end());
    while (!queue.empty())
    {
        string name = queue.back();
        queue.pop_back();
        if (!collected.insert(name).second)
            continue;
        const RuntimeBlock* block = FindBlock(name);
        if (block == nullptr)
        {
            ReportError("runtime block not found in the template: " + name);
            continue;
        }
        for (const string& need : block->needs)
            queue.push_back(need);
    }

    std::vector<const RuntimeBlock*> result;
    for (const RuntimeBlock& block : m_blocks)
    {
        if (collected.count(block.name) > 0)
            result.push_back(&block);
    }
    return result;
}


//////////////////////////////////////////////////////////////////////
