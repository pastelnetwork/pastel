// Copyright (c) 2022-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <utils/ping_util.h>
#include <str_utils.h>
#include <util.h>
#include <fs.h>

using namespace std;
using namespace chrono;

CPingUtility::CPingUtility() : 
    m_bPingUtilityChecked(false),
    m_bPingUtilityAvailable(false)
{}

bool CPingUtility::pingHostInternal(const std::string& sHostName)
{
#ifdef WIN32
    string sPingCommand = strprintf("%s -n 1 %s > nul 2>&1", m_sPingPath, sHostName);
#else
    string sPingCommand = strprintf("%s -c 1 %s >/dev/null 2>&1", m_sPingPath, sHostName);
#endif
    return system(sPingCommand.c_str()) == 0;
}

CPingUtility::PingResult CPingUtility::pingHost(const string& sHostName)
{
    // Periodically recheck if the ping utility is available
    if (!m_bPingUtilityChecked || duration_cast<seconds>(steady_clock::now() - m_lastCheckTime).count() > m_recheckIntervalSeconds)
    {
        string error;
        m_bPingUtilityAvailable = checkPingUtility(error);
        if (!m_bPingUtilityAvailable)
		{
			LogPrintf("%s\n", error);
		}
        m_bPingUtilityChecked = true;
        m_lastCheckTime = steady_clock::now();
    }

    if (!m_bPingUtilityAvailable)
        return PingResult::UtilityNotAvailable;

    return pingHostInternal(sHostName) ? PingResult::Success : PingResult::Failure;
}

bool CPingUtility::checkPingUtility(string &error)
{
#ifdef WIN32
    string sPingFindCommand = "where ping";
#else
    string sPingFindCommand = "which ping";
#endif
    string stdOut, stdErr;
    const int nRetCode = exec_system_command(sPingFindCommand.c_str(), stdOut, stdErr);
    if (nRetCode != 0)
    {
		error = strprintf("Couldn't find ping utility: %s", stdErr);
		return false;
	}
    trim(stdOut);
    if (!fs::exists(stdOut))
    {
        error = strprintf("Couldn't find ping utility at [%s]", stdOut);
        return false;
    }

    m_sPingPath = stdOut;
    LogPrintf("Found ping utility at [%s]\n", m_sPingPath);
    if (!pingHostInternal("127.0.0.1"))
    {
		error = "Ping utility is not working";
		return false;
	}
    return true;
}
