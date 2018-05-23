// Copyright (c) 2018 airk42
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"

#include "mnode-validation.h"
#include "mnode-controller.h"

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
    if (coins.vout[outpoint.n].IsNull())
        return false;
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

    BOOST_FOREACH(COutput& out, vPossibleCoins)
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

    CTxDestination address1;
    ExtractDestination(pubScript, address1);
    CBitcoinAddress address2(address1);

    CKeyID keyID;
    if (!address2.GetKeyID(keyID)) {
        LogPrintf("GetOutpointAndKeysFromOutput -- Address does not refer to a key\n");
        return false;
    }

    if (!pWalletMain->GetKey(keyID, keyRet)) {
        LogPrintf ("GetOutpointAndKeysFromOutput -- Private key for address is not known\n");
        return false;
    }

    pubKeyRet = keyRet.GetPubKey();
    return true;
}
#endif

void FillOtherBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutMasternodeRet, CTxOut& txoutGovernanceRet)
{
    // Fill Governance payment
    masterNodeCtrl.masternodeGovernance.FillGovernancePayment(txNew, nBlockHeight, blockReward, txoutGovernanceRet);
    
    // FILL BLOCK PAYEE WITH MASTERNODE PAYMENT
    masterNodeCtrl.masternodePayments.FillMasterNodePayment(txNew, nBlockHeight, blockReward, txoutMasternodeRet);

    LogPrint("mnpayments", "FillOtherBlockPayments -- nBlockHeight %d blockReward %lld txoutMasternodeRet %s txoutGovernanceRet %s txNew %s",
                            nBlockHeight, blockReward, txoutMasternodeRet.ToString(), txoutGovernanceRet.ToString(), txNew.ToString());
}