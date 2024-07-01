// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#pragma once
#include <functional>

#include <utils/uint256.h>
#include <amount.h>
#include <script/script.h>
#include <script/standard.h>
#include <blockscanner.h>

struct CBurnTxIndexKey
{
	ScriptType addressType;
	uint160	addressHash;
	uint32_t nBlockHeight;
	uint256 txid;
	uint32_t nTxIndex;

	CBurnTxIndexKey(const ScriptType _addressType, const uint160& _addressHash, const uint32_t _nBlockHeight,
		const uint256& _txid, const uint32_t _nTxIndex) noexcept :
		addressType(_addressType),
		addressHash(_addressHash),
		nBlockHeight(_nBlockHeight),
		txid(_txid),
		nTxIndex(_nTxIndex)
	{}

	CBurnTxIndexKey() noexcept
	{
		clear();
	}

	void clear() noexcept
	{
		addressType = ScriptType::UNKNOWN;
		addressHash.SetNull();
		nBlockHeight = 0;
		txid.SetNull();
		nTxIndex = 0;
	}

    ADD_SERIALIZE_METHODS;

	template <typename Stream>
	inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
	{
		const bool bRead = ser_action == SERIALIZE_ACTION::Read;
		uint8_t nAddressType = to_integral_type(addressType);
		READWRITE(nAddressType);
		if (bRead)
		{
			if (!is_enum_valid<ScriptType>(nAddressType, ScriptType::P2PKH, ScriptType::P2SH))
				throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressType));
			addressType = static_cast<ScriptType>(nAddressType);
		}
		READWRITE(addressHash);
		if (bRead)
			nBlockHeight = ser_readdata32be(s);
		else
			ser_writedata32be(s, nBlockHeight);
		READWRITE(txid);
		READWRITE(nTxIndex);
	}
};

struct CBurnTxIndexValue
{
    CAmount nValuePat;
	uint256 blockHash;
	int64_t nBlockTime;

	CBurnTxIndexValue(const CAmount _nValuePat, const uint256& _blockHash, const int64_t _nBlockTime) noexcept :
		nValuePat(_nValuePat),
		blockHash(_blockHash),
		nBlockTime(_nBlockTime)
	{}

	CBurnTxIndexValue() noexcept
	{
		clear();
	}

	void clear() noexcept
	{
		nValuePat = -1;
		blockHash.SetNull();
		nBlockTime = 0;
	}

	ADD_SERIALIZE_METHODS;

	template <typename Stream>
	inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
	{
		READWRITE(nValuePat);
		READWRITE(blockHash);
		READWRITE(VARINT(nBlockTime));
	}

	bool IsNull() const noexcept
	{
		return nValuePat == -1;
	}
};

struct CBurnIndexIteratorKey
{
	ScriptType addressType;
	uint160 addressHash;

	CBurnIndexIteratorKey(const ScriptType _addressType, const uint160& _addressHash) noexcept :
		addressType(_addressType),
		addressHash(_addressHash)
	{}

	CBurnIndexIteratorKey() noexcept
	{
		clear();
	}

	void clear() noexcept
	{
		addressType = ScriptType::UNKNOWN;
		addressHash.SetNull();
	}

	ADD_SERIALIZE_METHODS;

	template <typename Stream>
	inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
	{
		const bool bRead = ser_action == SERIALIZE_ACTION::Read;
		uint8_t nAddressType = to_integral_type(addressType);
		READWRITE(nAddressType);
		if (bRead)
		{
			if (!is_enum_valid<ScriptType>(nAddressType, ScriptType::P2PKH, ScriptType::P2SH))
				throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressType));
			addressType = static_cast<ScriptType>(nAddressType);
		}
		READWRITE(addressHash);
	}
};

struct CBurnIndexIteratorHeightKey
{
	ScriptType addressType;
	uint160	addressHash;
	uint32_t nBlockHeight;

	CBurnIndexIteratorHeightKey(const ScriptType _addressType, const uint160& _addressHash, const uint32_t _nBlockHeight) noexcept :
		addressType(_addressType),
		addressHash(_addressHash),
		nBlockHeight(_nBlockHeight)
	{}

	CBurnIndexIteratorHeightKey() noexcept
	{
		clear();
	}

	void clear() noexcept
	{
		addressType = ScriptType::UNKNOWN;
		addressHash.SetNull();
		nBlockHeight = 0;
	}

	ADD_SERIALIZE_METHODS;

	template <typename Stream>
	inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
	{
		const bool bRead = ser_action == SERIALIZE_ACTION::Read;
		uint8_t nAddressType = to_integral_type(addressType);
		READWRITE(nAddressType);
		if (bRead)
		{
			if (!is_enum_valid<ScriptType>(nAddressType, ScriptType::P2PKH, ScriptType::P2SH))
				throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressType));
			addressType = static_cast<ScriptType>(nAddressType);
		}
		READWRITE(addressHash);
		if (bRead)
			nBlockHeight = ser_readdata32be(s);
		else
			ser_writedata32be(s, nBlockHeight);
	}
};

using process_burntx_item_func_t = std::function<void(const uint256& txid, const uint32_t nTxInIndex, 
    const uint256& blockHash, const uint32_t nBlockHeight, const int64_t nBlockTime, 
    const CTxDestination &address, const CAmount nValuePat)>;

bool GenerateBurnTxIndex(const CChainParams& chainparams, std::string& error);

void ProcessBurnTxIndexTask(BlockScannerTask* pTask,
	const uint160& destBurnAddress, const bool bScanAllAddresses,
	const CTxDestination& destTrackingAddress,
	const process_burntx_item_func_t& fnProcessItem);
