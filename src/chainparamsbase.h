#pragma once
// Copyright (c) 2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <string>
#include <memory>

#include <consensus/consensus.h>

/**
 * CBaseChainParams defines the base parameters (shared between pastel-cli and pasteld)
 * of a given instance of the Bitcoin system.
 */
class CBaseChainParams
{
public:
    const std::string& DataDir() const noexcept { return strDataDir; }
    int RPCPort() const noexcept { return nRPCPort; }

protected:
    CBaseChainParams() = default;

    int nRPCPort = 0;
    std::string strDataDir;
};

/**
 * Creates and returns a std::unique_ptr<CBaseChainParams>. This won't change after app
 * startup, except for unit tests.
 */
std::unique_ptr<CBaseChainParams> CreateBaseChainParams(const ChainNetwork network);

/** Sets the params returned by Params() to those for the given network. */
void SelectBaseParams(const ChainNetwork network);

/**
 * Return the currently selected parameters. This won't change after app
 * startup, except for unit tests.
 */
const CBaseChainParams& BaseParams();

/**
 * Looks for -regtest or -testnet and returns the appropriate Network ID.
 * Returns MAX_NETWORK_TYPES if an invalid combination is given.
 */
ChainNetwork NetworkIdFromCommandLine();

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
