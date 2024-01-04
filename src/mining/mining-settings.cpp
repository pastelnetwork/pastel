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
    m_nGenIdIndex = 0;
    m_bInitialized = false;
}

bool CMinerSettings::refreshMnIdInfo(string &error, const bool bRefreshConfig)
{
    auto mapLocalPastelIds = CPastelID::GetStoredPastelIDs(true);

    unique_lock<mutex> lock(m_mutexGenIds);

    if (bRefreshConfig)
        ReadConfigFile(mapArgs, mapMultiArgs, "-gen-*");

    // read Pastel IDs and passphrases from the config file
    int nSuffix = 0;
    do
    {
        string sParam = strprintf("-gen-pastelid%s", nSuffix ? strprintf("_%d", nSuffix) : "");
        string sPastelID = GetArg(sParam, "");
        trim(sPastelID);
        if (m_mapGenIds.count(sPastelID))
		{
			error = strprintf("Duplicate PastelID '%s' defined in [%s] option", 
                sPastelID, sParam);
			return false;
		}

        sParam = strprintf("-gen-passphrase%s", nSuffix ? strprintf("_%d", nSuffix) : "");
        string sPassphrase = GetArg(sParam, "");
        trim(sPassphrase);
        if (!sPastelID.empty())
        {
            if (sPassphrase.empty())
            {
                error = strprintf("Passphrase for Pastel ID '%s' is not defined in [%s] option",
                    sPastelID, sParam);
                return false;
            }
            if (!mapLocalPastelIds.count(sPastelID))
            {
                error = strprintf("Secure container for Pastel ID '%s' does not exist locally", sPastelID);
                return false;
			}
            if (!CPastelID::isValidPassphrase(sPastelID, SecureString(sPassphrase)))
			{
				error = strprintf("Passphrase for Pastel ID '%s' is not valid", sPastelID);
				return false;
			}
        }
        // check that we have secure container for this PastelID
        if (sPastelID.empty() || sPassphrase.empty())
			break;
        m_mapGenIds.emplace(move(sPastelID), move(sPassphrase));
        ++nSuffix;
    } while (false);
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

s_strings CMinerSettings::getMnIdsAndRotate() noexcept
{
    s_strings setMnIds;
    if (m_mapGenIds.empty())
        return setMnIds;

    auto it = m_mapGenIds.cbegin();
    auto itStart = it;

    advance(it, m_nGenIdIndex % m_mapGenIds.size());
    do
    {
		// Add the PastelID to the set
		setMnIds.emplace(it->first);

		// Increment the iterator
		++it;

		// If we're at the end of the map, wrap around to the beginning
		if (it == m_mapGenIds.cend())
			it = m_mapGenIds.cbegin();

		// If we've reached the starting point, break out of the loop
		if (it == itStart)
			break;
	} while (true);

    // Increment the index for next call, wrapping around if necessary
    m_nGenIdIndex = (m_nGenIdIndex + 1) % m_mapGenIds.size();

    return setMnIds;
}

bool CMinerSettings::getGenIdInfo(const std::string& mnid, SecureString& sPassPhrase) const noexcept
{
    if (m_mapGenIds.empty())
		return false;
	auto it = m_mapGenIds.find(mnid);
	if (it == m_mapGenIds.cend())
		return false;
	sPassPhrase = SecureString(it->second);
	return true;
}

CMinerSettings gl_MinerSettings;
