// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mutex>
#include <list>
#include <chrono>
#include <thread>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>

#include <utils/logmanager.h>
#include <utils/util.h>
#include <utils/str_utils.h>
#include <utils/serialize.h>

using namespace std;

constexpr bool DEFAULT_LOGIPS        = false;
constexpr bool DEFAULT_LOGTIMESTAMPS = true;
constexpr int DEFAULT_OLD_LOGS_CLEANUP_DAYS = 14;
constexpr uintmax_t DEFAULT_MAX_LOG_SIZE  = 10 * 1024 * 1024; // 10 MB
constexpr auto OLD_LOGS_SUBFOLDER = "old_logs";

bool fLogTimestamps = DEFAULT_LOGTIMESTAMPS;
bool fLogIPs = DEFAULT_LOGIPS;

unique_ptr<CLogManager> gl_LogMgr;

CLogManager::CLogManager() noexcept
{
    m_pvStartupLogs = new list<string>;
}

/**
 * Set print-to-console mode.
 * Supported modes:
 *   0 - do not print anything to console (default)
 *   1 - print only to console
 *   2 - print to console and debug.log
 * 
 * \param error - error message if the function fails
 * \return true if the function succeeds, false otherwise
 */
bool CLogManager::SetPrintToConsoleMode(string &error)
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
		 m_nPrintToConsoleMode = nPrintToConsoleMode;

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

static size_t FileWriteStr(const string &str, FILE *fp)
{
    return fwrite(str.data(), 1, str.size(), fp);
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
            ptrCategory = make_unique<set<string>>(std::move(setCategories));
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

size_t CLogManager::LogPrintStr(const string &str)
{
    size_t nCharsWritten = 0; // Returns total number of characters written
    static bool fStartedNewLine = true;
    const uint32_t nPrintToConsoleMode = m_nPrintToConsoleMode;
    if (nPrintToConsoleMode > 0)
    {
        // print to console
        nCharsWritten = fwrite(str.data(), 1, str.size(), stdout);
        fflush(stdout);
    }
    if (m_fPrintToDebugLog && (nPrintToConsoleMode != 1) && gl_LogMgr)
    {
        scoped_lock scoped_lock(m_mutexDebugLog);

        string strTimestamped = LogTimestampStr(str, &fStartedNewLine);

        // buffer if we haven't opened the log yet
        if (!m_LogFileHandle)
        {
            assert(m_pvStartupLogs);
            nCharsWritten = strTimestamped.length();
            m_pvStartupLogs->push_back(strTimestamped);
        }
        else
        {
            // reopen the log file, if requested
            if (m_fReopenDebugLog)
            {
                m_fReopenDebugLog = false;
#if defined(_MSC_VER) && (_MSC_VER >= 1400)
                const errno_t err = freopen_s(&m_LogFileHandle, m_DebugLogFilePath.string().c_str(), "a", m_LogFileHandle);
#else
                m_LogFileHandle = freopen(m_DebugLogFilePath.string().c_str(), "a", m_LogFileHandle);
#endif
                if (m_LogFileHandle)
                    setvbuf(m_LogFileHandle, nullptr, _IONBF, 0); // unbuffered
            }

            nCharsWritten = FileWriteStr(strTimestamped, m_LogFileHandle);
        }
    }
    return nCharsWritten;
}

void CLogManager::LogFlush()
{
    scoped_lock scoped_lock(m_mutexDebugLog);
    FileCommit(m_LogFileHandle);
}   

void CLogManager::OpenDebugLogFile()
{
    if (!m_fPrintToDebugLog)
        return;

    assert(m_LogFileHandle == nullptr);
    assert(m_pvStartupLogs);
    if (m_DebugLogFilePath.empty())
        m_DebugLogFilePath = GetDataDir() / "debug.log";

#if defined(_MSC_VER) && (_MSC_VER >= 1400)
    m_LogFileHandle = _fsopen(m_DebugLogFilePath.string().c_str(), "a+", _SH_DENYWR);
#else
    m_LogFileHandle = fopen(m_DebugLogFilePath.string().c_str(), "a");
#endif
    const int err = m_LogFileHandle ? 0 : errno;
    if (!m_LogFileHandle)
    {
        printf("ERROR: failed to open debug log file [%s]. %s", m_DebugLogFilePath.string().c_str(), GetErrorString(err).c_str());
        return;
    }
    setvbuf(m_LogFileHandle, nullptr, _IONBF, 0); // unbuffered

    // dump buffered messages from before we opened the log
    while (!m_pvStartupLogs->empty())
    {
        FileWriteStr(m_pvStartupLogs->front(), m_LogFileHandle);
        m_pvStartupLogs->pop_front();
    }

    delete m_pvStartupLogs;
    m_pvStartupLogs = nullptr;
}

bool CLogManager::RotateDebugLogFile()
{
    if (m_OldDebugLogDirPath.empty())
        m_OldDebugLogDirPath = m_DebugLogFilePath.parent_path() / OLD_LOGS_SUBFOLDER;
    fss::error_code ec;
    if (!fs::is_directory(m_OldDebugLogDirPath) && !fs::create_directory(m_OldDebugLogDirPath, ec))
	{
		LogPrintf("ERROR: failed to create directory [%s] for old logs. %s\n", 
            m_OldDebugLogDirPath.string(), ec.message());
		return false;
	}

    auto now = chrono::system_clock::now();
    auto now_time_t = chrono::system_clock::to_time_t(now);
    stringstream ss;
    
    struct tm tm;
#ifdef _MSC_VER
    const errno_t err = localtime_s(&tm, &now_time_t);
    if (err)
		return false;
#else
    auto ptm = localtime_r(&now_time_t, &tm);
    if (!ptm)
        return false;
#endif
    ss << put_time(&tm, "%Y%m%d_%H%M%S");

    fs::path newLogFilePath = m_OldDebugLogDirPath / ("debug." + ss.str() + ".log");
    // close the current log file and enable startup logs buffering
    CloseDebugLogFile();
    fs::rename(m_DebugLogFilePath, newLogFilePath, ec);
    if (ec.failed())
	{
		LogPrintf("ERROR: failed to rotate debug log file [%s] to [%s]. %s\n",
			m_DebugLogFilePath.string(), newLogFilePath.string(), ec.message());
		return false;
	}
	OpenDebugLogFile();
    LogPrintf("Log file rotated at %s to [%s]\n\n",
        DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()), newLogFilePath.string());
    return true;
}

void CLogManager::CloseDebugLogFile()
{
    if (!m_fPrintToDebugLog)
		return;

    scoped_lock scoped_lock(m_mutexDebugLog);
	if (!m_LogFileHandle)
        return;

    if (!m_pvStartupLogs)
        m_pvStartupLogs = new list<string>;

    FileCommit(m_LogFileHandle);
	fclose(m_LogFileHandle);
	m_LogFileHandle = nullptr;
}

void CLogManager::CleanupOldLogFiles()
{
    auto maxLogAge = chrono::days(DEFAULT_OLD_LOGS_CLEANUP_DAYS);
    auto now = chrono::system_clock::now();
    for (const auto& entry : fs::directory_iterator(m_OldDebugLogDirPath))
	{
        // boost returns the last write time as time_t
        // std::filesystem returns the last write time as std::chrono::time_point
        auto lastWriteTime = chrono::system_clock::from_time_t(fs::last_write_time(entry));
        auto logFileAge = now - lastWriteTime;
		if (logFileAge > maxLogAge)
		{
            fss::error_code ec;
			if (!fs::remove(entry, ec))
				LogPrintf("ERROR: failed to remove old log file [%s]. %s\n", entry.path().string(), ec.message());
			else
    			LogPrintf("Removed old log file [%s]\n", entry.path().string());
		}
	}
}

void CLogManager::ShrinkDebugLogFile(const bool bForce)
{
    if (m_DebugLogFilePath.empty())
        m_DebugLogFilePath = GetDataDir() / "debug.log";

    if (fs::exists(m_DebugLogFilePath) && fs::is_regular_file(m_DebugLogFilePath))
    {
        if (bForce || fs::file_size(m_DebugLogFilePath) > DEFAULT_MAX_LOG_SIZE)
        {
            if (RotateDebugLogFile())
                CleanupOldLogFiles();
        }
    }
}

