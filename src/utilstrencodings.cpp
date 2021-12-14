// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "utilstrencodings.h"
#include "ascii85.h"

#include "tinyformat.h"
#include "vector_types.h"
#include "str_utils.h"
#include "util.h"

#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <iomanip>
#include <limits>

using namespace std;

static const string CHARS_ALPHA_NUM = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

static const string SAFE_CHARS[] =
{
    CHARS_ALPHA_NUM + " .,;_/:?@()", // SAFE_CHARS_DEFAULT
    CHARS_ALPHA_NUM + " .,;_?@" // SAFE_CHARS_UA_COMMENT
};

string SanitizeString(const string& str, int rule)
{
    string strResult;
    for (std::string::size_type i = 0; i < str.size(); i++)
    {
        if (SAFE_CHARS[rule].find(str[i]) != std::string::npos)
            strResult.push_back(str[i]);
    }
    return strResult;
}

string SanitizeFilename(const string& str)
{
    /**
     * safeChars chosen to restrict filename, keeping it simple to avoid cross-platform issues.
     * http://stackoverflow.com/a/2306003
     */
    static string safeChars("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ01234567890");
    string strResult;
    for (std::string::size_type i = 0; i < str.size(); i++)
    {
        if (safeChars.find(str[i]) != std::string::npos)
            strResult.push_back(str[i]);
    }
    return strResult;
}

std::string HexInt(uint32_t val)
{
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(sizeof(uint32_t) * 2) << std::hex << val;
    return ss.str();
}

uint32_t ParseHexToUInt32(const std::string& str) {
    std::istringstream converter(str);
    uint32_t value;
    converter >> std::hex >> value;
    return value;
}

static constexpr signed char p_util_hexdigit[] =
{ -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  0,1,2,3,4,5,6,7,8,9,-1,-1,-1,-1,-1,-1,
  -1,0xa,0xb,0xc,0xd,0xe,0xf,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,0xa,0xb,0xc,0xd,0xe,0xf,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
  -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, };

signed char HexDigit(char c)
{
    return p_util_hexdigit[(unsigned char)c];
}

bool IsHex(const string& str)
{
    for(std::string::const_iterator it(str.begin()); it != str.end(); ++it)
    {
        if (HexDigit(*it) < 0)
            return false;
    }
    return (str.size() > 0) && (str.size()%2 == 0);
}

v_uint8 ParseHex(const char* psz)
{
    // convert hex dump to vector
    v_uint8 vch;
    const size_t nLength = psz ? strlen(psz) : 0;
    vch.reserve(nLength / 2);
    while (true)
    {
        while (isspace(*psz))
            psz++;
        signed char c = HexDigit(*psz++);
        if (c == (signed char)-1)
            break;
        unsigned char n = (c << 4);
        c = HexDigit(*psz++);
        if (c == (signed char)-1)
            break;
        n |= c;
        vch.push_back(n);
    }
    return vch;
}

v_uint8 ParseHex(const string& str)
{
    return ParseHex(str.c_str());
}

string EncodeAscii85(const unsigned char* istr, size_t len) noexcept
{
    string sRetVal; //Default is empty-string
    do
    {
        if (!istr)
            break;
        
        const int32_t nInputSize = static_cast<int32_t>(len);
        const int32_t nMaxLength = ascii85_get_max_encoded_length(nInputSize);
        if (nMaxLength <= 0)
            break;
        
        v_uint8 vOut;
        vOut.resize(nMaxLength);

        const int32_t nEncodedLength = encode_ascii85(reinterpret_cast<const uint8_t*>(istr), nInputSize, vOut.data(), nMaxLength);
        if (nEncodedLength > 0)
            sRetVal.assign(vOut.cbegin(), vOut.cbegin() + nEncodedLength);
    } while (false);
    return sRetVal;
}

string EncodeAscii85(const string& str) noexcept
{
    return EncodeAscii85(reinterpret_cast<const unsigned char *>(str.c_str()), str.size());
}

v_uint8 DecodeAscii85(const char* ostr, bool* pfInvalid) noexcept
{
    v_uint8 vOut;
    do
    {
        if (!ostr)
            break;
        
        const int32_t nInputSize = static_cast<int32_t>(strlen(ostr));
        const int32_t nMaxLength = ascii85_get_max_decoded_length(nInputSize);
        if (nMaxLength < 0)
        {
            if(pfInvalid)
                *pfInvalid = true;//Decode size error
            break;
        }
        
        vOut.resize(nMaxLength);
        const int32_t nDecodedLength = decode_ascii85(reinterpret_cast<const uint8_t*>(ostr), nInputSize, vOut.data(), nMaxLength);
        if (nDecodedLength < 0)
        {
            if (pfInvalid)
                *pfInvalid = true;//Decode error
            break;
        }
        vOut.resize(static_cast<size_t>(nDecodedLength));
    } while (false);
    return vOut;
}

string DecodeAscii85(const string& str) noexcept
{
    return vector_to_string(DecodeAscii85(str.c_str()));
}

string EncodeBase64(const unsigned char* pch, size_t len)
{
    static constexpr auto PBASE64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string str;
    str.reserve(((len + 2) / 3) * 4);
    ConvertBits<8, 6, true>([&](int v)
    {
        str += PBASE64[v];
    }, pch, pch + len);
    while (str.size() % 4)
        str += '=';
    return str;
}

string EncodeBase64(const string& str)
{
    return EncodeBase64((const unsigned char*)str.c_str(), str.size());
}

v_uint8 DecodeBase64(const char* p, bool* pfInvalid)
{
    static constexpr int decode64_table[] =
    {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1,
        -1, -1, -1, -1, -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28,
        29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
        49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    };

    const char* e = p;
    v_uint8 val;
    val.reserve(strlen(p));
    while (*p)
    {
        const int x = decode64_table[static_cast<unsigned char>(*p)];
        if (x == -1)
            break;
        val.push_back(x);
        ++p;
    }

    v_uint8 ret;
    ret.reserve((val.size() * 3) / 4);
    bool bValid = ConvertBits<6, 8, false>([&](unsigned char c)
        {
            ret.push_back(c);
        }, 
        val.begin(), val.end());

    const char* q = p;
    while (bValid && *p)
    {
        if (*p != '=')
        {
            bValid = false;
            break;
        }
        ++p;
    }
    bValid &= (p - e) % 4 == 0 && p - q < 4;
    if (pfInvalid) 
        *pfInvalid = !bValid;

    return ret;
}

/**
 * Decode base64 encoded string.
 *  
 * \param str - base64 encoded string
 * \param pfInvalid - pointer to bool, set to true if there was an error decoding string
 * \return decoded string, may be partial if there was a failure
 */
string DecodeBase64(const string& str, bool* pfInvalid)
{
    return vector_to_string(DecodeBase64(str.c_str(), pfInvalid));
}

string EncodeBase32(const unsigned char* pch, size_t len)
{
    static constexpr auto PBASE32 = "abcdefghijklmnopqrstuvwxyz234567";

    std::string str;
    str.reserve(((len + 4) / 5) * 8);
    ConvertBits<8, 5, true>([&](int v)
    {
        str += PBASE32[v];
    }, pch, pch + len);
    while (str.size() % 8)
        str += '=';
    return str;
}

string EncodeBase32(const string& str)
{
    return EncodeBase32(reinterpret_cast<const unsigned char*>(str.c_str()), str.size());
}

v_uint8 DecodeBase32(const char* p, bool* pfInvalid)
{
    static constexpr int decode32_table[] =
    {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, -1, -1, -1, -1,
        -1, -1, -1, -1, -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, -1,  0,  1,  2,
         3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
        23, 24, 25, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    };

    const char* e = p;
    v_uint8 val;
    val.reserve(strlen(p));
    while (*p != 0) {
        int x = decode32_table[(unsigned char)*p];
        if (x == -1) break;
        val.push_back(x);
        ++p;
    }

    v_uint8 ret;
    ret.reserve((val.size() * 5) / 8);
    bool valid = ConvertBits<5, 8, false>([&](unsigned char c) { ret.push_back(c); }, val.begin(), val.end());

    const char* q = p;
    while (valid && *p != 0) {
        if (*p != '=') {
            valid = false;
            break;
        }
        ++p;
    }
    valid = valid && (p - e) % 8 == 0 && p - q < 8;
    if (pfInvalid) *pfInvalid = !valid;

    return ret;
}

string DecodeBase32(const string& str)
{
    return vector_to_string(DecodeBase32(str.c_str()));
}

static bool ParsePrechecks(const std::string& str)
{
    if (str.empty()) // No empty string allowed
        return false;
    if (isspace(str[0]) || isspace(str[str.size()-1])) // No padding allowed
        return false;
    if (str.size() != strlen(str.c_str())) // No embedded NUL characters allowed
        return false;
    return true;
}

bool ParseInt32(const std::string& str, int32_t *out)
{
    if (!ParsePrechecks(str))
        return false;
    char *endp = nullptr;
    errno = 0; // strtol will not set errno if valid
    long int n = strtol(str.c_str(), &endp, 10);
    if(out) *out = (int32_t)n;
    // Note that strtol returns a *long int*, so even if strtol doesn't report a over/underflow
    // we still have to check that the returned value is within the range of an *int32_t*. On 64-bit
    // platforms the size of these types may be different.
    return endp && *endp == 0 && !errno &&
        n >= std::numeric_limits<int32_t>::min() &&
        n <= std::numeric_limits<int32_t>::max();
}

bool ParseInt64(const std::string& str, int64_t *out)
{
    if (!ParsePrechecks(str))
        return false;
    char *endp = nullptr;
    errno = 0; // strtoll will not set errno if valid
    long long int n = strtoll(str.c_str(), &endp, 10);
    if(out) *out = (int64_t)n;
    // Note that strtoll returns a *long long int*, so even if strtol doesn't report a over/underflow
    // we still have to check that the returned value is within the range of an *int64_t*.
    return endp && *endp == 0 && !errno &&
        n >= std::numeric_limits<int64_t>::min() &&
        n <= std::numeric_limits<int64_t>::max();
}

bool ParseDouble(const std::string& str, double *out)
{
    if (!ParsePrechecks(str))
        return false;
    if (str.size() >= 2 && str[0] == '0' && str[1] == 'x') // No hexadecimal floats allowed
        return false;
    std::istringstream text(str);
    text.imbue(std::locale::classic());
    double result;
    text >> result;
    if(out) *out = result;
    return text.eof() && !text.fail();
}

std::string FormatParagraph(const std::string& in, size_t width, size_t indent)
{
    std::stringstream out;
    size_t col = 0;
    size_t ptr = 0;
    while(ptr < in.size())
    {
        // Find beginning of next word
        ptr = in.find_first_not_of(' ', ptr);
        if (ptr == std::string::npos)
            break;
        // Find end of next word
        size_t endword = in.find_first_of(' ', ptr);
        if (endword == std::string::npos)
            endword = in.size();
        // Add newline and indentation if this wraps over the allowed width
        if (col > 0)
        {
            if ((col + endword - ptr) > width)
            {
                out << '\n';
                for(size_t i=0; i<indent; ++i)
                    out << ' ';
                col = 0;
            } else
                out << ' ';
        }
        // Append word
        out << in.substr(ptr, endword - ptr);
        col += endword - ptr + 1;
        ptr = endword;
    }
    return out.str();
}

std::string i64tostr(int64_t n)
{
    return strprintf("%d", n);
}

std::string itostr(int n)
{
    return strprintf("%d", n);
}

int64_t atoi64(const char* psz)
{
    return strtoll(psz, nullptr, 10);
}

int64_t atoi64(const std::string& str)
{
    return strtoll(str.c_str(), nullptr, 10);
}

int atoi(const std::string& str)
{
    return atoi(str.c_str());
}

/** Upper bound for mantissa.
 * 10^18-1 is the largest arbitrary decimal that will fit in a signed 64-bit integer.
 * Larger integers cannot consist of arbitrary combinations of 0-9:
 *
 *   999999999999999999  1^18-1
 *  9223372036854775807  (1<<63)-1  (max int64_t)
 *  9999999999999999999  1^19-1     (would overflow)
 */
static const int64_t UPPER_BOUND = 1000000000000000000LL - 1LL;

/** Helper function for ParseFixedPoint */
static inline bool ProcessMantissaDigit(char ch, int64_t &mantissa, int &mantissa_tzeros)
{
    if(ch == '0')
        ++mantissa_tzeros;
    else {
        for (int i=0; i<=mantissa_tzeros; ++i) {
            if (mantissa > (UPPER_BOUND / 10LL))
                return false; /* overflow */
            mantissa *= 10;
        }
        mantissa += ch - '0';
        mantissa_tzeros = 0;
    }
    return true;
}

bool ParseFixedPoint(const std::string &val, int decimals, int64_t *amount_out)
{
    int64_t mantissa = 0;
    int64_t exponent = 0;
    int mantissa_tzeros = 0;
    bool mantissa_sign = false;
    bool exponent_sign = false;
    size_t ptr = 0;
    size_t end = val.size();
    int point_ofs = 0;
    bool bRet = false;
    do
    {
        bool bOverflow = false;
        if (ptr < end && val[ptr] == '-')
        {
            mantissa_sign = true;
            ++ptr;
        }
        if (ptr < end)
        {
            if (val[ptr] == '0')
                ++ptr; // pass single 0
            else 
                if (val[ptr] >= '1' && val[ptr] <= '9')
                {
                    while (ptr < end && isdigitex(val[ptr]))
                    {
                        if (!ProcessMantissaDigit(val[ptr], mantissa, mantissa_tzeros))
                        {
                            bOverflow = true; // overflow
                            break;
                        }
                        ++ptr;
                    }
                    if (bOverflow)
                        break;
                } else
                    break; // missing expected digit
        } else
            break; // empty string or loose '-'
        if (ptr < end && val[ptr] == '.')
        {
            ++ptr;
            if (ptr < end && isdigitex(val[ptr]))
            {
                while (ptr < end && isdigitex(val[ptr]))
                {
                    if (!ProcessMantissaDigit(val[ptr], mantissa, mantissa_tzeros))
                    {
                        bOverflow = true; // overflow
                        break;
                    }
                    ++ptr;
                    ++point_ofs;
                }
                if (bOverflow)
                    break;
            } else
                break; // missing expected digit
        }
        if (ptr < end && (val[ptr] == 'e' || val[ptr] == 'E'))
        {
            ++ptr;
            if (ptr < end && val[ptr] == '+')
                ++ptr;
            else if (ptr < end && val[ptr] == '-') {
                exponent_sign = true;
                ++ptr;
            }
            if (ptr < end && isdigitex(val[ptr]))
            {
                while (ptr < end && isdigitex(val[ptr]))
                {
                    if (exponent > (UPPER_BOUND / 10LL))
                    {
                        bOverflow = true; // overflow
                        break;
                    }
                    exponent = exponent * 10 + val[ptr] - '0';
                    ++ptr;
                }
                if (bOverflow)
                    break;
            } else
                break; // missing expected digit
        }
        if (ptr != end)
            break; // trailing garbage

        /* finalize exponent */
        if (exponent_sign)
            exponent = -exponent;
        exponent = exponent - point_ofs + mantissa_tzeros;

        /* finalize mantissa */
        if (mantissa_sign)
            mantissa = -mantissa;

        /* convert to one 64-bit fixed-point value */
        exponent += decimals;
        if (exponent < 0)
            break; // cannot represent values smaller than 10^-decimals
        if (exponent >= 18)
            break; // cannot represent values larger than or equal to 10^(18-decimals)

        for (int i = 0; i < exponent; ++i)
        {
            if (mantissa > (UPPER_BOUND / 10LL) || mantissa < -(UPPER_BOUND / 10LL))
            {
                bOverflow = true; // overflow
                break;
            }
            mantissa *= 10;
        }
        if (bOverflow)
            break;
        if (mantissa > UPPER_BOUND || mantissa < -UPPER_BOUND)
            break; // overflow

        if (amount_out)
            *amount_out = mantissa;
        bRet = true;
    } while (false);
    return bRet;
}

