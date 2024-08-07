// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <assert.h>

#include <config/port_config.h>
#include <chainparamsbase.h>
#include <utils/util.h>

using namespace std;

/**
 * Main network
 */
class CBaseMainParams : public CBaseChainParams
{
public:
    CBaseMainParams() noexcept
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
    CBaseTestNetParams() noexcept
    {
        nRPCPort = TESTNET_DEFAULT_RPC_PORT;
        strDataDir = "testnet3";
    }
};

/**
 * Devnet
 */
class CBaseDevNetParams : public CBaseChainParams
{
public:
    CBaseDevNetParams() noexcept
    {
        nRPCPort = DEVNET_DEFAULT_RPC_PORT;
        strDataDir = "devnet";
    }
};

/*
 * Regression test
 */
class CBaseRegTestParams : public CBaseChainParams
{
public:
    CBaseRegTestParams() noexcept
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
    CBaseUnitTestParams() noexcept
    {
        strDataDir = "unittest";
    }
};

static unique_ptr<CBaseChainParams> globalChainBaseParams;

const CBaseChainParams& BaseParams()
{
    assert(globalChainBaseParams);
    return *globalChainBaseParams;
}

/**
 * Creates and returns a unique_ptr<CBaseChainParams>.
 * 
 * \param network - blockchain type (MAIN, TESTNET or REGTEST)
 * \return unique_ptr<CBaseChainParams>
 */
unique_ptr<CBaseChainParams> CreateBaseChainParams(const ChainNetwork network)
{
    unique_ptr<CBaseChainParams> BaseChainParams;
    switch (network)
    {
    case ChainNetwork::MAIN:
        BaseChainParams = make_unique<CBaseMainParams>();
        break;

    case ChainNetwork::TESTNET:
        BaseChainParams = make_unique<CBaseTestNetParams>();
        break;

    case ChainNetwork::DEVNET:
        BaseChainParams = make_unique<CBaseDevNetParams>();
        break;

    case ChainNetwork::REGTEST:
        BaseChainParams = make_unique<CBaseRegTestParams>();
        break;

    default:
        assert(false && "Unimplemented network");
        BaseChainParams = make_unique<CBaseMainParams>();
        break;
    }
    return BaseChainParams;
}

/** Sets the params returned by Params() to those for the given network. */
void SelectBaseParams(const ChainNetwork network)
{
    globalChainBaseParams = CreateBaseChainParams(network);
}

ChainNetwork NetworkIdFromCommandLine() noexcept
{
    const bool fRegTest = GetBoolArg("-regtest", false);
    const bool fTestNet = GetBoolArg("-testnet", false);
    const bool fDevNet = GetBoolArg("-devnet", false);

    const int nNetworkTypesSum = fRegTest + fTestNet + fDevNet;
    if (nNetworkTypesSum > 1)
        return ChainNetwork::MAX_NETWORK_TYPES;
    if (fRegTest)
        return ChainNetwork::REGTEST;
    if (fTestNet)
        return ChainNetwork::TESTNET;
    if (fDevNet)
        return ChainNetwork::DEVNET;
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
