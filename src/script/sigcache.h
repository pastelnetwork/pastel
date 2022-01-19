#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <vector_types.h>
#include <script/interpreter.h>

// DoS prevention: limit cache size to less than 40MB (over 500000
// entries on 64-bit systems).
static const unsigned int DEFAULT_MAX_SIG_CACHE_SIZE = 40;

class CPubKey;

class CachingTransactionSignatureChecker : public TransactionSignatureChecker
{
private:
    bool m_bStore;

public:
    CachingTransactionSignatureChecker(const CTransaction* txToIn, unsigned int nInIn, const CAmount& amount, bool storeIn, PrecomputedTransactionData& txdataIn) :
        TransactionSignatureChecker(txToIn, nInIn, amount, txdataIn),
        m_bStore(storeIn)
    {}

    bool VerifySignature(const v_uint8 & vchSig, const CPubKey& vchPubKey, const uint256& sighash) const;
};
