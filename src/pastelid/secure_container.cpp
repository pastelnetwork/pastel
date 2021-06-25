#include "pastelid/secure_container.h"
#include "tinyformat.h"

#include <fstream>
#include <vector>
#include <algorithm>
using namespace std;

/**
 * Add secure item to the container.
 * 
 * \param type - item type
 * \param data - data to encrypt (can be empty if pHandler is used)
 * \param pHandler - interface to set/get secure data for the item
 */
void CSecureContainer::add_item(const SECURE_ITEM_TYPE type, const nlohmann::json::binary_t& data, ISecureDataHandler* pHandler) noexcept
{
    m_vItems.emplace_back(type, data, pHandler);
}

/**
 * Encrypt and save secure container to the file in msgpack format.
 * Throws std::runtime_error exception in case of failure.
 * 
 * \param sFilePath - container file path
 * \param sPassphrase - passphrase in clear text to use for encryption
 * \return true if file was successfully written
 */
bool CSecureContainer::write_to_file(const string& sFilePath, SecureString&& sPassphrase)
{
    using json = nlohmann::ordered_json;
    bool bRet = false;
    do
    {
        m_nTimestamp = time(nullptr);
        // generate header
        nlohmann::ordered_json j =
        {
                {"version", SECURE_CONTAINER_VERSION},
                {"timestamp", m_nTimestamp},
                {"encryption", SECURE_CONTAINER_ENCRYPTION}
        };
        json jItems;
        CSodiumAutoBuf pw;
        // allocate secure memory for the key, buffer is reused for all secure items
        if (!pw.allocate(PWKEY_BUFSUZE))
            throw runtime_error(strprintf("Failed to allocate memory (%zu bytes)", PWKEY_BUFSUZE));
        // encryption buffer is reused for all messages
        json::binary_t encrypted_data;
        for (auto& item : m_vItems)
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
                if (!item.pHandler->Get(item.data))
                    throw runtime_error(strprintf("Failed to get '%s' data", GetSecureItemTypeName(item.type)));
                // possibility for caller to cleanup data
                item.pHandler->Cleanup();
            }
            // encrypt data using XChaCha20-Poly1305 construction
            unsigned long long nEncSize = 0;
            encrypted_data.resize(item.data.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
            if (crypto_aead_xchacha20poly1305_ietf_encrypt(encrypted_data.data(), &nEncSize,
                                                           item.data.data(), item.data.size(), nullptr, 0, nullptr, item.nonce.data(), pw.p) != 0)
                throw runtime_error(strprintf("Failed to encrypt '%s' data", GetSecureItemTypeName(item.type)));
            jItems.push_back({
                {"type", GetSecureItemTypeName(item.type)},
                {"nonce", move(item.nonce)},
                {"data", move(encrypted_data)}
            });
        }
        j.emplace("items", move(jItems));

        // serialize as msgpack to file
        ofstream f(sFilePath, ios::out | ios::binary);
        if (!f)
            throw runtime_error(strprintf("Cannot open file [%s] to write the secure container", sFilePath.c_str()));
        const auto vOut = json::to_msgpack(j);
        f.write(reinterpret_cast<const char*>(vOut.data()), vOut.size());
        f.close();
        bRet = true;
    } while (false);
    return bRet;
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
    m_vItems.clear();
}

/**
 * Read from file secure container data encoded as a msgpack and decrypt.
 * Throws std::runtime_error exception in case of failure.
 * 
 * \param sFilePath - container file path
 * \param sPassphrase - passphrase in clear text to use for data decryption
 * \return true if file was successfully read and decrypted
 */
bool CSecureContainer::read_from_file(const string& sFilePath, SecureString&& sPassphrase)
{
    using json = nlohmann::json;
    bool bRet = false;

    try
    {
        ifstream fs(sFilePath, ios::in | ios::ate | ios::binary);
        fs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        vector<uint8_t> v;
        v.resize(fs.tellg());
        fs.seekg(0);
        fs.read(reinterpret_cast<char*>(v.data()), v.size());
        json j = json::from_msgpack(v);
        v.clear();

        clear();

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
        for (auto &jItem : j.at("items"))
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
                throw runtime_error(strprintf("Failed to generate encryption key for '%s'", GetSecureItemTypeName(item.type)));
            }
            item.data.resize(encrypted_data.size());
            unsigned long long nDecryptedLength = 0;
            if (crypto_aead_xchacha20poly1305_ietf_decrypt(item.data.data(), &nDecryptedLength, nullptr,
                    encrypted_data.data(), encrypted_data.size(), nullptr, 0, item.nonce.data(), pw.p) != 0)
            {
                throw runtime_error(strprintf("Failed to decrypt '%s' data", sType));
            }
            item.data.resize(nDecryptedLength);
            m_vItems.push_back(move(item));
        }
        bRet = true;
    }
    catch (const std::out_of_range &ex)
    {
        throw runtime_error(strprintf("Secure container file format error. %s", ex.what()));
    }
    catch (const std::exception &ex)
    {
        throw runtime_error(strprintf("Failed to read secure container file [%s]. %s", sFilePath.c_str(), ex.what()));
    }
    return bRet;
}

auto CSecureContainer::find_item(const SECURE_ITEM_TYPE type) noexcept
{
    return find_if(m_vItems.begin(), m_vItems.end(), [=](const auto& Item) { return Item.type == type; });
}

/**
 * Extract secure data from the container by type.
 * 
 * \param type
 * \return
 */
nlohmann::json::binary_t CSecureContainer::extract_data(const SECURE_ITEM_TYPE type)
{
    auto it = find_item(type);
    if (it != m_vItems.end())
        return move(it->data);
    return nlohmann::json::binary_t();
}
