// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2015-2018 The Zcash developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <limits>
#include <cinttypes>
#include <atomic>

#include <accept_to_mempool.h>
#include <chain_options.h>
#include <timedata.h>
#include <main.h>
#include <metrics.h>
#include <validationinterface.h>
#include <mnode/ticket-processor.h>

#include <librustzcash.h>

using namespace std;

bool IsInitialBlockDownload(const Consensus::Params& consensusParams)
{
    // Once this function has returned false, it must remain false.
    static atomic_bool latchToFalse{false};
    // Optimization: pre-test latch before taking the lock.
    if (latchToFalse.load(memory_order_relaxed))
        return false;

    LOCK(cs_main);
    if (latchToFalse.load(memory_order_relaxed))
        return false;
    if (fImporting || fReindex)
        return true;
    if (!chainActive.Tip())
        return true;
    if (chainActive.Tip()->nChainWork < UintToArith256(consensusParams.nMinimumChainWork))
        return true;
    if (consensusParams.network != ChainNetwork::REGTEST)
    {
        if (chainActive.Tip()->GetBlockTime() < (GetTime() - nMaxTipAge))
            return true;
    }
    LogPrintf("Leaving InitialBlockDownload (latching to false)\n");
    latchToFalse.store(true, memory_order_relaxed);
    return false;
}
funcIsInitialBlockDownload_t fnIsInitialBlockDownload = IsInitialBlockDownload;


bool IsStandardTx(const CTransaction& tx, string& reason, const CChainParams& chainparams, const int nHeight)
{
    const auto& consensusParams = chainparams.GetConsensus();
    const bool overwinterActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_OVERWINTER);
    const bool saplingActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_SAPLING);

    if (saplingActive)
    {
        // Sapling standard rules apply
        if (tx.nVersion > CTransaction::SAPLING_MAX_CURRENT_VERSION || tx.nVersion < CTransaction::SAPLING_MIN_CURRENT_VERSION)
        {
            reason = "sapling-version";
            return false;
        }
    } else if (overwinterActive) {
        // Overwinter standard rules apply
        if (tx.nVersion > CTransaction::OVERWINTER_MAX_CURRENT_VERSION || tx.nVersion < CTransaction::OVERWINTER_MIN_CURRENT_VERSION)
        {
            reason = "overwinter-version";
            return false;
        }
    } else {
        // Sprout standard rules apply
        if (tx.nVersion > CTransaction::SPROUT_MAX_CURRENT_VERSION || tx.nVersion < CTransaction::SPROUT_MIN_CURRENT_VERSION)
        {
            reason = "version";
            return false;
        }
    }

    for (const auto& txin : tx.vin)
    {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys. (remember the 520 byte limit on redeemScript size) That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard)
        if (txin.scriptSig.size() > 1650)
        {
            reason = "scriptsig-size";
            return false;
        }
        if (!txin.scriptSig.IsPushOnly())
        {
            reason = "scriptsig-not-pushonly";
            return false;
        }
    }

    unsigned int nDataOut = 0;
    txnouttype whichType;
    for (const auto& txout : tx.vout)
    {
        if (!::IsStandard(txout.scriptPubKey, whichType))
        {
            reason = "scriptpubkey";
            return false;
        }

        if (whichType == TX_NULL_DATA)
            nDataOut++;
        else if ((whichType == TX_MULTISIG) && (!fIsBareMultisigStd)) {
            reason = "bare-multisig";
            return false;
        } else if (txout.IsDust(gl_ChainOptions.minRelayTxFee)) {
            reason = "dust";
            return false;
        }
    }

    // only one OP_RETURN txout is permitted
    if (nDataOut > 1)
    {
        reason = "multi-op-return";
        return false;
    }

    return true;
}

/**
 * Checks if the transaction is final and can be included in a block with the
 * specified height and time. Consensus critical.
 * 
 * \param tx - transaction to check
 * \param nBlockHeight - current block height
 * \param nBlockTime - current block time
 * \return - true if the transaction is final, false otherwise
 */
bool IsFinalTx(const CTransaction& tx, const uint32_t nBlockHeight, int64_t nBlockTime)
{
    if (tx.nLockTime == 0)
        return true;
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? static_cast<int64_t>(nBlockHeight) : nBlockTime))
        return true;
    for (const auto& txin : tx.vin)
    {
        if (!txin.IsFinal())
            return false;
    }
    return true;
}

bool CheckFinalTx(const CTransaction &tx, int flags)
{
    AssertLockHeld(cs_main);

    // By convention a negative value for flags indicates that the
    // current network-enforced consensus rules should be used. In
    // a future soft-fork scenario that would mean checking which
    // rules would be enforced for the next block and setting the
    // appropriate flags. At the present time no soft-forks are
    // scheduled, so no flags are set.
    flags = max(flags, 0);

    // CheckFinalTx() uses chainActive.Height()+1 to evaluate
    // nLockTime because when IsFinalTx() is called within
    // CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a
    // transaction can be part of the *next* block, we need to call
    // IsFinalTx() with one more than chainActive.Height().
    const int32_t nBlockHeight = chainActive.Height() + 1;

    // Timestamps on the other hand don't get any special treatment,
    // because we can't know what timestamp the next block will have,
    // and there aren't timestamp applications where it matters.
    // However this changes once median past time-locks are enforced:
    const int64_t nBlockTime = (flags & LOCKTIME_MEDIAN_TIME_PAST)
                             ? chainActive.Tip()->GetMedianTimePast()
                             : GetAdjustedTime();

    return IsFinalTx(tx, nBlockHeight, nBlockTime);
}

bool IsExpiredTx(const CTransaction &tx, int nBlockHeight)
{
    if (tx.nExpiryHeight == 0 || tx.IsCoinBase())
        return false;
    return static_cast<uint32_t>(nBlockHeight) > tx.nExpiryHeight;
}

bool IsExpiringSoonTx(const CTransaction &tx, int nNextBlockHeight)
{
    return IsExpiredTx(tx, nNextBlockHeight + TX_EXPIRING_SOON_THRESHOLD);
}

/**
 * Check transaction inputs to mitigate two
 * potential denial-of-service attacks:
 *
 * 1. scriptSigs with extra data stuffed into them,
 *    not consumed by scriptPubKey (or P2SH script)
 * 2. P2SH scripts with a crazy number of expensive
 *    CHECKSIG/CHECKMULTISIG operations
 */
bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs, uint32_t consensusBranchId)
{
    if (tx.IsCoinBase())
        return true; // Coinbases don't use vin normally

    vector<v_uint8> vSolutions;
    vector<v_uint8> vSolutions2;
    vector<v_uint8> vStack;
    for (const auto& txIn : tx.vin)
    {
        const CTxOut& prev = mapInputs.GetOutputFor(txIn);

        vSolutions.clear();
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions))
            return false;
        int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
        if (nArgsExpected < 0)
            return false;

        // Transactions with extra stuff in their scriptSigs are
        // non-standard. Note that this EvalScript() call will
        // be quick, because if there are any operations
        // beside "push data" in the scriptSig
        // IsStandardTx() will have already returned false
        // and this method isn't called.
        vStack.clear();
        if (!EvalScript(vStack, txIn.scriptSig, SCRIPT_VERIFY_NONE, BaseSignatureChecker(), consensusBranchId))
            return false;

        if (whichType == TX_SCRIPTHASH)
        {
            if (vStack.empty())
                return false;
            CScript subscript(vStack.back().begin(), vStack.back().end());
            vSolutions2.clear();
            txnouttype whichType2;
            if (Solver(subscript, whichType2, vSolutions2))
            {
                int tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);
                if (tmpExpected < 0)
                    return false;
                nArgsExpected += tmpExpected;
            }
            else
            {
                // Any other Script with less than 15 sigops OK:
                unsigned int sigops = subscript.GetSigOpCount(true);
                // ... extra data left on the stack after execution is OK, too:
                return (sigops <= MAX_P2SH_SIGOPS);
            }
        }

        if (vStack.size() != (unsigned int)nArgsExpected)
            return false;
    }

    return true;
}

/**
 * Check a transaction contextually against a set of consensus rules valid at a given block height.
 * 
 * Notes:
 * 1. AcceptToMemoryPool calls CheckTransaction and this function.
 * 2. ProcessNewBlock calls AcceptBlock, which calls CheckBlock (which calls CheckTransaction)
 *    and ContextualCheckBlock (which calls this function).
 * 3. For consensus rules that relax restrictions (where a transaction that is invalid at height M
 *    can become valid at a later height N), we make the bans conditional on not being in Initial
 *    Block Download (IBD) mode.
 * 
 * \param tx - transaction to check
 * \param state - validation state
 * \param chainparams - chain parameters
 * \param nHeight - height of the block being evaluated
 * \param pindexPrev - pointer to the previous block index
 * \param isInitBlockDownload - functor to check IBD mode
 * \return true if the transaction is valid, false otherwise
 */
bool ContextualCheckTransaction(
    const CTransaction& tx,
    CValidationState &state,
    const CChainParams& chainparams,
    const int nHeight,
    const CBlockIndex *pindexPrev,
    funcIsInitialBlockDownload_t isInitBlockDownload)
{
    const auto& consensusParams = chainparams.GetConsensus();
    const bool overwinterActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_OVERWINTER);
    const bool beforeOverwinter = !overwinterActive;
    const bool saplingActive = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_SAPLING);
    string strRejectReasonDetails;

    // DoS level to ban peers.
    const int DOS_LEVEL_BLOCK = 100;
    // DoS level set to 10 to be more forgiving.
    const int DOS_LEVEL_MEMPOOL = 10;

    // For constricting rules, we don't need to account for IBD mode.
    const bool isMined = is_enum_any_of(state.getTxOrigin(), TxOrigin::MINED_BLOCK, TxOrigin::GENERATED, TxOrigin::LOADED_BLOCK);
    const auto dosLevelConstricting = isMined ? DOS_LEVEL_BLOCK : DOS_LEVEL_MEMPOOL;
    // For rules that are relaxing (or might become relaxing when a future network upgrade is implemented), we need to account for IBD mode.
    const auto dosLevelPotentiallyRelaxing = isMined ? DOS_LEVEL_BLOCK : 
                                                    (isInitBlockDownload(consensusParams) ? 0 : DOS_LEVEL_MEMPOOL);

    // Rules that apply only to Sprout
    if (beforeOverwinter)
    {
        // Reject transactions which are intended for Overwinter and beyond
        if (tx.fOverwintered)
        {
            strRejectReasonDetails = strprintf("overwinter is not active yet, height=%d", nHeight);
            return state.DoS(
                dosLevelPotentiallyRelaxing,
                error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                REJECT_INVALID, "tx-overwinter-not-active", false, strRejectReasonDetails);
        }
    }

    // Rules that apply to Overwinter and later
    if (overwinterActive)
    {
        // Reject transactions intended for Sprout
        if (!tx.fOverwintered)
        {
            strRejectReasonDetails = strprintf("overwintered flag must be set when Overwinter is active, height=%d", nHeight);
            return state.DoS(
                dosLevelConstricting,
                error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                REJECT_INVALID, "tx-overwintered-flag-not-set", false, strRejectReasonDetails);
        }

        // Check that all transactions are unexpired
        if (IsExpiredTx(tx, nHeight))
        {
            // Don't increase banscore if the transaction only just expired
            const int expiredDosLevel = IsExpiredTx(tx, nHeight - 1) ? dosLevelConstricting : 0;
            strRejectReasonDetails = strprintf("transaction is expired at %u, height=%d", tx.nExpiryHeight, nHeight);
            return state.DoS(
                expiredDosLevel,
                error("%s: %s", __FUNCTION__, strRejectReasonDetails), 
                REJECT_INVALID, "tx-overwinter-expired", false, strRejectReasonDetails);
        }

        // Rules that became inactive after Sapling activation.
        if (!saplingActive)
        {
            // Reject transactions with invalid version
            // OVERWINTER_MIN_TX_VERSION is checked against as a non-contextual check.
            if (tx.nVersion > OVERWINTER_MAX_TX_VERSION)
            {
                strRejectReasonDetails = strprintf("overwinter version too high, height=%d", nHeight);
                return state.DoS(
                    dosLevelPotentiallyRelaxing,
                    error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                    REJECT_INVALID, "bad-tx-overwinter-version-too-high", false, strRejectReasonDetails);
            }

            // Reject transactions with non-Overwinter version group ID
            if (tx.nVersionGroupId != OVERWINTER_VERSION_GROUP_ID)
            {
                strRejectReasonDetails = strprintf("invalid Overwinter tx version group id, height=%d", nHeight);
                return state.DoS(
                    dosLevelPotentiallyRelaxing,
                    error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                    REJECT_INVALID, "bad-overwinter-tx-version-group-id", false, strRejectReasonDetails);
            }
        }
    }

    // Rules that apply to Sapling and later
    if (saplingActive)
    {
        if (tx.nVersionGroupId == SAPLING_VERSION_GROUP_ID)
        {
            // Reject transactions with invalid version
            if (tx.fOverwintered && tx.nVersion < SAPLING_MIN_TX_VERSION)
            {
                strRejectReasonDetails = strprintf("Sapling version too low, height=%d", nHeight);
                return state.DoS(
                    dosLevelConstricting,
                    error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                    REJECT_INVALID, "bad-tx-sapling-version-too-low", false, strRejectReasonDetails);
            }

            // Reject transactions with invalid version
            if (tx.fOverwintered && tx.nVersion > SAPLING_MAX_TX_VERSION)
            {
                strRejectReasonDetails = strprintf("Sapling version too high, height=%d", nHeight);
                return state.DoS(
                    dosLevelPotentiallyRelaxing,
                    error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                    REJECT_INVALID, "bad-tx-sapling-version-too-high", false, strRejectReasonDetails);
            }
        }
        else
        {
            // Reject transactions with non-Sapling version group ID
            if (tx.fOverwintered)
            {
                strRejectReasonDetails = strprintf("invalid Sapling tx version group id, height=%d", nHeight);
                return state.DoS(
                    dosLevelPotentiallyRelaxing,
                    error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                    REJECT_INVALID, "bad-sapling-tx-version-group-id", false, strRejectReasonDetails);
            }
        }
    } else {
        // Rules that apply generally before Sapling. These were previously 
        // noncontextual checks that became contextual after Sapling activation.
        
        // Size limits
        static_assert(MAX_BLOCK_SIZE > MAX_TX_SIZE_BEFORE_SAPLING); // sanity
        if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_TX_SIZE_BEFORE_SAPLING)
        {
            strRejectReasonDetails = strprintf("size limits failed, height=%d", nHeight);
            return state.DoS(
                dosLevelPotentiallyRelaxing,
                error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                REJECT_INVALID, "bad-txns-oversize", false, strRejectReasonDetails);
        }
    }

    uint256 dataToBeSigned;

    if (!tx.vShieldedSpend.empty() || !tx.vShieldedOutput.empty())
    {
        auto consensusBranchId = CurrentEpochBranchId(nHeight, consensusParams);
        // Empty output script.
        CScript scriptCode;
        try
        {
            dataToBeSigned = SignatureHash(scriptCode, tx, NOT_AN_INPUT, to_integral_type(SIGHASH::ALL), 0, consensusBranchId);
        } catch (logic_error ex)
        {
            strRejectReasonDetails = strprintf("error computing signature hash, height=%d", nHeight);
            return state.DoS(
                DOS_LEVEL_BLOCK,
                error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                REJECT_INVALID, "error-computing-signature-hash", false, strRejectReasonDetails);
        }

        auto ctx = librustzcash_sapling_verification_ctx_init();
        for (const auto &spend : tx.vShieldedSpend)
        {
            if (!librustzcash_sapling_check_spend(
                ctx,
                spend.cv.begin(),
                spend.anchor.begin(),
                spend.nullifier.begin(),
                spend.rk.begin(),
                spend.zkproof.data(),
                spend.spendAuthSig.data(),
                dataToBeSigned.begin()
            ))
            {
                librustzcash_sapling_verification_ctx_free(ctx);
                strRejectReasonDetails = strprintf("Sapling spend description invalid, height=%d", nHeight);
                return state.DoS(
                    dosLevelPotentiallyRelaxing,
                    error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                    REJECT_INVALID, "bad-txns-sapling-spend-description-invalid", false, strRejectReasonDetails);
            }
        }

        for (const auto &output : tx.vShieldedOutput)
        {
            if (!librustzcash_sapling_check_output(
                ctx,
                output.cv.begin(),
                output.cm.begin(),
                output.ephemeralKey.begin(),
                output.zkproof.data()
            ))
            {
                librustzcash_sapling_verification_ctx_free(ctx);
                // This should be a non-contextual check, but we check it here
                // as we need to pass over the outputs anyway in order to then
                // call librustzcash_sapling_final_check().
                strRejectReasonDetails = strprintf("Sapling output description invalid, height=%d", nHeight);
                return state.DoS(
                    DOS_LEVEL_BLOCK,
                    error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                    REJECT_INVALID, "bad-txns-sapling-output-description-invalid", false, strRejectReasonDetails);
            }
        }

        if (!librustzcash_sapling_final_check(
            ctx,
            tx.valueBalance,
            tx.bindingSig.data(),
            dataToBeSigned.begin()
        ))
        {
            librustzcash_sapling_verification_ctx_free(ctx);
            strRejectReasonDetails = strprintf("Sapling binding signature invalid, height=%d", nHeight);
            return state.DoS(
                dosLevelPotentiallyRelaxing,
                error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                REJECT_INVALID, "bad-txns-sapling-binding-signature-invalid", false, strRejectReasonDetails);
        }

        librustzcash_sapling_verification_ctx_free(ctx);
    }
    
    // Check Pastel Ticket transactions
    const auto tv = CPastelTicketProcessor::ValidateIfTicketTransaction(state, nHeight, tx, pindexPrev);
    if (tv.state == TICKET_VALIDATION_STATE::NOT_TICKET || tv.state == TICKET_VALIDATION_STATE::VALID)
        return true;
    if (tv.state == TICKET_VALIDATION_STATE::MISSING_INPUTS)
    {
        strRejectReasonDetails = strprintf("missing dependent transactions, height=%d. %s", nHeight, tv.errorMsg);
        return state.DoS(0, warning_msg("%s: %s", __FUNCTION__, strRejectReasonDetails),
            REJECT_MISSING_INPUTS, "tx-missing-inputs", false, strRejectReasonDetails);
    }
    strRejectReasonDetails = strprintf("invalid ticket transaction, height=%d. %s", nHeight, tv.errorMsg);
    return state.DoS(10, error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                     REJECT_INVALID, "bad-tx-invalid-ticket", false, strRejectReasonDetails);
}

/**
 * Contextual validation of the block and its transactions.
 * 
 * \param block - block to check
 * \param state - validation state
 * \param chainparams - chain parameters
 * \param pindexPrev - pointer to the previous block index
 * \return true if the block is valid, false otherwise
 */
bool ContextualCheckBlock(
    const CBlock& block,
    CValidationState& state,
    const CChainParams& chainparams,
    const CBlockIndex *pindexPrev)
{
    const int nHeight = !pindexPrev ? 0 : pindexPrev->nHeight + 1;
    const auto& consensusParams = chainparams.GetConsensus();
    string strRejectReasonDetails;

    // Check that all transactions are finalized
    for (const auto& tx : block.vtx)
    {
        // Check transaction contextually against consensus rules at block height
        if (!ContextualCheckTransaction(tx, state, chainparams, nHeight, pindexPrev, fnIsInitialBlockDownload))
            return false; // Failure reason has been set in validation state object

        int nLockTimeFlags = 0;
        const int64_t nLockTimeCutoff = (nLockTimeFlags & LOCKTIME_MEDIAN_TIME_PAST) ? pindexPrev->GetMedianTimePast() : block.GetBlockTime(); //-V547
        if (!IsFinalTx(tx, nHeight, nLockTimeCutoff))
        {
            strRejectReasonDetails = strprintf("contains non-final transaction, height=%d", nHeight);
            return state.DoS(10, error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                REJECT_INVALID, "bad-txns-nonfinal", false, strRejectReasonDetails);
        }
    }

    // Enforce BIP 34 rule that the coinbase starts with serialized block height.
    // In Zcash this has been enforced since launch, except that the genesis
    // block didn't include the height in the coinbase (see Zcash protocol spec
    // section '6.8 Bitcoin Improvement Proposals').
    if (nHeight > 0)
    {
        CScript expect = CScript() << nHeight;
        if (block.vtx[0].vin[0].scriptSig.size() < expect.size() ||
            !equal(expect.begin(), expect.end(), block.vtx[0].vin[0].scriptSig.begin()))
        {
            strRejectReasonDetails = strprintf("block height mismatch in coinbase, height=%d", nHeight);
            return state.DoS(100, error("%s: %s", __FUNCTION__, strRejectReasonDetails),
                REJECT_INVALID, "bad-cb-height", false, strRejectReasonDetails);
        }
    }

    return true;
}

// Set default values of new CMutableTransaction based on consensus rules at given height.
CMutableTransaction CreateNewContextualCMutableTransaction(const Consensus::Params& consensusParams, const uint32_t nHeight)
{
    CMutableTransaction mtx;

    const bool isOverwintered = NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_OVERWINTER);
    if (isOverwintered)
    {
        mtx.fOverwintered = true;
        mtx.nExpiryHeight = nHeight + gl_ChainOptions.expiryDelta;

        // NOTE: If the expiry height crosses into an incompatible consensus epoch, and it is changed to the last block
        // of the current epoch (see below: Overwinter->Sapling), the transaction will be rejected if it falls within
        // the expiring soon threshold of 3 blocks (for DoS mitigation) based on the current height.
        // TODO: Generalise this code so behaviour applies to all post-Overwinter epochs
        if (NetworkUpgradeActive(nHeight, consensusParams, Consensus::UpgradeIndex::UPGRADE_SAPLING))
        {
            mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
            mtx.nVersion = SAPLING_TX_VERSION;
        } else {
            mtx.nVersionGroupId = OVERWINTER_VERSION_GROUP_ID;
            mtx.nVersion = OVERWINTER_TX_VERSION;
            mtx.nExpiryHeight = min(
                mtx.nExpiryHeight,
                consensusParams.vUpgrades[to_integral_type(Consensus::UpgradeIndex::UPGRADE_SAPLING)].nActivationHeight - 1);
        }
    }
    return mtx;
}

bool CheckTransaction(const CTransaction& tx, CValidationState &state,
                      libzcash::ProofVerifier& verifier)
{
    // Don't count coinbase transactions because mining skews the count
    if (!tx.IsCoinBase())
        transactionsValidated.increment();

    return CheckTransactionWithoutProofVerification(tx, state);
}

bool CheckTransactionWithoutProofVerification(const CTransaction& tx, CValidationState &state)
{
    // Basic checks that don't depend on any context

    /**
     * Previously:
     * 1. The consensus rule below was:
     *        if (tx.nVersion < SPROUT_MIN_TX_VERSION) { ... }
     *    which checked if tx.nVersion fell within the range:
     *        INT32_MIN <= tx.nVersion < SPROUT_MIN_TX_VERSION
     * 2. The parser allowed tx.nVersion to be negative
     *
     * Now:
     * 1. The consensus rule checks to see if tx.Version falls within the range:
     *        0 <= tx.nVersion < SPROUT_MIN_TX_VERSION
     * 2. The previous consensus rule checked for negative values within the range:
     *        INT32_MIN <= tx.nVersion < 0
     *    This is unnecessary for Overwinter transactions since the parser now
     *    interprets the sign bit as fOverwintered, so tx.nVersion is always >=0,
     *    and when Overwinter is not active ContextualCheckTransaction rejects
     *    transactions with fOverwintered set.  When fOverwintered is set,
     *    this function and ContextualCheckTransaction will together check to
     *    ensure tx.nVersion avoids the following ranges:
     *        0 <= tx.nVersion < OVERWINTER_MIN_TX_VERSION
     *        OVERWINTER_MAX_TX_VERSION < tx.nVersion <= INT32_MAX
     */
    string strRejectReasonDetails;
    if (!tx.fOverwintered && tx.nVersion < SPROUT_MIN_TX_VERSION)
    {
        strRejectReasonDetails = "version too low";
        return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
            REJECT_INVALID, "bad-txns-version-too-low", false, strRejectReasonDetails);
    }
    else if (tx.fOverwintered) {
        if (tx.nVersion < OVERWINTER_MIN_TX_VERSION)
        {
            strRejectReasonDetails = "overwinter version too low";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-tx-overwinter-version-too-low", false, strRejectReasonDetails);
        }
        if (tx.nVersionGroupId != OVERWINTER_VERSION_GROUP_ID &&
                tx.nVersionGroupId != SAPLING_VERSION_GROUP_ID)
        {
            strRejectReasonDetails = "unknown tx version group id";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-tx-version-group-id", false, strRejectReasonDetails);
        }
        if (tx.nExpiryHeight >= TX_EXPIRY_HEIGHT_THRESHOLD)
        {
            strRejectReasonDetails = "expiry height too high";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-tx-expiry-height-too-high", false, strRejectReasonDetails);
        }
    }

    // Transactions containing empty `vin` must have non-empty `vShieldedSpend`.
    if (tx.vin.empty() && tx.vShieldedSpend.empty())
    {
        strRejectReasonDetails = "vin empty";
        return state.DoS(10, error("CheckTransaction(): %s", strRejectReasonDetails),
            REJECT_INVALID, "bad-txns-vin-empty", false, strRejectReasonDetails);
    }
    // Transactions containing empty `vout` must have non-empty `vShieldedOutput`.
    if (tx.vout.empty() && tx.vShieldedOutput.empty())
    {
        strRejectReasonDetails = "vout empty";
        return state.DoS(10, error("CheckTransaction(): %s", strRejectReasonDetails),
            REJECT_INVALID, "bad-txns-vout-empty", false, strRejectReasonDetails);
    }

    // Size limits
    static_assert(MAX_BLOCK_SIZE >= MAX_TX_SIZE_AFTER_SAPLING); // sanity
    static_assert(MAX_TX_SIZE_AFTER_SAPLING > MAX_TX_SIZE_BEFORE_SAPLING); // sanity
    if (::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION) > MAX_TX_SIZE_AFTER_SAPLING)
    {
        strRejectReasonDetails = "size limits failed";
        return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
            REJECT_INVALID, "bad-txns-oversize", false, strRejectReasonDetails);
    }

    // Check for negative or overflow output values
    CAmount nValueOut = 0;
    for (const auto &txout : tx.vout)
    {
        if (txout.nValue < 0)
        {
            strRejectReasonDetails = "txout.nValue negative";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-txns-vout-negative", false, strRejectReasonDetails);
        }
        if (txout.nValue > MAX_MONEY)
        {
            strRejectReasonDetails = "txout.nValue too high";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-txns-vout-toolarge", false, strRejectReasonDetails);
        }
        nValueOut += txout.nValue;
        if (!MoneyRange(nValueOut))
        {
            strRejectReasonDetails = "txout total out of range";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-txns-txouttotal-toolarge", false, strRejectReasonDetails);
        }
    }

    // Check for non-zero valueBalance when there are no Sapling inputs or outputs
    if (tx.vShieldedSpend.empty() && tx.vShieldedOutput.empty() && tx.valueBalance != 0)
    {
        strRejectReasonDetails = "tx.valueBalance has no sources or sinks";
        return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
            REJECT_INVALID, "bad-txns-valuebalance-nonzero", false, strRejectReasonDetails);
    }

    // Check for overflow valueBalance
    if (tx.valueBalance > MAX_MONEY || tx.valueBalance < -MAX_MONEY)
    {
        strRejectReasonDetails = "abs(tx.valueBalance) too large";
        return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
            REJECT_INVALID, "bad-txns-valuebalance-toolarge", false, strRejectReasonDetails);
    }

    if (tx.valueBalance <= 0) {
        // NB: negative valueBalance "takes" money from the transparent value pool just as outputs do
        nValueOut += -tx.valueBalance;

        if (!MoneyRange(nValueOut))
        {
            strRejectReasonDetails = "txout total out of range";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-txns-txouttotal-toolarge", false, strRejectReasonDetails);
        }
    }

    // Check for duplicate inputs
    set<COutPoint> vInOutPoints;
    for (const auto& txin : tx.vin)
    {
        if (vInOutPoints.count(txin.prevout))
        {
            strRejectReasonDetails = "duplicate inputs";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-txns-inputs-duplicate", false, strRejectReasonDetails);
        }
        vInOutPoints.insert(txin.prevout);
    }

    // Check for duplicate sapling nullifiers in this transaction
    {
        set<uint256> vSaplingNullifiers;
        for (const auto& spend_desc : tx.vShieldedSpend)
        {
            if (vSaplingNullifiers.count(spend_desc.nullifier))
            {
                strRejectReasonDetails = "duplicate nullifiers";
                return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                    REJECT_INVALID, "bad-spend-description-nullifiers-duplicate", false, strRejectReasonDetails);
            }

            vSaplingNullifiers.insert(spend_desc.nullifier);
        }
    }

    if (tx.IsCoinBase())
    {
        // A coinbase transaction cannot have spend descriptions or output descriptions
        if (tx.vShieldedSpend.size() > 0)
        {
            strRejectReasonDetails = "coinbase has spend descriptions";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-cb-has-spend-description", false, strRejectReasonDetails);
        }
        if (tx.vShieldedOutput.size() > 0)
        {
            strRejectReasonDetails = "coinbase has output descriptions";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-cb-has-output-description", false, strRejectReasonDetails);
        }

        if (tx.vin[0].scriptSig.size() < 2 || tx.vin[0].scriptSig.size() > 100)
        {
            strRejectReasonDetails = "coinbase script size";
            return state.DoS(100, error("CheckTransaction(): %s", strRejectReasonDetails),
                REJECT_INVALID, "bad-cb-length", false, strRejectReasonDetails);
        }
    }
    else
    {
        for (const auto& txin : tx.vin)
        {
            if (txin.prevout.IsNull())
            {
                strRejectReasonDetails = "prevout is null";
                return state.DoS(10, error("CheckTransaction(): %s", strRejectReasonDetails),
                    REJECT_INVALID, "bad-txns-prevout-null", false, strRejectReasonDetails);
            }
        }
    }

    return true;
}

bool AcceptToMemoryPool(
    const CChainParams& chainparams,
    CTxMemPool& pool, CValidationState& state, 
    const CTransaction& tx, bool fLimitFree,
    bool* pfMissingInputs, bool fRejectAbsurdFee)
{
    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    int nextBlockHeight = gl_nChainHeight + 1;
    const auto& consensusParams = chainparams.GetConsensus();
    auto consensusBranchId = CurrentEpochBranchId(nextBlockHeight, consensusParams);

    // Node operator can choose to reject tx by number of transparent inputs
    static_assert(numeric_limits<size_t>::max() >= numeric_limits<uint64_t>::max(), "size_t too small");

    const uint256 hash = tx.GetHash();
    string sFuncLog = strprintf("AcceptToMemoryPool [%s]", hash.ToString());
    string strRejectReasonDetails;

    auto verifier = libzcash::ProofVerifier::Strict();
    if (!CheckTransaction(tx, state, verifier))
        return error("%s: CheckTransaction failed. %s", sFuncLog, state.GetRejectReason());

    // Check transaction contextually against the set of consensus rules which apply in the next block to be mined.
    if (!ContextualCheckTransaction(tx, state, chainparams, nextBlockHeight, nullptr, fnIsInitialBlockDownload))
    {
        if (state.IsRejectCode(REJECT_MISSING_INPUTS))
        {
            if (pfMissingInputs)
                *pfMissingInputs = true;
            return warning_msg("%s: ContextualCheckTransaction missing inputs", sFuncLog);
        }
        return error("%s: ContextualCheckTransaction failed. %s", sFuncLog, state.GetRejectReason());
    }

    // DoS mitigation: reject transactions expiring soon
    // Note that if a valid transaction belonging to the wallet is in the mempool and the node is shutdown,
    // upon restart, CWalletTx::AcceptToMemoryPool() will be invoked which might result in rejection.
    if (IsExpiringSoonTx(tx, nextBlockHeight))
    {
        strRejectReasonDetails = strprintf("transaction is expiring soon at height=%d", nextBlockHeight);
        return state.DoS(0, error("%s: %s", sFuncLog, strRejectReasonDetails),
            REJECT_INVALID, "tx-expiring-soon", false, strRejectReasonDetails);
    }

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
    {
        strRejectReasonDetails = "coinbase as individual tx";
        return state.DoS(100, error("%s: %s", sFuncLog, strRejectReasonDetails),
            REJECT_INVALID, "coinbase", false, strRejectReasonDetails);
    }

    // Rather not work on nonstandard transactions (unless -testnet/-regtest)
    string reason;
    if (chainparams.RequireStandard() && !IsStandardTx(tx, reason, chainparams, nextBlockHeight))
    {
        strRejectReasonDetails = strprintf("nonstandard transaction: %s", reason);
        return state.DoS(0, error("%s: %s", sFuncLog, strRejectReasonDetails),
            REJECT_NONSTANDARD, reason, false, strRejectReasonDetails);
    }

    // Only accept nLockTime-using transactions that can be mined in the next
    // block; we don't want our mempool filled up with transactions that can't
    // be mined yet.
    if (!CheckFinalTx(tx, STANDARD_LOCKTIME_VERIFY_FLAGS))
    {
        strRejectReasonDetails = "non-final transaction";
        return state.DoS(0, error("%s: %s", sFuncLog, strRejectReasonDetails),
            REJECT_NONSTANDARD, "non-final", false, strRejectReasonDetails);
    }

    // is it already in the memory pool?
    if (pool.exists(hash))
    {
        return warning_msg("%s: duplication transaction", sFuncLog);
    }

    // Check for conflicts with in-memory transactions
    {
        LOCK(pool.cs); // protect pool.mapNextTx
        for (const auto &txIn : tx.vin)
        {
            const COutPoint &outpoint = txIn.prevout;
            if (pool.mapNextTx.count(outpoint))
            {
                // Disable replacement feature for now
                return warning_msg("%s: transaction with the same input already exists in the memory pool", sFuncLog);
            }
        }
        for (const auto &spendDescription : tx.vShieldedSpend)
        {
            if (pool.nullifierExists(spendDescription.nullifier, SAPLING))
                return warning_msg("%s: nullifier exists for the shielded spend in the memory pool", sFuncLog);
        }
    } // end of mempool locked section (pool.cs)

    {
        CCoinsView dummy;
        CCoinsViewCache view(&dummy);

        CAmount nValueIn = 0;
        {
            LOCK(pool.cs);
            CCoinsViewMemPool viewMemPool(gl_pCoinsTip.get(), pool);
            view.SetBackend(viewMemPool);

            // do we already have it?
            if (view.HaveCoins(hash))
            {
                return warning_msg("%s: transaction already exists in the mempool coins cache", sFuncLog);
            }

            // do all inputs exist?
            // Note that this does not check for the presence of actual outputs (see the next check for that),
            // and only helps with filling in pfMissingInputs (to determine missing vs spent).
            for (const auto &txin : tx.vin)
            {
                if (!view.HaveCoins(txin.prevout.hash))
                {
                    if (pfMissingInputs)
                        *pfMissingInputs = true;
                    return false;
                }
            }

            // are the actual inputs available?
            if (!view.HaveInputs(tx))
            {
                strRejectReasonDetails = "inputs already spent";
                return state.Invalid(error("%s: %s", sFuncLog, strRejectReasonDetails),
                    REJECT_DUPLICATE, "bad-txns-inputs-spent", strRejectReasonDetails);
            }

            // are the sapling spends requirements met in tx(valid anchors/nullifiers)?
            if (!view.HaveShieldedRequirements(tx))
            {
                strRejectReasonDetails = "sapling spends requirements not met";
                return state.Invalid(error("%s: %s", sFuncLog, strRejectReasonDetails),
                    REJECT_DUPLICATE, "bad-txns-shielded-requirements-not-met", strRejectReasonDetails);
            }

            // Bring the best block into scope
            view.GetBestBlock();

            nValueIn = view.GetValueIn(tx);

            // we have all inputs cached now, so switch back to dummy, so we don't need to keep lock on mempool
            view.SetBackend(dummy);
        } // end of mempool locked section (pool.cs)

        // Check for non-standard pay-to-script-hash in inputs
        if (chainparams.RequireStandard() && !AreInputsStandard(tx, view, consensusBranchId))
            return error("%s: nonstandard transaction input", sFuncLog);

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_STANDARD_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        unsigned int nSigOps = GetLegacySigOpCount(tx);
        nSigOps += GetP2SHSigOpCount(tx, view);
        if (nSigOps > MAX_STANDARD_TX_SIGOPS)
        {
            strRejectReasonDetails = strprintf("too many sigops %u > %u", nSigOps, MAX_STANDARD_TX_SIGOPS);
            return state.DoS(0, error("%s: %s", sFuncLog, strRejectReasonDetails),
                REJECT_NONSTANDARD, "bad-txns-too-many-sigops", false, strRejectReasonDetails);
        }

        CAmount nValueOut = tx.GetValueOut();
        CAmount nFees = nValueIn-nValueOut;
        double dPriority = view.GetPriority(tx, chainActive.Height());

        // Keep track of transactions that spend a coinbase, which we re-scan
        // during reorgs to ensure COINBASE_MATURITY is still met.
        bool fSpendsCoinbase = false;
        for (const auto &txin : tx.vin)
        {
            const CCoins *coins = view.AccessCoins(txin.prevout.hash);
            if (coins->IsCoinBase())
            {
                fSpendsCoinbase = true;
                break;
            }
        }

        // Grab the branch ID we expect this transaction to commit to. We don't
        // yet know if it does, but if the entry gets added to the mempool, then
        // it has passed ContextualCheckInputs and therefore this is correct.
        consensusBranchId = CurrentEpochBranchId(gl_nChainHeight + 1, consensusParams);

        CTxMemPoolEntry entry(tx, nFees, GetTime(), dPriority, gl_nChainHeight, mempool.HasNoInputsOf(tx), fSpendsCoinbase, consensusBranchId);
        const size_t nTxSize = entry.GetTxSize();

        // Accept a tx if it contains joinsplits and has at least the default fee specified by z_sendmany.
        // Don't accept it if it can't get into a block
        CAmount txMinFee = GetMinRelayFee(tx, nTxSize, true);
        if (fLimitFree && nFees < txMinFee)
        {
            strRejectReasonDetails = strprintf("not enough fees %" PRId64 " < % " PRId64, nFees, txMinFee);
            return state.DoS(0, error("%s: %s", sFuncLog, strRejectReasonDetails),
                REJECT_INSUFFICIENTFEE, "insufficient fee", false, strRejectReasonDetails);
        }

        // Require that free transactions have sufficient priority to be mined in the next block.
        if (GetBoolArg("-relaypriority", false) && nFees < gl_ChainOptions.minRelayTxFee.GetFee(nTxSize) && 
            !AllowFree(view.GetPriority(tx, chainActive.Height() + 1)))
        {
            strRejectReasonDetails = "insufficient priority to be mined in the next block";
            return state.DoS(0, error("%s: %s", sFuncLog, strRejectReasonDetails),
                REJECT_INSUFFICIENTFEE, "insufficient priority", false, strRejectReasonDetails);
        }

        // Continuously rate-limit free (really, very-low-fee) transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (fLimitFree && nFees < gl_ChainOptions.minRelayTxFee.GetFee(nTxSize))
        {
            static CCriticalSection csFreeLimiter;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            LOCK(csFreeLimiter);

            // Use an exponentially decaying ~10-minute window:
            dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
            nLastTime = nNow;
            // -limitfreerelay unit is thousand-bytes-per-minute
            // At default rate it would take over a month to fill 1GB
            if (dFreeCount >= GetArg("-limitfreerelay", 15) * 10 * 1000)
            {
                strRejectReasonDetails = "free transaction rejected by rate limiter";
                return state.DoS(0, error("%s: %s", sFuncLog, strRejectReasonDetails),
                    REJECT_INSUFFICIENTFEE, "rate limited free transaction", false, strRejectReasonDetails);
            }
            LogPrint("mempool", "Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nTxSize);
            dFreeCount += nTxSize;
        }

        if (fRejectAbsurdFee && nFees > gl_ChainOptions.minRelayTxFee.GetFee(nTxSize) * 10000)
        {
            string errmsg = strprintf("absurdly high fees %s, %" PRId64 " > %" PRId64,
                                      hash.ToString(),
                                      nFees, gl_ChainOptions.minRelayTxFee.GetFee(nTxSize) * 10000);
            LogPrint("mempool", errmsg.c_str());
            return state.Error(strprintf("%s: %s", sFuncLog, errmsg));
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        PrecomputedTransactionData txdata(tx);
        if (!ContextualCheckInputs(tx, state, view, true, STANDARD_SCRIPT_VERIFY_FLAGS, true, txdata, consensusParams, consensusBranchId))
        {
            return error("%s: ConnectInputs failed", sFuncLog);
        }

        // Check again against just the consensus-critical mandatory script
        // verification flags, in case of bugs in the standard flags that cause
        // transactions to pass as valid when they're actually invalid. For
        // instance the STRICTENC flag was incorrectly allowing certain
        // CHECKSIG NOT scripts to pass, even though they were invalid.
        //
        // There is a similar check in CreateNewBlock() to prevent creating
        // invalid blocks, however allowing such transactions into the mempool
        // can be exploited as a DoS attack.
        if (!ContextualCheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true, txdata, consensusParams, consensusBranchId))
        {
            return error("%s: BUG! PLEASE REPORT THIS! ConnectInputs failed against MANDATORY but not STANDARD flags", sFuncLog);
        }

        // Store transaction in memory
        pool.addUnchecked(hash, entry, !fnIsInitialBlockDownload(consensusParams));

        // insightexplorer: add memory address index
        if (fAddressIndex)
            pool.addAddressIndex(entry, view);

        // insightexplorer: add memory spent index
        if (fSpentIndex)
            pool.addSpentIndex(entry, view);
    }

    SyncWithWallets(tx, nullptr);

    return true;
}
