#include "pastelid/secure_container.h"
#include "tinyformat.h"
#include "compat/endian.h"
#include "vector_types.h"
#include "hash.h"

#include <algorithm>
using namespace std;
using namespace secure_container;

/**
 * Add secure item to the container (data in a string).
 * 
 * \param type - item type
 * \param sData - data string to encrypt
 */
void CSecureContainer::add_secure_item_string(const SECURE_ITEM_TYPE type, const std::string& sData) noexcept
{
    m_vSecureItems.emplace_back(type, nlohmann::json::binary_t(move(string_to_vector(sData))), nullptr);
}

/**
 * Add secure item to the container (data in a byte vector).
 * 
 * \param type - item type
 * \param vData - data in a byte vector to encrypt
 */
void CSecureContainer::add_secure_item_vector(const SECURE_ITEM_TYPE type, const v_uint8& vData) noexcept
{
    m_vSecureItems.emplace_back(type, nlohmann::json::binary_t(vData), nullptr);
}

void CSecureContainer::add_secure_item_vector(const SECURE_ITEM_TYPE type, v_uint8&& vData) noexcept
{
    m_vSecureItems.emplace_back(type, nlohmann::json::binary_t(move(vData)), nullptr);
}

/**
 * Add secure item to the container (handler interface to get data).
 * 
 * \param type - item type
 * \param sData - data string to encrypt
 * \param pHandler - interface to set/get secure data for the item
 */
void CSecureContainer::add_secure_item_handler(const SECURE_ITEM_TYPE type, ISecureDataHandler* pHandler) noexcept 
{
    m_vSecureItems.emplace_back(type, nlohmann::json::binary_t(), pHandler);
}

/**
 * Add public item to the secure container.
 * 
 * \param type - public item type
 * \param sData - public item data
 */
void CSecureContainer::add_public_item(const PUBLIC_ITEM_TYPE type, const std::string& sData) noexcept
{
     m_vPublicItems.emplace_back(type, move(string_to_vector(sData)));
}

/**
 * Encrypt and save secure container to the file in msgpack format.
 * Throws std::runtime_error exception in case of failure.
 * 
 * \param sFilePath - container file path
 * \param sPassphrase - passphrase in clear text to use for encryption
 * \return true if file was successfully written
 */
bool CSecureContainer::write_to_file(const string& sFilePath, const SecureString& sPassphrase)
{
    using json = nlohmann::ordered_json;
    m_nTimestamp = time(nullptr);
    json jItems;
    // generate json for the public items
    json jPublic =
    {
            {"version", SECURE_CONTAINER_VERSION }
    };
    size_t nJsonPublicSize = 20; // used to estimate size of the json with public items
    
    for (const auto& item: m_vPublicItems)
    {
        const auto szTypeName = GetPublicItemTypeName(item.type);
        jItems.push_back({
                {"type", szTypeName},
                {"data", item.data}
            });
        nJsonPublicSize += 25 + strlen(szTypeName) + item.data.size();
    }
    jPublic.emplace("public_items", move(jItems));
    jItems.clear();

    // generate a json header for the secure items
    json jSecure =
    {
            {"version", SECURE_CONTAINER_VERSION},
            {"timestamp", m_nTimestamp},
            {"encryption", SECURE_CONTAINER_ENCRYPTION}
    };
    size_t nJsonSecureSize = 200; // used to estimate size of the json with secure items
    CSodiumAutoBuf pw;
    // allocate secure memory for the key, buffer is reused for all secure items
    if (!pw.allocate(PWKEY_BUFSUZE))
        throw runtime_error(strprintf("Failed to allocate memory (%zu bytes)", PWKEY_BUFSUZE));
    // encryption buffer is reused for all messages
    json::binary_t encrypted_data;
    for (auto& item : m_vSecureItems)
    {
        // generate nonce
        item.nonce.resize(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
        randombytes_buf(item.nonce.data(), item.nonce.size());
        // derive key from the passphrase
        if (crypto_pwhash(pw.p, crypto_box_SEEDBYTES,
            sPassphrase.c_str(), sPassphrase.length(), item.nonce.data(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_DEFAULT) != 0)
        {
            throw runtime_error(strprintf("Failed to generate encryption key for '%s'", GetSecureItemTypeName(item.type)));
        }
        // if data handler is defined -> use it to get secure data
        if (item.pHandler)
        {
            if (!item.pHandler->GetSecureData(item.data))
                throw runtime_error(strprintf("Failed to get '%s' data", GetSecureItemTypeName(item.type)));
            // possibility for caller to cleanup data
            item.pHandler->CleanupSecureData();
        }
        // encrypt data using XChaCha20-Poly1305 construction
        unsigned long long nEncSize = 0;
        encrypted_data.resize(item.data.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
        if (crypto_aead_xchacha20poly1305_ietf_encrypt(encrypted_data.data(), &nEncSize,
                                                       item.data.data(), item.data.size(), nullptr, 0, nullptr, item.nonce.data(), pw.p) != 0)
            throw runtime_error(strprintf("Failed to encrypt '%s' data", GetSecureItemTypeName(item.type)));
        const auto szTypeName = GetSecureItemTypeName(item.type);
        jItems.push_back({
            {"type", szTypeName},
            {"nonce", move(item.nonce)},
            {"data", move(encrypted_data)}
        });
        nJsonSecureSize += 50 + strlen(szTypeName) + item.nonce.size() + encrypted_data.size();
    }
    jSecure.emplace("secure_items", move(jItems));

    // serialize as a msgpack to file
    ofstream fs(sFilePath, ios::out | ios::binary);
    if (!fs)
        throw runtime_error(strprintf("Cannot open file [%s] to write the secure container", sFilePath.c_str()));
    fs.write(SECURE_CONTAINER_PREFIX, std::char_traits<char>::length(SECURE_CONTAINER_PREFIX));
    v_uint8 vOut;
    const auto nMsgPackReserve = std::max(nJsonPublicSize, nJsonSecureSize);
    vOut.reserve(nMsgPackReserve);
    // write json for public items to the file serialized into msgpack format
    json::to_msgpack(jPublic, vOut);
    jPublic.clear();
    // write msgpack size in network byte order (big endian)
    const uint64_t nMsgPackSize = htobe64(vOut.size());
    fs.write(reinterpret_cast<const char*>(&nMsgPackSize), sizeof(nMsgPackSize));
    // calculate and write hash of the msgpack
    const auto hash = Hash(vOut.cbegin(), vOut.cend());
    hash.Serialize(fs);
    // write public items in msgpack format
    fs.write(reinterpret_cast<const char*>(vOut.data()), vOut.size());
    vOut.clear();

    // write json for secure items to the file serialized into msgpack format
    json::to_msgpack(jSecure, vOut);
    jSecure.clear();
    fs.write(reinterpret_cast<const char*>(vOut.data()), vOut.size());
    return true;
}

/**
 * Clear the container.
 * 
 */
void CSecureContainer::clear() noexcept
{
    m_nVersion = 0; // version not defined
    m_nTimestamp = -1;
    m_sEncryptionAlgorithm.clear();
    for (auto& item : m_vSecureItems)
        item.cleanup();
    m_vSecureItems.clear();
    m_vPublicItems.clear();
}

/**
 * Read from secure container header and public items.
 * 
 * \param fs - input file stream
 * \param nDataSize - returns 
 * \return true - if public items were successfully read. In this case current position is fs 
 *                will be set to the beginning of the secure items msgpack.
 *         false - if secure container prefix does not match
 *                throws runtime_error if any error occurred while reading secure container public data
 */
bool CSecureContainer::read_public_items_ex(ifstream& fs, uint64_t& nDataSize)
{
    using json = nlohmann::json;
    bool bRet = false;
    do
    {
        // get file size
        const auto nFileSize = fs.tellg();
        if (nFileSize < 0)
            break;
        nDataSize = static_cast<uint64_t>(nFileSize);
        // read prefix from the file and compare with SECURE_CONTAINER_PREFIX
        constexpr auto nPrefixLength = std::char_traits<char>::length(SECURE_CONTAINER_PREFIX);
        if (nDataSize < nPrefixLength)
            break;
        char szPrefix[nPrefixLength + 1];
        fs.seekg(0);
        fs.read(szPrefix, nPrefixLength);
        if (fs.gcount() != nPrefixLength)
            break;
        szPrefix[nPrefixLength] = 0;
        // check if prefix matches
        if (strcmp(szPrefix, SECURE_CONTAINER_PREFIX) != 0)
            break;
        nDataSize -= nPrefixLength;
        // here we should have two fields:
        // [ size of the public items msgpack in network bytes order - uint64_t, 8-bytes] [ hash of the public items msgpack, uint256, 32-bytes ] 
        if (nDataSize < sizeof(uint64_t) + uint256::SIZE)
            throw runtime_error("No public data found in the secure container");
        uint64_t nMsgPackSize = 0;
        v_uint8 vHash;
        vHash.resize(uint256::SIZE);
        fs.read(reinterpret_cast<char*>(&nMsgPackSize), sizeof(uint64_t))
          .read(reinterpret_cast<char*>(vHash.data()), vHash.size());
        nDataSize -= sizeof(uint64_t) + uint256::SIZE;
        // convert size to host order
        nMsgPackSize = be64toh(nMsgPackSize);
        if (nMsgPackSize > nDataSize)
            throw runtime_error(strprintf("Invalid size [%zu] for the public data in the secure container", nMsgPackSize));
        // read public data from the secure container as msgpack
        v_uint8 v;
        v.resize(nMsgPackSize);
        fs.read(reinterpret_cast<char*>(v.data()), v.size());
        // verify hash
        const auto MsgPackHash = Hash(v.cbegin(), v.cend());
        if (memcmp(&MsgPackHash, vHash.data(), uint256::SIZE) != 0)
            throw runtime_error("Failed to verify public data integrity in the secure container");
        nDataSize -= nMsgPackSize;
        json j = json::from_msgpack(v);
        v.clear();
        // process public items
        string sType;
        for (auto& jItem : j.at("public_items"))
        {
            jItem["type"].get_to(sType);
            public_item_t item;
            item.type = GetPublicItemTypeByName(sType);
            if (item.type == PUBLIC_ITEM_TYPE::not_defined)
                throw runtime_error(strprintf("Public item type '%s' is not supported in the secure container", sType));
            item.data = move(jItem["data"].get_binary());
            m_vPublicItems.push_back(move(item));
        }

        bRet = true;
    } while (false);
    return bRet;
}

/**
 * Read from secure container file public data as a msgpack.
 * 
 * \param error - error message
 * \param sFilePath - container file path
 * \return true if public items were successfully read from the container 
 */
bool CSecureContainer::read_public_from_file(string &error, const string& sFilePath)
{
    clear();

    bool bRet = false;
    try
    {
    	ifstream fs(sFilePath, ios::in | ios::ate | ios::binary);
	    fs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
	    uint64_t nDataSize = 0;
    	bRet = read_public_items_ex(fs, nDataSize);
    }
    catch (const system_error &ex)
    {
        error = strprintf("Failed to read public items from secure container [%s]. %s", sFilePath, ex.code().message());
    }
    return bRet;
}

/**
 * Read from secure container file public and secure data encoded as a msgpack.
 * Decrypt secure data. Throws std::runtime_error exception in case of failure.
 * 
 * \param sFilePath - container file path
 * \param sPassphrase - passphrase in clear text to use for data decryption
 * \return true if file was successfully read and decrypted
 *         false if file does not contain Pastel secure container
 *         if container data cannot be read or decrypted - throws std::runtime_error
 */
bool CSecureContainer::read_from_file(const string& sFilePath, const SecureString& sPassphrase)
{
    using json = nlohmann::json;
    bool bRet = false;
    try
    {
        do
        {
            clear();

            ifstream fs(sFilePath, ios::in | ios::ate | ios::binary);
            fs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
            v_uint8 v;
            uint64_t nDataSize = 0;
            if (!read_public_items_ex(fs, nDataSize))
                break;
            // read secure container data as json msgpack
            v.resize(nDataSize);
            fs.read(reinterpret_cast<char*>(v.data()), v.size());
            json j = json::from_msgpack(v);
            v.clear();

            // read header
            j.at("version").get_to(m_nVersion);
            j.at("timestamp").get_to(m_nTimestamp);
            j.at("encryption").get_to(m_sEncryptionAlgorithm);
            if (m_sEncryptionAlgorithm.compare(SECURE_CONTAINER_ENCRYPTION) != 0)
                throw runtime_error(strprintf("Encryption algorithm '%s' is not supported", m_sEncryptionAlgorithm.c_str()));

            CSodiumAutoBuf pw;
            // allocate secure memory for the key, buffer is reused for all secure items
            if (!pw.allocate(PWKEY_BUFSUZE))
                throw runtime_error(strprintf("Failed to allocate memory (%zu bytes)", PWKEY_BUFSUZE));

            // process encrypted items
            // read nonce for each item and use it to derive password key from passphrase and 
            // to decrypt data
            string sType;
            for (auto &jItem : j.at("secure_items"))
            {
                jItem["type"].get_to(sType);
                secure_item_t item;
                item.type = GetSecureItemTypeByName(sType);
                if (item.type == SECURE_ITEM_TYPE::not_defined)
                    throw runtime_error(strprintf("Secure item type '%s' is not supported", sType));
                jItem["nonce"].get_to(item.nonce);
                // encrypted data
                auto& encrypted_data = jItem["data"].get_binary();

                // derive key from the passphrase
                if (crypto_pwhash(pw.p, crypto_box_SEEDBYTES,
                                  sPassphrase.c_str(), sPassphrase.length(), item.nonce.data(),
                                  crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE, crypto_pwhash_ALG_DEFAULT) != 0)
                {
                    throw runtime_error(strprintf("Failed to generate encryption key for the secure item '%s'", GetSecureItemTypeName(item.type)));
                }
                item.data.resize(encrypted_data.size());
                unsigned long long nDecryptedLength = 0;
                if (crypto_aead_xchacha20poly1305_ietf_decrypt(item.data.data(), &nDecryptedLength, nullptr,
                        encrypted_data.data(), encrypted_data.size(), nullptr, 0, item.nonce.data(), pw.p) != 0)
                {
                    throw runtime_error(strprintf("Failed to decrypt secure item '%s' data", sType));
                }
                item.data.resize(nDecryptedLength);
                m_vSecureItems.push_back(move(item));
            }
            bRet = true;
        } while (false);
    }
    catch (const std::out_of_range &ex)
    {
        throw runtime_error(strprintf("Pastel secure container file format error. %s", ex.what()));
    }
    catch (const std::exception &ex)
    {
        throw runtime_error(strprintf("Failed to read Pastel secure container file [%s]. %s", sFilePath.c_str(), ex.what()));
    }
    return bRet;
}

/**
 * Find secure item in the container by type.
 * 
 * \param type - secure item type to find
 * \return 
 */
auto CSecureContainer::find_secure_item(const SECURE_ITEM_TYPE type) noexcept
{
    return find_if(m_vSecureItems.begin(), m_vSecureItems.end(), [=](const auto& Item) { return Item.type == type; });
}

/**
 * Find public item in the container by type.
 * 
 * \param type - secure item type to find
 * \return 
 */
auto CSecureContainer::find_public_item(const PUBLIC_ITEM_TYPE type) const noexcept
{
    return find_if(m_vPublicItems.cbegin(), m_vPublicItems.cend(), [=](const auto& Item) { return Item.type == type; });
}

/**
 * Get public data (byte vector) from the container by type.
 * 
 * \param type - public item type
 * \param data - public binary data
 * \return true if public item was found in the secure container
 */
bool CSecureContainer::get_public_data_vector(const PUBLIC_ITEM_TYPE type, v_uint8& data) const noexcept
{
    const auto it = find_public_item(type);
    if (it != m_vPublicItems.cend())
    {
        data = it->data;
        return true;
    }
    return false;
}

/**
 * Get public data (string) from the container by type.
 * 
 * \param type - public item type
 * \param sData - public string data
 * \return true if public item was found in the secure container
 */
bool CSecureContainer::get_public_data(const PUBLIC_ITEM_TYPE type, std::string& sData) const noexcept
{
    const auto it = find_public_item(type);
    if (it != m_vPublicItems.cend())
    {
        sData.assign(it->data.cbegin(), it->data.cend());
        return true;
    }
    return false;
}

/**
 * Extract secure data from the container by type (byte vector).
 * 
 * \param type - secure item type
 * \return - secure data in byte vector (moved from storage)
 */
v_uint8 CSecureContainer::extract_secure_data(const SECURE_ITEM_TYPE type)
{
    auto it = find_secure_item(type);
    if (it != m_vSecureItems.end())
        return move(it->data);
    return v_uint8();
}

/**
 * Extract secure data from the container by type (string).
 * 
 * \param type - secure item type
 * \return - secure data (moved from storage)
 */
string CSecureContainer::extract_secure_data_string(const SECURE_ITEM_TYPE type)
{
    auto it = find_secure_item(type);
    string sData;
    if (it != m_vSecureItems.end())
    {
        sData.assign(reinterpret_cast<const char *>(it->data.data()), it->data.size());
        memory_cleanse(it->data.data(), it->data.size());
        it->data.clear();
    }
    return sData;
}
