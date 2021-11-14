// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparamsbase.h"
#include "port_config.h"
#include "util.h"

#include <assert.h>

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
std::unique_ptr<CBaseChainParams> CreateBaseChainParams(const CBaseChainParams::Network network)
{
    std::unique_ptr<CBaseChainParams> BaseChainParams;
    switch (network)
    {
    case CBaseChainParams::Network::MAIN:
        BaseChainParams = std::make_unique<CBaseMainParams>();
        break;

    case CBaseChainParams::Network::TESTNET:
        BaseChainParams = std::make_unique<CBaseTestNetParams>();
        break;

    case CBaseChainParams::Network::REGTEST:
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
void SelectBaseParams(const CBaseChainParams::Network network)
{
    globalChainBaseParams = CreateBaseChainParams(network);
}

CBaseChainParams::Network NetworkIdFromCommandLine()
{
    bool fRegTest = GetBoolArg("-regtest", false);
    bool fTestNet = GetBoolArg("-testnet", false);

    if (fTestNet && fRegTest)
        return CBaseChainParams::Network::MAX_NETWORK_TYPES;
    if (fRegTest)
        return CBaseChainParams::Network::REGTEST;
    if (fTestNet)
        return CBaseChainParams::Network::TESTNET;
    return CBaseChainParams::Network::MAIN;
}

bool SelectBaseParamsFromCommandLine()
{
    CBaseChainParams::Network network = NetworkIdFromCommandLine();
    if (network == CBaseChainParams::Network::MAX_NETWORK_TYPES)
        return false;

    SelectBaseParams(network);
    return true;
}

bool AreBaseParamsConfigured()
{
    return (bool)globalChainBaseParams;
}
