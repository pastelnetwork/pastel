// Copyright (c) 2021-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <pastel_gtest_utils.h>
#include <utils/fs.h>

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

string generateRandomTxId()
{
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dist;

    string sTxId;
    sTxId.reserve(64);

    for (int i = 0; i < 4; ++i)
    {
        const uint64_t rand_value = dist(gen);
        for (int j = 60; j >= 0; j -= 4)
            sTxId.push_back("0123456789abcdef"[(rand_value >> j) & 0xF]);
    }
    return sTxId;
}

// generate random uint256
uint256 generateRandomUint256()
{
    return uint256S(generateRandomTxId());
}

/**
 * Generate temporary file name with extension.
 * 
 * \param szFileExt - optional extension
 * \return generated filename
 */
string generateTempFileName(const char* szFileExt)
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
