// Copyright (c) 2018 airk42
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mnode/mnode-validation.h"
#include "mnode/mnode-controller.h"

/*
Wrappers for BlockChain specific logic
*/

bool GetBlockHash(uint256& hashRet, int nBlockHeight)
{
    LOCK(cs_main);
    if(chainActive.Tip() == NULL) return false;
    if(nBlockHeight < -1 || nBlockHeight > chainActive.Height()) return false;
    if(nBlockHeight == -1) nBlockHeight = chainActive.Height();
    hashRet = chainActive[nBlockHeight]->GetBlockHash();
    return true;
}

bool GetUTXOCoin(const COutPoint& outpoint, CCoins& coins)
{
    LOCK(cs_main);
    if (!pcoinsTip->GetCoins(outpoint.hash, coins))
        return false;
    if (outpoint.n >= coins.vout.size() || coins.vout[outpoint.n].IsNull())
        return false; // SPENT!!! (spent outputs are .IsNull(); spent outputs at the end of the array are dropped!!!)
    return true;
}

int GetUTXOHeight(const COutPoint& outpoint)
{
    // -1 means UTXO is yet unknown or already spent
    CCoins coins;
    return GetUTXOCoin(outpoint, coins) ? coins.nHeight : -1;
}

int GetUTXOConfirmations(const COutPoint& outpoint)
{
    // -1 means UTXO is yet unknown or already spent
    LOCK(cs_main);
    int nPrevoutHeight = GetUTXOHeight(outpoint);
    return (nPrevoutHeight > -1 && chainActive.Tip()) ? chainActive.Height() - nPrevoutHeight + 1 : -1;
}

#ifdef ENABLE_WALLET
bool GetMasternodeOutpointAndKeys(CWallet* pWalletMain, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex || pWalletMain == NULL) return false;

    // Find possible candidates
    std::vector<COutput> vPossibleCoins;
    pWalletMain->AvailableCoins(vPossibleCoins, true, NULL, false, true, masterNodeCtrl.MasternodeCollateral, true);
    if(vPossibleCoins.empty()) {
        LogPrintf("GetMasternodeOutpointAndKeys -- Could not locate any valid masternode vin\n");
        return false;
    }

    if(strTxHash.empty()) // No output specified, select the first one
        return GetOutpointAndKeysFromOutput(pWalletMain, vPossibleCoins[0], outpointRet, pubKeyRet, keyRet);

    // Find specific vin
    uint256 txHash = uint256S(strTxHash);
    int nOutputIndex = atoi(strOutputIndex.c_str());

    for (const auto& out : vPossibleCoins)
        if(out.tx->GetHash() == txHash && out.i == nOutputIndex) // found it!
            return GetOutpointAndKeysFromOutput(pWalletMain, out, outpointRet, pubKeyRet, keyRet);

    LogPrintf("GetMasternodeOutpointAndKeys -- Could not locate specified masternode vin\n");
    return false;
}

bool GetOutpointAndKeysFromOutput(CWallet* pWalletMain, const COutput& out, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet)
{
    // wait for reindex and/or import to finish
    if (fImporting || fReindex || pWalletMain == NULL) return false;

    CScript pubScript;

    outpointRet = COutPoint(out.tx->GetHash(), out.i);
    pubScript = out.tx->vout[out.i].scriptPubKey; // the inputs PubKey

    CTxDestination dest;
    ExtractDestination(pubScript, dest);

    const CKeyID *keyID = std::get_if<CKeyID>(&dest);
    if (!keyID) {
        LogPrintf("GetOutpointAndKeysFromOutput -- Address does not refer to a key\n");
        return false;
    }

    if (!pWalletMain->GetKey(*keyID, keyRet)) {
        LogPrintf ("GetOutpointAndKeysFromOutput -- Private key for address is not known\n");
        return false;
    }

    pubKeyRet = keyRet.GetPubKey();
    return true;
}
#endif

void FillOtherBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, 
                            CTxOut& txoutMasternodeRet, CTxOut& txoutGovernanceRet)
{
    // Fill Governance payment
    //TODO: Fix governance tickets processing
    masterNodeCtrl.masternodeGovernance.FillGovernancePayment(txNew, nBlockHeight, blockReward, txoutGovernanceRet);
    
    // FILL BLOCK PAYEE WITH MASTERNODE PAYMENT
    masterNodeCtrl.masternodePayments.FillMasterNodePayment(txNew, nBlockHeight, blockReward, txoutMasternodeRet);

    LogPrint("mnpayments", "FillOtherBlockPayments -- nBlockHeight %d blockReward %lld txoutMasternodeRet %s txoutGovernanceRet %s txNew %s",
                            nBlockHeight, blockReward, txoutMasternodeRet.ToString(), txoutGovernanceRet.ToString(), txNew.ToString());
}

/**
* IsBlockValid
*
*   Determine if coinbase outgoing created money is the correct value
*
*   Called from ConnectBlock
*
*   Governance payments in each CB should not exceed the amount in the current voted payment
*/
bool IsBlockValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet)
{
    strErrorRet = "";

    //1. less then total reward per block
    if (block.vtx[0].GetValueOut() > blockReward) {
        strErrorRet = strprintf("coinbase pays too much at height %d (actual=%d vs limit=%d), exceeded block reward, budgets are disabled",
                                nBlockHeight, block.vtx[0].GetValueOut(), blockReward);
        return false;
    }

    if(!masterNodeCtrl.masternodeSync.IsSynced()) {
        //there is no data to use to check anything, let's just accept the longest chain
        if(fDebug) LogPrintf("IsBlockValid -- WARNING: Client not synced, skipping block payee checks\n");
        return true;
    }

    //3. check governance and masternode payments and payee
    if(!masterNodeCtrl.masternodePayments.IsTransactionValid(block.vtx[0], nBlockHeight)) {
        strErrorRet = strprintf("InValid coinbase transaction (MN payment) at height %d: %s", nBlockHeight, block.vtx[0].ToString());
        return false;
    }
    if(!masterNodeCtrl.masternodeGovernance.IsTransactionValid(block.vtx[0], nBlockHeight)) {
        strErrorRet = strprintf("InValid coinbase transaction (governance payment) at height %d: %s", nBlockHeight, block.vtx[0].ToString());
        return false;
    }

    // there was no MN nor Govenrance payments on this block
    LogPrint("mnpayments", "IsBlockValid -- Valid masternode payment at height %d: %s", nBlockHeight, block.vtx[0].ToString());
    return true;
}
