
#include <fstream>
#include <iterator>

#include "main.h"

#if defined(_MSC_VER)
#include <windows.h>        // GetModuleFileNameW
#elif defined(__APPLE__)
#include <mach-o/dyld.h>    // _NSGetExecutablePath
#include <climits>
#else
#include <climits>
#include <unistd.h>         // readlink
#endif


std::filesystem::path getexepath()
{
#if defined(_MSC_VER)
    wchar_t path[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, path, MAX_PATH);
    return path;
#elif defined(__APPLE__)
    char path[PATH_MAX] = { 0 };
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0)
        return std::filesystem::path();
    return std::filesystem::path(path);
#else
    char path[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    return std::string(path, (count > 0) ? count : 0);
#endif
}

bool ReadFileBinary(const std::filesystem::path& path, string& content)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file)
        return false;
    content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !file.bad();
}

std::filesystem::path FindFileIgnoreCase(const std::filesystem::path& path)
{
    // Always look through the directory, so the result has the real letter case of the file name
    // on any file system; this keeps file names in messages the same on Windows, Linux and macOS
    std::error_code ec;
    std::filesystem::path dir = path.parent_path();
    std::filesystem::path dirtoscan = dir.empty() ? std::filesystem::path(".") : dir;
    if (!std::filesystem::is_directory(dirtoscan, ec))
        return std::filesystem::path();

    auto toupper = [](string s)
    {
        for (char& c : s)
            if (c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
        return s;
    };
    string exact = path.filename().string();
    string target = toupper(exact);
    string found;
    for (const auto& entry : std::filesystem::directory_iterator(dirtoscan, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        string name = entry.path().filename().string();
        if (name == exact)
            return dir / name;  // Exact match wins over the other case variants
        if (found.empty() && toupper(name) == target)
            found = name;
    }
    if (found.empty())
        return std::filesystem::path();
    return dir / found;
}
