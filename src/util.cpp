// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "config/bitcoin-config.h"
#endif

#include <fstream>
#include <mutex>
#include <stdarg.h>
#include <stdio.h>
#include <thread>
#include <unistd.h>
#include <errno.h>

#if (defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__))
#include <pthread.h>
#include <pthread_np.h>
#endif

#include <util.h>
#include <chainparamsbase.h>
#include <random.h>
#include <serialize.h>
#include <sync.h>
#include <utilstrencodings.h>
#include <utiltime.h>
#include <clientversion.h>
#include <str_utils.h>
#include <map_types.h>


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

#include <io.h> /* for _commit */
#include <shlobj.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

#include <boost/program_options/detail/config_file.hpp>
#include <boost/program_options/parsers.hpp>

using namespace std;

m_strings mapArgs;
map<string, v_strings> mapMultiArgs;
bool fDebug = false;
/**
 * Print to console modes:
 * 0 - do not print anything to console
 * 1 - print only to console
 * 2 - print to console and debug.log
 */
atomic_uint32_t gl_nPrintToConsoleMode = 0;
bool fPrintToDebugLog = true;
bool fDaemon = false;
bool fServer = false;
string strMiscWarning;
bool fLogTimestamps = DEFAULT_LOGTIMESTAMPS;
bool fLogTimeMicros = DEFAULT_LOGTIMEMICROS;
bool fLogIPs = DEFAULT_LOGIPS;
atomic<bool> fReopenDebugLog(false);
CTranslationInterface translationInterface;

/**
 * LogPrintf() has been broken a couple of times now
 * by well-meaning people adding mutexes in the most straightforward way.
 * It breaks because it may be called by global destructors during shutdown.
 * Since the order of destruction of static/global objects is undefined,
 * defining a mutex as a global object doesn't work (the mutex gets
 * destroyed, and then some later destructor calls OutputDebugStringF,
 * maybe indirectly, and you get a core dump at shutdown trying to lock
 * the mutex).
 */

static once_flag debugPrintInitFlag;

/**
 * We use call_once() to make sure mutexDebugLog and
 * vMsgsBeforeOpenLog are initialized in a thread-safe manner.
 *
 * NOTE: fileout, mutexDebugLog and sometimes vMsgsBeforeOpenLog
 * are leaked on exit. This is ugly, but will be cleaned up by
 * the OS/libc. When the shutdown sequence is fully audited and
 * tested, explicit destruction of these objects can be implemented.
 */
static FILE* fileout = nullptr;
static mutex* mutexDebugLog = nullptr;
static list<string> *vMsgsBeforeOpenLog;

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

/**
 * Set print-to-console mode gl_nPrintToConsoleMode.
 * Supported modes:
 *   0 - do not print anything to console
 *   1 - print only to console
 *   2 - print to console and debug.log
 * 
 * \param error - error message if the function fails
 * \return true if the function succeeds, false otherwise
 */
bool SetPrintToConsoleMode(string &error)
{
     string sPrintToConsoleMode = GetArg("-printtoconsole", "0");
     string sConversionErrorMsg;
     bool bConversionError = false;
     try
     {
         const int nPrintToConsoleMode = stoi(sPrintToConsoleMode);

         // Check if the mode is valid
         if (nPrintToConsoleMode < 0 || nPrintToConsoleMode > 2)
		 {
			 error = translate(strprintf("-printtoconsole option value [%s] is invalid. Supported values are: 0, 1, or 2.",
                 				 sPrintToConsoleMode).c_str());
			 return false;
		 }
         // Set the mode
		 gl_nPrintToConsoleMode = nPrintToConsoleMode;

     } catch (const invalid_argument &e1)
	 {
         sConversionErrorMsg = SAFE_SZ(e1.what());
         bConversionError = true;
     } catch (const out_of_range& e2)
     {
         sConversionErrorMsg = SAFE_SZ(e2.what());
         bConversionError = true;
     }
     if (bConversionError)
     {
         error = translate(strprintf("-printtoconsole option value [%s] is invalid - %s. Supported values are: 0, 1, or 2.",
             sPrintToConsoleMode, sConversionErrorMsg).c_str());
         return false;
     }
     return true;
}

bool IsPrintToConsole() noexcept
{
    return gl_nPrintToConsoleMode > 0;
}

static size_t FileWriteStr(const string &str, FILE *fp)
{
    return fwrite(str.data(), 1, str.size(), fp);
}

static void DebugPrintInit()
{
    assert(mutexDebugLog == nullptr);
    mutexDebugLog = new mutex();
    vMsgsBeforeOpenLog = new list<string>;
}

void OpenDebugLog()
{
    call_once(debugPrintInitFlag, &DebugPrintInit);
    scoped_lock scoped_lock(*mutexDebugLog);

    assert(fileout == nullptr);
    assert(vMsgsBeforeOpenLog);
    const fs::path pathDebugLog = GetDataDir() / "debug.log";
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    fileout = _fsopen(pathDebugLog.string().c_str(), "a+", _SH_DENYWR);
#else
    fileout = fopen(pathDebugLog.string().c_str(), "a");
#endif
    const int err = fileout ? 0 : errno;
    if (!fileout)
    {
        printf("ERROR: failed to open debug log file [%s]. %s", pathDebugLog.string().c_str(), GetErrorString(err).c_str());
        return;
    }
    setvbuf(fileout, nullptr, _IONBF, 0); // unbuffered

    // dump buffered messages from before we opened the log
    while (!vMsgsBeforeOpenLog->empty())
    {
        FileWriteStr(vMsgsBeforeOpenLog->front(), fileout);
        vMsgsBeforeOpenLog->pop_front();
    }

    delete vMsgsBeforeOpenLog;
    vMsgsBeforeOpenLog = nullptr;
}

bool LogAcceptCategory(const char* category)
{
    if (category)
    {
        if (!fDebug)
            return false;

        // Give each thread quick access to -debug settings.
        // This helps prevent issues debugging global destructors,
        // where mapMultiArgs might be deleted before another
        // global destructor calls LogPrint()
        static thread_local unique_ptr<set<string>> ptrCategory;
        if (!ptrCategory)
        {
            const auto &vCategories = mapMultiArgs["-debug"];
            // preprocess debug log categories
            // support multiple categories separated by comma
            set<string> setCategories;
            for (const auto& sCategory : vCategories)
            {
                if (sCategory.find(',') != string::npos)
                {
                    v_strings v;
                    str_split(v, sCategory, ',');
                    setCategories.insert(v.cbegin(), v.cend());
                }
                else
                    setCategories.insert(sCategory);
            }
            ptrCategory = make_unique<set<string>>(move(setCategories));
            // thread_specific_ptr automatically deletes the set when the thread ends.
        }
        const auto& setCategories = *ptrCategory.get();

        // if not debugging everything and not debugging specific category, LogPrint does nothing.
        if (setCategories.count(string("")) == 0 &&
            setCategories.count(string("1")) == 0 &&
            setCategories.count(string(category)) == 0)
            return false;
    }
    return true;
}

#ifdef __linux__
thread_local pid_t gl_LinuxTID = gettid();
#endif // __linux__

/**
* Get thread id in decimal format.
* 
* \return thread id
*/
string get_tid() noexcept
{
#ifdef __linux__
    auto tid = gl_LinuxTID;
#else
    auto tid = this_thread::get_id();
#endif
    ostringstream s;
    s << tid;
    return s.str();
}

/**
* Get thread id in hex format.
* 
* \return thread id
*/
inline string get_tid_hex() noexcept
{
#ifdef __linux__
    auto tid = gl_LinuxTID;
#else
    auto tid = this_thread::get_id();
#endif
    ostringstream s;
    s << hex << uppercase << tid;
    return s.str();
}

/**
 * fStartedNewLine is a state variable held by the calling context that will
 * suppress printing of the timestamp when multiple calls are made that don't
 * end in a newline. Initialize it to true, and hold it, in the calling context.
 */
static string LogTimestampStr(const string &str, bool *fStartedNewLine)
{
    string strStamped;
    strStamped.reserve(30 + str.size());

    if (!fLogTimestamps)
        return str;

    strStamped = get_tid_hex() + " - ";
    if (*fStartedNewLine)
        strStamped += DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()) + ' ' + str;
    else
        strStamped += str;

    if (!str.empty() && str[str.size()-1] == '\n')
        *fStartedNewLine = true;
    else
        *fStartedNewLine = false;

    return strStamped;
}

size_t LogPrintStr(const string &str)
{
    size_t nCharsWritten = 0; // Returns total number of characters written
    static bool fStartedNewLine = true;
    const uint32_t nPrintToConsoleMode = gl_nPrintToConsoleMode;
    if (nPrintToConsoleMode > 0)
    {
        // print to console
        nCharsWritten = fwrite(str.data(), 1, str.size(), stdout);
        fflush(stdout);
    }
    if (fPrintToDebugLog && (nPrintToConsoleMode != 1))
    {
        call_once(debugPrintInitFlag, &DebugPrintInit);
        scoped_lock scoped_lock(*mutexDebugLog);

        string strTimestamped = LogTimestampStr(str, &fStartedNewLine);

        // buffer if we haven't opened the log yet
        if (!fileout)
        {
            assert(vMsgsBeforeOpenLog);
            nCharsWritten = strTimestamped.length();
            vMsgsBeforeOpenLog->push_back(strTimestamped);
        }
        else
        {
            // reopen the log file, if requested
            if (fReopenDebugLog)
            {
                fReopenDebugLog = false;
                fs::path pathDebug = GetDataDir() / "debug.log";
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
                const errno_t err = freopen_s(&fileout, pathDebug.string().c_str(), "a", fileout);
#else
                fileout = freopen(pathDebug.string().c_str(), "a", fileout);
#endif
                if (fileout)
                    setvbuf(fileout, nullptr, _IONBF, 0); // unbuffered
            }

            nCharsWritten = FileWriteStr(strTimestamped, fileout);
        }
    }
    return nCharsWritten;
}

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

void ReadConfigFile(map<string, string>& mapSettingsRet,
                    map<string, vector<string> >& mapMultiSettingsRet)
{
    fs::ifstream streamConfig(GetConfigFile());
    if (!streamConfig.good())
        throw missing_pastel_conf();

    set<string> setOptions;
    setOptions.insert("*");

    for (boost::program_options::detail::config_file_iterator it(streamConfig, setOptions), end; it != end; ++it)
    {
        // Don't overwrite existing settings so command line settings override pastel.conf
        string strKey = string("-") + it->string_key;
        if (mapSettingsRet.count(strKey) == 0)
        {
            mapSettingsRet[strKey] = it->value[0];
            // interpret nofoo=1 as foo=0 (and nofoo=0 as foo=1) as long as foo not set)
            InterpretNegativeSetting(strKey, mapSettingsRet);
        }
        mapMultiSettingsRet[strKey].push_back(it->value[0]);
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

void LogFlush()
{
    FileCommit(fileout);
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
 * It returns the actual file descriptor limit (which may be more or less than nMinFD)
 */
size_t RaiseFileDescriptorLimit(const size_t nMinFD)
{
#if defined(WIN32)
    return 2048;
#else
    struct rlimit limitFD;
    if (getrlimit(RLIMIT_NOFILE, &limitFD) != -1)
    {
        if (limitFD.rlim_cur < (rlim_t)nMinFD) {
            limitFD.rlim_cur = nMinFD;
            if (limitFD.rlim_cur > limitFD.rlim_max)
                limitFD.rlim_cur = limitFD.rlim_max;
            setrlimit(RLIMIT_NOFILE, &limitFD);
            getrlimit(RLIMIT_NOFILE, &limitFD);
        }
        return limitFD.rlim_cur;
    }
    return nMinFD; // getrlimit failed, assume it's fine
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

void ShrinkDebugFile()
{
    // Scroll debug.log if it's getting too big
    fs::path pathLog = GetDataDir() / "debug.log";

    FILE* file = nullptr;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    errno_t err = fopen_s(&file, pathLog.string().c_str(), "r");
#else
    file = fopen(pathLog.string().c_str(), "r");
#endif
    if (file && fs::file_size(pathLog) > 10 * 1000000)
    {
        // Restart the file with some of the end
        vector <char> vch(200000,0);
        fseek(file, -((long)vch.size()), SEEK_END);
        size_t nBytes = fread(begin_ptr(vch), 1, vch.size(), file);
        fclose(file);

#if defined(_MSC_VER) && (_MSC_VER >= 1400)
        err = fopen_s(&file, pathLog.string().c_str(), "w");
#else
        file = fopen(pathLog.string().c_str(), "w");
#endif
        if (file)
        {
            fwrite(begin_ptr(vch), 1, nBytes, file);
            fclose(file);
        }
    }
    else if (file)
        fclose(file);
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
           FormatParagraph(strprintf(translate("Copyright (C) 2009-%i The Bitcoin Core Developers"), COPYRIGHT_YEAR)) + "\n" +
           FormatParagraph(strprintf(translate("Copyright (C) 2015-%i The Zcash Developers"), COPYRIGHT_YEAR)) + "\n" +
           FormatParagraph(strprintf(translate("Copyright (C) 2018-%i The Pastel Developers"), COPYRIGHT_YEAR)) + "\n" +
           "\n" +
           FormatParagraph(translate("This is experimental software.")) + "\n" +
           "\n" +
           FormatParagraph(translate("Distributed under the MIT software license, see the accompanying file COPYING or <http://www.opensource.org/licenses/mit-license.php>.")) + "\n" +
           "\n" +
           FormatParagraph(translate("This product includes software developed by the OpenSSL Project for use in the OpenSSL Toolkit <https://www.openssl.org/> and cryptographic software written by Eric Young.")) +
           "\n";
}

int GetNumCores()
{
    return thread::hardware_concurrency();
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