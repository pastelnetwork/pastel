// Copyright (c) 2014-2016 The Bitcoin Core developers
// Copyright (c) 2016-2018 The Zcash developers
// Copyright (c) 2018-2021 Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "key_io.h"
#include "base58.h"
#include "bech32.h"
#include "script/script.h"
#include "utilstrencodings.h"
#include "vector_types.h"

#include <assert.h>
#include <string.h>
#include <algorithm>
#include <variant>

namespace
{
class DestinationEncoder
{
private:
	const KeyConstants& m_KeyConstants;

public:
	DestinationEncoder(const KeyConstants& keyConstants) : 
		m_KeyConstants(keyConstants)
	{}

    std::string operator()(const CKeyID& id) const
    {
        v_uint8 data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::PUBKEY_ADDRESS);
        data.insert(data.end(), id.begin(), id.end());
        return EncodeBase58Check(data);
    }

    std::string operator()(const CScriptID& id) const
    {
        v_uint8 data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::SCRIPT_ADDRESS);
        data.insert(data.end(), id.begin(), id.end());
        return EncodeBase58Check(data);
    }

    std::string operator()(const CNoDestination& no) const { return {}; }
};

class PaymentAddressEncoder
{
private:
	const KeyConstants& m_KeyConstants;

public:
	PaymentAddressEncoder(const KeyConstants& keyConstants) :
		m_KeyConstants(keyConstants)
	{}

    std::string operator()(const libzcash::SproutPaymentAddress& zaddr) const
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << zaddr;
        std::vector<unsigned char> data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::ZCPAYMENT_ADDRESS);
        data.insert(data.end(), ss.begin(), ss.end());
        return EncodeBase58Check(data);
    }

    std::string operator()(const libzcash::SaplingPaymentAddress& zaddr) const
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << zaddr;
        // ConvertBits requires unsigned char, but CDataStream uses char
        v_uint8 seraddr(ss.cbegin(), ss.cend());
        v_uint8 data;
        // See calculation comment below
        data.reserve((seraddr.size() * 8 + 4) / 5);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, seraddr.begin(), seraddr.end());
        return bech32::Encode(m_KeyConstants.Bech32HRP(KeyConstants::Bech32Type::SAPLING_PAYMENT_ADDRESS), data);
    }

    std::string operator()(const libzcash::InvalidEncoding& no) const { return {}; }
};

class ViewingKeyEncoder
{
private:
	const KeyConstants& m_KeyConstants;

public:
	ViewingKeyEncoder(const KeyConstants& keyConstants) :
		m_KeyConstants(keyConstants)
	{}

    std::string operator()(const libzcash::SproutViewingKey& vk) const
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << vk;
        std::vector<unsigned char> data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::ZCVIEWING_KEY);
        data.insert(data.end(), ss.begin(), ss.end());
        std::string ret = EncodeBase58Check(data);
        memory_cleanse(data.data(), data.size());
        return ret;
    }

    std::string operator()(const libzcash::SaplingExtendedFullViewingKey& extfvk) const
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << extfvk;
        // ConvertBits requires unsigned char, but CDataStream uses char
        v_uint8 serkey(ss.cbegin(), ss.cend());
        v_uint8 data;
        // See calculation comment below
        data.reserve((serkey.size() * 8 + 4) / 5);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, serkey.begin(), serkey.end());
        std::string ret = bech32::Encode(m_KeyConstants.Bech32HRP(KeyConstants::Bech32Type::SAPLING_EXTENDED_FVK), data);
        memory_cleanse(serkey.data(), serkey.size());
        memory_cleanse(data.data(), data.size());
        return ret;
    }

    std::string operator()(const libzcash::InvalidEncoding& no) const { return {}; }
};

class SpendingKeyEncoder
{
private:
	const KeyConstants& m_KeyConstants;

public:
	SpendingKeyEncoder(const KeyConstants& keyConstants) :
		m_KeyConstants(keyConstants)
	{}

    std::string operator()(const libzcash::SproutSpendingKey& zkey) const
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << zkey;
        std::vector<unsigned char> data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::ZCSPENDING_KEY);
        data.insert(data.end(), ss.begin(), ss.end());
        std::string ret = EncodeBase58Check(data);
        memory_cleanse(data.data(), data.size());
        return ret;
    }

    std::string operator()(const libzcash::SaplingExtendedSpendingKey& zkey) const
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << zkey;
        // ConvertBits requires unsigned char, but CDataStream uses char
        v_uint8 serkey(ss.cbegin(), ss.cend());
        v_uint8 data;
        // See calculation comment below
        data.reserve((serkey.size() * 8 + 4) / 5);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, serkey.begin(), serkey.end());
        std::string ret = bech32::Encode(m_KeyConstants.Bech32HRP(KeyConstants::Bech32Type::SAPLING_EXTENDED_SPEND_KEY), data);
        memory_cleanse(serkey.data(), serkey.size());
        memory_cleanse(data.data(), data.size());
        return ret;
    }

    std::string operator()(const libzcash::InvalidEncoding& no) const { return {}; }
};

// Sizes of SaplingPaymentAddress, SaplingExtendedFullViewingKey, and
// SaplingExtendedSpendingKey after ConvertBits<8, 5, true>(). The calculations
// below take the regular serialized size in bytes, convert to bits, and then
// perform ceiling division to get the number of 5-bit clusters.
const size_t ConvertedSaplingPaymentAddressSize = ((32 + 11) * 8 + 4) / 5;
const size_t ConvertedSaplingExtendedFullViewingKeySize = (ZIP32_XFVK_SIZE * 8 + 4) / 5;
const size_t ConvertedSaplingExtendedSpendingKeySize = (ZIP32_XSK_SIZE * 8 + 4) / 5;
} // namespace

CTxDestination KeyIO::DecodeDestination(const std::string& str)
{
    v_uint8 data;
    uint160 hash;
    if (DecodeBase58Check(str, data))
    {
        // base58-encoded Bitcoin addresses.
        // Public-key-hash-addresses have version 0 (or 111 testnet).
        // The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
        const auto& pubkey_prefix = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::PUBKEY_ADDRESS);
        if (data.size() == hash.size() + pubkey_prefix.size() && std::equal(pubkey_prefix.begin(), pubkey_prefix.end(), data.begin()))
        {
            std::copy(data.begin() + pubkey_prefix.size(), data.end(), hash.begin());
            return CKeyID(hash);
        }
        // Script-hash-addresses have version 5 (or 196 testnet).
        // The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
        const auto& script_prefix = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::SCRIPT_ADDRESS);
        if (data.size() == hash.size() + script_prefix.size() && std::equal(script_prefix.begin(), script_prefix.end(), data.begin()))
        {
            std::copy(data.begin() + script_prefix.size(), data.end(), hash.begin());
            return CScriptID(hash);
        }
    }
    return CNoDestination();
}

/**
 * Decodes private key string (base58 encoded) to CKey object.
 * 
 * \param str - private key string
 * \return CKey object that encapsulates private key
 */
CKey KeyIO::DecodeSecret(const std::string& str, std::string& error)
{
    CKey key;
    v_uint8 data;
    do
    {
        if (!DecodeBase58Check(str, data))
        {
            error = "failed to decode base58-encoded string";
            break;
        }
        // secret key prefix
        const auto& privkey_prefix = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::SECRET_KEY);
        // check that:
        //   - key string is exactly 32 bytes or 32 bytes with trailing compression flag
        //   - key string starts with secret key prefix
        const auto nKeySize = privkey_prefix.size() + CKey::KEY_SIZE;
        if  ((data.size() == nKeySize ||
            ((data.size() == nKeySize + 1) && data.back() == 1)) &&
            std::equal(privkey_prefix.cbegin(), privkey_prefix.cend(), data.cbegin()))
        {
            const bool bCompressed = data.size() == nKeySize + 1;
            key.Set(data.cbegin() + privkey_prefix.size(), data.cbegin() + nKeySize, bCompressed);
        }
        else
        {
            if (data.size() < nKeySize)
            {
                error = tfm::format("length is less than %zu bytes", CKey::KEY_SIZE);
                break;
            }
            error = "invalid prefix";
            break;
        }
    } while (false);
    // wipe out memory
    memory_cleanse(data.data(), data.size());
    return key;
}

/**
 * Encodes CKey private key object to string.
 * This function expects that key is valid
 * 
 * \param key - CKey object that encapsulates private key
 * \return string representation of private key
 */
std::string KeyIO::EncodeSecret(const CKey& key)
{
    assert(key.IsValid());
    v_uint8 data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::SECRET_KEY);
    data.insert(data.end(), key.cbegin(), key.cend());
    // add "compressed" flag = 1
    if (key.IsCompressed())
        data.push_back(1);
    // base58 encoding
    std::string ret = EncodeBase58Check(data);
    memory_cleanse(data.data(), data.size());
    return ret;
}

CExtPubKey KeyIO::DecodeExtPubKey(const std::string& str)
{
    CExtPubKey key;
    std::vector<unsigned char> data;
    if (DecodeBase58Check(str, data))
    {
        const auto& prefix = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::EXT_PUBLIC_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin()))
            key.Decode(data.data() + prefix.size());
    }
    return key;
}

std::string KeyIO::EncodeExtPubKey(const CExtPubKey& key)
{
    v_uint8 data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::EXT_PUBLIC_KEY);
    const size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    std::string ret = EncodeBase58Check(data);
    return ret;
}

CExtKey KeyIO::DecodeExtKey(const std::string& str)
{
    CExtKey key;
    v_uint8 data;
    if (DecodeBase58Check(str, data))
    {
        const auto& prefix = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::EXT_SECRET_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && std::equal(prefix.cbegin(), prefix.cend(), data.cbegin()))
            key.Decode(data.data() + prefix.size());
    }
    return key;
}

std::string KeyIO::EncodeExtKey(const CExtKey& key)
{
    v_uint8 data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::EXT_SECRET_KEY);
    const size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    std::string ret = EncodeBase58Check(data);
    memory_cleanse(data.data(), data.size());
    return ret;
}

std::string KeyIO::EncodeDestination(const CTxDestination& dest)
{
    return std::visit(DestinationEncoder(m_KeyConstants), dest);
}

bool KeyIO::IsValidDestinationString(const std::string& str)
{
    return IsValidDestination(DecodeDestination(str));
}

std::string KeyIO::EncodePaymentAddress(const libzcash::PaymentAddress& zaddr)
{
    return std::visit(PaymentAddressEncoder(m_KeyConstants), zaddr);
}

template<typename T1, typename T2, typename T3>
T1 DecodeAny(
    const KeyConstants& keyConstants,
    const std::string& str,
    std::pair<KeyConstants::Base58Type, size_t> sprout,
    std::pair<KeyConstants::Bech32Type, size_t> sapling)
{
    std::vector<unsigned char> data;
    if (DecodeBase58Check(str, data)) {
        const std::vector<unsigned char>& prefix = keyConstants.Base58Prefix(sprout.first);
        if ((data.size() == sprout.second + prefix.size()) &&
            std::equal(prefix.begin(), prefix.end(), data.begin())) {
            CSerializeData serialized(data.begin() + prefix.size(), data.end());
            CDataStream ss(serialized, SER_NETWORK, PROTOCOL_VERSION);
            T2 ret;
            ss >> ret;
            memory_cleanse(serialized.data(), serialized.size());
            memory_cleanse(data.data(), data.size());
            return ret;
        }
    }

    data.clear();
    auto bech = bech32::Decode(str);
    if (bech.first == keyConstants.Bech32HRP(sapling.first) &&
        bech.second.size() == sapling.second) {
        // Bech32 decoding
        data.reserve((bech.second.size() * 5) / 8);
        if (ConvertBits<5, 8, false>([&](unsigned char c) { data.push_back(c); }, bech.second.begin(), bech.second.end())) {
            CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
            T3 ret;
            ss >> ret;
            memory_cleanse(data.data(), data.size());
            return ret;
        }
    }

    memory_cleanse(data.data(), data.size());
    return libzcash::InvalidEncoding();
}

libzcash::PaymentAddress KeyIO::DecodePaymentAddress(const std::string& str)
{
    return DecodeAny<libzcash::PaymentAddress,
        libzcash::SproutPaymentAddress,
        libzcash::SaplingPaymentAddress>(
            m_KeyConstants,
            str,
            std::make_pair(KeyConstants::Base58Type::ZCPAYMENT_ADDRESS, libzcash::SerializedSproutPaymentAddressSize),
            std::make_pair(KeyConstants::Bech32Type::SAPLING_PAYMENT_ADDRESS, ConvertedSaplingPaymentAddressSize)
        );
}

bool KeyIO::IsValidPaymentAddressString(const std::string& str) {
    return IsValidPaymentAddress(DecodePaymentAddress(str));
}

std::string KeyIO::EncodeViewingKey(const libzcash::ViewingKey& vk)
{
    return std::visit(ViewingKeyEncoder(m_KeyConstants), vk);
}

libzcash::ViewingKey KeyIO::DecodeViewingKey(const std::string& str)
{
    return DecodeAny<libzcash::ViewingKey,
        libzcash::SproutViewingKey,
        libzcash::SaplingExtendedFullViewingKey>(
            m_KeyConstants,
            str,
            std::make_pair(KeyConstants::Base58Type::ZCVIEWING_KEY, libzcash::SerializedSproutViewingKeySize),
            std::make_pair(KeyConstants::Bech32Type::SAPLING_EXTENDED_FVK, ConvertedSaplingExtendedFullViewingKeySize)
        );
}

std::string KeyIO::EncodeSpendingKey(const libzcash::SpendingKey& zkey)
{
    return std::visit(SpendingKeyEncoder(m_KeyConstants), zkey);
}

libzcash::SpendingKey KeyIO::DecodeSpendingKey(const std::string& str)
{
    return DecodeAny<libzcash::SpendingKey,
        libzcash::SproutSpendingKey,
        libzcash::SaplingExtendedSpendingKey>(
            m_KeyConstants,
            str,
            std::make_pair(KeyConstants::Base58Type::ZCSPENDING_KEY, libzcash::SerializedSproutSpendingKeySize),
            std::make_pair(KeyConstants::Bech32Type::SAPLING_EXTENDED_SPEND_KEY, ConvertedSaplingExtendedSpendingKeySize)
        );
}
