#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "primitives/block.h"
#include "chainparams.h"

#include <optional>
#include <stdint.h>

class CBlockIndex;
class CScript;
#ifdef ENABLE_WALLET
class CReserveKey;
class CWallet;
#endif
namespace Consensus { struct Params; };

struct CBlockTemplate
{
    CBlock block;
    std::vector<CAmount> vTxFees;
    std::vector<int64_t> vTxSigOps;
};

/** Generate a new block, without valid proof-of-work */
CBlockTemplate* CreateNewBlock(const CChainParams& chainparams, const CScript& scriptPubKeyIn);
#ifdef ENABLE_WALLET
std::optional<CScript> GetMinerScriptPubKey(CReserveKey& reservekey, const CChainParams& chainparams);
CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, const CChainParams& chainparams);
#else
std::optional<CScript> GetMinerScriptPubKey(const CChainParams& chainparams);
CBlockTemplate* CreateNewBlockWithKey(const CChainParams& chainparams);
#endif

#ifdef ENABLE_MINING
/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce);
/** Run the miner threads */
 #ifdef ENABLE_WALLET
void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads, const CChainParams& chainparams);
 #else
void GenerateBitcoins(bool fGenerate, int nThreads, const CChainParams& chainparams);
 #endif
#endif

void UpdateTime(CBlockHeader* pblock, const Consensus::Params& consensusParams, const CBlockIndex* pindexPrev);
