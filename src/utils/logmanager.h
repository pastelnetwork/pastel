#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <string>
#include <memory>
#include <atomic>
#include <mutex>

#include <utils/fs.h>

class CLogManager final
{
public:
	CLogManager() noexcept;

	void OpenDebugLogFile();
	void CloseDebugLogFile();

	void ShrinkDebugLogFile(const bool bForce = false);
	// Set print-to-console mode.
	bool SetPrintToConsoleMode(std::string& error);
	void LogFlush();
	void ScheduleReopenDebugLog() { m_fReopenDebugLog = true; }

	// Send a string to the log/stdout output
	size_t LogPrintStr(const std::string& str);

	bool IsPrintToConsole() const noexcept { return m_nPrintToConsoleMode > 0; }
	bool IsPrintToDebugLog() const noexcept { return m_fPrintToDebugLog; }

private:
	fs::path m_DebugLogFilePath;
	fs::path m_OldDebugLogDirPath;
	std::mutex m_mutexDebugLog;
	std::list<std::string>* m_pvStartupLogs;

	/**
	* Print to console modes:
	* 0 - do not print anything to console
	* 1 - print only to console
	* 2 - print to console and debug.log
	*/
	std::atomic_uint32_t m_nPrintToConsoleMode = 0;
	bool m_fPrintToDebugLog = true;
	std::atomic_bool m_fReopenDebugLog = false;
	FILE* m_LogFileHandle = nullptr;

	bool RotateDebugLogFile(const bool bReopenLog = true);
	void CleanupOldLogFiles();
};

/** Return true if log accepts specified category */
bool LogAcceptCategory(const char* category);

extern bool fLogTimestamps;
extern bool fLogIPs;
extern std::unique_ptr<CLogManager> gl_LogMgr;