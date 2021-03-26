#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "chainparamsbase.h"
#include "consensus/params.h"
#include "primitives/block.h"
#include "protocol.h"
#include "enum_util.h"

#include <vector>

struct CDNSSeedData {
    std::string name, host;
    CDNSSeedData(const std::string &strName, const std::string &strHost) : name(strName), host(strHost) {}
};

struct SeedSpec6 {
    uint8_t addr[16];
    uint16_t port;
};

typedef std::map<int, uint256> MapCheckpoints;

struct CCheckpointData {
    MapCheckpoints mapCheckpoints;
    int64_t nTimeLastCheckpoint;
    int64_t nTransactionsLastCheckpoint;
    double fTransactionsPerDay;
};

/**
 * CChainParams defines various tweakable parameters of a given instance of the
 * Bitcoin system. There are three: the main network on which people trade goods
 * and services, the public test network which gets reset from time to time and
 * a regression test mode which is intended for private networks only. It has
 * minimal difficulty to ensure that blocks can be found instantly.
 */
class CChainParams
{
public:
    enum struct Base58Type : uint32_t
    {
        PUBKEY_ADDRESS = 0,
        SCRIPT_ADDRESS,
        SECRET_KEY,
        EXT_PUBLIC_KEY,
        EXT_SECRET_KEY,

        ZCPAYMENT_ADDRRESS,
        ZCSPENDING_KEY,
        ZCVIEWING_KEY,

        MAX_BASE58_TYPES
    };

    enum struct Bech32Type : uint32_t
    {
        SAPLING_PAYMENT_ADDRESS = 0,
        SAPLING_FULL_VIEWING_KEY,
        SAPLING_INCOMING_VIEWING_KEY,
        SAPLING_EXTENDED_SPEND_KEY,

        MAX_BECH32_TYPES
    };

    const Consensus::Params& GetConsensus() const { return consensus; }
    const CMessageHeader::MessageStartChars& MessageStart() const { return pchMessageStart; }
    const std::vector<unsigned char>& AlertKey() const { return vAlertPubKey; }
    int GetDefaultPort() const noexcept { return nDefaultPort; }

    const CBlock& GenesisBlock() const noexcept { return genesis; }
    /** Make miner wait to have peers to avoid wasting work */
    bool MiningRequiresPeers() const noexcept { return fMiningRequiresPeers; }
    /** Default value for -checkmempool and -checkblockindex argument */
    bool DefaultConsistencyChecks() const noexcept { return fDefaultConsistencyChecks; }
    /** Policy: Filter transactions that do not match well-defined patterns */
    bool RequireStandard() const noexcept { return fRequireStandard; }
    int64_t PruneAfterHeight() const noexcept { return nPruneAfterHeight; }
    unsigned int EquihashN() const noexcept { return nEquihashN; }
    unsigned int EquihashK() const noexcept { return nEquihashK; }
    std::string CurrencyUnits() const noexcept { return strCurrencyUnits; }
    uint32_t BIP44CoinType() const noexcept { return bip44CoinType; }
    /** Make miner stop after a block is found. In RPC, don't return until nGenProcLimit blocks are generated */
    bool MineBlocksOnDemand() const noexcept { return fMineBlocksOnDemand; }
    /** In the future use NetworkIDString() for RPC fields */
    bool TestnetToBeDeprecatedFieldRPC() const noexcept { return fTestnetToBeDeprecatedFieldRPC; }
    /** Return the BIP70 network string (main, test or regtest) */
    std::string NetworkIDString() const noexcept { return strNetworkID; }    
    const std::vector<CDNSSeedData>& DNSSeeds() const noexcept { return vSeeds; }
    const std::vector<unsigned char>& Base58Prefix(const Base58Type type) const noexcept { return base58Prefixes[to_integral_type(type)]; }
    const std::string& Bech32HRP(const Bech32Type type) const noexcept { return bech32HRPs[to_integral_type(type)]; }
    const std::vector<SeedSpec6>& FixedSeeds() const noexcept { return vFixedSeeds; }
    const CCheckpointData& Checkpoints() const noexcept { return checkpointData; }

    bool IsMainNet() const noexcept { return network == CBaseChainParams::Network::MAIN; }
    bool IsTestNet() const noexcept { return network == CBaseChainParams::Network::TESTNET; }
    bool IsRegTest() const noexcept { return network == CBaseChainParams::Network::REGTEST; }

protected:
    CChainParams()
    {
        memset(&pchMessageStart, 0, sizeof(pchMessageStart));
    }

    Consensus::Params consensus;
    CMessageHeader::MessageStartChars pchMessageStart;
    //! Raw pub key bytes for the broadcast alert signing key.
    std::vector<unsigned char> vAlertPubKey;
    int nDefaultPort = 0;
    uint64_t nPruneAfterHeight = 0;
    unsigned int nEquihashN = 0;
    unsigned int nEquihashK = 0;
    std::vector<CDNSSeedData> vSeeds;
    std::vector<unsigned char> base58Prefixes[to_integral_type(Base58Type::MAX_BASE58_TYPES)];
    std::string bech32HRPs[to_integral_type(Bech32Type::MAX_BECH32_TYPES)];
    std::string strNetworkID;
    CBaseChainParams::Network network = CBaseChainParams::Network::MAIN;
    std::string strCurrencyUnits;
    uint32_t bip44CoinType = 0;
    CBlock genesis;
    std::vector<SeedSpec6> vFixedSeeds;
    bool fMiningRequiresPeers = false;
    bool fDefaultConsistencyChecks = false;
    bool fRequireStandard = false;
    bool fMineBlocksOnDemand = false;
    bool fTestnetToBeDeprecatedFieldRPC = false;
    CCheckpointData checkpointData;
};

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CChainParams &Params();

/** Return parameters for the given network. */
CChainParams &Params(CBaseChainParams::Network network);

/** Sets the params returned by Params() to those for the given network. */
void SelectParams(CBaseChainParams::Network network);

/**
 * Looks for -regtest or -testnet and then calls SelectParams as appropriate.
 * Returns false if an invalid combination is given.
 */
bool SelectParamsFromCommandLine();

/**
 * Allows modifying the network upgrade regtest parameters.
 */
void UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex idx, int nActivationHeight);
