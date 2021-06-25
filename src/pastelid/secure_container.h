#pragma once
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "enum_util.h"
#include "support/allocators/secure.h"

#include <string>
#include <vector>

#include "json/json.hpp"
#include "sodium.h"

using bytes = std::vector<std::byte>;

constexpr uint16_t SECURE_CONTAINER_VERSION = 1;
constexpr auto SECURE_CONTAINER_ENCRYPTION = "xchacha20-poly1305";

enum class SECURE_ITEM_TYPE : uint8_t
{
    not_defined = 0,
    pkey_ed448 = 1,    // private key ed448
    pkey_legroast = 2, // private key
    wallet = 3,        // wallet.dat
    COUNT // +1
};

static constexpr const char * SECURE_ITEM_TYPE_NAMES[] = 
{
    "not defined",
    "pkey_ed448", 
    "pkey_legroast", 
    "wallet"
};

inline const char* GetSecureItemTypeName(const SECURE_ITEM_TYPE type) 
{
    return SECURE_ITEM_TYPE_NAMES[to_integral_type<SECURE_ITEM_TYPE>(type)];
}

/**
 * Get SECURE_ITEM_TYPE by name.
 * 
 * \param sType
 * \return 
 */
inline SECURE_ITEM_TYPE GetSecureItemTypeByName(const std::string &sType)
{
    SECURE_ITEM_TYPE ItemType = SECURE_ITEM_TYPE::not_defined;
    for (auto i = to_integral_type<SECURE_ITEM_TYPE>(SECURE_ITEM_TYPE::not_defined); i < 
            to_integral_type<SECURE_ITEM_TYPE>(SECURE_ITEM_TYPE::COUNT); ++i)
    {
        if (sType.compare(SECURE_ITEM_TYPE_NAMES[i]) == 0)
        {
            ItemType = static_cast<SECURE_ITEM_TYPE>(i);
            break;
        }
    }
    return ItemType;
}

class ISecureDataHandler
{
public:
    virtual ~ISecureDataHandler() {}

    virtual void Set(const nlohmann::json::binary_t& data) = 0;
    virtual bool Get(nlohmann::json::binary_t& data) const noexcept = 0;
    virtual void Cleanup() = 0;
};

class CSecureContainer
{
public:
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

       SECURE_ITEM_TYPE type;
       nlohmann::json::binary_t nonce; // public nonce used to encrypt the data
       nlohmann::json::binary_t data;  // secure item data
       ISecureDataHandler* pHandler;
   };

   CSecureContainer() : 
       m_nVersion(SECURE_CONTAINER_VERSION),
        m_nTimestamp(-1)
   {}

   // clear the container
   void clear() noexcept;
   // add secure item to the container
   void add_item(const SECURE_ITEM_TYPE type, const nlohmann::json::binary_t& data, ISecureDataHandler* pHandler = nullptr) noexcept;
   // encrypt and write container to file as a msgpack
   bool write_to_file(const std::string &sFilePath, SecureString &&sPassphrase);
   // read from file secure container data encoded as a msgpack and decrypt
   bool read_from_file(const std::string& sFilePath, SecureString&& sPassphrase);

   nlohmann::json::binary_t extract_data(const SECURE_ITEM_TYPE type);

private:
    static constexpr size_t PWKEY_BUFSUZE = crypto_box_SEEDBYTES;

    // header
    uint16_t m_nVersion;  // container version
    int64_t m_nTimestamp; // time stamp
    std::string m_sEncryptionAlgorithm; // encryption algorithm

    // vector of secure items
    std::vector<secure_item_t> m_vItems;

    auto find_item(const SECURE_ITEM_TYPE type) noexcept;
};

/*
* json structure for secure container (stored as msgpack):
{
    "version":1,
    "timestamp": int64_t,
    "encryption": "",
    "items": [
        {
            "type":"secure_item_type_name",
            "nonce": binary_t,
            "data": binary_t
        },
        {
            "type":"secure_item_type_name",
            "nonce": binary_t,
            "data": binary_t
        }
    ]
}
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
