// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#pragma once
#include <utils/uint256.h>
#include <amount.h>
#include <script/script.h>

constexpr size_t ADDRESS_UNSPENT_KEY_SIZE = sizeof(uint8_t) + sizeof(uint160) + sizeof(uint256) + sizeof(uint32_t);
struct CAddressUnspentKey
{
    ScriptType type;
    uint160 hashBytes;
    uint256 txhash;
    uint32_t index;

    CAddressUnspentKey(const ScriptType addressType, const uint160 &addressHash, const uint256 &txid, 
        const uint32_t indexValue) noexcept :
        type(addressType),
        hashBytes(addressHash),
        txhash(txid),
        index(indexValue)
    {}

    CAddressUnspentKey() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        type = ScriptType::UNKNOWN;
        hashBytes.SetNull();
        txhash.SetNull();
        index = 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
        uint8_t nAddressType = to_integral_type(type);
        READWRITE(nAddressType);
        if (bRead)
        {
            if (!is_enum_valid<ScriptType>(nAddressType, ScriptType::P2PKH, ScriptType::P2SH))
                throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressType));
            type = static_cast<ScriptType>(nAddressType);
        }
		READWRITE(hashBytes);
		READWRITE(txhash);
		READWRITE(index);
	}

    size_t GetSerializeSize(int nType, int nVersion) const
    {
        return ADDRESS_UNSPENT_KEY_SIZE;
    }
};

struct CAddressUnspentValue
{
    CAmount patoshis;
    CScript script;
    uint32_t blockHeight;

    CAddressUnspentValue(const CAmount pats, const CScript &scriptPubKey, const uint32_t height) noexcept :
        patoshis(pats),
		script(scriptPubKey),
		blockHeight(height)
	{}

    CAddressUnspentValue() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        patoshis = -1;
        script.clear();
        blockHeight = 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        READWRITE(patoshis);
        READWRITE(*(CScriptBase*)(&script));
        READWRITE(blockHeight);
    }

    size_t GetSerializeSize(int nType, int nVersion) const
    {
		return sizeof(CAmount) + GetSizeOfCompactSize(script.size()) + script.size() + sizeof(uint32_t);
	}

    bool IsNull() const noexcept
    {
        return (patoshis == -1);
    }
};

constexpr size_t ADDRESS_INDEX_KEY_SIZE = sizeof(uint8_t) + sizeof(uint160) + sizeof(uint32_t) + sizeof(uint32_t) + sizeof(uint256) + sizeof(uint32_t) + sizeof(bool);
struct CAddressIndexKey
{
    uint8_t type;
    uint160 hashBytes;
    uint32_t blockHeight;
    uint32_t txindex;
    uint256 txhash;
    uint32_t index;
    bool bSpending;

    CAddressIndexKey(const ScriptType addressType, const uint160 &addressHash, uint32_t height, uint32_t blockindex,
                     const uint256 &txid, uint32_t indexValue, bool isSpending) noexcept :
        type(to_integral_type(addressType)),
        hashBytes(addressHash),
        blockHeight(height),
        txindex(blockindex),
        txhash(txid),
        index(indexValue),
        bSpending(isSpending)
    {}

    CAddressIndexKey() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
        txindex = 0;
        txhash.SetNull();
        index = 0;
        bSpending = false;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
        const bool bRead = ser_action == SERIALIZE_ACTION::Read;
		READWRITE(type);
		READWRITE(hashBytes);
        if (bRead)
        {
			blockHeight = ser_readdata32be(s);
			txindex = ser_readdata32be(s);
		}
		else
		{
			ser_writedata32be(s, blockHeight);
			ser_writedata32be(s, txindex);
        }
		READWRITE(txhash);
		READWRITE(index);
		READWRITE(bSpending);
	}

    size_t GetSerializeSize(int nType, int nVersion) const noexcept
    {
        return ADDRESS_INDEX_KEY_SIZE;
    }
};

constexpr size_t ADDRESS_INDEX_ITERATOR_KEY_SIZE = sizeof(uint8_t) + sizeof(uint160);
struct CAddressIndexIteratorKey
{
    uint8_t type;
    uint160 hashBytes;

    CAddressIndexIteratorKey(const uint8_t addressType, const uint160 &addressHash) noexcept :
        type(addressType),
        hashBytes(addressHash)
    {}

    CAddressIndexIteratorKey() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        type = 0;
        hashBytes.SetNull();
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
	    READWRITE(type);
		READWRITE(hashBytes);
	}

    size_t GetSerializeSize(int nType, int nVersion) const noexcept
    {
        return ADDRESS_INDEX_ITERATOR_KEY_SIZE;
    }
};

constexpr size_t ADDRESS_INDEX_ITERATOR_HEIGHT_KEY_SIZE = sizeof(uint8_t) + sizeof(uint160) + sizeof(uint32_t);
struct CAddressIndexIteratorHeightKey
{
    uint8_t type;
    uint160 hashBytes;
    uint32_t blockHeight;

    CAddressIndexIteratorHeightKey(const uint8_t addressType, const uint160 &addressHash, uint32_t height) noexcept
    {
        type = addressType;
        hashBytes = addressHash;
        blockHeight = height;
    }

    CAddressIndexIteratorHeightKey() noexcept
    {
        SetNull();
    }

    void SetNull() noexcept
    {
        type = 0;
        hashBytes.SetNull();
        blockHeight = 0;
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action)
    {
		const bool bRead = ser_action == SERIALIZE_ACTION::Read;
		READWRITE(type);
		READWRITE(hashBytes);
		if (bRead)
			blockHeight = ser_readdata32be(s);
		else
			ser_writedata32be(s, blockHeight);
	}

    size_t GetSerializeSize(int nType, int nVersion) const noexcept
    {
        return ADDRESS_INDEX_ITERATOR_HEIGHT_KEY_SIZE;
    }
};

struct CMempoolAddressDelta
{
    int64_t time;
    CAmount amount;
    uint256 prevhash;
    unsigned int prevout;

    CMempoolAddressDelta(int64_t t, CAmount a, const uint256 &hash, unsigned int out) noexcept
    {
        time = t;
        amount = a;
        prevhash = hash;
        prevout = out;
    }

    CMempoolAddressDelta(int64_t t, CAmount a) noexcept
    {
        time = t;
        amount = a;
        prevhash.SetNull();
        prevout = 0;
    }
};

struct CMempoolAddressDeltaKey
{
    ScriptType type;
    uint160 addressBytes;
    uint256 txhash;
    unsigned int index;
    int spending;

    CMempoolAddressDeltaKey(const ScriptType addressType, const uint160 &addressHash, const uint256 &hash, unsigned int i, int s) noexcept
    {
        type = addressType;
        addressBytes = addressHash;
        txhash = hash;
        index = i;
        spending = s;
    }

    CMempoolAddressDeltaKey(const ScriptType addressType, uint160 addressHash) noexcept
    {
        type = addressType;
        addressBytes = addressHash;
        txhash.SetNull();
        index = 0;
        spending = 0;
    }
};

struct CMempoolAddressDeltaKeyCompare
{
    bool operator()(const CMempoolAddressDeltaKey& a, const CMempoolAddressDeltaKey& b) const
    {
        if (a.type != b.type)
            return a.type < b.type;
        if (a.addressBytes != b.addressBytes)
            return a.addressBytes < b.addressBytes;
        if (a.txhash != b.txhash)
            return a.txhash < b.txhash;
        if (a.index != b.index)
            return a.index < b.index;
        return a.spending < b.spending;
    }
};
