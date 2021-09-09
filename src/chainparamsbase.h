#pragma once
// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>
#include <memory>

/**
 * CBaseChainParams defines the base parameters (shared between pastel-cli and pasteld)
 * of a given instance of the Bitcoin system.
 */
class CBaseChainParams
{
public:
    enum class Network
    {
        MAIN,
        TESTNET,
        REGTEST,

        MAX_NETWORK_TYPES
    };

    const std::string& DataDir() const { return strDataDir; }
    int RPCPort() const { return nRPCPort; }

protected:
    CBaseChainParams() {}

    int nRPCPort = 0;
    std::string strDataDir;
};

/**
 * Creates and returns a std::unique_ptr<CBaseChainParams>. This won't change after app
 * startup, except for unit tests.
 */
std::unique_ptr<CBaseChainParams> CreateBaseChainParams(const CBaseChainParams::Network network);

/** Sets the params returned by Params() to those for the given network. */
void SelectBaseParams(const CBaseChainParams::Network network);

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CBaseChainParams& BaseParams();

/**
 * Looks for -regtest or -testnet and returns the appropriate Network ID.
 * Returns MAX_NETWORK_TYPES if an invalid combination is given.
 */
CBaseChainParams::Network NetworkIdFromCommandLine();

/**
 * Calls NetworkIdFromCommandLine() and then calls SelectParams as appropriate.
 * Returns false if an invalid combination is given.
 */
bool SelectBaseParamsFromCommandLine();

/**
 * Return true if SelectBaseParamsFromCommandLine() has been called to select
 * a network.
 */
bool AreBaseParamsConfigured();
