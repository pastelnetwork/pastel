#pragma once

#include <iomanip>

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/crypto.h>
#include "vector_types.h"

namespace ed_crypto {

    static constexpr int OK = 1;
    static constexpr auto BASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    inline std::string Base64_Encode(const unsigned char *in, const size_t len) noexcept
    {
        std::string out;
        out.reserve(static_cast<size_t>(ceil(len / 3) * 4));
        int val=0, valb=-6;

        for (size_t i = 0; i < len; i++)
        {
            val = (val << 8) + in[i];
            valb += 8;
            while (valb >= 0)
            {
                out.push_back(BASE64[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb>-6) 
            out.push_back(BASE64[((val << 8) >> (valb + 8)) & 0x3F]);
        while (out.size() % 4)
            out.push_back('=');
        return out;
    }
	
    inline v_uint8 Base64_Decode(const std::string &in) noexcept
    {
        v_uint8 out;
        out.reserve((in.size() / 4) * 3);

        std::vector<int> T(256,-1);
        for (int i=0; i<64; i++)
            T[BASE64[i]] = i;

        int val=0, valb=-8;
        for (unsigned char c : in)
        {
            if (T[c] == -1)
                break;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0)
            {
                out.push_back( (unsigned char)((val >> valb) & 0xFF));
                valb-=8;
            }
        }
        return out;
    }
	
    inline std::string Hex_Encode(const unsigned char *in, const size_t len)
    {
        std::ostringstream hex_str_str;
        for (size_t i = 0; i < len; i++)
            hex_str_str << std::setfill('0') << std::setw(2) << std::hex << (int) in[i];
        return hex_str_str.str();
    }
	
    inline v_uint8 Hex_Decode(const std::string &in)
    {
        v_uint8 out;
        for (size_t i = 0; i < in.length(); i+=2)
        {
            unsigned int c;
            std::stringstream ss;
            std::string byte = in.substr(i,2);
            ss << byte;
            ss >> std::hex >> c;
            out.push_back(c);
        }
        return out;
    }

    //stream
    class stream {

        struct BioDeleterFunctor {
            void operator()(BIO *buf) {
                BIO_free(buf);
            }
        };

    public:
        using unique_bio_ptr = std::unique_ptr<BIO, BioDeleterFunctor>;

        template <class Bio_method, class Writer>
        static std::string bioToString(Bio_method method, Writer writer)
        {
            auto bio = unique_bio_ptr(BIO_new(method));

            writer(bio.get());
//            if (OK != writer(bio.get()))
//                return std::string();

            BUF_MEM* buffer = nullptr;
            BIO_get_mem_ptr(bio.get(), &buffer);

            if (!buffer || !buffer->data || !buffer->length)
                return std::string();

            return std::string(buffer->data, buffer->length);
        }
    };

    //unsigned char buffer
    class buffer {

        struct BufferDeleterFunctor {
            void operator()(unsigned char *buf) {
                OPENSSL_free(buf);
            }
        };

        using unique_buffer_ptr = std::unique_ptr<unsigned char, BufferDeleterFunctor>;

    public:
        buffer(unsigned char *pbuf, const std::size_t len) : 
            m_buf(pbuf), 
            m_nLength(len)
        {}

        std::string str() const noexcept
        {
            std::string s(reinterpret_cast<char const*>(m_buf.get()), m_nLength);
            return s;
        }

        v_uint8 data() const noexcept
        {
            const auto pBuf = m_buf.get();
            v_uint8 out{pBuf, pBuf + m_nLength};
            return out;
        }

        std::string Base64() noexcept
        {
            return Base64_Encode(m_buf.get(), m_nLength);
        }

        std::string Hex() noexcept
        {
            return Hex_Encode(m_buf.get(), m_nLength);
        }

        std::size_t len() const noexcept { return m_nLength; }
        unsigned char* get() const noexcept { return m_buf.get(); }

    private:
        unique_buffer_ptr m_buf;
        std::size_t m_nLength;
    };

    class crypto_exception : public std::exception
    {
        std::string message;
    public:
        crypto_exception(const std::string &error, const std::string &details, const std::string &func_name)
        {
            std::ostringstream str_str;
            str_str << func_name << " - " << error << ": " << details;

            std::string errStr = stream::bioToString(BIO_s_mem(), [this](BIO* bio)
            {
                return ERR_print_errors(bio);
            });
            str_str << std::endl << "OpenSSL error: " << std::endl << errStr;

            message = str_str.str();
        }

        const char *what() const noexcept override
        {
            return message.c_str();
        }
    };
	
	inline std::string Password_Stretching(const std::string& password)
	{
		unsigned char pout[32] = {};
		
		if (OK != PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.length()), nullptr, 0, 1000, EVP_sha512(), 32, pout))
			throw crypto_exception("", std::string(), "PKCS5_PBKDF2_HMAC");
		
		std::string out{reinterpret_cast<char*>(pout), 32};
		return out;
	}
}