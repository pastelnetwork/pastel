#pragma once

#include <sstream>
#include <iostream>
#include <fstream>
#include <memory>

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

#include "common.h"

//EdDSA uses small public keys ED25519 - 32 bytes; ED448 - 57 bytes
// and signatures ED25519 - 64 bytes; Ed448 - 114 bytes

//DER prefixes
// for private keys: 3047020100300506032b6571043b0439
// for public keys:  3043300506032b6571033a00


namespace ed_crypto {

    template<int type>
    class key {

        struct KeyCtxDeleterFunctor {
            void operator()(EVP_PKEY_CTX *ctx) {
                EVP_PKEY_CTX_free(ctx);
            }
        };
        struct KeyDeleterFunctor {
            void operator()(EVP_PKEY *pkey) {
                EVP_PKEY_free(pkey);
            }
        };
        struct FileCloserFunctor {
            void operator()(FILE *fp) {
                std::fclose(fp);
            }
        };

        using unique_key_ctx_ptr = std::unique_ptr<EVP_PKEY_CTX, KeyCtxDeleterFunctor>;
        using unique_file_ptr = std::unique_ptr<FILE, FileCloserFunctor>;

    public:
        using unique_key_ptr = std::unique_ptr<EVP_PKEY, KeyDeleterFunctor>;

        key(unique_key_ptr key) : key_(std::move(key)) {}

        EVP_PKEY* get() const {return key_.get();}

        static key generate_key() {

            unique_key_ctx_ptr ctx(EVP_PKEY_CTX_new_id(type, nullptr));
            if (!ctx) {
                throw (crypto_exception("Key context is NULL!", std::string(), "EVP_PKEY_CTX_new_id"));
            }

            EVP_PKEY_CTX *pctx = ctx.get();

            if (OK != EVP_PKEY_keygen_init(pctx)) {
                throw (crypto_exception("", std::string(), "EVP_PKEY_keygen_init"));
            }

            EVP_PKEY *pkey = nullptr;
            if (OK != EVP_PKEY_keygen(pctx, &pkey))
                throw (crypto_exception("", std::string(), "EVP_PKEY_keygen"));

            if (nullptr == pkey)
                throw (crypto_exception("Key is NULL!", std::string(), "EVP_PKEY_keygen"));

            unique_key_ptr uniqueKeyPtr(pkey);
            key key(std::move(uniqueKeyPtr));
            return key;
        }

        static key create_from_private(const std::string& privateKey, const std::string& passPhrase)
        {
            auto bio = stream::unique_bio_ptr(BIO_new_mem_buf(privateKey.c_str(), static_cast<int>(privateKey.size())));

            auto pPassPhrase = passPhrase.empty()? nullptr: const_cast<char*>(passPhrase.c_str());
            unique_key_ptr uniqueKeyPtr(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, pPassPhrase));
            if (!uniqueKeyPtr)
                throw (crypto_exception("Cannot read key from string", std::string(), "PEM_read_bio_PrivateKey"));

            key key(std::move(uniqueKeyPtr));
            return key;
        }

        static key create_from_public(const std::string& publicKey)
        {
            auto bio = stream::unique_bio_ptr(BIO_new_mem_buf(publicKey.c_str(), static_cast<int>(publicKey.size())));

            unique_key_ptr uniqueKeyPtr(PEM_read_bio_PUBKEY(bio.get(), nullptr, nullptr, nullptr));
            if (!uniqueKeyPtr)
                throw (crypto_exception("Cannot read public key from string", std::string(), "PEM_read_bio_PUBKEY"));

            key key(std::move(uniqueKeyPtr));
            return key;
        }

        static key create_from_raw_public(const unsigned char *rawkey, size_t keylen)
        {
            unique_key_ptr uniqueKeyPtr(EVP_PKEY_new_raw_public_key(type, nullptr, rawkey, keylen));
            if (!uniqueKeyPtr)
                throw (crypto_exception("Cannot read public key from string", std::string(), "EVP_PKEY_new_raw_public_key"));

            key key(std::move(uniqueKeyPtr));
            return key;
        }

        static key create_from_raw_public_hex(const std::string& rawPublicKey)
        {
            std::vector<unsigned char > vec = Hex_Decode(rawPublicKey);
            return create_from_raw_public(vec.data(), vec.size());
        }

        static key create_from_raw_public_base64(const std::string& rawPublicKey)
        {
            std::vector<unsigned char > vec = Base64_Decode(rawPublicKey);
            return create_from_raw_public(vec.data(), vec.size());
        }

        static key read_private_key_from_PKCS8_file(const std::string& fileName, const std::string& passPhrase)
        {
            std::ifstream file(fileName);
            if (!file)
                throw (crypto_exception("Cannot open file to read key from", fileName, "fopen"));

            std::stringstream buffer;
            buffer << file.rdbuf();
            return create_from_private(buffer.str(), passPhrase);
        }

        std::string public_key() const
        {
            return stream::bioToString(BIO_s_mem(), [this](BIO* bio)
            {
                return PEM_write_bio_PUBKEY(bio, key_.get());
            });
        }

        std::string private_key() const
        {
            return stream::bioToString(BIO_s_mem(), [this](BIO* bio)
            {
                return PEM_write_bio_PrivateKey(bio, key_.get(), nullptr, nullptr, 0, nullptr, nullptr);
            });
        }

        std::string private_key_in_PKCS8(std::string passPhrase) const
        {
            return stream::bioToString(BIO_s_mem(), [this, &passPhrase](BIO* bio)
            {
                auto ptr = const_cast<char*>(passPhrase.c_str());
                return PEM_write_bio_PKCS8PrivateKey(bio, key_.get(), EVP_aes_256_cbc(), nullptr, 0, nullptr, ptr);
            });
        }

        void write_private_key_to_PKCS8_file(std::string fileName, std::string passPhrase)
        {
            std::ofstream file(fileName);
            if (!file)
                throw (crypto_exception("Cannot open file to write the key", fileName, "fopen"));

            file << private_key_in_PKCS8(passPhrase);
        }

        buffer public_key_raw() const
        {
            std::size_t raw_key_len = 0;
            // Get length of the raw key
            if (OK != EVP_PKEY_get_raw_public_key(key_.get(), nullptr, &raw_key_len)) {
                throw (crypto_exception("Cannot get length of raw public key", std::string(), "EVP_PKEY_get_raw_public_key"));
            }
            if (0 == raw_key_len) {
                throw (crypto_exception("Returned length is 0!", std::string(), "EVP_PKEY_get_raw_public_key"));
            }

            // Allocate memory for the key based on size in raw_key_len
            auto prawkey = (unsigned char *) OPENSSL_malloc(raw_key_len);
            if (nullptr == prawkey)
                throw (crypto_exception("Returned buffer is NULL!", std::string(), "public_key_raw/OPENSSL_malloc"));

            // Obtain the raw key
            if (OK != EVP_PKEY_get_raw_public_key(key_.get(), prawkey, &raw_key_len))
                throw (crypto_exception("Cannot get raw public key", std::string(), "EVP_PKEY_get_raw_public_key"));

            buffer rawkey(prawkey, raw_key_len);
            return rawkey;
        }

        std::string public_key_raw_hex() const
        {
            return public_key_raw().Hex();
        }

        std::string public_key_raw_base64() const
        {
            return public_key_raw().Base64();
        }

        buffer private_key_raw() const
        {
            std::size_t raw_key_len = 0;
            // Get length of the raw key
            if (OK != EVP_PKEY_get_raw_private_key(key_.get(), nullptr, &raw_key_len)) {
                throw (crypto_exception("Cannot get length of raw public key", std::string(), "EVP_PKEY_get_raw_private_key"));
            }
            if (0 == raw_key_len) {
                throw (crypto_exception("Returned length is 0!", std::string(), "EVP_PKEY_get_raw_private_key"));
            }

            // Allocate memory for the key based on size in raw_key_len
            auto prawkey = (unsigned char *) OPENSSL_malloc(raw_key_len);
            if (nullptr == prawkey)
                throw (crypto_exception("Returned buffer is NULL!", std::string(), "private_key_raw/OPENSSL_malloc"));

            // Obtain the raw key
            if (OK != EVP_PKEY_get_raw_private_key(key_.get(), prawkey, &raw_key_len))
                throw (crypto_exception("Cannot get raw public key", std::string(), "EVP_PKEY_get_raw_private_key"));

            buffer rawkey(prawkey, raw_key_len);
            return rawkey;
        }

        std::string private_key_raw_hex() const
        {
            return private_key_raw().Hex();
        }

        std::string private_key_raw_base64() const
        {
            return private_key_raw().Base64();
        }

    private:

        buffer generate_shared_secret(key &remoteKey) {

            unique_key_ctx_ptr ctxDeriv(EVP_PKEY_CTX_new(key_.get(), nullptr));
            if (!ctxDeriv) {
                throw (crypto_exception("Derived Key context is NULL!", std::string(), "EVP_PKEY_CTX_new"));
            }

            EVP_PKEY_CTX *pctxDeriv = ctxDeriv.get();

            if (OK != EVP_PKEY_derive_init(pctxDeriv)) {
                throw (crypto_exception("", std::string(), "EVP_PKEY_derive_init"));
            }

            if (OK != EVP_PKEY_derive_set_peer(pctxDeriv, remoteKey.key_.get())) {
                throw (crypto_exception("", std::string(), "EVP_PKEY_derive_set_peer"));
            }

            std::size_t secret_len = 0;
            // Determine buffer length for shared secret
            if (OK != EVP_PKEY_derive(pctxDeriv, nullptr, &secret_len)) {
                throw (crypto_exception("", std::string(), "EVP_PKEY_derive"));
            }
            if (0 == secret_len) {
                throw (crypto_exception("Returned length is 0!", std::string(), "EVP_PKEY_derive"));
            }

            auto psecret = (unsigned char *) OPENSSL_malloc(secret_len);
            if (nullptr == psecret)
                throw (crypto_exception("Returned buffer is NULL!", std::string(), "OPENSSL_malloc"));

            // Derive the shared secret
            if (OK != EVP_PKEY_derive(pctxDeriv, psecret, &secret_len))
                throw (crypto_exception("", std::string(), "EVP_PKEY_derive"));

            buffer secret(psecret, secret_len);
            return secret;
        }

    private:
        unique_key_ptr key_;
    };

    //ed DSA
    class crypto_sign {

        struct MdCtxDeleterFunctor {
            void operator()(EVP_MD_CTX *ctx) {
                EVP_MD_CTX_destroy(ctx);
            }
        };

        using unique_md_ctx_ptr = std::unique_ptr<EVP_MD_CTX, MdCtxDeleterFunctor>;

    public:

        crypto_sign() = default;

        template<int type>
        static buffer sign(const unsigned char* message, std::size_t length, const key<type>& secret_key)
        {
            unique_md_ctx_ptr ctx(EVP_MD_CTX_create());
            if (!ctx) throw (crypto_exception("MD context is NULL!", std::string(), "EVP_MD_CTX_create"));

            EVP_MD_CTX *mdctx = ctx.get();
            EVP_PKEY *pkey = secret_key.get();

            // Initialise the DigestSign operation - EdDSA has builtin digest function
            if (OK != EVP_DigestSignInit(mdctx, nullptr, nullptr, nullptr, pkey)) {
                throw (crypto_exception("", std::string(), "EVP_DigestSignInit"));
            }

            std::size_t signature_len = 0;
            // Get length of the signature
            if (OK != EVP_DigestSign(mdctx, nullptr, &signature_len, message, length)) {
                throw (crypto_exception("", std::string(), "EVP_DigestSign"));
            }
            if (0 == signature_len) {
                throw (crypto_exception("Returned length is 0!", std::string(), "EVP_DigestSign"));
            }

            // Allocate memory for the signature based on size in slen
            auto psignature = (unsigned char *) OPENSSL_malloc(signature_len);
            if (nullptr == psignature)
                throw (crypto_exception("Returned buffer is NULL!", std::string(), "OPENSSL_malloc"));

            // Obtain the signature
            if (OK != EVP_DigestSign(mdctx, psignature, &signature_len, message, length))
                throw (crypto_exception("", std::string(), "EVP_DigestSign"));

            buffer signature(psignature, signature_len);
            return signature;
        }

        template<int type>
        static buffer sign_base64(const std::string& messageBase64, const key<type>& secret_key)
        {
            std::vector<unsigned char > vec = Base64_Decode(messageBase64);
            return sign(vec.data(), vec.size(), secret_key);
        }

        template<int type>
        static buffer sign_hex(const std::string& messageHex, const key<type>& secret_key)
        {
            std::vector<unsigned char > vec = Hex_Decode(messageHex);
            return sign(vec.data(), vec.size(), secret_key);
        }

        template<int type>
        static buffer sign(const std::string& message, const key<type>& secret_key)
        {
            return sign(reinterpret_cast <const unsigned char*>(message.c_str()), message.length(), secret_key);
        }
    
        template<int type>
        static bool verify(const unsigned char* message, std::size_t msglen, const unsigned char* signature, std::size_t siglen, const key<type>& public_key)
        {
            unique_md_ctx_ptr ctx(EVP_MD_CTX_create());
            if (!ctx) throw (crypto_exception("MD context is NULL!", std::string(), "EVP_MD_CTX_create"));
    
            EVP_MD_CTX *mdctx = ctx.get();
            EVP_PKEY *pkey = public_key.get();
    
            // Initialise the DigestVerify operation - EdDSA has builtin digest function
            if (OK != EVP_DigestVerifyInit(mdctx, nullptr, nullptr, nullptr, pkey)) {
                throw (crypto_exception("", std::string(), "EVP_DigestVerifyInit"));
            }
    
            // Verify the signature
            return (OK == EVP_DigestVerify(mdctx, signature, siglen, message, msglen));
        }
        
        template<int type>
        static bool verify(const std::string& message, const unsigned char* signature, std::size_t siglen, const key<type>& public_key)
        {
            return verify(reinterpret_cast <const unsigned char*>(message.c_str()), message.length(), signature, siglen, public_key);
        }
    
        template<int type>
        static bool verify(const std::string& message, const std::string& signature, const key<type>& public_key)
        {
            return verify(reinterpret_cast <const unsigned char*>(message.c_str()), message.length(),
                          reinterpret_cast <const unsigned char*>(signature.c_str()), signature.length(), public_key);
        }

        template<int type>
        static bool verify_base64(const std::string& message, const std::string& signatureBase64, const key<type>& public_key)
        {
            std::vector<unsigned char > vec = Base64_Decode(signatureBase64);
            return verify(message, vec.data(), vec.size(), public_key);
        }

        template<int type>
        static bool verify_hex(const std::string& message, const std::string& signatureHex, const key<type>& public_key)
        {
            std::vector<unsigned char > vec = Hex_Decode(signatureHex);
            return verify(message, vec.data(), vec.size(), public_key);
        }
    };

    //ed DH
    class crypto_box {
        static std::string encrypt()
        {
            return std::string();
        }

        static std::string decrypt()
        {
            return std::string();
        }
    };

    using key_dsa448 = key<EVP_PKEY_ED448>;
    using key_dh448 = key<EVP_PKEY_X448>;
    using key_dsa25519 = key<EVP_PKEY_ED25519>;
    using key_hd25519 = key<EVP_PKEY_X25519>;
}