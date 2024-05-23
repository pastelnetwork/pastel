#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Zcash developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <functional>

#include <chainparams.h>
#include <consensus/validation.h>
#include <consensus/params.h>
#include <primitives/transaction.h>
#include <txmempool.h>
#include <chain.h>

/** Check whether we are doing an initial block download (synchronizing from disk or network) */
extern funcIsInitialBlockDownload_t fnIsInitialBlockDownload;

/** Transaction validation functions */

/** Check a transaction contextually against a set of consensus rules */
bool ContextualCheckTransaction(
    const CTransaction& tx,
    CValidationState &state,
    const CChainParams& chainparams,
    const int nHeight,
    const CBlockIndex *pindexPrev,
    funcIsInitialBlockDownload_t isInitBlockDownload);

/** Context-independent validity checks */
bool CheckTransaction(const CTransaction& tx, CValidationState& state, libzcash::ProofVerifier& verifier);
bool CheckTransactionWithoutProofVerification(const CTransaction& tx, CValidationState &state);

/** (try to) add transaction to memory pool **/
bool AcceptToMemoryPool(
     const CChainParams& chainparams,
     CTxMemPool& pool, CValidationState &state,
     const CTransaction &tx,
     bool fLimitFree,
     bool* pfMissingInputs, bool fRejectAbsurdFee=false);

/** Return a CMutableTransaction with contextual default values based on set of consensus rules at height */
CMutableTransaction CreateNewContextualCMutableTransaction(const Consensus::Params& consensusParams, const uint32_t nHeight);

/** Check for standard transaction types
 * @return True if all outputs (scriptPubKeys) use only standard transaction forms
 */
bool IsStandardTx(const CTransaction& tx, std::string& reason, const CChainParams& chainarams, const int nHeight = 0);

/**
 * Check if transaction is final and can be included in a block with the
 * specified height and time. Consensus critical.
 */
bool IsFinalTx(const CTransaction &tx, const uint32_t nBlockHeight, int64_t nBlockTime);

/**
 * Check if transaction will be final in the next block to be created.
 *
 * Calls IsFinalTx() with current block height and appropriate block time.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckFinalTx(const CTransaction &tx, int flags = -1);

/**
 * Check if transaction is expired and can be included in a block with the
 * specified height. Consensus critical.
 */
bool IsExpiredTx(const CTransaction &tx, int nBlockHeight);

/**
 * Check if transaction is expiring soon.  If yes, not propagating the transaction
 * can help DoS mitigation.  This is not consensus critical.
 */
bool IsExpiringSoonTx(const CTransaction &tx, int nNextBlockHeight);

/**
 * Check transaction inputs, and make sure any
 * pay-to-script-hash transactions are evaluating IsStandard scripts
 * 
 * Why bother? To avoid denial-of-service attacks; an attacker
 * can submit a standard HASH... OP_EQUAL transaction,
 * which will get accepted into blocks. The redemption
 * script can be anything; an attacker could use a very
 * expensive-to-check-upon-redemption script like:
 *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
 */

/** 
 * Check for standard transaction types
 * @param[in] mapInputs    Map of previous transactions that have outputs we're spending
 * @return True if all inputs (scriptSigs) use only standard transaction forms
 */
bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs, uint32_t consensusBranchId);
