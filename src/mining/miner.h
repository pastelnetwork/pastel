#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <optional>
#include <cstdint>
#include <vector>
#include <string>
#include <atomic>

#include <primitives/block.h>
#include <chainparams.h>
#include <amount.h>

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
    v_amounts vTxFees;
    std::vector<int64_t> vTxSigOps;
};

/** Generate a new block, without valid proof-of-work */
CBlockTemplate* CreateNewBlock(const CChainParams& chainparams, const CScript& scriptPubKeyIn, const std::string& sEligiblePastelID);
#ifdef ENABLE_WALLET
std::optional<CScript> GetMinerScriptPubKey(CReserveKey& reservekey, const CChainParams& chainparams);
CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, const CChainParams& chainparams, const std::string& sEligiblePastelID);
#else
std::optional<CScript> GetMinerScriptPubKey(const CChainParams& chainparams);
CBlockTemplate* CreateNewBlockWithKey(const CChainParams& chainparams, const std::string& sEligiblePastelID);
#endif

#ifdef ENABLE_MINING
extern std::atomic_bool gl_bEligibleForMiningNextBlock;
// delay in seconds before a mined block is validated against blocks mined by other miners
constexpr int64_t MINED_BLOCK_VALIDATION_DELAY_SECS = 20;

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
