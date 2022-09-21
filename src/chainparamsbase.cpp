// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <assert.h>

#include <chainparamsbase.h>
#include <port_config.h>
#include <util.h>

/**
 * Main network
 */
class CBaseMainParams : public CBaseChainParams
{
public:
    CBaseMainParams()
    {
        nRPCPort = MAINNET_DEFAULT_RPC_PORT;
    }
};

/**
 * Testnet (v3)
 */
class CBaseTestNetParams : public CBaseChainParams
{
public:
    CBaseTestNetParams()
    {
        nRPCPort = TESTNET_DEFAULT_RPC_PORT;
        strDataDir = "testnet3";
    }
};

/*
 * Regression test
 */
class CBaseRegTestParams : public CBaseChainParams
{
public:
    CBaseRegTestParams()
    {
        nRPCPort = 18232;
        strDataDir = "regtest";
    }
};

/*
 * Unit test
 */
class CBaseUnitTestParams : public CBaseMainParams
{
public:
    CBaseUnitTestParams()
    {
        strDataDir = "unittest";
    }
};

static std::unique_ptr<CBaseChainParams> globalChainBaseParams;

const CBaseChainParams& BaseParams()
{
    assert(globalChainBaseParams);
    return *globalChainBaseParams;
}

/**
 * Creates and returns a std::unique_ptr<CBaseChainParams>.
 * 
 * \param network - blockchain type (MAIN, TESTNET or REGTEST)
 * \return std::unique_ptr<CBaseChainParams>
 */
std::unique_ptr<CBaseChainParams> CreateBaseChainParams(const ChainNetwork network)
{
    std::unique_ptr<CBaseChainParams> BaseChainParams;
    switch (network)
    {
    case ChainNetwork::MAIN:
        BaseChainParams = std::make_unique<CBaseMainParams>();
        break;

    case ChainNetwork::TESTNET:
        BaseChainParams = std::make_unique<CBaseTestNetParams>();
        break;

    case ChainNetwork::REGTEST:
        BaseChainParams = std::make_unique<CBaseRegTestParams>();
        break;

    default:
        assert(false && "Unimplemented network");
        BaseChainParams = std::make_unique<CBaseMainParams>();
        break;
    }
    return BaseChainParams;
}

/** Sets the params returned by Params() to those for the given network. */
void SelectBaseParams(const ChainNetwork network)
{
    globalChainBaseParams = CreateBaseChainParams(network);
}

ChainNetwork NetworkIdFromCommandLine()
{
    const bool fRegTest = GetBoolArg("-regtest", false);
    const bool fTestNet = GetBoolArg("-testnet", false);

    if (fTestNet && fRegTest)
        return ChainNetwork::MAX_NETWORK_TYPES;
    if (fRegTest)
        return ChainNetwork::REGTEST;
    if (fTestNet)
        return ChainNetwork::TESTNET;
    return ChainNetwork::MAIN;
}

bool SelectBaseParamsFromCommandLine()
{
    const ChainNetwork network = NetworkIdFromCommandLine();
    if (network == ChainNetwork::MAX_NETWORK_TYPES)
        return false;

    SelectBaseParams(network);
    return true;
}

bool AreBaseParamsConfigured()
{
    return (bool)globalChainBaseParams;
}
