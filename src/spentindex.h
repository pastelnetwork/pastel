// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#ifndef BITCOIN_SPENTINDEX_H
#define BITCOIN_SPENTINDEX_H

#include "uint256.h"
#include "amount.h"
#include "util.h"
#include "script/script.h"

struct CSpentIndexKey {
    uint256 txid;
    unsigned int outputIndex;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action) {
        READWRITE(txid);
        READWRITE(outputIndex);
    }

    CSpentIndexKey(uint256 t, unsigned int i) {
        txid = t;
        outputIndex = i;
    }

    CSpentIndexKey() {
        SetNull();
    }

    void SetNull() {
        txid.SetNull();
        outputIndex = 0;
    }
};

struct CSpentIndexValue {
    uint256 txid;
    unsigned int inputIndex;
    int blockHeight;
    CAmount patoshis;
    CScript::ScriptType addressType;
    uint160 addressHash;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action) {
        READWRITE(txid);
        READWRITE(inputIndex);
        READWRITE(blockHeight);
        READWRITE(patoshis);
        READWRITE(addressType);
        READWRITE(addressHash);
    }

    CSpentIndexValue(const uint256 t, const unsigned int i, const int h, const CAmount s, const CScript::ScriptType type, const uint160 a) {
        txid = t;
        inputIndex = i;
        blockHeight = h;
        patoshis = s;
        addressType = type;
        addressHash = a;
    }

    CSpentIndexValue() {
        SetNull();
    }

    void SetNull() {
        txid.SetNull();
        inputIndex = 0;
        blockHeight = 0;
        patoshis = 0;
        addressType = CScript::ScriptType::UNKNOWN;
        addressHash.SetNull();
    }

    bool IsNull() const {
        try {
            return txid.IsNull();
        } catch (const std::runtime_error& e) {
            return true;
        }
    }
};

struct CSpentIndexKeyCompare
{
    bool operator()(const CSpentIndexKey& a, const CSpentIndexKey& b) const {
        if (a.txid == b.txid) {
            return a.outputIndex < b.outputIndex;
        } else {
            return a.txid < b.txid;
        }
    }
};

#endif // BITCOIN_SPENTINDEX_H