#pragma once
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <compat.h>
#include <compat/endian.h>

#include <algorithm>
#include <array>
#include <assert.h>
#include <ios>
#include <limits>
#include <list>
#include <map>
#include <atomic>
#include <unordered_map>
#include <memory>
#include <set>
#include <stdint.h>
#include <string>
#include <string.h>
#include <utility>
#include <vector>
#include <optional>
#include <stdexcept>

#include <prevector.h>
#include <enum_util.h>

static constexpr uint32_t MAX_DATA_SIZE = 0x02000000;       // 33'554'432
static constexpr uint32_t MAX_CONTAINER_SIZE = 0x100000;     // 1'048'576
static constexpr uint8_t PROTECTED_SERIALIZE_MARKER = 0x55; // 01010101

typedef enum class _PROTECTED_DATA_TYPE : uint8_t
{
    PAIR_KEY = 0,
    PAIR_VALUE = 1,
    MAP = 2,
    UNORDERED_MAP = 3,
    SET = 4,
    SET_ITEM = 5,
    LIST = 6,
    LIST_ITEM = 7,
} PROTECTED_DATA_TYPE;

class unexpected_serialization_version : public std::runtime_error
{
public:
    explicit unexpected_serialization_version (const std::string& _Message) :
        std::runtime_error(_Message.c_str())
    {}

    explicit unexpected_serialization_version(const char* _Message) :
        std::runtime_error(_Message)
    {}
};

/**
 * Dummy data type to identify deserializing constructors.
 *
 * By convention, a constructor of a type T with signature
 *
 *   template <typename Stream> T::T(deserialize_type, Stream& s)
 *
 * is a deserializing constructor, which builds the type by
 * deserializing it from s. If T contains const fields, this
 * is likely the only way to do so.
 */
struct deserialize_type {};
constexpr deserialize_type deserialize {};

/**
 * Used to bypass the rule against non-const reference to temporary
 * where it makes sense with wrappers such as CFlatData or CTxDB
 */
template<typename T>
inline T& REF(const T& val)
{
    return const_cast<T&>(val);
}

/**
 * Used to acquire a non-const pointer "this" to generate bodies
 * of const serialization operations from a template
 */
template<typename T>
inline T* NCONST_PTR(const T* val)
{
    return const_cast<T*>(val);
}

/** 
 * Get begin pointer of vector (non-const version).
 * @note These functions avoid the undefined case of indexing into an empty
 * vector, as well as that of indexing after the end of the vector.
 */
template <typename V>
inline typename V::value_type* begin_ptr(V& v)
{
    return v.empty() ? nullptr : &v[0];
}
/** Get begin pointer of vector (const version) */
template <typename V>
inline const typename V::value_type* begin_ptr(const V& v)
{
    return v.empty() ? nullptr : &v[0];
}
/** Get end pointer of vector (non-const version) */
template <typename V>
inline typename V::value_type* end_ptr(V& v)
{
    return v.empty() ? nullptr : (&v[0] + v.size());
}
/** Get end pointer of vector (const version) */
template <typename V>
inline const typename V::value_type* end_ptr(const V& v)
{
    return v.empty() ? nullptr : (&v[0] + v.size());
}

/*
 * Lowest-level serialization and conversion.
 * @note Sizes of these types are verified in the tests
 */
template<typename Stream> inline void ser_writedata8(Stream &s, uint8_t obj)
{
    s.write((char*)&obj, 1);
}
template<typename Stream> inline void ser_writedata16(Stream &s, uint16_t obj)
{
    obj = htole16(obj);
    s.write((char*)&obj, 2);
}
template<typename Stream> inline void ser_writedata32(Stream &s, uint32_t obj)
{
    obj = htole32(obj);
    s.write((char*)&obj, 4);
}
template<typename Stream> inline void ser_writedata64(Stream &s, uint64_t obj)
{
    obj = htole64(obj);
    s.write((char*)&obj, 8);
}
template<typename Stream> inline uint8_t ser_readdata8(Stream &s)
{
    uint8_t obj;
    s.read((char*)&obj, 1);
    return obj;
}
template<typename Stream> inline uint16_t ser_readdata16(Stream &s)
{
    uint16_t obj;
    s.read((char*)&obj, 2);
    return le16toh(obj);
}
template<typename Stream> inline uint32_t ser_readdata32(Stream &s)
{
    uint32_t obj;
    s.read((char*)&obj, 4);
    return le32toh(obj);
}
template<typename Stream> inline uint64_t ser_readdata64(Stream &s)
{
    uint64_t obj;
    s.read((char*)&obj, 8);
    return le64toh(obj);
}
inline uint64_t ser_double_to_uint64(double x)
{
    union { double x; uint64_t y; } tmp;
    tmp.x = x;
    return tmp.y;
}
inline uint32_t ser_float_to_uint32(float x)
{
    union { float x; uint32_t y; } tmp;
    tmp.x = x;
    return tmp.y;
}
inline double ser_uint64_to_double(uint64_t y)
{
    union { double x; uint64_t y; } tmp;
    tmp.y = y;
    return tmp.x;
}
inline float ser_uint32_to_float(uint32_t y)
{
    union { float x; uint32_t y; } tmp;
    tmp.y = y;
    return tmp.x;
}

/////////////////////////////////////////////////////////////////
//
// Templates for serializing to anything that looks like a stream,
// i.e. anything that supports .read(char*, size_t) and .write(char*, size_t)
//

class CSizeComputer;

enum
{
    // primary actions
    SER_NETWORK         = (1 << 0),
    SER_DISK            = (1 << 1),
    SER_GETHASH         = (1 << 2),
};

#define READWRITE(obj)              (::SerReadWrite(s, (obj), ser_action))
#define READWRITE_PROTECTED(obj)    (::SerReadWriteProtected(s, (obj), ser_action))
#define READWRITEMANY(...)          (::SerReadWriteMany(s, ser_action, __VA_ARGS__))

/** 
 * Implement three methods for serializable objects. These are actually wrappers over
 * "SerializationOp" template, which implements the body of each class' serialization
 * code. Adding "ADD_SERIALIZE_METHODS" in the body of the class causes these wrappers to be
 * added as members. 
 */
#define ADD_SERIALIZE_METHODS                                         \
    template<typename Stream>                                         \
    void Serialize(Stream& s) const { NCONST_PTR(this)->SerializationOp(s, SERIALIZE_ACTION::Write); } \
    template<typename Stream>                                         \
    void Unserialize(Stream& s) { SerializationOp(s, SERIALIZE_ACTION::Read); }

template<typename Stream> inline void Serialize(Stream& s, char a    ) { ser_writedata8(s, a); } // TODO Get rid of bare char
template<typename Stream> inline void Serialize(Stream& s, int8_t a  ) { ser_writedata8(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint8_t a ) { ser_writedata8(s, a); }
template<typename Stream> inline void Serialize(Stream& s, int16_t a ) { ser_writedata16(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint16_t a) { ser_writedata16(s, a); }
template<typename Stream> inline void Serialize(Stream& s, int32_t a ) { ser_writedata32(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint32_t a) { ser_writedata32(s, a); }
template<typename Stream> inline void Serialize(Stream& s, int64_t a ) { ser_writedata64(s, a); }
template<typename Stream> inline void Serialize(Stream& s, uint64_t a) { ser_writedata64(s, a); }
template<typename Stream> inline void Serialize(Stream& s, float a   ) { ser_writedata32(s, ser_float_to_uint32(a)); }
template<typename Stream> inline void Serialize(Stream& s, double a  ) { ser_writedata64(s, ser_double_to_uint64(a)); }

template<typename Stream> inline void Unserialize(Stream& s, char& a    ) { a = ser_readdata8(s); } // TODO Get rid of bare char
template<typename Stream> inline void Unserialize(Stream& s, int8_t& a  ) { a = ser_readdata8(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint8_t& a ) { a = ser_readdata8(s); }
template<typename Stream> inline void Unserialize(Stream& s, int16_t& a ) { a = ser_readdata16(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint16_t& a) { a = ser_readdata16(s); }
template<typename Stream> inline void Unserialize(Stream& s, int32_t& a ) { a = ser_readdata32(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint32_t& a) { a = ser_readdata32(s); }
template<typename Stream> inline void Unserialize(Stream& s, int64_t& a ) { a = ser_readdata64(s); }
template<typename Stream> inline void Unserialize(Stream& s, uint64_t& a) { a = ser_readdata64(s); }
template<typename Stream> inline void Unserialize(Stream& s, float& a   ) { a = ser_uint32_to_float(ser_readdata32(s)); }
template<typename Stream> inline void Unserialize(Stream& s, double& a  ) { a = ser_uint64_to_double(ser_readdata64(s)); }

template<typename Stream> inline void Serialize(Stream& s, bool a)    { char f=a; ser_writedata8(s, f); }
template<typename Stream> inline void Unserialize(Stream& s, bool& a) { char f=ser_readdata8(s); a=f; }

template<typename Stream> inline void Serialize(Stream& s, std::atomic_bool a)    { char f = a.load(); ser_writedata8(s, f); }
template<typename Stream> inline void Unserialize(Stream& s, std::atomic_bool& a) { char f = ser_readdata8(s); a.store(bool(f)); }

/**
 * Compact Size
 * size <  253        -- 1 byte
 * size <= 0xFFFF     -- 3 bytes  (253 + 2 bytes)
 * size <= 0xFFFFFFFF -- 5 bytes  (254 + 4 bytes)
 * size >  0xFFFFFFFF -- 9 bytes  (255 + 8 bytes)
 */
inline unsigned int GetSizeOfCompactSize(const uint64_t nSize)
{
    if (nSize < 253)                return 1;
    else if (nSize <= 0xFFFFu)      return 3;
    else if (nSize <= 0xFFFFFFFFu)  return 5;
    else                            return 9;
}

inline void WriteCompactSize(CSizeComputer& os, const uint64_t nSize);

template<typename Stream>
void WriteCompactSize(Stream& os, const uint64_t nSize)
{
    if (nSize < 253)
    {
        ser_writedata8(os, static_cast<uint8_t>(nSize));
    }
    else if (nSize <= 0xFFFFu)
    {
        ser_writedata8(os, 253);
        ser_writedata16(os, static_cast<uint16_t>(nSize));
    }
    else if (nSize <= 0xFFFFFFFFu)
    {
        ser_writedata8(os, 254);
        ser_writedata32(os, static_cast<uint32_t>(nSize));
    }
    else
    {
        ser_writedata8(os, 255);
        ser_writedata64(os, nSize);
    }
}

template<typename Stream>
uint64_t ReadCompactSize(Stream& is, uint64_t max_size = std::numeric_limits<uint64_t>::max())
{
    uint8_t chSize = ser_readdata8(is);
    uint64_t nSizeRet = 0;
    if (chSize < 253)
    {
        nSizeRet = chSize;
    }
    else if (chSize == 253)
    {
        nSizeRet = ser_readdata16(is);
        if (nSizeRet < 253)
            throw std::ios_base::failure("non-canonical ReadCompactSize()");
    }
    else if (chSize == 254)
    {
        nSizeRet = ser_readdata32(is);
        if (nSizeRet < 0x10000u)
            throw std::ios_base::failure("non-canonical ReadCompactSize()");
    }
    else
    {
        nSizeRet = ser_readdata64(is);
        if (nSizeRet < 0x100000000ULL)
            throw std::ios_base::failure("non-canonical ReadCompactSize()");
    }
    if ((max_size < std::numeric_limits<uint64_t>::max()) && (nSizeRet > max_size))
        throw std::ios_base::failure("ReadCompactSize(): size too large");
    return nSizeRet;
}

/**
 * Variable-length integers: bytes are a MSB base-128 encoding of the number.
 * The high bit in each byte signifies whether another digit follows. To make
 * sure the encoding is one-to-one, one is subtracted from all but the last digit.
 * Thus, the byte sequence a[] with length len, where all but the last byte
 * has bit 128 set, encodes the number:
 * 
 *  (a[len-1] & 0x7F) + sum(i=1..len-1, 128^i*((a[len-i-1] & 0x7F)+1))
 * 
 * Properties:
 * * Very small (0-127: 1 byte, 128-16511: 2 bytes, 16512-2113663: 3 bytes)
 * * Every integer has exactly one encoding
 * * Encoding does not depend on size of original integer type
 * * No redundancy: every (infinite) byte sequence corresponds to a list
 *   of encoded integers.
 * 
 * 0:         [0x00]  256:        [0x81 0x00]
 * 1:         [0x01]  16383:      [0xFE 0x7F]
 * 127:       [0x7F]  16384:      [0xFF 0x00]
 * 128:  [0x80 0x00]  16511: [0x80 0xFF 0x7F]
 * 255:  [0x80 0x7F]  65535: [0x82 0xFD 0x7F]
 * 2^32:           [0x8E 0xFE 0xFE 0xFF 0x00]
 */

template<typename I>
inline unsigned int GetSizeOfVarInt(I n)
{
    int nRet = 0;
    while(true) {
        nRet++;
        if (n <= 0x7F)
            break;
        n = (n >> 7) - 1;
    }
    return nRet;
}

template<typename I>
inline void WriteVarInt(CSizeComputer& os, I n);

template<typename Stream, typename I>
void WriteVarInt(Stream& os, I n)
{
    unsigned char tmp[(sizeof(n)*8+6)/7];
    int len=0;
    while(true) {
        tmp[len] = (n & 0x7F) | (len ? 0x80 : 0x00);
        if (n <= 0x7F)
            break;
        n = (n >> 7) - 1;
        len++;
    }
    do {
        ser_writedata8(os, tmp[len]);
    } while(len--);
}

template<typename Stream, typename I>
I ReadVarInt(Stream& is)
{
    I n = 0;
    while(true) {
        unsigned char chData = ser_readdata8(is);
        n = (n << 7) | (chData & 0x7F);
        if (chData & 0x80)
            n++;
        else
            return n;
    }
}

#define FLATDATA(obj) REF(CFlatData((char*)&(obj), (char*)&(obj) + sizeof(obj)))
#define VARINT(obj) REF(WrapVarInt(REF(obj)))
#define COMPACTSIZE(obj) REF(CCompactSize(REF(obj)))
#define LIMITED_STRING(obj,n) REF(LimitedString< n >(REF(obj)))

/** 
 * Wrapper for serializing arrays and POD.
 */
class CFlatData
{
protected:
    char* pbegin;
    char* pend;
public:
    CFlatData(void* pbeginIn, void* pendIn) : pbegin((char*)pbeginIn), pend((char*)pendIn) { }
    template <class T, class TAl>
    explicit CFlatData(std::vector<T,TAl> &v)
    {
        pbegin = (char*)begin_ptr(v);
        pend = (char*)end_ptr(v);
    }
    template <unsigned int N, typename T, typename S, typename D>
    explicit CFlatData(prevector<N, T, S, D> &v)
    {
        pbegin = (char*)begin_ptr(v);
        pend = (char*)end_ptr(v);
    }
    char* begin() { return pbegin; }
    const char* begin() const { return pbegin; }
    char* end() { return pend; }
    const char* end() const { return pend; }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        s.write(pbegin, pend - pbegin);
    }

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        s.read(pbegin, pend - pbegin);
    }
};

template<typename I>
class CVarInt
{
protected:
    I &n;
public:
    CVarInt(I& nIn) : n(nIn) { }

    template<typename Stream>
    void Serialize(Stream &s) const {
        WriteVarInt<Stream,I>(s, n);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        n = ReadVarInt<Stream,I>(s);
    }
};

class CCompactSize
{
protected:
    uint64_t &n;
public:
    CCompactSize(uint64_t& nIn) : n(nIn) { }

    template<typename Stream>
    void Serialize(Stream &s) const {
        WriteCompactSize<Stream>(s, n);
    }

    template<typename Stream>
    void Unserialize(Stream& s) {
        n = ReadCompactSize<Stream>(s);
    }
};

template<size_t Limit>
class LimitedString
{
protected:
    std::string& string;
public:
    LimitedString(std::string& _string) : string(_string) {}

    template<typename Stream>
    void Unserialize(Stream& s)
    {
        const uint64_t size = ReadCompactSize(s);
        if (size > Limit) {
            throw std::ios_base::failure("String length limit exceeded");
        }
        string.resize(size);
        if (size != 0)
            s.read((char*)&string[0], size);
    }

    template<typename Stream>
    void Serialize(Stream& s) const
    {
        WriteCompactSize(s, string.size());
        if (!string.empty())
            s.write((char*)&string[0], string.size());
    }
};

template<typename I>
CVarInt<I> WrapVarInt(I& n) { return CVarInt<I>(n); }

/**
 * Forward declarations
 */

/**
 *  string
 */
template<typename Stream, typename C> void Serialize(Stream& os, const std::basic_string<C>& str);
template<typename Stream, typename C> void Unserialize(Stream& is, std::basic_string<C>& str);

/**
 * prevector
 * prevectors of unsigned char are a special case and are intended to be serialized as a single opaque blob.
 */
template<typename Stream, unsigned int N, typename T> void Serialize_impl(Stream& os, const prevector<N, T>& v, const unsigned char&);
template<typename Stream, unsigned int N, typename T, typename V> void Serialize_impl(Stream& os, const prevector<N, T>& v, const V&);
template<typename Stream, unsigned int N, typename T> inline void Serialize(Stream& os, const prevector<N, T>& v);
template<typename Stream, unsigned int N, typename T> void Unserialize_impl(Stream& is, prevector<N, T>& v, const unsigned char&);
template<typename Stream, unsigned int N, typename T, typename V> void Unserialize_impl(Stream& is, prevector<N, T>& v, const V&);
template<typename Stream, unsigned int N, typename T> inline void Unserialize(Stream& is, prevector<N, T>& v);

/**
 * vector
 * vectors of unsigned char are a special case and are intended to be serialized as a single opaque blob.
 */
template<typename Stream, typename T, typename A> void Serialize_impl(Stream& os, const std::vector<T, A>& v, const unsigned char&);
template<typename Stream, typename T, typename A, typename V> void Serialize_impl(Stream& os, const std::vector<T, A>& v, const V&);
template<typename Stream, typename T, typename A> inline void Serialize(Stream& os, const std::vector<T, A>& v);
template<typename Stream, typename T, typename A> void Unserialize_impl(Stream& is, std::vector<T, A>& v, const unsigned char&);
template<typename Stream, typename T, typename A, typename V> void Unserialize_impl(Stream& is, std::vector<T, A>& v, const V&);
template<typename Stream, typename T, typename A> inline void Unserialize(Stream& is, std::vector<T, A>& v);

/**
 * optional
 */
template<typename Stream, typename T> void Serialize(Stream& os, const std::optional<T>& item);
template<typename Stream, typename T> void Unserialize(Stream& is, std::optional<T>& item);

/**
 * array
 */
template<typename Stream, typename T, std::size_t N> void Serialize(Stream& os, const std::array<T, N>& item);
template<typename Stream, typename T, std::size_t N> void Unserialize(Stream& is, std::array<T, N>& item);

/**
 * pair
 */
template<typename Stream, typename K, typename T> void Serialize(Stream& os, const std::pair<K, T>& item);
template<typename Stream, typename K, typename T> void Unserialize(Stream& is, std::pair<K, T>& item);

/**
 * map
 */
template<typename Stream, typename K, typename T, typename Pred, typename A> void Serialize(Stream& os, const std::map<K, T, Pred, A>& m);
template<typename Stream, typename K, typename T, typename Pred, typename A> void Unserialize(Stream& is, std::map<K, T, Pred, A>& m);

/**
 * set
 */
template<typename Stream, typename K, typename Pred, typename A> void Serialize(Stream& os, const std::set<K, Pred, A>& m);
template<typename Stream, typename K, typename Pred, typename A> void Unserialize(Stream& is, std::set<K, Pred, A>& m);

/**
 * list
 */
template<typename Stream, typename T, typename A> void Serialize(Stream& os, const std::list<T, A>& m);
template<typename Stream, typename T, typename A> void Unserialize(Stream& is, std::list<T, A>& m);

/**
 * shared_ptr
 */
template<typename Stream, typename T> void Serialize(Stream& os, const std::shared_ptr<const T>& p);
template<typename Stream, typename T> void Unserialize(Stream& os, std::shared_ptr<const T>& p);

template<typename Stream, typename T> void Serialize(Stream& os, const std::shared_ptr<T>& p);
template<typename Stream, typename T> void Unserialize(Stream& os, std::shared_ptr<T>& p);

/**
 * unique_ptr
 */
template<typename Stream, typename T> void Serialize(Stream& os, const std::unique_ptr<const T>& p);
template<typename Stream, typename T> void Unserialize(Stream& os, std::unique_ptr<const T>& p);

/**
 * If none of the specialized versions above matched, default to calling member function.
 */
template<typename Stream, typename T>
inline void Serialize(Stream& os, const T& a)
{
    a.Serialize(os);
}

template<typename Stream, typename T>
inline void Unserialize(Stream& is, T& a)
{
    a.Unserialize(is);
}

/**
 * string
 */
template<typename Stream, typename C>
void Serialize(Stream& os, const std::basic_string<C>& str)
{
    WriteCompactSize(os, str.size());
    if (!str.empty())
        os.write((char*)&str[0], str.size() * sizeof(str[0]));
}

template<typename Stream, typename C>
void Unserialize(Stream& is, std::basic_string<C>& str)
{
    const uint64_t nSize = ReadCompactSize(is);
    str.resize(nSize);
    if (nSize != 0)
        is.read((char*)&str[0], nSize * sizeof(str[0]));
}

/**
 * prevector
 */
template<typename Stream, unsigned int N, typename T>
void Serialize_impl(Stream& os, const prevector<N, T>& v, const unsigned char&)
{
    WriteCompactSize(os, v.size());
    if (!v.empty())
        os.write((char*)&v[0], v.size() * sizeof(T));
}

template<typename Stream, unsigned int N, typename T, typename V>
void Serialize_impl(Stream& os, const prevector<N, T>& v, const V&)
{
    WriteCompactSize(os, v.size());
    for (typename prevector<N, T>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, (*vi));
}

template<typename Stream, unsigned int N, typename T>
inline void Serialize(Stream& os, const prevector<N, T>& v)
{
    Serialize_impl(os, v, T());
}

template<typename Stream, unsigned int N, typename T>
void Unserialize_impl(Stream& is, prevector<N, T>& v, const unsigned char&)
{
    // Limit size per read so bogus size value won't cause out of memory
    v.clear();
    const uint64_t nSize = ReadCompactSize(is);
    uint64_t i = 0;
    while (i < nSize)
    {
        const uint64_t blk = std::min<uint64_t>(nSize - i, 1 + 4999999 / sizeof(T));
        v.resize(static_cast<uint32_t>(i + blk));
        is.read((char*)&v[static_cast<typename prevector<N, T>::size_type>(i)], blk * sizeof(T));
        i += blk;
    }
}

template<typename Stream, unsigned int N, typename T, typename V>
void Unserialize_impl(Stream& is, prevector<N, T>& v, const V&)
{
    v.clear();
    const uint64_t nSize = ReadCompactSize(is);
    uint64_t i = 0;
    uint64_t nMid = 0;
    while (nMid < nSize)
    {
        nMid += 5000000 / sizeof(T);
        if (nMid > nSize)
            nMid = nSize;
        v.resize(nMid);
        for (; i < nMid; i++)
            Unserialize(is, v[i]);
    }
}

template<typename Stream, unsigned int N, typename T>
inline void Unserialize(Stream& is, prevector<N, T>& v)
{
    Unserialize_impl(is, v, T());
}

/**
 * vector
 */
template<typename Stream, typename T, typename A>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, const unsigned char&)
{
    WriteCompactSize(os, v.size());
    if (!v.empty())
        os.write((char*)&v[0], v.size() * sizeof(T));
}

template<typename Stream, typename T, typename A>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, const std::shared_ptr<T>&)
{
    WriteCompactSize(os, v.size());
    for (typename std::vector<T, A>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, (*vi));
}
template<typename Stream, typename T, typename A, typename V>
void Serialize_impl(Stream& os, const std::vector<T, A>& v, const V&)
{
    WriteCompactSize(os, v.size());
    for (typename std::vector<T, A>::const_iterator vi = v.begin(); vi != v.end(); ++vi)
        ::Serialize(os, (*vi));
}

template<typename Stream, typename T, typename A>
inline void Serialize(Stream& os, const std::vector<T, A>& v)
{
    Serialize_impl(os, v, T());
}

template<typename Stream, typename T, typename A>
void Unserialize_impl(Stream& is, std::vector<T, A>& v, const unsigned char&)
{
    // Limit size per read so bogus size value won't cause out of memory
    v.clear();
    const uint64_t nSize = ReadCompactSize(is);
    uint64_t i = 0;
    while (i < nSize)
    {
        uint64_t blk = std::min<uint64_t>(nSize - i, 1 + 4999999 / sizeof(T));
        v.resize(i + blk);
        is.read((char*)&v[i], blk * sizeof(T));
        i += blk;
    }
}

template<typename Stream, typename T, typename A, typename V>
void Unserialize_impl(Stream& is, std::vector<T, A>& v, const V&)
{
    v.clear();
    const uint64_t nSize = ReadCompactSize(is);
    uint64_t i = 0;
    uint64_t nMid = 0;
    while (nMid < nSize)
    {
        nMid += 5000000 / sizeof(T);
        if (nMid > nSize)
            nMid = nSize;
        v.resize(nMid);
        for (; i < nMid; i++)
            Unserialize(is, v[i]);
    }
}

template<typename Stream, typename T, typename A>
inline void Unserialize(Stream& is, std::vector<T, A>& v)
{
    Unserialize_impl(is, v, T());
}

/**
 * optional
 */
template<typename Stream, typename T>
void Serialize(Stream& os, const std::optional<T>& item)
{
    // If the value is there, put 0x01 and then serialize the value.
    // If it's not, put 0x00.
    const bool bHasValue = item.has_value();
    Serialize(os, static_cast<unsigned char>(bHasValue ? 0x01 : 0x00));
    if (bHasValue)
        Serialize(os, item.value());
}

template<typename Stream, typename T>
void Unserialize(Stream& is, std::optional<T>& item)
{
    unsigned char discriminant = 0x00;
    Unserialize(is, discriminant);

    if (discriminant == 0x00)
        item = std::nullopt;
    else if (discriminant == 0x01) {
        T object;
        Unserialize(is, object);
        item = object;
    } else {
        throw std::ios_base::failure("non-canonical optional discriminant");
    }
}

/**
 * array
 */
template<typename Stream, typename T, std::size_t N>
void Serialize(Stream& os, const std::array<T, N>& item)
{
    for (size_t i = 0; i < N; i++)
        Serialize(os, item[i]);
}

template<typename Stream, typename T, std::size_t N>
void Unserialize(Stream& is, std::array<T, N>& item)
{
    for (size_t i = 0; i < N; i++)
        Unserialize(is, item[i]);
}

template<typename Stream>
void ReadProtectedSerializeMarker(Stream& is, const PROTECTED_DATA_TYPE expectedDataType)
{
    if (is.empty())
        throw std::ios_base::failure("protected serialization marker not found (eof)");
    uint8_t ch = 0;
    is >> ch;
    if (ch != PROTECTED_SERIALIZE_MARKER)
        throw std::ios_base::failure(strprintf("protected serialization marker not found, expected-0x%X, found-0x%X", PROTECTED_SERIALIZE_MARKER, static_cast<uint8_t>(ch)));
    if (is.empty())
        throw std::ios_base::failure("protected serialization data type not found (eof)");
    is >> ch;
    if (ch != to_integral_type(expectedDataType))
		throw std::ios_base::failure(strprintf("protected serialization data type mismatch, expected-0x%X, found-0x%X", to_integral_type(expectedDataType), static_cast<uint8_t>(ch)));
}

template<typename Stream>
void ReadProtectedSerializeMarkerAlt(Stream& is, const PROTECTED_DATA_TYPE expectedDataType, const PROTECTED_DATA_TYPE altDataType)
{
    if (is.empty())
        throw std::ios_base::failure("protected serialization marker not found (eof)");
    uint8_t ch = 0;
    is >> ch;
    if (ch != PROTECTED_SERIALIZE_MARKER)
        throw std::ios_base::failure(strprintf("protected serialization marker not found, expected-0x%X, found-0x%X", PROTECTED_SERIALIZE_MARKER, static_cast<uint8_t>(ch)));
    if (is.empty())
        throw std::ios_base::failure("protected serialization data type not found (eof)");
    is >> ch;
    if ((ch != to_integral_type(expectedDataType)) && (ch != to_integral_type(altDataType)))
		throw std::ios_base::failure(strprintf("protected serialization data type mismatch, expected-0x%X or 0x%X, found-0x%X",
            to_integral_type(expectedDataType), to_integral_type(altDataType), static_cast<uint8_t>(ch)));
}

template<typename Stream>
void Serialize(Stream& os, const PROTECTED_DATA_TYPE protectedDataType)
{
	os << PROTECTED_SERIALIZE_MARKER << to_integral_type(protectedDataType);
}

/**
 * pair
 */
template<typename Stream, typename K, typename T>
void Serialize(Stream& os, const std::pair<K, T>& item)
{
    Serialize(os, item.first);
    Serialize(os, item.second);
}

template<typename Stream, typename K, typename T>
void Serialize_Protected(Stream& os, const std::pair<K, T>& item, Stream &helperStream)
{
    // serialize and write pair key
    helperStream.clear();
    Serialize(helperStream, item.first);
    os << PROTECTED_DATA_TYPE::PAIR_KEY;
    WriteCompactSize(os, helperStream.size());
    os << helperStream;

    // serialize and write pair value
    helperStream.clear();
    Serialize(helperStream, item.second);
    os << PROTECTED_DATA_TYPE::PAIR_VALUE;
    WriteCompactSize(os, helperStream.size());
    os << helperStream;
}

template<typename Stream, typename K, typename T>
void Unserialize(Stream& is, std::pair<K, T>& item)
{
    Unserialize(is, item.first);
    Unserialize(is, item.second);
}

template<typename Stream, typename K, typename T>
void Unserialize_Protected(Stream& is, std::pair<K, T>& item, Stream &helperStream)
{
    helperStream.clear();
    ReadProtectedSerializeMarker(is, PROTECTED_DATA_TYPE::PAIR_KEY);
	uint64_t nSize = ReadCompactSize(is);
    helperStream.reserve(nSize);
    is.read(helperStream, nSize);
	Unserialize(helperStream, item.first);

    helperStream.clear();
    ReadProtectedSerializeMarker(is, PROTECTED_DATA_TYPE::PAIR_VALUE);
    nSize = ReadCompactSize(is);
    helperStream.reserve(nSize);
    is.read(helperStream, nSize);
    Unserialize(helperStream, item.second);
}

/**
 * map
 */
template<typename Stream, typename K, typename T, typename Pred, typename A>
void Serialize(Stream& os, const std::map<K, T, Pred, A>& m)
{
    WriteCompactSize(os, m.size());
    for (const auto &mapPair : m)
        Serialize(os, mapPair);
}

template<typename Stream, typename K, typename T, typename Pred, typename A>
void Serialize_Protected(Stream& os, const std::map<K, T, Pred, A>& m)
{
    Stream helperStream(os.GetType(), os.GetVersion());
    Stream helperItemStream(os.GetType(), os.GetVersion());

    WriteCompactSize(helperStream, m.size());
    for (const auto &mapPair : m)
        Serialize_Protected(helperStream, mapPair, helperItemStream);

    os << PROTECTED_DATA_TYPE::MAP;
    WriteCompactSize(os, helperStream.size());
    os << helperStream;
}

template<typename Stream, typename K, typename T, typename Pred, typename A>
void Unserialize(Stream& is, std::map<K, T, Pred, A>& m)
{
    m.clear();
    const uint64_t nSize = ReadCompactSize(is, MAX_CONTAINER_SIZE);
    auto mi = m.begin();
    for (uint64_t i = 0; i < nSize; i++)
    {
        std::pair<K, T> item;
        Unserialize(is, item);
        mi = m.insert(mi, item);
    }
}

template<typename Stream, typename K, typename T, typename Pred, typename A>
void Unserialize_Protected(Stream& is, std::map<K, T, Pred, A>& m)
{
    m.clear();
    ReadProtectedSerializeMarkerAlt(is, PROTECTED_DATA_TYPE::MAP, PROTECTED_DATA_TYPE::UNORDERED_MAP);

    Stream helperStream(is.GetType(), is.GetVersion());
    Stream helperItemStream(is.GetType(), is.GetVersion());
    
    uint64_t nSize = ReadCompactSize(is);
    helperStream.reserve(nSize);
    is.read(helperStream, nSize);

    nSize = ReadCompactSize(helperStream, MAX_CONTAINER_SIZE);
    auto mi = m.begin();
    for (uint64_t i = 0; i < nSize; i++)
    {
        std::pair<K, T> item;
        Unserialize_Protected(helperStream, item, helperItemStream);
        mi = m.insert(mi, item);
    }
}

/**
 * unordered_map
 */
template <typename Stream, typename K, typename T, typename Pred, typename A>
void Serialize(Stream& os, const std::unordered_map<K, T, Pred, A>& m)
{
    WriteCompactSize(os, m.size());
    for (const auto &mapPair : m)
        Serialize(os, mapPair);
}

template <typename Stream, typename K, typename T, typename Pred, typename A>
void Serialize_Protected(Stream& os, const std::unordered_map<K, T, Pred, A>& m)
{
    Stream helperStream(os.GetType(), os.GetVersion());
    Stream helperItemStream(os.GetType(), os.GetVersion());

    WriteCompactSize(helperStream, m.size());
    for (const auto &mapPair : m)
        Serialize_Protected(helperStream, mapPair, helperItemStream);

    os << PROTECTED_DATA_TYPE::UNORDERED_MAP;
    WriteCompactSize(os, helperStream.size());
    os << helperStream;
}

template <typename Stream, typename K, typename T, typename Pred, typename A>
void Unserialize(Stream& is, std::unordered_map<K, T, Pred, A>& m)
{
    m.clear();
    const uint64_t nSize = ReadCompactSize(is, MAX_CONTAINER_SIZE);
    auto mi = m.begin();
    for (uint64_t i = 0; i < nSize; i++)
    {
        std::pair<K, T> item;
        Unserialize(is, item);
        mi = m.insert(mi, item);
    }
}

template <typename Stream, typename K, typename T, typename Pred, typename A>
void Unserialize_Protected(Stream& is, std::unordered_map<K, T, Pred, A>& m)
{
    m.clear();
    ReadProtectedSerializeMarkerAlt(is, PROTECTED_DATA_TYPE::UNORDERED_MAP, PROTECTED_DATA_TYPE::MAP);
    uint64_t nSize = ReadCompactSize(is);

    Stream helperStream(is.GetType(), is.GetVersion());
    Stream helperItemStream(is.GetType(), is.GetVersion());
    helperStream.reserve(nSize);
    is.read(helperStream, nSize);

    nSize = ReadCompactSize(helperStream, MAX_CONTAINER_SIZE);
    auto mi = m.begin();
    for (uint64_t i = 0; i < nSize; i++)
    {
        std::pair<K, T> item;
        Unserialize_Protected(helperStream, item, helperItemStream);
        mi = m.insert(mi, item);
    }
}

/**
 * set
 */
template<typename Stream, typename K, typename Pred, typename A>
void Serialize(Stream& os, const std::set<K, Pred, A>& m)
{
    WriteCompactSize(os, m.size());
    for (typename std::set<K, Pred, A>::const_iterator it = m.begin(); it != m.end(); ++it)
        Serialize(os, (*it));
}

template<typename Stream, typename K, typename Pred, typename A>
void Serialize_Protected(Stream& os, const std::set<K, Pred, A>& m)
{
    Stream helperStream(os.GetType(), os.GetVersion());
    Stream helperItemStream(os.GetType(), os.GetVersion());

    WriteCompactSize(helperStream, m.size());
    for (typename std::set<K, Pred, A>::const_iterator it = m.begin(); it != m.end(); ++it)
    {
        helperItemStream.clear();
        Serialize(helperItemStream, (*it));

        helperStream << PROTECTED_DATA_TYPE::SET_ITEM;
        WriteCompactSize(helperStream, helperItemStream.size());
        helperStream << helperItemStream;
    }

    os << PROTECTED_DATA_TYPE::SET;
    WriteCompactSize(os, helperStream.size());
    os << helperStream;
}

template<typename Stream, typename K, typename Pred, typename A>
void Unserialize(Stream& is, std::set<K, Pred, A>& m)
{
    m.clear();
    const uint64_t nSize = ReadCompactSize(is, MAX_CONTAINER_SIZE); // nSize here is number of items in the list, 33,554,432 is too big for that!
    typename std::set<K, Pred, A>::iterator it = m.begin();
    for (uint64_t i = 0; i < nSize; i++)
    {
        K key;
        Unserialize(is, key);
        it = m.insert(it, key);
    }
}

template<typename Stream, typename K, typename Pred, typename A>
void Unserialize_Protected(Stream& is, std::set<K, Pred, A>& m)
{
    m.clear();
    ReadProtectedSerializeMarker(is, PROTECTED_DATA_TYPE::SET);
    uint64_t nSize = ReadCompactSize(is);

    Stream helperStream(is.GetType(), is.GetVersion());
    Stream helperItemStream(is.GetType(), is.GetVersion());
    helperStream.reserve(nSize);
    is.read(helperStream, nSize);

    nSize = ReadCompactSize(helperStream, MAX_CONTAINER_SIZE); // nSize here is number of items in the list
    typename std::set<K, Pred, A>::iterator it = m.begin();
    for (uint64_t i = 0; i < nSize; i++)
    {
        K key;
        helperItemStream.clear();
        ReadProtectedSerializeMarker(helperStream, PROTECTED_DATA_TYPE::SET_ITEM);
        uint64_t nItemSize = ReadCompactSize(helperStream);
        helperItemStream.reserve(nItemSize);
        helperStream.read(helperItemStream, nItemSize);
        Unserialize(helperItemStream, key);
        it = m.insert(it, key);
    }
}

/**
 * list
 */
template<typename Stream, typename T, typename A>
void Serialize(Stream& os, const std::list<T, A>& l)
{
    WriteCompactSize(os, l.size());
    for (typename std::list<T, A>::const_iterator it = l.begin(); it != l.end(); ++it)
        Serialize(os, (*it));
}

template<typename Stream, typename T, typename A>
void Serialize_Protected(Stream& os, const std::list<T, A>& l)
{
    Stream helperStream(os.GetType(), os.GetVersion());
    Stream helperItemStream(os.GetType(), os.GetVersion());

    WriteCompactSize(helperStream, l.size());
    for (typename std::list<T, A>::const_iterator it = l.begin(); it != l.end(); ++it)
    {
        helperItemStream.clear();
        Serialize(helperItemStream, (*it));
        helperStream << PROTECTED_DATA_TYPE::LIST_ITEM;
        WriteCompactSize(helperStream, helperItemStream.size());
        helperStream << helperItemStream;
    }

    os << PROTECTED_DATA_TYPE::LIST;
    WriteCompactSize(os, helperStream.size());
    os << helperStream;
}

template<typename Stream, typename T, typename A>
void Unserialize(Stream& is, std::list<T, A>& l)
{
    l.clear();
    const uint64_t nSize = ReadCompactSize(is, MAX_CONTAINER_SIZE); // nSize here is number of items in the list, 33,554,432 is too big for that!
    typename std::list<T, A>::iterator it = l.begin();
    for (uint64_t i = 0; i < nSize; i++)
    {
        T item;
        Unserialize(is, item);
        l.push_back(item);
    }
}

template<typename Stream, typename T, typename A>
void Unserialize_Protected(Stream& is, std::list<T, A>& l)
{
    l.clear();
    ReadProtectedSerializeMarker(is, PROTECTED_DATA_TYPE::LIST);
    uint64_t nSize = ReadCompactSize(is);

    Stream helperStream(is.GetType(), is.GetVersion());
    Stream helperItemStream(is.GetType(), is.GetVersion());
    helperStream.reserve(nSize);
    is.read(helperStream, nSize);

    nSize = ReadCompactSize(helperStream, MAX_CONTAINER_SIZE); // nSize here is number of items in the list
    typename std::list<T, A>::iterator it = l.begin();
    for (uint64_t i = 0; i < nSize; i++)
    {
        T item;
        helperItemStream.clear();
        ReadProtectedSerializeMarker(helperStream, PROTECTED_DATA_TYPE::LIST_ITEM);
        uint64_t nItemSize = ReadCompactSize(helperStream);
        helperItemStream.reserve(nItemSize);
        helperStream.read(helperItemStream, nItemSize);
        Unserialize(helperItemStream, item);
        l.push_back(item);
    }
}

/**
 * unique_ptr
 */
template<typename Stream, typename T> void
Serialize(Stream& os, const std::unique_ptr<const T>& p)
{
    Serialize(os, *p);
}

template<typename Stream, typename T>
void Unserialize(Stream& is, std::unique_ptr<const T>& p)
{
    p.reset(new T(deserialize, is));
}

/**
 * shared_ptr
 */
template<typename Stream, typename T>
void Serialize(Stream& os, const std::shared_ptr<const T>& p)
{
    Serialize(os, *p);
}

template<typename Stream, typename T>
void Unserialize(Stream& is, std::shared_ptr<const T>& p)
{
    p = std::make_shared<const T>(deserialize, is);
}

template<typename Stream, typename T>
void Serialize(Stream& os, const std::shared_ptr<T>& p)
{
    Serialize(os, *p);
}

template<typename Stream, typename T>
void Unserialize(Stream& is, std::shared_ptr<T>& p)
{
    p = std::make_shared<T>(deserialize, is);
}

/**
 * Support for ADD_SERIALIZE_METHODS and READWRITE macro
 */
enum class SERIALIZE_ACTION
{
    NoAction = 0,
    Read = 1,
    Write = 2
};

template <typename Stream, typename _T>
inline void SerReadWrite(Stream& s, _T& obj, const SERIALIZE_ACTION ser_action)
{
    switch (ser_action)
    {
    case SERIALIZE_ACTION::Read:
        ::Unserialize(s, obj);
        break;

    case SERIALIZE_ACTION::Write:
        ::Serialize(s, obj);
        break;

    default:
        break;
    }
}

template <typename Stream, typename _T>
inline void SerReadWriteProtected(Stream& s, _T& obj, const SERIALIZE_ACTION ser_action)
{
    switch (ser_action)
    {
    case SERIALIZE_ACTION::Read:
        ::Unserialize_Protected(s, obj);
        break;

    case SERIALIZE_ACTION::Write:
        ::Serialize_Protected(s, obj);
        break;

    default:
        break;
    }
}


/* ::GetSerializeSize implementations
 *
 * Computing the serialized size of objects is done through a special stream
 * object of type CSizeComputer, which only records the number of bytes written
 * to it.
 *
 * If your Serialize or SerializationOp method has non-trivial overhead for
 * serialization, it may be worthwhile to implement a specialized version for
 * CSizeComputer, which uses the s.seek() method to record bytes that would
 * be written instead.
 */
class CSizeComputer
{
protected:
    size_t m_nSize;

    const int m_nType;
    const int m_nVersion;

public:
    CSizeComputer(const int nTypeIn, const int nVersionIn) : 
        m_nSize(0), 
        m_nType(nTypeIn), 
        m_nVersion(nVersionIn) {}

    void write(const char *psz, const size_t nSize)
    {
        m_nSize += nSize;
    }
    void read(char* pch, const size_t nSize) {} // stub, class is used only in write-mode
    void ignore(const size_t nSizeToSkip) {}    // stub

    /** Pretend _nSize bytes are written, without specifying them. */
    void seek(const size_t nSize)
    {
        m_nSize += nSize;
    }

    template<typename T>
    CSizeComputer& operator<<(const T& obj)
    {
        ::Serialize(*this, obj);
        return (*this);
    }
    template <typename T>
    CSizeComputer& operator>>(T& obj)
    {
        return (*this);
    }

    size_t size() const noexcept { return m_nSize; }
    int GetVersion() const noexcept { return m_nVersion; }
    int GetType() const noexcept { return m_nType; }
};

template<typename Stream>
void SerializeMany(Stream& s)
{
}

template<typename Stream, typename Arg>
void SerializeMany(Stream& s, Arg&& arg)
{
    ::Serialize(s, std::forward<Arg>(arg));
}

template<typename Stream, typename Arg, typename... Args>
void SerializeMany(Stream& s, Arg&& arg, Args&&... args)
{
    ::Serialize(s, std::forward<Arg>(arg));
    ::SerializeMany(s, std::forward<Args>(args)...);
}

template<typename Stream>
inline void UnserializeMany(Stream& s)
{
}

template<typename Stream, typename Arg>
inline void UnserializeMany(Stream& s, Arg& arg)
{
    ::Unserialize(s, arg);
}

template<typename Stream, typename Arg, typename... Args>
inline void UnserializeMany(Stream& s, Arg& arg, Args&... args)
{
    ::Unserialize(s, arg);
    ::UnserializeMany(s, args...);
}

template<typename Stream, typename... Args>
inline void SerReadWriteMany(Stream& s, const SERIALIZE_ACTION ser_action, Args&&... args)
{
    if (ser_action == SERIALIZE_ACTION::Write)
        ::SerializeMany(s, std::forward<Args>(args)...);
    else
        ::UnserializeMany(s, std::forward<Args>(args)...);
}

template<typename I>
inline void WriteVarInt(CSizeComputer &s, I n)
{
    s.seek(GetSizeOfVarInt<I>(n));
}

inline void WriteCompactSize(CSizeComputer &s, const uint64_t nSize)
{
    s.seek(GetSizeOfCompactSize(nSize));
}

template <typename T>
size_t GetSerializeSize(const T& t, int nType, int nVersion = 0)
{
    return (CSizeComputer(nType, nVersion) << t).size();
}

template <typename S, typename T>
size_t GetSerializeSize(const S& s, const T& t)
{
    return (CSizeComputer(s.GetType(), s.GetVersion()) << t).size();
}

