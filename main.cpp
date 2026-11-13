
#include <cassert>
#include <fstream>

#include "main.h"


//////////////////////////////////////////////////////////////////////
// Globals

string g_infilename;    // Input file name, as given in the command line
string g_outfilename;   // Output .MAC file name for the program code
string g_rtfilename;    // Output .MAC file name for the runtime

bool g_quiet = false;
TargetPlatform g_platform = PlatformUKNC;
bool g_onefile = false;
bool g_noopt = false;
bool g_tokenizeonly = false;
bool g_parsingonly = false;
bool g_validationonly = false;
bool g_showgeneration = false;

std::vector<string> g_sourcefiles;

static int g_errorcount = 0;

const char* RUNTIME_FILE_NAME = "PESTO.MAC";
const char* RUNTIME_TEMPLATE_NAME = "runtime-UKNC.tmac";
const size_t RT11_MAX_FILE_NAME = 6;  // RT-11 file name is up to 6 characters, type up to 3


//////////////////////////////////////////////////////////////////////
// Platforms

const char* GetPlatformName(TargetPlatform platform)
{
    switch (platform)
    {
    case PlatformUKNC: return "UKNC";
    case PlatformBK0010: return "BK0010";
    default:
        return nullptr;
    }
}

TargetPlatform FindPlatformByName(const string& name)
{
    if (name == "UKNC") return PlatformUKNC;
    if (name == "BK0010") return PlatformBK0010;
    return PlatformNone;
}


//////////////////////////////////////////////////////////////////////
// Diagnostics

int RegisterSourceFile(const string& filename)
{
    g_sourcefiles.push_back(filename);
    return (int)g_sourcefiles.size() - 1;
}

static void PrintPosition(std::ostream& out, const SourcePos& pos)
{
    if (pos.file >= 0 && pos.file < (int)g_sourcefiles.size())
        out << g_sourcefiles[pos.file];
    else
        out << "pesto11";
    if (pos.line > 0)
        out << "(" << pos.line << "," << pos.col << ")";
    out << ": ";
}

void ReportError(const SourcePos& pos, int code, const string& message)
{
    g_errorcount++;
    PrintPosition(std::cout, pos);
    std::cout << "error";
    if (code > 0)
        std::cout << " " << code;
    std::cout << ": " << message << std::endl;
}

void ReportWarning(const SourcePos& pos, const string& message)
{
    PrintPosition(std::cout, pos);
    std::cout << "warning: " << message << std::endl;
}

void ReportError(const string& message)
{
    ReportError(SourcePos(), 0, message);
}

void ReportWarning(const string& message)
{
    ReportWarning(SourcePos(), message);
}

int GetErrorCount()
{
    return g_errorcount;
}

void ResetErrorCount()
{
    g_errorcount = 0;
}


//////////////////////////////////////////////////////////////////////
// Command line

static void PrintWelcome()
{
    std::cout << "Pesto/11 version " << PESTO11_VERSION
              << " - Turbo Pascal 3.0 cross-compiler for PDP-11" << std::endl;
}

static void PrintUsage()
{
    std::cout << std::endl
              << "Usage: pesto11 [options] PROG.PAS" << std::endl
              << "Options:" << std::endl
              << "  -q --quiet            Be quiet" << std::endl
              << "  --onefile             Put the program and the runtime into one .MAC file" << std::endl
              << "  --platform=UKNC       Target platform (default UKNC)" << std::endl
              << "  --noopt               Disable peephole optimization" << std::endl
              << "  -t --tokenizeonly     Show tokens and quit" << std::endl
              << "  -p --parsingonly      Show parsing result and quit" << std::endl
              << "  -v --validationonly   Show validation result and quit" << std::endl
              << "  -g --showgeneration   Show generated code" << std::endl
              << "  -h --help             Show this help" << std::endl;
}

// Returns false if the compiler should stop
static bool ParseCommandLine(int argc, char* argv[])
{
    for (int argi = 1; argi < argc; argi++)
    {
        string arg = argv[argi];
        if (arg.empty())
            continue;
        if (arg[0] != '-')
        {
            if (!g_infilename.empty())
            {
                ReportError("more than one input file: " + arg);
                return false;
            }
            g_infilename = arg;
            continue;
        }

        if (arg == "-q" || arg == "--quiet")
            g_quiet = true;
        else if (arg == "--onefile")
            g_onefile = true;
        else if (arg == "--noopt")
            g_noopt = true;
        else if (arg == "-t" || arg == "--tokenizeonly")
            g_tokenizeonly = true;
        else if (arg == "-p" || arg == "--parsingonly")
            g_parsingonly = true;
        else if (arg == "-v" || arg == "--validationonly")
            g_validationonly = true;
        else if (arg == "-g" || arg == "--showgeneration")
            g_showgeneration = true;
        else if (arg == "-h" || arg == "--help")
        {
            PrintUsage();
            return false;
        }
        else if (arg.compare(0, 11, "--platform=") == 0)
        {
            string name = arg.substr(11);
            g_platform = FindPlatformByName(name);
            if (g_platform == PlatformNone)
            {
                ReportError("unknown platform: " + name);
                return false;
            }
            if (g_platform != PlatformUKNC)
            {
                ReportError("platform is not supported yet: " + name);
                return false;
            }
        }
        else
        {
            ReportError("unknown option: " + arg);
            return false;
        }
    }

    if (g_infilename.empty())
    {
        PrintUsage();
        return false;
    }

    return true;
}

// Make output file names from the input file name: PROG.PAS -> PROG.MAC
static void PrepareOutputFileNames()
{
    std::filesystem::path inpath(g_infilename);
    std::filesystem::path outpath = inpath;
    outpath.replace_extension(".MAC");
    g_outfilename = outpath.string();

    std::filesystem::path rtpath = inpath.parent_path() / RUNTIME_FILE_NAME;
    g_rtfilename = rtpath.string();

    string stem = inpath.stem().string();
    if (stem.size() > RT11_MAX_FILE_NAME)
        ReportWarning("file name '" + stem + "' is longer than 6 characters, RT-11 will not accept it");
}


//////////////////////////////////////////////////////////////////////
// Compilation stages

// -t mode: print all the tokens, one per line
static void ShowTokenization(Tokenizer& tokenizer)
{
    unsigned directives = DIRECTIVES_DEFAULT;
    for (;;)
    {
        Token token = tokenizer.GetNextToken();

        if (token.directives != directives)
        {
            directives = token.directives;
            std::cout << "DIRECTIVES " << GetDirectivesString(directives) << std::endl;
        }

        if (token.pos.file > 0 && token.pos.file < (int)g_sourcefiles.size())
            std::cout << g_sourcefiles[token.pos.file] << ":";
        std::cout << token.pos.line << "," << token.pos.col << " " << token.GetTokenTypeStr();
        string value = token.GetDisplayValue();
        if (!value.empty())
            std::cout << " " << value;
        std::cout << std::endl;

        if (token.IsEOF())
            break;
    }
}

static void ProcessFiles()
{
    Tokenizer tokenizer;
    if (!tokenizer.OpenMainFile(g_infilename))
        return;

    if (g_tokenizeonly)
    {
        ShowTokenization(tokenizer);
        return;
    }

    Parser parser(&tokenizer);
    parser.ParseProgram();
    if (GetErrorCount() > 0)
        return;

    if (g_parsingonly)
    {
        g_program.Dump(std::cout);
        return;
    }

    Validator validator;
    validator.ValidateProgram();
    if (GetErrorCount() > 0)
        return;

    if (g_validationonly)
    {
        g_showvalidation = true;
        g_program.Dump(std::cout);
        return;
    }

    RuntimeGenerator runtime;
    std::filesystem::path templatepath = getexepath().parent_path() / RUNTIME_TEMPLATE_NAME;
    if (!runtime.LoadTemplate(templatepath))
    {
        ReportError("cannot read the runtime template " + templatepath.string());
        return;
    }

    FinalModel final;
    Generator generator(&final, &runtime);
    generator.GenerateProgram();
    if (GetErrorCount() > 0)
        return;

    string title = std::filesystem::path(g_infilename).stem().string();
    for (char& c : title)
        c = (char)toupper((unsigned char)c);
    WriteAssemblyFiles(final, runtime, g_outfilename, g_rtfilename, title.substr(0, 6));
}


//////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    bool quietarg = false;
    for (int argi = 1; argi < argc; argi++)
    {
        if (strcmp(argv[argi], "-q") == 0 || strcmp(argv[argi], "--quiet") == 0)
            quietarg = true;
    }
    if (!quietarg)
        PrintWelcome();

    if (!ParseCommandLine(argc, argv))
        return (GetErrorCount() > 0) ? 1 : 0;

    PrepareOutputFileNames();

    ProcessFiles();

    return (GetErrorCount() > 0) ? 1 : 0;
}
