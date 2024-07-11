// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#pragma once 
#include <utils/uint256.h>
#include <utils/util.h>
#include <amount.h>
#include <script/scripttype.h>

struct CSpentIndexKey
{
    uint256 txid;
    unsigned int outputIndex;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(txid);
        READWRITE(outputIndex);
    }

    CSpentIndexKey(uint256 t, unsigned int i) noexcept
    {
        txid = t;
        outputIndex = i;
    }

    CSpentIndexKey() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        txid.SetNull();
        outputIndex = 0;
    }
};

struct CSpentIndexValue
{
    uint256 txid;
    uint32_t inputIndex;
    uint32_t blockHeight;
    CAmount patoshis;
    ScriptType addressType;
    uint160 addressHash;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;

        READWRITE(txid);
        READWRITE(inputIndex);
        READWRITE(blockHeight);
        READWRITE(patoshis);
        int nAddressType = to_integral_type(addressType);
        READWRITE(nAddressType);
        if (bRead)
        {
            if (!is_enum_valid<ScriptType>(nAddressType, ScriptType::P2PKH, ScriptType::P2SH))
                throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressType));
            addressType = static_cast<ScriptType>(nAddressType);
        }
        READWRITE(addressHash);
    }

    CSpentIndexValue(const uint256 &_txid, const uint32_t _inputIndex, const int height, 
        const CAmount s, const ScriptType type, const uint160 &a) noexcept
    {
        txid = _txid;
        inputIndex = _inputIndex;
        blockHeight = height;
        patoshis = s;
        addressType = type;
        addressHash = a;
    }

    CSpentIndexValue() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        txid.SetNull();
        inputIndex = 0;
        blockHeight = 0;
        patoshis = 0;
        addressType = ScriptType::UNKNOWN;
        addressHash.SetNull();
    }

    bool IsNull() const noexcept
    {
        try
        {
            return txid.IsNull();
        } catch ([[maybe_unused]] const std::runtime_error& e)
        {
            return true;
        }
    }
};

struct CSpentIndexKeyCompare
{
    bool operator()(const CSpentIndexKey& a, const CSpentIndexKey& b) const
    {
        if (a.txid == b.txid) {
            return a.outputIndex < b.outputIndex;
        } else {
            return a.txid < b.txid;
        }
    }
};
