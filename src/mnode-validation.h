// Copyright (c) 2018 airk42
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef MASTERNODEVALIDATION_H
#define MASTERNODEVALIDATION_H

#include <string>

#include "main.h"

#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#endif


bool GetBlockHash(uint256& hashRet, int nBlockHeight);
bool GetUTXOCoin(const COutPoint& outpoint, CCoins& coins);
int GetUTXOHeight(const COutPoint& outpoint);
int GetUTXOConfirmations(const COutPoint& outpoint);

void FillOtherBlockPayments(CMutableTransaction& txNew, int nBlockHeight, CAmount blockReward, CTxOut& txoutMasternodeRet, CTxOut& txoutGovernanceRet);

#ifdef ENABLE_WALLET
    bool GetMasternodeOutpointAndKeys(CWallet* pwalletMain, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet, std::string strTxHash, std::string strOutputIndex);
    bool GetOutpointAndKeysFromOutput(CWallet* pwalletMain, const COutput& out, COutPoint& outpointRet, CPubKey& pubKeyRet, CKey& keyRet);
#endif

bool IsBlockValid(const CBlock& block, int nBlockHeight, CAmount blockReward, std::string &strErrorRet);

#endif