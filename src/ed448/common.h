#pragma once

#include <iomanip>

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/crypto.h>

namespace ed_crypto {

    static constexpr int OK = 1;
	
	inline std::string Base64_Encode(const unsigned char *in, size_t len)
    {
        std::string out;
        int val=0, valb=-6;

        std::ostringstream hex_str_str;
        for (int i = 0; i < len; i++){
            val = (val<<8) + in[i];
            valb += 8;
            while (valb>=0) {
                out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(val>>valb)&0x3F]);
                valb-=6;
            }
        }
        if (valb>-6) out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((val<<8)>>(valb+8))&0x3F]);
        while (out.size()%4) out.push_back('=');
        return out;
    }
	
	inline std::vector<unsigned char> Base64_Decode(const std::string &in)
    {
        std::vector<unsigned char> out;

        std::vector<int> T(256,-1);
        for (int i=0; i<64; i++) T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;

        int val=0, valb=-8;
        for (unsigned char c : in) {
            if (T[c] == -1) break;
            val = (val<<6) + T[c];
            valb += 6;
            if (valb>=0) {
                out.push_back( (unsigned char)((val>>valb)&0xFF));
                valb-=8;
            }
        }
        return out;
    }
	
	inline std::string Hex_Encode(const unsigned char *in, size_t len)
    {
        std::ostringstream hex_str_str;
        for (int i = 0; i < len; i++)
            hex_str_str << std::setfill('0') << std::setw(2) << std::hex << (int) in[i];
        return hex_str_str.str();
    }
	
	inline std::vector<unsigned char> Hex_Decode(const std::string &in)
    {
        std::vector<unsigned char> out;
        for (int i=0;i<in.length();i+=2)
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

        buffer(unsigned char *pbuf, std::size_t len) : buffer_(pbuf), len_(len) {}

        std::string str()
        {
            std::string s( reinterpret_cast<char const*>(buffer_.get()), len_ );
            return s;
        }

        std::vector<unsigned char> data()
        {
            const unsigned char *buffer = buffer_.get();
            std::vector<unsigned char> out{buffer, buffer + len_};
            return out;
        }

        std::string Base64()
        {
            return Base64_Encode(buffer_.get(), len_);
        }

        std::string Hex()
        {
            return Hex_Encode(buffer_.get(), len_);
        }

        std::size_t len() const { return len_;}
        unsigned char* get() const { return buffer_.get();}

    private:
        unique_buffer_ptr buffer_;
        std::size_t len_;
    };
    class crypto_exception : public std::exception {
        std::string message;
    public:
        crypto_exception(std::string error, std::string details, std::string func_name) {
            std::ostringstream str_str;
            str_str << func_name << " - " << error << ": " << details;

            std::string errStr = stream::bioToString(BIO_s_mem(), [this](BIO* bio)
            {
                return ERR_print_errors(bio);
            });
            str_str << std::endl << "OpenSSL error: " << std::endl << errStr;

            message = str_str.str();
        }

        const char *what() const noexcept {
            return message.c_str();
        }
    };
	
	inline std::string Password_Stretching(const std::string& password)
	{
		unsigned char pout[32] = {};
		
		if (OK != PKCS5_PBKDF2_HMAC(password.c_str(), password.length(), nullptr, 0, 1000, EVP_sha512(), 32, pout))
			throw (crypto_exception("", std::string(), "PKCS5_PBKDF2_HMAC"));
		
		std::string out{reinterpret_cast<char*>(pout), 32};
		return out;
	}
}