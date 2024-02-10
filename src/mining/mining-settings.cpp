// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mining/mining-settings.h>
#include <primitives/block.h>
#include <pastelid/pastel_key.h>
#include <main.h>

using namespace std;

CMinerSettings::CMinerSettings() noexcept
{
    m_nBlockVersion = CBlockHeader::CURRENT_VERSION;
	m_nBlockMaxSize = DEFAULT_BLOCK_MAX_SIZE;
	m_nBlockPrioritySize = DEFAULT_BLOCK_PRIORITY_SIZE;
	m_nBlockMinSize = DEFAULT_BLOCK_MIN_SIZE;
    m_equihashSolver = EquihashSolver::Default;
    m_bEligibleForMining = false;
    m_nGenIdIndex = 0;
    m_bInitialized = false;
}

bool CMinerSettings::refreshMnIdInfo(string &error, const bool bRefreshConfig)
{
    auto mapLocalPastelIds = CPastelID::GetStoredPastelIDs(true);

    unique_lock<mutex> lock(m_mutexGenIds);

    if (bRefreshConfig)
        ReadConfigFile(mapArgs, mapMultiArgs, "-gen*");

    // read Pastel ID and passphrases from the config file
    string sParam = "-genpastelid";
    m_sGenId = GetArg(sParam, "");
    trim(m_sGenId);

    sParam = "-genpassphrase";
    string sPassphrase = GetArg(sParam, "");
    trim(sPassphrase);
    if (!m_sGenId.empty())
    {
        if (sPassphrase.empty())
        {
            error = strprintf("Passphrase for Pastel ID '%s' is not defined in [%s] option",
                m_sGenId, sParam);
            return false;
        }
        if (!mapLocalPastelIds.count(m_sGenId))
        {
            error = strprintf("Secure container for Pastel ID '%s' does not exist locally", m_sGenId);
            return false;
		}
        if (!CPastelID::isValidPassphrase(m_sGenId, SecureString(sPassphrase)))
		{
			error = strprintf("Passphrase for Pastel ID '%s' is not valid", m_sGenId);
			return false;
		}
        m_sGenPassPhrase = sPassphrase;
    }
    m_bEligibleForMining = GetBoolArg("-genenablemnmining", false);
    if (m_bEligibleForMining)
    {
        if (m_sGenId.empty() || m_sGenPassPhrase.empty())
        {
			error = "No MasterNode Pastel ID (genpastelid) or passphrase (genpassphrase) defined in the config file";
			return false;
		}
        LogPrintf("MasterNode mining is enabled\n");
	}
    else
    {
        LogPrintf("MasterNode mining is not enabled (genenablemnmining option), -gen* options are ignored\n");
	}
    return true;
}

bool CMinerSettings::initialize(const CChainParams& chainparams, string &error)
{
    if (m_bInitialized)
		return true;
    m_nBlockVersion = GetIntArg("-blockversion", CBlockHeader::CURRENT_VERSION);

    m_nBlockMaxSize = static_cast<uint32_t>(GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE));
    // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
    m_nBlockMaxSize = max<uint32_t>(1000, min<uint32_t>(MAX_BLOCK_SIZE - 1000, m_nBlockMaxSize));

    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    m_nBlockPrioritySize = static_cast<uint32_t>(GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE));
    m_nBlockPrioritySize = min(m_nBlockMaxSize, m_nBlockPrioritySize);

    // Minimum block size you want to create; block will be filled with free transactions
    // until there are no more or the block reaches this size:
    m_nBlockMinSize = static_cast<uint32_t>(GetArg("-blockminsize", DEFAULT_BLOCK_MIN_SIZE));
    m_nBlockMinSize = min(m_nBlockMaxSize, m_nBlockMinSize);

    string sEquihashSolver = GetArg("-equihashsolver", "default");
    if (str_icmp(sEquihashSolver, "default"))
        m_equihashSolver = EquihashSolver::Default;
    else if (str_icmp(sEquihashSolver, "tromp"))
        m_equihashSolver = EquihashSolver::Tromp;
    else
    {
        error = strprintf("Invalid equihash solver option: %s, supported values: [default, tromp]", sEquihashSolver);
        return false;
    }

    if (!refreshMnIdInfo(error, false))
    {
		error = strprintf("Failed to refresh PastelID info. %s", error);
		return false;
	}

    m_bInitialized = true;
    return true;
}

std::string CMinerSettings::getEquihashSolverName() const noexcept
{
    switch (m_equihashSolver)
    {
        case EquihashSolver::Default:
            return "default";
        case EquihashSolver::Tromp:
            return "tromp";
        default:
            return "unknown";
    }
}

bool CMinerSettings::getGenIdInfo(const std::string &sMnId, SecureString& sPassPhrase) const noexcept
{
    if (m_sGenId.empty())
        return false;
    if (!str_icmp(sMnId, m_sGenId))
        return false;
	sPassPhrase = SecureString(m_sGenPassPhrase);
	return true;
}

CMinerSettings gl_MiningSettings;
