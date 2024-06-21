#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <fstream>

#include <utils/enum_util.h>
#include <utils/vector_types.h>
#include <support/allocators/secure.h>

#include <extlibs/json.hpp>
#include <sodium.h>

namespace secure_container
{

constexpr uint16_t SECURE_CONTAINER_VERSION = 1;
constexpr auto SECURE_CONTAINER_ENCRYPTION = "xchacha20-poly1305";
// Pastel secure container prefix - used to detect new container
constexpr auto SECURE_CONTAINER_PREFIX = "PastelSecureContainer";

/**
 * List of possible secure item types in the secure container.
 */
enum class SECURE_ITEM_TYPE : uint8_t
{
    not_defined = 0,
    pkey_ed448 = 1,    // private key ed448
    pkey_legroast = 2, // LegRoast private key
    wallet = 3,        // wallet.dat
    COUNT              // +1
};

/**
 * List of possible secure item type names in the secure container.
 */
static constexpr const char* SECURE_ITEM_TYPE_NAMES[] =
    {
    "not defined",
    "pkey_ed448",
    "pkey_legroast",
    "wallet"
};

/**
 * List of possible public item types in the secure container.
 */
enum class PUBLIC_ITEM_TYPE : uint8_t {
    not_defined = 0,
    pubkey_legroast = 1, // LegRoast public key
    COUNT                
};

/**
 * List of possible public item type names in the secure container.
 */
static constexpr const char* PUBLIC_ITEM_TYPE_NAMES[] =
    {
    "not defined",
    "pubkey_legroast"
};

/**
 * Get secure item type string by type.
 * 
 * \param type - secure item type
 * \return secure item type name
 */
inline const char* GetSecureItemTypeName(const SECURE_ITEM_TYPE type)
{
    return SECURE_ITEM_TYPE_NAMES[to_integral_type<SECURE_ITEM_TYPE>(type)];
}

/**
 * Get public item type string by type.
 * 
 * \param type - public item type
 * \return public item type name
 */
inline const char* GetPublicItemTypeName(const PUBLIC_ITEM_TYPE type)
{
    return PUBLIC_ITEM_TYPE_NAMES[to_integral_type<PUBLIC_ITEM_TYPE>(type)];
}

/**
 * Get SECURE_ITEM_TYPE by name.
 * 
 * \param sType - secure item type name
 * \return - secure item type
 */
inline SECURE_ITEM_TYPE GetSecureItemTypeByName(const std::string& sType)
{
    SECURE_ITEM_TYPE ItemType = SECURE_ITEM_TYPE::not_defined;
    for (auto i = to_integral_type<SECURE_ITEM_TYPE>(SECURE_ITEM_TYPE::not_defined); 
            i < to_integral_type<SECURE_ITEM_TYPE>(SECURE_ITEM_TYPE::COUNT); ++i)
    {
        if (sType.compare(SECURE_ITEM_TYPE_NAMES[i]) == 0)
        {
            ItemType = static_cast<SECURE_ITEM_TYPE>(i);
            break;
        }
    }
    return ItemType;
}

/**
 * Get PUBLIC_ITEM_TYPE by name.
 * 
 * \param sType - public item type name
 * \return - public item type
 */
inline PUBLIC_ITEM_TYPE GetPublicItemTypeByName(const std::string& sType)
{
    PUBLIC_ITEM_TYPE ItemType = PUBLIC_ITEM_TYPE::not_defined;
    for (auto i = to_integral_type<PUBLIC_ITEM_TYPE>(PUBLIC_ITEM_TYPE::not_defined);
         i < to_integral_type<PUBLIC_ITEM_TYPE>(PUBLIC_ITEM_TYPE::COUNT); ++i)
    {
        if (sType.compare(PUBLIC_ITEM_TYPE_NAMES[i]) == 0)
        {
            ItemType = static_cast<PUBLIC_ITEM_TYPE>(i);
            break;
        }
    }
    return ItemType;
}

class ISecureDataHandler
{
public:
    virtual ~ISecureDataHandler() {}

    virtual bool GetSecureData(nlohmann::json::binary_t& data) const noexcept = 0;
    virtual void CleanupSecureData() = 0;
};

/**
 * Secure container used for storing public/private keys and other secure info.
 * 
 * Secure container has binary format:
 *     PastelSecureContainer(public_items_header)(public_items_msgpack)(secure_items_msgpack)
 * 
 * 
 * public_items_header:
 *     msgpack_public_items_size(datatype: uint64_t in network byte order) public_items_hash (256-bit)
 * 
 * json structure for public items, stored as msgpack:
 * {
 *    "version":1,
 *    "public_items": [
 *      {
 *          "type":"item_type_name",
 *          "data": binary_t
 *      },
 *      {
 *          "type":"item_type_name",
 *          "data": binary_t
 *      }
 *    ]
 * }
 *
 * json structure for secure items, stored as msgpack:
 * {
 *     "version":1,
 *     "timestamp": int64_t,
 *     "encryption": "xchacha20-poly1305",
 *     "secure_items": [
 *         {
 *             "type":"secure_item_type_name",
 *             "nonce": binary_t,
 *             "data": binary_t
 *         },
 *         {
 *             "type":"secure_item_type_name",
 *             "nonce": binary_t,
 *             "data": binary_t
 *         }
 *     ]
 * }
 */
class CSecureContainer
{
public:
    /*
    * secure item structure
    */
    using secure_item_t = struct _secure_item_t
    {
        _secure_item_t() : 
            type(SECURE_ITEM_TYPE::not_defined),
            pHandler(nullptr)
        {}
        _secure_item_t(const SECURE_ITEM_TYPE atype, const nlohmann::json::binary_t& adata, ISecureDataHandler* pDataHandler) : 
            type(atype),
            data(adata),
            pHandler(pDataHandler)
        {}

        void cleanup()
        {
            type = SECURE_ITEM_TYPE::not_defined;
            memory_cleanse(nonce.data(), nonce.size());
            memory_cleanse(data.data(), data.size());
            pHandler = nullptr;
        }

        SECURE_ITEM_TYPE type;
        nlohmann::json::binary_t nonce; // public nonce used to encrypt the data
        nlohmann::json::binary_t data;  // secure item data
        ISecureDataHandler* pHandler;
    };

    /*
    * public item structure
    */
    using public_item_t = struct _public_item_t
    {
        _public_item_t() : 
            type(PUBLIC_ITEM_TYPE::not_defined)
        {}
        _public_item_t(const PUBLIC_ITEM_TYPE atype, nlohmann::json::binary_t&& adata) : 
            type(atype),
            data(std::move(adata))
        {}

        PUBLIC_ITEM_TYPE type;
        nlohmann::json::binary_t data; // public item data
    };

    CSecureContainer() : 
        m_nVersion(SECURE_CONTAINER_VERSION),
        m_nTimestamp(-1)
    {}

    // clear the container
    void clear() noexcept;
    // add secure item to the container (data in a string)
    void add_secure_item_string(const SECURE_ITEM_TYPE type, const std::string& sData) noexcept;
    // add secure item to the container (data in a byte vector)
    void add_secure_item_vector(const SECURE_ITEM_TYPE type, const v_uint8& vData) noexcept;
    void add_secure_item_vector(const SECURE_ITEM_TYPE type, v_uint8&& vData) noexcept;
    // add secure item to the container(handler interface to get data)
    void add_secure_item_handler(const SECURE_ITEM_TYPE type, ISecureDataHandler* pHandler) noexcept;
    // add public item to the container
    void add_public_item(const PUBLIC_ITEM_TYPE type, const std::string& sData) noexcept;
    // encrypt and write container to file as a msgpack
    bool write_to_file(const std::string& sFilePath, SecureString&& sPassphrase);
    // read from secure container file encrypted secure data as a msgpack and decrypt
    bool read_from_file(const std::string& sFilePath, const SecureString& sPassphrase);
    // change passphrase that was used to encrypt the secure container
    bool change_passphrase(const std::string& sFilePath, SecureString&& sOldPassphrase, SecureString&& sNewPassphrase);
    // validate passphrase from secure container
    bool is_valid_passphrase(const std::string& sFilePath, const SecureString& sPassphrase) noexcept;
    // read from secure container file public data as a msgpack
    bool read_public_from_file(std::string &error, const std::string& sFilePath);
    // Get public data (byte vector) from the container by type
    bool get_public_data_vector(const PUBLIC_ITEM_TYPE type, v_uint8& data) const noexcept;
    bool get_public_data(const PUBLIC_ITEM_TYPE type, std::string &sData) const noexcept;
    // Extract secure data from the container by type (returns byte vector)
    v_uint8 extract_secure_data(const SECURE_ITEM_TYPE type);
    // Extract secure data from the container by type (returns string)
    std::string extract_secure_data_string(const SECURE_ITEM_TYPE type);

private:
    static constexpr size_t PWKEY_BUFSUZE = crypto_box_SEEDBYTES;

    // header
    uint16_t m_nVersion;                // container version
    int64_t m_nTimestamp;               // time stamp
    std::string m_sEncryptionAlgorithm; // encryption algorithm

    // vector of public items
    std::vector<public_item_t> m_vPublicItems;
    // vector of secure items
    std::vector<secure_item_t> m_vSecureItems;

    auto find_secure_item(const SECURE_ITEM_TYPE type) noexcept;
    auto find_public_item(const PUBLIC_ITEM_TYPE type) const noexcept;
    bool read_public_items_ex(std::ifstream& fs, uint64_t& nDataSize);
};

    class secure_container_exception : public std::runtime_error 
    {
    public:
        explicit secure_container_exception(const std::string &what) : std::runtime_error(what) { }
    };

} // namespace secure_container

/*
* Helper autoclass to allocate/free sodium buffer.
*/
class CSodiumAutoBuf
{
public:
    CSodiumAutoBuf() : 
        p(nullptr)
    {}
    ~CSodiumAutoBuf()
    {
        free();
    }
    bool allocate(const size_t nSize)
    {
        free();
        p = static_cast<unsigned char *>(sodium_malloc(nSize));
        return p != nullptr;
    }
    void free()
    {
        if (p)
            sodium_free(p);
    }

    unsigned char* p;
};
