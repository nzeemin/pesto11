// Test runner for Pesto/11.
// Run from the repository root. For every tests/*.test file it creates tests.temp/<name>/,
// writes the Pascal source, runs the compiler, compares the compiler output and, when the
// tools are available in x-tools/, assembles and links the result.

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
typedef std::string string;

const char* TESTS_SUB_DIR = "tests";
const char* TESTS_TEMP_SUB_DIR = "tests.temp";
const char* TOOLS_SUB_DIR = "x-tools";
const char* SOURCE_FILE_NAME = "TEST.PAS";
const char* MAC_FILE_NAME = "TEST.MAC";
const char* RUNTIME_MAC_FILE_NAME = "PESTO.MAC";

#ifdef _WIN32
const char* EXE_SUFFIX = ".exe";
#else
const char* EXE_SUFFIX = "";
#endif

bool g_verbose = false;
std::vector<string> g_filters;  // Test name prefixes from the command line
fs::path g_compilerpath;
fs::path g_macro11path;         // Empty if the tool is not available
fs::path g_pclink11path;        // Empty if the tool is not available

int g_failedtests = 0;
int g_failedtodo = 0;           // Failed because the .MAC file has TODOs in it


//////////////////////////////////////////////////////////////////////
// Utilities

// Read a line, stripping a trailing CR so CRLF/LF files compare equal on any platform
static std::istream& getlinecr(std::istream& is, string& line)
{
    std::getline(is, line);
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return is;
}

static std::vector<string> read_lines(const fs::path& path, bool skipempty)
{
    std::vector<string> lines;
    std::ifstream file(path, std::ios::in | std::ios::binary);
    string line;
    while (getlinecr(file, line))
    {
        if (skipempty && line.empty())
            continue;
        lines.push_back(line);
    }
    return lines;
}

static void write_lines(const fs::path& path, const std::vector<string>& lines)
{
    std::ofstream ofs(path, std::ios::out | std::ios::binary);
    for (const string& line : lines)
    {
        ofs.write(line.data(), line.size());
        ofs.put('\n');
    }
}

// Strip comment and trailing whitespace from an assembly line
static string strip_mac_line(string line)
{
    size_t commentpos = line.find(';');
    if (commentpos != string::npos)
        line.erase(commentpos);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t'))
        line.pop_back();
    return line;
}

// Run the command in the given directory, stdout and stderr go to outfilename
static void run_command(const fs::path& workingdir, const fs::path& program, const string& args, const string& outfilename)
{
    string command = "\"" + program.string() + "\" " + args + " >" + outfilename + " 2>&1";
#ifdef _WIN32
    command = "\"" + command + "\"";  // cmd.exe strips the outer quotes
#endif
    if (g_verbose)
        std::cout << std::endl << "  > " << command;

    fs::path savedcwd = fs::current_path();
    fs::current_path(workingdir);
    std::fflush(nullptr);
    int result = std::system(command.c_str());
    (void)result;  // Non-zero exit code is normal for a failed compilation, the output decides
    fs::current_path(savedcwd);
}

static bool matches_filter(const string& testname)
{
    if (g_filters.empty())
        return true;
    for (const string& filter : g_filters)
    {
        if (testname.compare(0, filter.size(), filter) == 0)
            return true;
    }
    return false;
}

static fs::path find_tool(const string& name)
{
    fs::path path = fs::absolute(fs::path(TOOLS_SUB_DIR) / (name + EXE_SUFFIX));
    if (fs::exists(path))
        return path;
    return fs::path();
}


//////////////////////////////////////////////////////////////////////
// Test processing

struct TestCase
{
    string options;                     // Compiler command line options
    std::vector<string> source;         // Pascal source lines
    std::vector<std::pair<string, std::vector<string>>> extrafiles;  // Include files: name and lines
    std::vector<string> expected;       // Expected compiler output lines, non-empty
    std::vector<string> etalon;         // Reference .MAC lines, stripped; empty if no section
};

static bool parse_test_file(const fs::path& path, TestCase& test)
{
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file)
        return false;

    string line;
    getlinecr(file, test.options);  // Line 1: compiler options
    getlinecr(file, line);          // Line 2: skipped (separator)

    int section = 2;
    while (getlinecr(file, line))
    {
        if (!line.empty() && line[0] == '-')
        {
            section++;
            continue;
        }
        switch (section)
        {
        case 2:
            // "@@@ FILE NAME.EXT" starts an extra file, like an include file, in the test directory
            if (line.compare(0, 9, "@@@ FILE ") == 0)
                test.extrafiles.push_back(std::make_pair(line.substr(9), std::vector<string>()));
            else if (!test.extrafiles.empty())
                test.extrafiles.back().second.push_back(line);
            else
                test.source.push_back(line);
            break;
        case 3:
            if (!line.empty())
                test.expected.push_back(line);
            break;
        case 4:
            if (line.empty() || line[0] == ';')
                break;
            line = strip_mac_line(line);
            if (!line.empty())
                test.etalon.push_back(line);
            break;
        default:
            break;  // Section 5 (screen output) is not supported yet
        }
    }
    return true;
}

// Options that make the compiler dump a stage and quit without generating code
static bool is_dump_mode(const string& options)
{
    static const char* dumpoptions[] =
    {
        "-t", "--tokenizeonly", "-p", "--parsingonly", "-v", "--validationonly"
    };
    size_t pos = 0;
    while (pos < options.size())
    {
        size_t end = options.find(' ', pos);
        if (end == string::npos)
            end = options.size();
        string option = options.substr(pos, end - pos);
        for (const char* dumpoption : dumpoptions)
        {
            if (option == dumpoption)
                return true;
        }
        pos = end + 1;
    }
    return false;
}

static void test_failed(const string& message, bool todo = false)
{
    std::cout << "FAILED: " << message << std::endl;
    g_failedtests++;
    if (todo)
        g_failedtodo++;
}

// Assemble a .MAC file; returns false and reports if the assembler printed anything
static bool assemble(const fs::path& testdir, const string& macname)
{
    string base = macname.substr(0, macname.find('.'));
    string outname = base + ".outas";
    run_command(testdir, g_macro11path, macname + " -l " + base + ".LST -o " + base + ".OBJ -rt11", outname);
    std::vector<string> lines = read_lines(testdir / outname, true);
    if (!lines.empty())
    {
        test_failed("macro11 output for " + macname + ": " + lines[0]);
        return false;
    }
    return true;
}

static void process_test(const string& testname, const fs::path& testfilepath)
{
    std::cout << std::left << std::setw(32) << testname << " " << std::flush;

    TestCase test;
    if (!parse_test_file(testfilepath, test))
    {
        test_failed("cannot read the test file");
        return;
    }

    // Prepare clean test directory
    fs::path testdir = fs::absolute(fs::path(TESTS_TEMP_SUB_DIR) / testname);
    std::error_code ec;
    fs::remove_all(testdir, ec);
    fs::create_directories(testdir);

    // Write the Pascal source and extra files, LF line endings, bytes as is
    write_lines(testdir / SOURCE_FILE_NAME, test.source);
    for (const auto& extrafile : test.extrafiles)
        write_lines(testdir / extrafile.first, extrafile.second);

    // Run the compiler
    run_command(testdir, g_compilerpath, test.options + " " + SOURCE_FILE_NAME, "TEST.out");
    std::vector<string> output = read_lines(testdir / "TEST.out", true);

    if (output != test.expected)
    {
        test_failed("compiler output is different");
        size_t count = std::max(output.size(), test.expected.size());
        for (size_t i = 0; i < count; i++)
        {
            const string& exp = (i < test.expected.size()) ? test.expected[i] : string("<none>");
            const string& act = (i < output.size()) ? output[i] : string("<none>");
            if (exp == act)
                continue;
            std::cout << "    expected: " << exp << std::endl;
            std::cout << "    actual:   " << act << std::endl;
        }
        return;
    }

    // Expected errors, or a dump-only mode (-t, -p, -v): no .MAC file, the output comparison is enough
    bool outputonly = is_dump_mode(test.options);
    for (const string& line : test.expected)
    {
        if (line.find("error") != string::npos)
            outputonly = true;
    }
    if (outputonly)
    {
        std::cout << "OK (output)" << std::endl;
        return;
    }

    // Check the .MAC file
    fs::path macpath = testdir / MAC_FILE_NAME;
    if (!fs::exists(macpath))
    {
        test_failed(string(MAC_FILE_NAME) + " not found");
        return;
    }
    std::vector<string> maclines = read_lines(macpath, true);
    std::vector<string> mactext;
    for (const string& line : maclines)
    {
        if (line.find("TODO") != string::npos)
        {
            test_failed(string(MAC_FILE_NAME) + " contains TODO", true);
            return;
        }
        if (line[0] == ';')
            continue;
        string stripped = strip_mac_line(line);
        if (!stripped.empty())
            mactext.push_back(stripped);
    }
    fs::path rtpath = testdir / RUNTIME_MAC_FILE_NAME;
    bool hasruntime = fs::exists(rtpath);
    if (hasruntime)
    {
        for (const string& line : read_lines(rtpath, true))
        {
            if (line.find("TODO") != string::npos)
            {
                test_failed(string(RUNTIME_MAC_FILE_NAME) + " contains TODO", true);
                return;
            }
        }
    }

    // Compare with the reference .MAC
    if (!test.etalon.empty())
    {
        size_t count = std::min(mactext.size(), test.etalon.size());
        for (size_t i = 0; i < count; i++)
        {
            if (mactext[i] != test.etalon[i])
            {
                test_failed(string(MAC_FILE_NAME) + " differs from the reference on code line " + std::to_string(i + 1));
                std::cout << "    expected: " << test.etalon[i] << std::endl;
                std::cout << "    actual:   " << mactext[i] << std::endl;
                return;
            }
        }
        if (mactext.size() != test.etalon.size())
        {
            test_failed(string(MAC_FILE_NAME) + " has " + std::to_string(mactext.size()) +
                    " code lines, the reference has " + std::to_string(test.etalon.size()));
            return;
        }
    }

    // Assemble and link when the tools are available
    if (!g_macro11path.empty() && !g_pclink11path.empty())
    {
        if (!assemble(testdir, MAC_FILE_NAME))
            return;
        if (hasruntime && !assemble(testdir, RUNTIME_MAC_FILE_NAME))
            return;

        string linkargs = string("TEST.OBJ") + (hasruntime ? " PESTO.OBJ" : "") + " -EXECUTE:TEST.SAV";
        run_command(testdir, g_pclink11path, linkargs, "TEST.outln");
        for (const string& line : read_lines(testdir / "TEST.outln", true))
        {
            if (line.find("Undefined globals:") != string::npos)
            {
                test_failed("linker found undefined globals");
                return;
            }
        }
    }

    std::cout << "OK" << std::endl;
}


//////////////////////////////////////////////////////////////////////

static fs::path default_compiler_path()
{
#ifdef _WIN32
    const char* configuration = std::getenv("CONFIGURATION");
    string config = (configuration != nullptr && configuration[0] != 0) ? configuration : "Debug";
    return fs::path(config) / "pesto11.exe";
#else
    return fs::path("pesto11");
#endif
}

static bool parse_commandline(int argc, char* argv[])
{
    for (int argi = 1; argi < argc; argi++)
    {
        string arg = argv[argi];
        if (arg == "-v" || arg == "--verbose")
            g_verbose = true;
        else if (arg.compare(0, 11, "--compiler=") == 0)
            g_compilerpath = arg.substr(11);
        else if (!arg.empty() && arg[0] == '-')
        {
            std::cout << "Unknown option: " << arg << std::endl;
            std::cout << "Usage: testrunner [-v] [--compiler=PATH] [TESTNAMEPREFIX ...]" << std::endl;
            return false;
        }
        else
            g_filters.push_back(arg);
    }
    return true;
}

int main(int argc, char* argv[])
{
    std::cout << "TestRunner utility for Pesto/11" << std::endl;

    if (!parse_commandline(argc, argv))
        return 2;

    if (g_compilerpath.empty())
        g_compilerpath = default_compiler_path();
    g_compilerpath = fs::absolute(g_compilerpath);
    if (!fs::exists(g_compilerpath))
    {
        std::cout << "Compiler not found: " << g_compilerpath.string() << std::endl;
        return 2;
    }

    g_macro11path = find_tool("macro11");
    g_pclink11path = find_tool("pclink11");
    if (g_macro11path.empty() || g_pclink11path.empty())
        std::cout << "macro11/pclink11 not found in " << TOOLS_SUB_DIR << ", assembling and linking are skipped" << std::endl;

    // Collect the test cases
    std::vector<fs::path> testfiles;
    if (fs::exists(TESTS_SUB_DIR))
    {
        for (const auto& entry : fs::directory_iterator(TESTS_SUB_DIR))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".test" &&
                matches_filter(entry.path().stem().string()))
                testfiles.push_back(entry.path());
        }
    }
    std::sort(testfiles.begin(), testfiles.end());

    fs::create_directories(TESTS_TEMP_SUB_DIR);

    for (const fs::path& testfile : testfiles)
        process_test(testfile.stem().string(), testfile);

    int total = (int)testfiles.size();
    std::cout << "TOTAL tests executed: " << total << ", passed: " << (total - g_failedtests);
    if (g_failedtests > 0)
    {
        std::cout << ", failed: " << g_failedtests;
        if (g_failedtodo > 0)
            std::cout << " (TODO: " << g_failedtodo << ")";
    }
    std::cout << std::endl;

    return (g_failedtests > 0) ? 1 : 0;
}
