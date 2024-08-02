// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#pragma once
#include <utils/uint256.h>
#include <amount.h>
#include <script/scripttype.h>
#include <utils/serialize.h>

struct CFundsTransferIndexKey
{
	ScriptType addressTypeFrom; // from address type
	uint160 addressHashFrom;    // from address hash
	ScriptType addressTypeTo;	// to address type
	uint160 addressHashTo;		// to address hash
	uint32_t blockHeight;		// block height
	uint256 txid;				// transaction id

	CFundsTransferIndexKey(const ScriptType addressTypeFrom, const uint160 &_addressHashFrom,
						   const ScriptType addressTypeTo, const uint160 &_addressHashTo,
						   const uint32_t nBlockHeight, const uint256 &_txid) noexcept :
		addressTypeFrom(addressTypeFrom),
		addressHashFrom(_addressHashFrom),
		addressTypeTo(addressTypeTo),
		addressHashTo(_addressHashTo),
		blockHeight(nBlockHeight),
		txid(_txid)
	{}

	CFundsTransferIndexKey() noexcept
	{
		SetNull();
	}

	void SetNull() noexcept
	{
		addressTypeFrom = ScriptType::UNKNOWN;
		addressHashFrom.SetNull();
		addressTypeTo = ScriptType::UNKNOWN;
		addressHashTo.SetNull();
		blockHeight = 0;
		txid.SetNull();
	}

	ADD_SERIALIZE_METHODS;

	template <typename Stream>
	inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
	{
		const bool bRead = ser_action == SERIALIZE_ACTION::Read;
		uint8_t nAddressTypeFrom = to_integral_type(addressTypeFrom);
		uint8_t nAddressTypeTo = to_integral_type(addressTypeTo);
		READWRITE(nAddressTypeFrom);
		if (bRead)
		{
			if (!is_enum_valid<ScriptType>(nAddressTypeFrom, ScriptType::P2PKH, ScriptType::P2SH))
				throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressTypeFrom));
			addressTypeFrom = static_cast<ScriptType>(nAddressTypeFrom);
		}
		READWRITE(addressHashFrom);
		READWRITE(nAddressTypeTo);
		if (bRead)
		{
			if (!is_enum_valid<ScriptType>(nAddressTypeTo, ScriptType::P2PKH, ScriptType::P2SH))
				throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressTypeTo));
			addressTypeTo = static_cast<ScriptType>(nAddressTypeTo);
		}
		READWRITE(addressHashTo);
		if (bRead)
			blockHeight = ser_readdata32be(s);
		else
			ser_writedata32be(s, blockHeight);
		READWRITE(txid);
	}
};

using address_intxdata_t = std::pair<uint32_t, CAmount>;
using address_intxdata_vector_t = std::vector<address_intxdata_t>;

struct CFundsTransferIndexValue
{
	// vin index and amount value (from prevout)
	address_intxdata_vector_t vInputIndex;
	uint32_t nOutputIndex; // txOut index
	CAmount nOutputValue;  // txOut value

	CFundsTransferIndexValue(address_intxdata_vector_t &&_vInputIndex, 
							 const uint32_t _nOutputIndex, const CAmount _nOutputValue) noexcept :
		vInputIndex(std::move(_vInputIndex)),
		nOutputIndex(_nOutputIndex),
		nOutputValue(_nOutputValue)
	{}

	CFundsTransferIndexValue() noexcept
	{
		SetNull();
	}

	void SetNull() noexcept
	{
		vInputIndex.clear();
		nOutputIndex = 0;
		nOutputValue = 0;
	}

	ADD_SERIALIZE_METHODS;

	template <typename Stream>
	inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
	{
		const bool bRead = ser_action == SERIALIZE_ACTION::Read;
		if (bRead)
		{
			vInputIndex.clear();
			uint64_t nInputIndexSize = ReadCompactSize(s);
			vInputIndex.reserve(nInputIndexSize);
			for (uint64_t i = 0; i < nInputIndexSize; ++i)
			{
				uint32_t nTxIn;
				CAmount nValue;
				READWRITE(VARINT(nTxIn));
				READWRITE(VARINT(nValue));
				vInputIndex.emplace_back(nTxIn, nValue);
			}
		}
		else
		{
			WriteCompactSize(s, vInputIndex.size());
			for (const auto& [nTxIn, nAmount] : vInputIndex)
			{
				READWRITE(VARINT(nTxIn));
				READWRITE(VARINT(nAmount));
			}
		}
		READWRITE(VARINT(nOutputIndex));
		READWRITE(VARINT(nOutputValue));
	}
};

struct CFundsTransferIndexInKey
{
	ScriptType addressType;
	const uint160 &addressHash;
	uint32_t nTxOrderNo;

	CFundsTransferIndexInKey(const ScriptType _addressType, const uint160 &_addressHash, const uint32_t _nTxOrderNo) noexcept :
		addressType(_addressType),
		addressHash(_addressHash),
		nTxOrderNo(_nTxOrderNo)
	{}

	std::size_t GetHash() const noexcept
	{
		std::size_t seed = 0;
		hash_combine(seed, to_integral_type(addressType));
		hash_combine(seed, addressHash);
		hash_combine(seed, nTxOrderNo);
		return seed;
	}
};

struct CFundsTransferIndexInValue
{
	ScriptType addressType;
	const uint160 addressHash;
	uint32_t nTxOrderNo;

	address_intxdata_vector_t vInputIndex;

	CFundsTransferIndexInValue(const ScriptType _addressType, const uint160 &_addressHash, const uint32_t _nTxOrderNo) noexcept :
		addressType(_addressType),
		addressHash(_addressHash),
		nTxOrderNo(_nTxOrderNo)
	{}

	void AddInputIndex(const uint32_t nTxIn, const CAmount nValue) noexcept
	{
		vInputIndex.emplace_back(nTxIn, nValue);
	}
};

constexpr size_t FUNDS_TRANSFER_INDEX_ITERATOR_KEY_SIZE = sizeof(uint8_t) + sizeof(uint160) + sizeof(uint8_t) + sizeof(uint160);

struct CFundsTransferIndexIteratorKey
{
	ScriptType addressTypeFrom;
	uint160 addressHashFrom;
	ScriptType addressTypeTo;
	uint160 addressHashTo;

	CFundsTransferIndexIteratorKey(const ScriptType _addressTypeFrom, const uint160 &_addressHashFrom,
								   const ScriptType _addressTypeTo, const uint160 &_addressHashTo) noexcept :
		addressTypeFrom(_addressTypeFrom),
		addressHashFrom(_addressHashFrom),
		addressTypeTo(_addressTypeTo),
		addressHashTo(_addressHashTo)
	{}

	CFundsTransferIndexIteratorKey() noexcept
	{
		SetNull();
	}

	void SetNull() noexcept
	{
		addressTypeFrom = ScriptType::UNKNOWN;
		addressHashFrom.SetNull();
		addressTypeTo = ScriptType::UNKNOWN;
		addressHashTo.SetNull();
	}

	ADD_SERIALIZE_METHODS;

	template <typename Stream>
	inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
	{
		const bool bRead = ser_action == SERIALIZE_ACTION::Read;
		uint8_t nAddressTypeFrom = to_integral_type(addressTypeFrom);
		uint8_t nAddressTypeTo = to_integral_type(addressTypeTo);
		READWRITE(nAddressTypeFrom);
		if (bRead)
		{
			if (!is_enum_valid<ScriptType>(nAddressTypeFrom, ScriptType::P2PKH, ScriptType::P2SH))
				throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressTypeFrom));
			addressTypeFrom = static_cast<ScriptType>(nAddressTypeFrom);
		}
		READWRITE(addressHashFrom);
		READWRITE(nAddressTypeTo);
		if (bRead)
		{
			if (!is_enum_valid<ScriptType>(nAddressTypeTo, ScriptType::P2PKH, ScriptType::P2SH))
				throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressTypeTo));
			addressTypeTo = static_cast<ScriptType>(nAddressTypeTo);
		}
		READWRITE(addressHashTo);
	}

	size_t GetSerializeSize(int nType, int nVersion) const noexcept
	{
		return FUNDS_TRANSFER_INDEX_ITERATOR_KEY_SIZE;
	}
};

struct CFundsTransferIndexIteratorHeightKey
{
	ScriptType addressTypeFrom;
	uint160 addressHashFrom;
	ScriptType addressTypeTo;
	uint160 addressHashTo;
	uint32_t blockHeight;

	CFundsTransferIndexIteratorHeightKey(const ScriptType _addressTypeFrom, const uint160 &_addressHashFrom,
										 const ScriptType _addressTypeTo, const uint160 &_addressHashTo,
										 const uint32_t _blockHeight) noexcept :
		addressTypeFrom(_addressTypeFrom),
		addressHashFrom(_addressHashFrom),
		addressTypeTo(_addressTypeTo),
		addressHashTo(_addressHashTo),
		blockHeight(_blockHeight)
	{}

	CFundsTransferIndexIteratorHeightKey() noexcept
	{
		SetNull();
	}

	void SetNull() noexcept
	{
		addressTypeFrom = ScriptType::UNKNOWN;
		addressHashFrom.SetNull();
		addressTypeTo = ScriptType::UNKNOWN;
		addressHashTo.SetNull();
		blockHeight = 0;
	}

	ADD_SERIALIZE_METHODS;

	template <typename Stream>
	inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
	{
		const bool bRead = ser_action == SERIALIZE_ACTION::Read;
		uint8_t nAddressTypeFrom = to_integral_type(addressTypeFrom);
		uint8_t nAddressTypeTo = to_integral_type(addressTypeTo);
		READWRITE(nAddressTypeFrom);
		if (bRead)
		{
			if (!is_enum_valid<ScriptType>(nAddressTypeFrom, ScriptType::P2PKH, ScriptType::P2SH))
				throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressTypeFrom));
			addressTypeFrom = static_cast<ScriptType>(nAddressTypeFrom);
		}
		READWRITE(addressHashFrom);
		READWRITE(nAddressTypeTo);
		if (bRead)
		{
			if (!is_enum_valid<ScriptType>(nAddressTypeTo, ScriptType::P2PKH, ScriptType::P2SH))
				throw std::runtime_error(strprintf("Not supported ScriptType [%d]", nAddressTypeTo));
			addressTypeTo = static_cast<ScriptType>(nAddressTypeTo);
		}
		READWRITE(addressHashTo);
		if (bRead)
			blockHeight = ser_readdata32be(s);
		else
			ser_writedata32be(s, blockHeight);
	}
};
