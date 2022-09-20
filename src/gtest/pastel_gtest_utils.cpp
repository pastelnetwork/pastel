// Copyright (c) 2021 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or httpa://www.opensource.org/licenses/mit-license.php.

#include <pastel_gtest_utils.h>
#include <fs.h>

#include <random>
#include <chainparams.h>
using namespace std;

int GenZero(int n)
{
    return 0;
}

int GenMax(int n)
{
    return n-1;
}

/**
 * generate random string.
 * 
 * \param nLength - string length
 * \return 
 */
string generateRandomId(const size_t nLength)
{
    static constexpr auto charset =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789_";

    static std::random_device rd;
    static std::mt19937 mt(rd());
    static std::uniform_int_distribution<int> dist(0, static_cast<int>(strlen(charset) - 1));

    string s;
    s.resize(nLength);
    for (size_t i = 0; i < nLength; ++i)
        s[i] = charset[dist(mt)];
    return s;
}

/**
 * Generate temporary file name with extension.
 * 
 * \param szFileExt - optional extension
 * \return generated filename
 */
std::string generateTempFileName(const char* szFileExt)
{
    string s = generateRandomId(50);
    if (szFileExt)
        s += szFileExt;
    auto file = fs::temp_directory_path() / fs::path(s);
    return file.string();
}

// Sapling
const Consensus::Params& RegtestActivateSapling()
{
    SelectParams(ChainNetwork::REGTEST);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
    return Params().GetConsensus();
}

void RegtestDeactivateSapling()
{
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}
