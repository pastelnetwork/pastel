// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include <config/pastel-config.h>
#endif

#include <fstream>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <stdexcept>
#include <array>
#include <memory>
#include <iostream>
#include <errno.h>
#include <regex>

#if (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
#include <pthread.h>
#include <pthread_np.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(__linux__)
#include <sys/sysinfo.h>
#elif defined(WIN32)
#include <fcntl.h>
#endif

#include <utils/str_utils.h>
#include <utils/set_types.h>
#include <utils/util.h>
#include <utils/sync.h>
#include <utils/utilstrencodings.h>
#include <extlibs/scope_guard.hpp>
#include <utils/utiltime.h>
#include <utils/random.h>
#include <chainparamsbase.h>
#include <clientversion.h>

#ifndef WIN32
// for posix_fallocate
#ifdef __linux__

#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif

#define _POSIX_C_SOURCE 200112L

#endif // __linux__

#include <algorithm>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>

#else

#ifdef _MSC_VER
#pragma warning(disable : 4786 4804 4805 4717)
#include <processthreadsapi.h>
#endif

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0601

#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0601

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#define popen _popen
#define pclose _pclose

#include <io.h> /* for _commit */
#include <shlobj.h>
#endif // WIN32

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <boost/program_options/detail/config_file.hpp>
#include <boost/program_options/parsers.hpp>

using namespace std;

m_strings mapArgs;
map<string, v_strings> mapMultiArgs;
bool fDebug = false;
bool fDaemon = false;
bool fServer = false;
string strMiscWarning;
CTranslationInterface translationInterface;

[[noreturn]] void new_handler_terminate()
{
    // Rather than throwing bad-alloc if allocation fails, terminate
    // immediately to (try to) avoid chain corruption.
    // Since LogPrintf may itself allocate memory, set the handler directly
    // to terminate first.
    set_new_handler(terminate);
    fputs("Error: Out of memory. Terminating.\n", stderr);
    LogPrintf("Error: Out of memory. Terminating.\n");

    // The log was successful, terminate now.
    terminate();
};

static void InterpretNegativeSetting(string name, map<string, string>& mapSettingsRet)
{
    // interpret -nofoo as -foo=0 (and -nofoo=0 as -foo=1) as long as -foo not set
    if (name.find("-no") == 0)
    {
        string positive("-");
        positive.append(name.begin()+3, name.end());
        if (mapSettingsRet.count(positive) == 0)
        {
            bool value = !GetBoolArg(name, false);
            mapSettingsRet[positive] = (value ? "1" : "0");
        }
    }
}

void ParseParameters(int argc, const char* const argv[])
{
    mapArgs.clear();
    mapMultiArgs.clear();

    for (int i = 1; i < argc; i++)
    {
        string str(argv[i]);
        string strValue;
        size_t is_index = str.find('=');
        if (is_index != string::npos)
        {
            strValue = str.substr(is_index+1);
            str = str.substr(0, is_index);
        }
#ifdef WIN32
        lowercase(str);
        if (str_starts_with(str, "/"))
            str = "-" + str.substr(1);
#endif

        if (str[0] != '-')
            break;

        // Interpret --foo as -foo.
        // If both --foo and -foo are set, the last takes effect.
        if (str.length() > 1 && str[1] == '-')
            str = str.substr(1);

        mapArgs[str] = strValue;
        mapMultiArgs[str].push_back(strValue);
    }

    // New 0.6 features:
    for (const auto &entry : mapArgs)
        // interpret -nofoo as -foo=0 (and -nofoo=0 as -foo=1) as long as -foo not set
        InterpretNegativeSetting(entry.first, mapArgs);
}

string GetArg(const string& strArg, const string& strDefault)
{
    if (mapArgs.count(strArg))
        return mapArgs[strArg];
    return strDefault;
}

int32_t GetIntArg(const string& strArg, const int32_t nDefaultValue)
{
    if (mapArgs.count(strArg))
        return atoi(mapArgs[strArg]);
    return nDefaultValue;
}


int64_t GetArg(const string& strArg, int64_t nDefault)
{
    if (mapArgs.count(strArg))
        return atoi64(mapArgs[strArg]);
    return nDefault;
}


bool GetBoolArg(const string& strArg, bool fDefault)
{
    if (mapArgs.count(strArg))
    {
        if (mapArgs[strArg].empty())
            return true;
        return (atoi(mapArgs[strArg]) != 0);
    }
    return fDefault;
}

bool IsParamDefined(const std::string& strArg) noexcept
{
    return mapArgs.count(strArg) > 0;
}

bool SoftSetArg(const string& strArg, const string& strValue)
{
    if (mapArgs.count(strArg))
        return false;
    mapArgs[strArg] = strValue;
    return true;
}

bool SoftSetBoolArg(const string& strArg, bool fValue)
{
    if (fValue)
        return SoftSetArg(strArg, string("1"));
    else
        return SoftSetArg(strArg, string("0"));
}

static const int screenWidth = 79;
static const int optIndent = 2;
static const int msgIndent = 7;

string HelpMessageGroup(const string &message)
{
    return string(message) + string("\n\n");
}

string HelpMessageOpt(const string &option, const string &message)
{
    return string(optIndent,' ') + string(option) +
           string("\n") + string(msgIndent,' ') +
           FormatParagraph(message, screenWidth - msgIndent, msgIndent) +
           string("\n\n");
}

#ifdef WIN32
string GetErrorString(const int err)
{
    char buf[256] = {};
    if (FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
            nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buf, sizeof(buf), nullptr))
        return strprintf("%s (%d)", buf, err);
    return strprintf("Unknown error (%d)", err);
}
#else
string GetErrorString(const int err)
{
    char buf[256];
    const char *s = buf;
    buf[0] = 0;
    /* Too bad there are two incompatible implementations of the
     * thread-safe strerror. */
#ifdef STRERROR_R_CHAR_P /* GNU variant can return a pointer outside the passed buffer */
    s = strerror_r(err, buf, sizeof(buf));
#else /* POSIX variant always returns message in buffer */
    if (strerror_r(err, buf, sizeof(buf)))
        buf[0] = 0;
#endif
    return strprintf("%s (%d)", s, err);
}
#endif

static string FormatException(const exception* pex, const char* pszThread)
{
#ifdef WIN32
    char pszModule[MAX_PATH] = "";
    GetModuleFileNameA(nullptr, pszModule, sizeof(pszModule));
#else
    const char* pszModule = "Pastel";
#endif
    if (pex)
        return strprintf(
            "EXCEPTION: %s       \n%s       \n%s in %s       \n", typeid(*pex).name(), pex->what(), pszModule, pszThread);
    else
        return strprintf(
            "UNKNOWN EXCEPTION       \n%s in %s       \n", pszModule, pszThread);
}

void PrintExceptionContinue(const exception* pex, const char* pszThread)
{
    string message = FormatException(pex, pszThread);
    LogPrintf("\n\n************************\n%s\n", message);
    fprintf(stderr, "\n\n************************\n%s\n", message.c_str());
    strMiscWarning = message;
}

fs::path GetDefaultDataDir()
{
    // Windows < Vista: C:\Documents and Settings\Username\Application Data\Pastel
    // Windows >= Vista: C:\Users\Username\AppData\Roaming\Pastel
    // Mac: ~/Library/Application Support/Pastel
    // Unix: ~/.pastel
#ifdef WIN32
    // Windows
    return GetSpecialFolderPath(CSIDL_APPDATA) / "Pastel";
#else
    fs::path pathRet;
    const char* pszHome = getenv("HOME");
    if (!pszHome || !*pszHome)
        pathRet = fs::path("/");
    else
        pathRet = fs::path(pszHome);
#ifdef MAC_OSX
    // Mac
    pathRet /= "Library/Application Support";
    TryCreateDirectory(pathRet);
    return pathRet / "Pastel";
#else
    // Unix
    return pathRet / ".pastel";
#endif
#endif
}

static fs::path pathCached;
static fs::path pathCachedNetSpecific;
static fs::path zc_paramsPathCached;
static CCriticalSection csPathCached;

static fs::path ZC_GetBaseParamsDir()
{
    // Copied from GetDefaultDataDir and adapter for pastel params.

    // Windows < Vista: C:\Documents and Settings\Username\Application Data\PastelParams
    // Windows >= Vista: C:\Users\Username\AppData\Roaming\PastelParams
    // Mac: ~/Library/Application Support/PastelParams
    // Unix: ~/.pastel-params
#ifdef WIN32
    // Windows
    return GetSpecialFolderPath(CSIDL_APPDATA) / "PastelParams";
#else
    fs::path pathRet;
    const char* pszHome = getenv("HOME");
    if (!pszHome || !*pszHome)
        pathRet = fs::path("/");
    else
        pathRet = fs::path(pszHome);
#ifdef MAC_OSX
    // Mac
    pathRet /= "Library/Application Support";
    TryCreateDirectory(pathRet);
    return pathRet / "PastelParams";
#else
    // Unix
    return pathRet / ".pastel-params";
#endif
#endif
}

const fs::path& ZC_GetParamsDir()
{
    LOCK(csPathCached); // Reuse the same lock as upstream.

    fs::path &path = zc_paramsPathCached;

    // This can be called during exceptions by LogPrintf(), so we cache the
    // value so we don't have to do memory allocations after that.
    if (!path.empty())
        return path;

    path = ZC_GetBaseParamsDir();

    return path;
}

// Return the user specified export directory.  Create directory if it doesn't exist.
// If user did not set option, return an empty path.
// If there is a filesystem problem, throw an exception.
const fs::path GetExportDir()
{
    fs::path path;
    if (mapArgs.count("-exportdir")) {
        path = fs::absolute(mapArgs["-exportdir"]);
        if (fs::exists(path) && !fs::is_directory(path)) {
            throw runtime_error(strprintf("The -exportdir '%s' already exists and is not a directory", path.string()));
        }
        if (!fs::exists(path) && !fs::create_directories(path)) {
            throw runtime_error(strprintf("Failed to create directory at -exportdir '%s'", path.string()));
        }
    }
    return path;
}


const fs::path& GetDataDir(bool fNetSpecific)
{
    LOCK(csPathCached);

    fs::path &path = fNetSpecific ? pathCachedNetSpecific : pathCached;

    // This can be called during exceptions by LogPrintf(), so we cache the
    // value so we don't have to do memory allocations after that.
    if (!path.empty())
        return path;

    if (mapArgs.count("-datadir"))
    {
        path = fs::absolute(mapArgs["-datadir"]);
        if (!fs::is_directory(path)) {
            path = "";
            return path;
        }
    } else
        path = GetDefaultDataDir();

    if (fNetSpecific)
        path /= BaseParams().DataDir();

    fs::create_directories(path);
    return path;
}

/**
 * Clear cached data dirs.
 */
void ClearDatadirCache()
{
    pathCached = fs::path();
    pathCachedNetSpecific = fs::path();
}

fs::path GetConfigFile()
{
    fs::path pathConfigFile(GetArg("-conf", "pastel.conf"));
    if (!pathConfigFile.is_absolute())
        pathConfigFile = GetDataDir(false) / pathConfigFile;

    return pathConfigFile;
}

void ReadConfigFile(m_strings& mapSettingsRet, map<string, v_strings>& mapMultiSettingsRet,
    const opt_string_t &sOptionFilter)
{
    fs::ifstream streamConfig(GetConfigFile());
    if (!streamConfig.good())
        throw missing_pastel_conf();

    s_strings setOptions;
    setOptions.insert("*");

    // Convert the wildcard pattern to regex pattern
    string regexPattern;
    if (sOptionFilter.has_value())
        regexPattern = regex_replace(sOptionFilter.value(), regex("\\*"), ".*");
    regex filterRegex(regexPattern);

    for (boost::program_options::detail::config_file_iterator it(streamConfig, setOptions), end; it != end; ++it)
    {
        // Don't overwrite existing settings so command line settings override pastel.conf
        string strKey = string("-") + it->string_key;

        // Process only if the key matches the regex filter
        if (!sOptionFilter.has_value() || regex_match(strKey, filterRegex))
        {
            if (mapSettingsRet.count(strKey) == 0)
            {
                mapSettingsRet[strKey] = it->value[0];
                // interpret nofoo=1 as foo=0 (and nofoo=0 as foo=1) as long as foo not set)
                InterpretNegativeSetting(strKey, mapSettingsRet);
            }
            mapMultiSettingsRet[strKey].push_back(it->value[0]);
        }
    }
    // If datadir is changed in .conf file:
    ClearDatadirCache();
}

#ifndef WIN32
fs::path GetPidFile()
{
    fs::path pathPidFile(GetArg("-pid", "pasteld.pid"));
    if (!pathPidFile.is_absolute()) 
        pathPidFile = GetDataDir() / pathPidFile;
    return pathPidFile;
}

void CreatePidFile(const fs::path& path, pid_t pid)
{
    FILE* file = fopen(path.string().c_str(), "w");
    if (file)
    {
        fprintf(file, "%d\n", pid);
        fclose(file);
    }
}
#endif

bool RenameOver(fs::path src, fs::path dest)
{
#ifdef WIN32
    return MoveFileExA(src.string().c_str(), dest.string().c_str(),
                       MOVEFILE_REPLACE_EXISTING) != 0;
#else
    int rc = rename(src.string().c_str(), dest.string().c_str());
    return (rc == 0);
#endif /* WIN32 */
}

/**
 * Ignores exceptions thrown by Boost's create_directory if the requested directory exists.
 * Specifically handles case where path p exists, but it wasn't possible for the user to
 * write to the parent directory.
 */
bool TryCreateDirectory(const fs::path& p)
{
    try
    {
        return fs::create_directory(p);
    } catch (const fs::filesystem_error&) {
        if (!fs::exists(p) || !fs::is_directory(p))
            throw;
    }

    // create_directory didn't create the directory, it had to have existed already
    return false;
}

void FileCommit(FILE *fileout)
{
    if (!fileout)
        return;
    fflush(fileout); // harmless if redundantly called
#ifdef WIN32
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(fileout));
    FlushFileBuffers(hFile);
#else
    #if defined(__linux__) || defined(__NetBSD__)
    fdatasync(fileno(fileout));
    #elif defined(__APPLE__) && defined(F_FULLFSYNC)
    fcntl(fileno(fileout), F_FULLFSYNC, 0);
    #else
    fsync(fileno(fileout));
    #endif
#endif
}

bool TruncateFile(FILE *file, unsigned int length) {
#if defined(WIN32)
    return _chsize(_fileno(file), length) == 0;
#else
    return ftruncate(fileno(file), length) == 0;
#endif
}

/**
 * this function tries to raise the file descriptor limit to the requested number.
 * It returns the actual file descriptor limit (which may be more or less than nFDSoftLimit)
 */
uint32_t RaiseFileDescriptorLimit(const uint32_t nFDSoftLimit)
{
#if defined(WIN32)
    return 2048;
#else
    rlim_t soft_limit = static_cast<rlim_t>(nFDSoftLimit);
    if (!soft_limit)
        soft_limit = DEFAULT_FD_SOFT_LIMIT;
    struct rlimit rl;
    memset(&rl, 0, sizeof(rl));
    if (getrlimit(RLIMIT_NOFILE, &rl) != -1)
    {
        if (soft_limit > rl.rlim_max)
			soft_limit = rl.rlim_max;
        if (soft_limit > rl.rlim_cur)
        {
            // try to set new fd soft limit 
            rl.rlim_cur = soft_limit;
            setrlimit(RLIMIT_NOFILE, &rl);
            getrlimit(RLIMIT_NOFILE, &rl);
        }
        return rl.rlim_cur;
    }
    return nFDSoftLimit; // getrlimit failed, assume it's fine
#endif
}

/**
 * this function tries to make a particular range of a file allocated (corresponding to disk space)
 * it is advisory, and the range specified in the arguments will never contain live data
 */
void AllocateFileRange(FILE *file, unsigned int offset, unsigned int length) {
#if defined(WIN32)
    // Windows-specific version
    HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(file));
    LARGE_INTEGER nFileSize;
    int64_t nEndPos = (int64_t)offset + length;
    nFileSize.u.LowPart = nEndPos & 0xFFFFFFFF;
    nFileSize.u.HighPart = nEndPos >> 32;
    SetFilePointerEx(hFile, nFileSize, 0, FILE_BEGIN);
    SetEndOfFile(hFile);
#elif defined(MAC_OSX)
    // OSX specific version
    fstore_t fst;
    fst.fst_flags = F_ALLOCATECONTIG;
    fst.fst_posmode = F_PEOFPOSMODE;
    fst.fst_offset = 0;
    fst.fst_length = (off_t)offset + length;
    fst.fst_bytesalloc = 0;
    if (fcntl(fileno(file), F_PREALLOCATE, &fst) == -1) {
        fst.fst_flags = F_ALLOCATEALL;
        fcntl(fileno(file), F_PREALLOCATE, &fst);
    }
    ftruncate(fileno(file), fst.fst_length);
#elif defined(__linux__)
    // Version using posix_fallocate
    off_t nEndPos = (off_t)offset + length;
    posix_fallocate(fileno(file), 0, nEndPos);
#else
    // Fallback version
    // TODO: just write one byte per block
    static const char buf[65536] = {};
    fseek(file, offset, SEEK_SET);
    while (length > 0) {
        unsigned int now = 65536;
        if (length < now)
            now = length;
        fwrite(buf, 1, now, file); // allowed to fail; this function is advisory anyway
        length -= now;
    }
#endif
}

#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate)
{
    char pszPath[MAX_PATH] = "";

    if(SHGetSpecialFolderPathA(nullptr, pszPath, nFolder, fCreate))
    {
        return fs::path(pszPath);
    }

    LogPrintf("SHGetSpecialFolderPathA() failed, could not obtain requested path.\n");
    return fs::path("");
}
#endif

fs::path GetTempPath()
{
    return fs::temp_directory_path();
}

void runCommand(const string& strCommand)
{
    int nErr = ::system(strCommand.c_str());
    if (nErr)
        LogPrintf("runCommand error: system(%s) returned %d\n", strCommand, nErr);
}

void RenameThread(const char* szThreadName, void* pThreadNativeHandle)
{
    if (!szThreadName || !*szThreadName)
        return;
#if defined(PR_SET_NAME)
    // Only the first 15 characters are used (16 - NUL terminator)
    ::prctl(PR_SET_NAME, szThreadName, 0, 0, 0);
#elif (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
    pthread_set_name_np(pthread_self(), szThreadName);

#elif defined(MAC_OSX)
    pthread_setname_np(szThreadName);
#elif defined(_MSC_VER)
    wstring wsName;
    const int nNameLen = static_cast<int>(strlen(szThreadName));
    int nRes = MultiByteToWideChar(CP_UTF8, 0, szThreadName, nNameLen, nullptr, 0);
    if (nRes > 0)
    {
        wsName.resize(nRes + 10);
        nRes = MultiByteToWideChar(CP_UTF8, 0, szThreadName, nNameLen, wsName.data(), static_cast<int>(wsName.size()));
        if (nRes > 0)
            SetThreadDescription(static_cast<HANDLE>(pThreadNativeHandle), wsName.c_str());
    }
#else
    // Prevent warnings for unused parameters...
    (void)szThreadName;
#endif
}

void SetupEnvironment()
{
    // On most POSIX systems (e.g. Linux, but not BSD) the environment's locale
    // may be invalid, in which case the "C" locale is used as fallback.
#if !defined(WIN32) && !defined(MAC_OSX) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
    try {
        locale(""); // Raises a runtime error if current locale is invalid
    } catch (const runtime_error&) {
        setenv("LC_ALL", "C", 1);
    }
#endif
    // The path locale is lazy initialized and to avoid deinitialization errors
    // in multithreading environments, it is set explicitly by the main thread.
    // A dummy locale is used to extract the internal default locale, used by
    // fs::path, which is then used to explicitly imbue the path.
    locale loc = fs::path::imbue(locale::classic());
    fs::path::imbue(loc);
}

bool SetupNetworking()
{
#ifdef WIN32
    // Initialize Windows Sockets
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (ret != NO_ERROR || LOBYTE(wsadata.wVersion ) != 2 || HIBYTE(wsadata.wVersion) != 2)
        return false;
#endif
    return true;
}

void SetThreadPriority(int nPriority)
{
#ifdef WIN32
    SetThreadPriority(GetCurrentThread(), nPriority);
#else // WIN32
#ifdef PRIO_THREAD
    setpriority(PRIO_THREAD, 0, nPriority);
#else // PRIO_THREAD
    setpriority(PRIO_PROCESS, 0, nPriority);
#endif // PRIO_THREAD
#endif // WIN32
}

string PrivacyInfo()
{
    return "\n" +
           FormatParagraph(strprintf(translate("In order to ensure you are adequately protecting your privacy when using Pastel, please see <%s>."),
                                     "")) + "\n";
}

string LicenseInfo()
{
    return "\n" +
           FormatParagraph(translate("Copyright (C) 2009-2014 The Bitcoin Core Developers") + "\n" +
           FormatParagraph(translate("Copyright (C) 2015-2017 The Zcash Developers")) + "\n" +
           FormatParagraph(strprintf(translate("Copyright (C) 2018-%i The Pastel Developers"), COPYRIGHT_YEAR)) + "\n" +
           "\n" +
           FormatParagraph(translate("This is experimental software.")) + "\n" +
           "\n" +
           FormatParagraph(translate("Distributed under the MIT software license, see the accompanying file COPYING or <http://www.opensource.org/licenses/mit-license.php>.")) + "\n" +
           "\n" +
           FormatParagraph(translate("This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit <https://www.openssl.org/> and cryptographic software written by Eric Young.")) +
           "\n");
}

/**
 * Get number of physical CPU cores available on the current system.
 * This does not count virtual cores.
 * 
 * \return number of CPU cores
 */
unsigned int GetNumCores()
{
    unsigned int nNumCores = thread::hardware_concurrency();
    if (nNumCores == 0)
    {
#ifdef __linux__
        ifstream cpuinfo("/proc/cpuinfo");
        if (!cpuinfo)
            return 0;
        string line;
        while (getline(cpuinfo, line))
        {
            if (line.find("processor") != string::npos)
                ++nNumCores;
        }
#elif defined(__APPLE__)
        int nm[2];
        size_t len = 4;

        nm[0] = CTL_HW;
        nm[1] = HW_AVAILCPU;
        sysctl(nm, 2, &nNumCores, &len, nullptr, 0);

        if (nNumCores < 1)
        {
            nm[1] = HW_NCPU;
            sysctl(nm, 2, &nNumCores, &len, nullptr, 0);
            if (nNumCores < 1)
                nNumCores = 1;
        }
#elif defined(WIN32)
		SYSTEM_INFO sysinfo;
		GetSystemInfo(&sysinfo);
		nNumCores = sysinfo.dwNumberOfProcessors;
#endif
    }
    return nNumCores;
}

/**
 * Get total physical memory (RAM) in bytes.
 * 
 * \return total available physical memory in bytes
 */
uint64_t GetTotalPhysicalMemory()
{
    uint64_t nTotalMemory = 0;
#ifdef __linux__
    struct sysinfo info;
    if (sysinfo(&info) == 0)
		nTotalMemory = static_cast<uint64_t>(info.totalram) * info.mem_unit;
#elif defined(__APPLE__)
    int mib[2];
    size_t len = sizeof(nTotalMemory);
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE; // Total physical memory in bytes

    sysctl(mib, 2, &nTotalMemory, &len, nullptr, 0);
#elif defined (WIN32)
    MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	if (GlobalMemoryStatusEx(&status))
		nTotalMemory = status.ullTotalPhys;
#endif
    return nTotalMemory;
}

InsecureRand::InsecureRand(bool _fDeterministic) :
    nRz(11),
    nRw(11),
    fDeterministic(_fDeterministic)
{
    // The seed values have some unlikely fixed points which we avoid.
    if (fDeterministic)
        return;
    uint32_t nTmp {};
    do {
        GetRandBytes((unsigned char*)&nTmp, 4);
    } while (nTmp == 0 || nTmp == 0x9068ffffU);
    nRz = nTmp;
    do {
        GetRandBytes((unsigned char*)&nTmp, 4);
    } while (nTmp == 0 || nTmp == 0x464fffffU);
    nRw = nTmp;
}

int exec_system_command(const char* szCommand, string& stdOutput, string& stdError)
{
    if (!szCommand || !*szCommand)
    {
        stdError = "Empty command!";
        return -1;
    }
    stdOutput.clear();
    stdError.clear();
    
    // Command to get both stdout and stderr
    string sFullCommand = string(szCommand);
    if (sFullCommand.find("2>&1") == string::npos)
        sFullCommand += " 2>&1";

    array<char, 128> buffer;
    FILE* pipe = popen(sFullCommand.c_str(), "r");
    if (!pipe)
    {
        stdError = "popen() failed!";
        return -1;
    }
    auto pipe_guard = sg::make_scope_guard([&]() noexcept
    {
        if (pipe)
			pclose(pipe);
    });

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        stdOutput += buffer.data();
    }

    int nExitStatus = pclose(pipe);
    pipe_guard.dismiss();
    if (nExitStatus != 0)
        stdError = stdOutput;  // In this case, the output is considered as an error.

    return nExitStatus;
}

