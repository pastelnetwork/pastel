#pragma once
// Copyright (c) 2018-2021 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
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
