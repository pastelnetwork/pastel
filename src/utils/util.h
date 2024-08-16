#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * logging, thread wrappers
 */
#if defined(HAVE_CONFIG_H)
#include <config/pastel-config.h>
#endif

#include <atomic>
#include <exception>
#include <map>
#include <optional>
#include <thread>

#include <boost/signals2.hpp>

#include <compat.h>
#include <utils/fs.h>
#include <utils/map_types.h>
#include <utils/str_types.h>
#include <utils/vector_types.h>
#include <utils/tinyformat.h>
#include <utils/utiltime.h>
#include <utils/logmanager.h>

/** Default file descriptor soft limit. */
constexpr uint32_t DEFAULT_FD_SOFT_LIMIT = 2048;

/** Signals for translation. */
class CTranslationInterface
{
public:
    /** Translate a message to the native language of the user. */
    boost::signals2::signal<std::string (const char* psz)> Translate;
};

extern std::map<std::string, std::string> mapArgs;
extern std::map<std::string, std::vector<std::string> > mapMultiArgs;
extern bool fDebug;
extern bool fServer;
extern std::string strMiscWarning;
extern CTranslationInterface translationInterface;

[[noreturn]] extern void new_handler_terminate();

/**
 * Translation function: Call Translate signal on UI interface, which returns a std::optional result.
 * If no translation slot is registered, nothing is returned, and simply return the input.
 */
inline std::string translate(const char* psz)
{
    auto rv = translationInterface.Translate(psz);
    return rv ? (*rv) : psz;
}

void SetupEnvironment();
bool SetupNetworking();

/**
* Extract method name from __PRETTY_FUNCTION__.
* Sample input: 
*      int  a::sub (int)
* 
* \param s - __PRETTY_FUNCTION__
* \return extracted method name (class_name::method_name)
*/
constexpr std::string_view method_name(const char* s)
{
    std::string_view prettyFunction(s);
    // trim function parameters
    const size_t bracket = prettyFunction.rfind("(");
    // find the start of the method name
    const size_t space = prettyFunction.rfind(" ", bracket) + 1;
    return prettyFunction.substr(space, bracket - space);
}

#if defined(__GNUC__) || defined(__GNUG__) || defined(__CLANG__)
// __PRETTY_FUNCTION__ is defined in gcc and clang
#define __METHOD_NAME__ std::string(method_name(__PRETTY_FUNCTION__)).c_str()
#elif defined(_MSC_VER)
#define __METHOD_NAME__ __FUNCTION__
#else
#define __METHOD_NAME__ __func__
#endif

template <typename... Args>
static inline void LogPrintf(const char* fmt, const Args&... args)
{
    std::string log_msg;
    try
    {
        log_msg = tfm::format(fmt, args...);
    }
    catch (const tfm::format_error &e)
    {
        /* Original format string will have newline so don't add one here */
        log_msg = "Error \"" + std::string(e.what()) + "\" while formatting log message: " + fmt;
    }
    if (gl_LogMgr)
        gl_LogMgr->LogPrintStr(log_msg);
}

#define VA_ARGS(...) , ##__VA_ARGS__

#define LogFnPrintf(fmt, ...) \
    LogPrintf(tfm::format("[%s] %s\n", __METHOD_NAME__, fmt).c_str(), ##__VA_ARGS__)

#define LogPrint(category, ...) do {        \
    if (LogAcceptCategory((category))) {    \
        LogPrintf(__VA_ARGS__);             \
    }                                       \
} while(false)

#define LogFnPrint(category, fmt, ...) do { \
    if (LogAcceptCategory((category))) {    \
        LogFnPrintf(fmt, ##__VA_ARGS__);      \
    }                                       \
} while(false)

template<typename... Args>
bool error(const char* fmt, const Args&... args)
{
    if (gl_LogMgr)
        gl_LogMgr->LogPrintStr("ERROR: " + tfm::format(fmt, args...) + "\n");
    return false;
}

template<typename... Args>
bool errorFn(const char *szMethodName, const char* fmt, const Args&... args)
{
    if (!szMethodName || !*szMethodName)
        szMethodName = __METHOD_NAME__;
    if (gl_LogMgr)
        gl_LogMgr->LogPrintStr(tfm::format("[%s] ", szMethodName) + "ERROR: " + tfm::format(fmt, args...) + "\n");
    return false;
}

template <typename... Args>
bool warning_msg(const char* fmt, const Args&... args)
{
    if (gl_LogMgr)
        gl_LogMgr->LogPrintStr("WARNING: " + tfm::format(fmt, args...) + "\n");
    return false;
}

template <typename... Args>
bool warning_msgFn(const char *szMethodName, const char* fmt, const Args&... args)
{
    if (!szMethodName || !*szMethodName)
        szMethodName = __METHOD_NAME__;
    if (gl_LogMgr)
        gl_LogMgr->LogPrintStr(tfm::format("[%s] ", szMethodName) + "WARNING: " + tfm::format(fmt, args...) + "\n");
    return false;
}


const fs::path& ZC_GetParamsDir();

/** Return readable error string for a system error code */
std::string GetErrorString(const int err);

void PrintExceptionContinue(const std::exception *pex, const char* pszThread);
void ParseParameters(int argc, const char*const argv[]);
void FileCommit(FILE *fileout);
bool TruncateFile(FILE *file, unsigned int length);
uint32_t RaiseFileDescriptorLimit(const uint32_t nMinFD);
void AllocateFileRange(FILE *file, unsigned int offset, unsigned int length);
bool RenameOver(fs::path src, fs::path dest);
bool TryCreateDirectory(const fs::path& p);
fs::path GetDefaultDataDir();
const fs::path& GetDataDir(bool fNetSpecific = true);
void ClearDatadirCache();
fs::path GetConfigFile();
#ifndef WIN32
fs::path GetPidFile();
void CreatePidFile(const fs::path& path, pid_t pid);
#endif
class missing_pastel_conf : public std::runtime_error {
public:
    missing_pastel_conf() : std::runtime_error("Missing pastel.conf") { }
};
void ReadConfigFile(m_strings& mapSettingsRet, std::map<std::string, v_strings>& mapMultiSettingsRet,
    const opt_string_t &sOptionFilter = std::nullopt);
#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif
fs::path GetTempPath();
void runCommand(const std::string& strCommand);
const fs::path GetExportDir();

/** Returns privacy notice (for -version, -help and metrics screen) */
std::string PrivacyInfo();

/** Returns licensing information (for -version) */
std::string LicenseInfo();

inline bool IsSwitchChar(char c)
{
#ifdef WIN32
    return c == '-' || c == '/';
#else
    return c == '-';
#endif
}

/**
 * Return string argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. "1")
 * @return command-line argument or default value
 */
std::string GetArg(const std::string& strArg, const std::string& strDefault);

/**
 * Return integer argument (int64_t) or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. 1)
 * @return command-line argument (0 if invalid number) or default value
 */
int64_t GetArg(const std::string& strArg, int64_t nDefault);

/**
 * Return integer argument (int32_t) or default value
 * *
 * @param strArg Argument to get (e.g. "-foo")
 * * @param default (e.g. 1)
 * * @return command-line argument (0 if invalid number) or default value
 */
int32_t GetIntArg(const std::string& strArg, const int32_t nDefaultValue);

/**
 * Return boolean argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (true or false)
 * @return command-line argument or default value
 */
bool GetBoolArg(const std::string& strArg, bool fDefault);

/**
 * Check if the given parameter is defined in config file
 *
 * \param strArg Argument to check (e.g. "-foo")
 * \return true if the parameter is defined in config file
 */
bool IsParamDefined(const std::string& strArg) noexcept;

/**
 * Set an argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param strValue Value (e.g. "1")
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetArg(const std::string& strArg, const std::string& strValue);

/**
 * Set a boolean argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param fValue Value (e.g. false)
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetBoolArg(const std::string& strArg, bool fValue);

/**
 * Format a string to be used as group of options in help messages
 *
 * @param message Group name (e.g. "RPC server options:")
 * @return the formatted string
 */
std::string HelpMessageGroup(const std::string& message);

/**
 * Format a string to be used as option description in help messages
 *
 * @param option Option message (e.g. "-rpcuser=<user>")
 * @param message Option description (e.g. "Username for JSON-RPC connections")
 * @return the formatted string
 */
std::string HelpMessageOpt(const std::string& option, const std::string& message);

/**
 * Return the number of physical cores available on the current system.
 * @note This does not count virtual cores
 */
unsigned int GetNumCores();
// get total available physical memory (RAM) in bytes
uint64_t GetTotalPhysicalMemory();

void SetThreadPriority(int nPriority);
// rename thread
void RenameThread(const char* szThreadName, void *pThreadNativeHandle = nullptr);

class InsecureRand
{
private:
    uint32_t nRz;
    uint32_t nRw;
    bool fDeterministic;

public:
    explicit InsecureRand(bool _fDeterministic = false);

   /**
    * MWC RNG of George Marsaglia
    * This is intended to be fast. It has a period of 2^59.3, though the
    * least significant 16 bits only have a period of about 2^30.1.
    *
    * @return random value < nMax
    */
    int64_t operator()(int64_t nMax)
    {
        nRz = 36969 * (nRz & 65535) + (nRz >> 16);
        nRw = 18000 * (nRw & 65535) + (nRw >> 16);
        return ((static_cast<int64_t>(nRw) << 16) + nRz) % nMax;
    }
};

template <typename _T>
inline void safe_delete_obj(_T*& obj)
{
    if (obj)
    {
        delete obj;
        obj = nullptr;
    }
}

int exec_system_command(const char* szCommand, std::string &stdOutput, std::string &stdError);
