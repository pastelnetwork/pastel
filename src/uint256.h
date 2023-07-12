#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <assert.h>
#include <stdexcept>

#include <vector_types.h>

template <class T>
inline void hash_combine(std::size_t& seed, const T& v)
{
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

/** Template base class for fixed-sized opaque blobs. */
template<unsigned int BITS>
class base_blob
{
protected:
    enum { WIDTH = BITS / 8 };
    alignas(uint32_t) uint8_t data[WIDTH];

public:
    inline static constexpr size_t SIZE = WIDTH;

    base_blob() noexcept
    {
        memset(data, 0, sizeof(data));
    }

    explicit base_blob(const v_uint8& vch);

    base_blob(base_blob && b) noexcept
    {
        std::swap(data, b.data);
    }
    base_blob& operator=(base_blob && b) noexcept
    {
        if (this != &b)
            std::swap(data, b.data);
        return *this;
    }
    base_blob(const base_blob &b) noexcept
    {
#ifdef _MSC_VER
        memcpy_s(data, sizeof(data), b.data, sizeof(b.data));
#else
        memcpy(data, b.data, sizeof(data));
#endif
    }
    base_blob& operator=(const base_blob &b) noexcept
    {
        if (this != &b)
        {
#ifdef _MSC_VER
            memcpy_s(data, sizeof(data), b.data, sizeof(b.data));
#else
            memcpy(data, b.data, sizeof(data));
#endif
        }
        return *this;
    }

    bool IsNull() const noexcept
    {
        for (int i = 0; i < WIDTH; i++)
            if (data[i] != 0)
                return false;
        return true;
    }

    void SetNull() noexcept
    {
        memset(data, 0, sizeof(data));
    }

    friend inline bool operator==(const base_blob& a, const base_blob& b) { return memcmp(a.data, b.data, sizeof(a.data)) == 0; }
    friend inline bool operator!=(const base_blob& a, const base_blob& b) { return memcmp(a.data, b.data, sizeof(a.data)) != 0; }
    friend inline bool operator<(const base_blob& a, const base_blob& b) { return memcmp(a.data, b.data, sizeof(a.data)) < 0; }

    std::string GetHex() const;
    void SetHex(const char* psz);
    void SetHex(const std::string& str);
    std::string ToString() const;

    unsigned char* begin() noexcept
    {
        return &data[0];
    }

    unsigned char* end() noexcept
    {
        return &data[WIDTH];
    }

    const unsigned char* begin() const noexcept
    {
        return &data[0];
    }

    const unsigned char* end() const noexcept
    {
        return &data[WIDTH];
    }

    const unsigned char* cbegin() const noexcept
    {
        return &data[0];
    }

    const unsigned char* cend() const noexcept
    {
        return &data[WIDTH];
    }

    unsigned int size() const noexcept
    {
        return sizeof(data);
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s.write((char*)data, sizeof(data));
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        s.read((char*)data, sizeof(data));
    }
};

/** 88-bit opaque blob.
 */
class blob88 : public base_blob<88> {
public:
    blob88() {}
    blob88(const base_blob<88>& b) : base_blob<88>(b) {}
    explicit blob88(const v_uint8& vch) : base_blob<88>(vch) {}
};

/** 160-bit opaque blob.
 * @note This type is called uint160 for historical reasons only. It is an opaque
 * blob of 160 bits and has no integer operations.
 */
class uint160 : public base_blob<160> {
public:
    uint160() {}
    uint160(const base_blob<160>& b) : base_blob<160>(b) {}
    explicit uint160(const v_uint8& vch) : base_blob<160>(vch) {}
};

/** 256-bit opaque blob.
 * @note This type is called uint256 for historical reasons only. It is an
 * opaque blob of 256 bits and has no integer operations. Use arith_uint256 if
 * those are required.
 */
class uint256 : public base_blob<256>
{
public:
    uint256() noexcept {}
    uint256(const base_blob<256>& b) noexcept : 
        base_blob<256>(b)
    {}
    explicit uint256(const v_uint8& vch) noexcept : 
        base_blob<256>(vch)
    {}

    /** A cheap hash function that just returns 64 bits from the result, it can be
     * used when the contents are considered uniformly random. It is not appropriate
     * when the value can easily be influenced from outside as e.g. a network adversary could
     * provide values to trigger worst-case behavior.
     * @note The result of this function is not stable between little and big endian.
     */
    uint64_t GetCheapHash() const noexcept
    {
        uint64_t result;
        memcpy((void*)&result, (void*)data, sizeof(uint64_t));
        return result;
    }

    /** A more secure, salted hash function.
     * @note This hash is not stable between little and big endian.
     */
    uint64_t GetHash(const uint256& salt) const noexcept;
};

/* uint256 from const char *.
 * This is a separate function because the constructor uint256(const char*) can result
 * in dangerously catching uint256(0).
 */
inline uint256 uint256S(const char *str)
{
    uint256 rv;
    rv.SetHex(str);
    return rv;
}

/* uint256 from std::string.
 * This is a separate function because the constructor uint256(const std::string &str) can result
 * in dangerously catching uint256(0) via std::string(const char*).
 */
inline uint256 uint256S(const std::string& str)
{
    uint256 rv;
    rv.SetHex(str);
    return rv;
}

// convert hex-encoded string to uint256 with error checking
bool parse_uint256(std::string& error, uint256& value, const std::string &sUint256, const char *szValueDesc = nullptr);

using v_uint256 = std::vector<uint256>;

namespace std
{
    template <>
    struct hash<uint256>
    {
        std::size_t operator()(const uint256& key) const noexcept
        {
            // Start with a hash value of 0
            size_t seed = 0;

            static const auto N = key.SIZE / sizeof(uint64_t);
            // Modify 'seed' by XORing and bit-shifting in
            // one member of after the other
            auto p = key.begin();
            for (int i = 0; i < N; ++i)
            {
                hash_combine<uint64_t>(seed, *reinterpret_cast<const uint64_t*>(p));
                p += sizeof(uint64_t);
            }
            return seed;
        }
    };
}
