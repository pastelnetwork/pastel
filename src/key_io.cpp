// Copyright (c) 2014-2016 The Bitcoin Core developers
// Copyright (c) 2016-2018 The Zcash developers
// Copyright (c) 2018-2021 Pastel Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <key_io.h>
#include <base58.h>
#include <bech32.h>
#include <script/script.h>
#include <utilstrencodings.h>
#include <vector_types.h>

#include <assert.h>
#include <string.h>
#include <algorithm>
#include <variant>

using namespace std;

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

    string operator()(const CKeyID& id) const
    {
        v_uint8 data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::PUBKEY_ADDRESS);
        data.insert(data.end(), id.begin(), id.end());
        return EncodeBase58Check(data);
    }

    string operator()(const CScriptID& id) const
    {
        v_uint8 data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::SCRIPT_ADDRESS);
        data.insert(data.end(), id.begin(), id.end());
        return EncodeBase58Check(data);
    }

    string operator()(const CNoDestination& no) const { return {}; }
};

class PaymentAddressEncoder
{
private:
	const KeyConstants& m_KeyConstants;

public:
	PaymentAddressEncoder(const KeyConstants& keyConstants) :
		m_KeyConstants(keyConstants)
	{}

    string operator()(const libzcash::SaplingPaymentAddress& zaddr) const
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

    string operator()(const libzcash::InvalidEncoding& no) const { return {}; }
};

class ViewingKeyEncoder
{
private:
	const KeyConstants& m_KeyConstants;

public:
	ViewingKeyEncoder(const KeyConstants& keyConstants) :
		m_KeyConstants(keyConstants)
	{}

    string operator()(const libzcash::SaplingExtendedFullViewingKey& extfvk) const
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << extfvk;
        // ConvertBits requires unsigned char, but CDataStream uses char
        v_uint8 serkey(ss.cbegin(), ss.cend());
        v_uint8 data;
        // See calculation comment below
        data.reserve((serkey.size() * 8 + 4) / 5);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, serkey.begin(), serkey.end());
        string ret = bech32::Encode(m_KeyConstants.Bech32HRP(KeyConstants::Bech32Type::SAPLING_EXTENDED_FVK), data);
        memory_cleanse(serkey.data(), serkey.size());
        memory_cleanse(data.data(), data.size());
        return ret;
    }

    string operator()(const libzcash::InvalidEncoding& no) const { return {}; }
};

class SpendingKeyEncoder
{
private:
	const KeyConstants& m_KeyConstants;

public:
	SpendingKeyEncoder(const KeyConstants& keyConstants) :
		m_KeyConstants(keyConstants)
	{}

    string operator()(const libzcash::SaplingExtendedSpendingKey& zkey) const
    {
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << zkey;
        // ConvertBits requires unsigned char, but CDataStream uses char
        v_uint8 serkey(ss.cbegin(), ss.cend());
        v_uint8 data;
        // See calculation comment below
        data.reserve((serkey.size() * 8 + 4) / 5);
        ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, serkey.begin(), serkey.end());
        string ret = bech32::Encode(m_KeyConstants.Bech32HRP(KeyConstants::Bech32Type::SAPLING_EXTENDED_SPEND_KEY), data);
        memory_cleanse(serkey.data(), serkey.size());
        memory_cleanse(data.data(), data.size());
        return ret;
    }

    string operator()(const libzcash::InvalidEncoding& no) const { return {}; }
};

// Sizes of SaplingPaymentAddress, SaplingExtendedFullViewingKey, and
// SaplingExtendedSpendingKey after ConvertBits<8, 5, true>(). The calculations
// below take the regular serialized size in bytes, convert to bits, and then
// perform ceiling division to get the number of 5-bit clusters.
const size_t ConvertedSaplingPaymentAddressSize = ((32 + 11) * 8 + 4) / 5;
const size_t ConvertedSaplingExtendedFullViewingKeySize = (ZIP32_XFVK_SIZE * 8 + 4) / 5;
const size_t ConvertedSaplingExtendedSpendingKeySize = (ZIP32_XSK_SIZE * 8 + 4) / 5;
} // namespace

CTxDestination KeyIO::DecodeDestination(const string& str)
{
    v_uint8 data;
    uint160 hash;
    if (DecodeBase58Check(str, data))
    {
        // base58-encoded Bitcoin addresses.
        // Public-key-hash-addresses have version 0 (or 111 testnet).
        // The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
        const auto& pubkey_prefix = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::PUBKEY_ADDRESS);
        if (data.size() == hash.size() + pubkey_prefix.size() && equal(pubkey_prefix.begin(), pubkey_prefix.end(), data.begin()))
        {
            copy(data.begin() + pubkey_prefix.size(), data.end(), hash.begin());
            return CKeyID(hash);
        }
        // Script-hash-addresses have version 5 (or 196 testnet).
        // The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
        const auto& script_prefix = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::SCRIPT_ADDRESS);
        if (data.size() == hash.size() + script_prefix.size() && equal(script_prefix.begin(), script_prefix.end(), data.begin()))
        {
            copy(data.begin() + script_prefix.size(), data.end(), hash.begin());
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
CKey KeyIO::DecodeSecret(const string& str, string& error)
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
            equal(privkey_prefix.cbegin(), privkey_prefix.cend(), data.cbegin()))
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
string KeyIO::EncodeSecret(const CKey& key)
{
    assert(key.IsValid());
    v_uint8 data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::SECRET_KEY);
    data.insert(data.end(), key.cbegin(), key.cend());
    // add "compressed" flag = 1
    if (key.IsCompressed())
        data.push_back(1);
    // base58 encoding
    string ret = EncodeBase58Check(data);
    memory_cleanse(data.data(), data.size());
    return ret;
}

CExtPubKey KeyIO::DecodeExtPubKey(const string& str)
{
    CExtPubKey key;
    v_uint8 data;
    if (DecodeBase58Check(str, data))
    {
        const auto& prefix = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::EXT_PUBLIC_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && equal(prefix.begin(), prefix.end(), data.begin()))
            key.Decode(data.data() + prefix.size());
    }
    return key;
}

string KeyIO::EncodeExtPubKey(const CExtPubKey& key)
{
    v_uint8 data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::EXT_PUBLIC_KEY);
    const size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    string ret = EncodeBase58Check(data);
    return ret;
}

CExtKey KeyIO::DecodeExtKey(const string& str)
{
    CExtKey key;
    v_uint8 data;
    if (DecodeBase58Check(str, data))
    {
        const auto& prefix = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::EXT_SECRET_KEY);
        if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && equal(prefix.cbegin(), prefix.cend(), data.cbegin()))
            key.Decode(data.data() + prefix.size());
    }
    return key;
}

string KeyIO::EncodeExtKey(const CExtKey& key)
{
    v_uint8 data = m_KeyConstants.Base58Prefix(KeyConstants::Base58Type::EXT_SECRET_KEY);
    const size_t size = data.size();
    data.resize(size + BIP32_EXTKEY_SIZE);
    key.Encode(data.data() + size);
    string ret = EncodeBase58Check(data);
    memory_cleanse(data.data(), data.size());
    return ret;
}

string KeyIO::EncodeDestination(const CTxDestination& dest)
{
    return visit(DestinationEncoder(m_KeyConstants), dest);
}

bool KeyIO::IsValidDestinationString(const string& str)
{
    return IsValidDestination(DecodeDestination(str));
}

string KeyIO::EncodePaymentAddress(const libzcash::PaymentAddress& zaddr)
{
    return visit(PaymentAddressEncoder(m_KeyConstants), zaddr);
}

template<typename T1, typename T2>
T1 DecodeSapling(
    const KeyConstants& keyConstants,
    const string& str,
    pair<KeyConstants::Bech32Type, size_t> sapling)
{
    v_uint8 data;
    auto bech = bech32::Decode(str);
    if (bech.first == keyConstants.Bech32HRP(sapling.first) &&
        bech.second.size() == sapling.second) {
        // Bech32 decoding
        data.reserve((bech.second.size() * 5) / 8);
        if (ConvertBits<5, 8, false>([&](unsigned char c) { data.push_back(c); }, bech.second.begin(), bech.second.end())) {
            CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
            T2 ret;
            ss >> ret;
            memory_cleanse(data.data(), data.size());
            return ret;
        }
    }

    memory_cleanse(data.data(), data.size());
    return libzcash::InvalidEncoding();
}

libzcash::PaymentAddress KeyIO::DecodePaymentAddress(const string& str)
{
    return DecodeSapling<libzcash::PaymentAddress, libzcash::SaplingPaymentAddress>
        (
            m_KeyConstants,
            str,
            make_pair(KeyConstants::Bech32Type::SAPLING_PAYMENT_ADDRESS, ConvertedSaplingPaymentAddressSize)
        );
}

bool KeyIO::IsValidPaymentAddressString(const string& str) {
    return IsValidPaymentAddress(DecodePaymentAddress(str));
}

string KeyIO::EncodeViewingKey(const libzcash::ViewingKey& vk)
{
    return visit(ViewingKeyEncoder(m_KeyConstants), vk);
}

libzcash::ViewingKey KeyIO::DecodeViewingKey(const string& str)
{
    return DecodeSapling<libzcash::ViewingKey, libzcash::SaplingExtendedFullViewingKey>
        (
            m_KeyConstants,
            str,
            make_pair(KeyConstants::Bech32Type::SAPLING_EXTENDED_FVK, ConvertedSaplingExtendedFullViewingKeySize)
        );
}

string KeyIO::EncodeSpendingKey(const libzcash::SpendingKey& zkey)
{
    return visit(SpendingKeyEncoder(m_KeyConstants), zkey);
}

libzcash::SpendingKey KeyIO::DecodeSpendingKey(const string& str)
{
    return DecodeSapling<libzcash::SpendingKey, libzcash::SaplingExtendedSpendingKey>
        (
            m_KeyConstants,
            str,
            make_pair(KeyConstants::Bech32Type::SAPLING_EXTENDED_SPEND_KEY, ConvertedSaplingExtendedSpendingKeySize)
        );
}
