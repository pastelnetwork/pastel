// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <mining/mining-settings.h>
#include <mnode/mnode-controller.h>
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
    m_bInitialized = false;
}

std::string CMinerSettings::getGenId() const noexcept
{
    return masterNodeCtrl.activeMasternode.getMNPastelID();
}

bool CMinerSettings::isEligibleForMining() const noexcept
{
	return masterNodeCtrl.activeMasternode.isEligibleForMining();
}

bool CMinerSettings::refreshMnIdInfo(string &error, const bool bRefreshConfig)
{
    unique_lock lock(m_mutexGenIds);

    if (bRefreshConfig)
        ReadConfigFile(mapArgs, mapMultiArgs, "-gen*");

    // read MN passphrase from the config file
    string sParam = "-genpassphrase";
    string sPassphrase = GetArg(sParam, "");
    trim(sPassphrase);
    m_sGenPassPhrase = sPassphrase;

    return true;
}

bool CMinerSettings::CheckMNSettingsForLocalMining(string &error)
{
    const bool bLocalMiningEnabled = GetBoolArg("-gen", false);

    if (bLocalMiningEnabled)
    {
        if (!masterNodeCtrl.IsActiveMasterNode())
        {
			error = "Local mining is enabled, but the node is not an active masternode";
			return false;
		}

        if (m_sGenPassPhrase.empty())
        {
            error = "Passphrase for MasterNode's Pastel ID is not defined in [genpassphrase] option";
            return false;
        }

        auto mapLocalPastelIds = CPastelID::GetStoredPastelIDs(true);
        string sGenId = getGenId();

        if (!mapLocalPastelIds.count(sGenId))
        {
            error = strprintf("Secure container for Pastel ID '%s' does not exist locally", sGenId);
            return false;
		}

        if (!CPastelID::isValidPassphrase(sGenId, m_sGenPassPhrase))
		{
			error = strprintf("Passphrase for Pastel ID '%s' is not valid", sGenId);
			return false;
		}

        if (!masterNodeCtrl.activeMasternode.isEligibleForMining())
        {
			error = "Local mining is enabled, but the Active MasterNode's mining eligibility option is not set in masternode.conf";
			return false;
		}
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

bool CMinerSettings::getGenInfo(SecureString& sPassPhrase) const noexcept
{
	sPassPhrase = SecureString(m_sGenPassPhrase);
	return true;
}

CMinerSettings gl_MiningSettings;
