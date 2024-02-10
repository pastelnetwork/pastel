#pragma once
// Copyright (c) 2021-2024 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <algorithm>
#include <set>
#include <limits>

#include <utils/vector_types.h>

static constexpr size_t DEFINE_SIZE = static_cast<size_t>(-1);

/**
 * test if character is white space not using locale.
 * 
 * \param ch - character to test`
 * \return true if the character ch is a whitespace
 */
static inline bool isspaceex(const char ch)
{
    return (ch == 0x20) || (ch >= 0x09 && ch <= 0x0D);
}

/**
 * Check if character is in lowercase (a..z).
 *
 * \param c - character to check
 * \return true - if c is in lowercase
 */
static inline bool islowerex(const char c) noexcept
{
    return (c >= 'a' && c <= 'z');
}

/**
 * Check if character is in uppercase (A..Z).
 *
 * \param c - character to check
 * \return true - if c is in uppercase
 */
static inline bool isupperex(const char c) noexcept
{
    return (c >= 'A' && c <= 'Z');
}

/**
 * Check if character is alphabetic without using locale
 *
 * \param c - character to test
 * \return true if character is alphabetic (A..Z,a..z)
 */
static inline bool isalphaex(const char c) noexcept
{
    return ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
}

/**
 * Check if character is decimal digit without using locale
 *
 * \param c - character to test
 * \return true if character is digit (0..9)
 */
static inline bool isdigitex(const char c) noexcept
{
    return (c >= '0') && (c <= '9');
}

/**
 * Check if character is alphanumeric without using locale.
 *
 * \param c - character to test
 * \return true if c is in one of these sets (A..Z, a..z, 0..9)
 */
static inline bool isalnumex(const char c) noexcept
{
    return isalphaex(c) || isdigitex(c);
}

// convert string to unsigned integer type
template<typename _UIntType, typename _IntType>
bool str_to_unsigned_integer_check(const char *str, const size_t cnLength, _UIntType &ui)
{
	size_t nLength = cnLength;
	if (nLength == DEFINE_SIZE)
		nLength = str ? strlen(str) : 0;
	ui = 0;
	if (!str || !nLength)
		return false;
	const char *s = str;
	bool bNegative = false;
	if (*s == '-')
    {
		++s;
		--nLength;
		bNegative = true;
	} else if (*s == '+') {
		++s;
		--nLength;
	}
	if (!nLength) // fail if we have only '-' or '+'
		return false;
	bool bHex = false;
	// check if it starts with 0x | 0X | x | X | $
	if (nLength >= 2 && (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) { // 0x | 0X
		s += 2;
		nLength -= 2;
		bHex = true;
	} else if (nLength && (s[0] == 'x' || s[0] == 'X')) {	// x | X
		++s;
		--nLength;
		bHex = true;
	} else if (nLength && (s[0] == '$')) { // $
		++s;
		--nLength;
		bHex = true;
	}
	if (!nLength) // fail if we have only '0x' | '0X' | 'x | 'X' | '$'
		return false;
	uint32_t nRadix = bHex ? 16 : 10;
	_UIntType nOverflowCheck = std::numeric_limits<_UIntType>::max() / nRadix;
	bool bRet = true;
	for (ui = 0; nLength && *s; ++s, --nLength) 
	{
		char c = tolower(*s);
		int nDelta = 0;
		if ((c >= '0') && (c <= '9'))
			nDelta = c - '0';
		else if (bHex && (c >= 'a') && (c <= 'f'))
			nDelta = c - 'a' + 10;
		else {
			bRet = false; // invalid char found
			break;
		}
		// save old value to check for overflow
		volatile _UIntType nOldValue = ui;
		if (nOverflowCheck < ui)
		{
			bRet = false; // multiplication will cause overflow
			break;
		}
		ui *= nRadix;
		ui += nDelta;
		if (ui < nOldValue) 
		{
			bRet = false; // overflow
			break;
		}
	}
	if (!bRet)
		ui = 0;
	else if (bNegative)
		ui = (_UIntType)(-(_IntType)ui);
	return bRet;
}

static inline bool str_to_uint32_check(const char *str, const size_t nLength, uint32_t &u) { return str_to_unsigned_integer_check<uint32_t, int32_t>(str, nLength, u); }

/**
 * trim string in-place from start (left trim).
 *
 * \param s - string to ltrim
 */
static inline void ltrim(std::string &s)
{
    s.erase(s.begin(), std::find_if(s.cbegin(), s.cend(), [](const auto ch) { return !isspaceex(ch); }));
}

/**
 * trim string in-place from end (right trim).
 *
 * \param s - string to rtrim
 */
static inline void rtrim(std::string& s)
{
    s.erase(std::find_if(s.crbegin(), s.crend(), [](const auto ch) { return !isspaceex(ch); }).base(), s.end());
}

/**
 * trim string in-place (both left & right trim).
 */
static inline void trim(std::string& s)
{
    ltrim(s);
    rtrim(s);
}

/**
 * lowercase string in-place.
 *
 * \param s - string to lowercase in-place
 * \return lowercased string (points to s)
 */
static inline std::string &lowercase(std::string &s)
{
    std::transform(s.cbegin(), s.cend(), s.begin(), [](const auto ch) { return std::tolower(ch); });
    return s;
}

/**
 * lowercase const string.
 *
 * \param s - const string
 * \return lowercased string
 */
static inline std::string lowercase(const std::string &s)
{
    std::string sResult;
    sResult.resize(s.size());
    std::transform(s.cbegin(), s.cend(), sResult.begin(), [](const auto ch) { return std::tolower(ch); });
    return sResult;
}

/**
 * uppercase string in-place.
 *
 * \param s
 * \return uppercased string (points to s)
 */
static inline std::string & uppercase(std::string& s)
{
    std::transform(s.cbegin(), s.cend(), s.begin(), [](const auto ch) { return std::toupper(ch); });
    return s;
}

/**
 * uppercase const string.
 *
 * \param s - const string
 * \return uppercased string
 */
static inline std::string uppercase(const std::string& s)
{
    std::string sResult;
    sResult.resize(s.size());
    std::transform(s.cbegin(), s.cend(), sResult.begin(), [](const auto ch) { return std::toupper(ch); });
    return sResult;
}

/**
 * Lowercase string, uppercase first character (STRING -> String).
 * 
 * \param s - string to convert in-place
 * \return converted string
 */
static inline std::string & lowerstring_first_capital(std::string &s)
{
    bool bFirstChar = true;
    std::transform(s.cbegin(), s.cend(), s.begin(), [&](const auto ch) 
        {
            if (bFirstChar)
            {
                bFirstChar = false;
                return std::toupper(ch);
            }
            return std::tolower(ch);
        });
    return s;
}

/**
 * Lowercase string, uppercase first character (STRING -> String).
 * 
 * \param s - string to convert in-place
 * \return converted string
 */
static inline std::string lowerstring_first_capital(const std::string& s)
{
    bool bFirstChar = true;
    std::string sResult;
    sResult.resize(s.size());
    std::transform(s.cbegin(), s.cend(), sResult.begin(), [&](const auto ch)
        {
            if (bFirstChar)
            {
                bFirstChar = false;
                return std::toupper(ch);
            }
            return std::tolower(ch);
        });
    return sResult;
}

/**
 * find all occurancies of sFrom and replace with sTo in a string s.
 *
 * \param s - string to use for find and replace
 * \param sFrom - string to search for
 * \param sTo - string to replace with
 */
static inline void replaceAll(std::string& s, const std::string& sFrom, const std::string& sTo)
{
    size_t nPos = 0;
    while ((nPos = s.find(sFrom, nPos)) != std::string::npos)
    {
        s.replace(nPos, sFrom.length(), sTo);
        nPos += sTo.length();
    }
}

/**
 * Returns empty sz-string in case szStr = nullptr.
 * 
 * \param szStr - input string or nullptr
 * \return non-null string
 */
static inline const char* SAFE_SZ(const char* szStr) noexcept
{
    return szStr ? szStr : "";
}

/**
 * Case-insensitive string compare.
 * 
 * \param s1 - first string
 * \param s2 - second string
 * \return true if strings are the same (using case-insensitive compare)
 */
static inline bool str_icmp(const std::string &s1, const std::string &s2) noexcept
{
    return (s1.size() == s2.size()) &&
        std::equal(s1.cbegin(), s1.cend(), s2.cbegin(), s2.cend(), [](const char& c1, const char& c2)
        {
               return (c1 == c2) || (std::toupper(c1) == std::toupper(c2));
        });
}

/**
 * Case-insensitive substring search.
 * 
 * \param str - string to search in
 * \param sSearchSubStr - substring to search for
 * \return true if substring sSearchSubStr was found in str using case-insensitive compare
 */
static inline bool str_ifind(const std::string &str, const std::string &sSearchSubStr)
{
    std::string sSearchIn(str);
    std::string sSearchFor(sSearchSubStr);
    lowercase(sSearchIn);
    lowercase(sSearchFor);
    return sSearchIn.find(sSearchFor) != std::string::npos;
}

/**
 * Convert string to boolean value.
 * 
 * \param str - string to convert to bool
 * \param bValue - detected bool balue
 * \return true if str can be validated as bool value
 */
static inline bool str_tobool(const std::string &str, bool &bValue)
{
    if (str.empty())
        return false;
    std::string s(str);
    trim(lowercase(s));
    if (s == "1" || s == "true" || s == "on" || s == "yes" || s == "y")
    {
        bValue = true;
        return true;
    }
    if (s == "0" || s == "false" || s == "off" || s == "no" || s == "n")
    {
        bValue = false;
        return true;
    }
    return false;
}

/**
 * Check if the string s starts with the given string strStart.
 * c++20 standard implements starts_with for std::string.
 * 
 * \param s - string to check
 * \param strStart - a null-terminated string to search for
 * \return true is string s starts with the provided string, false otherwise
 */
static bool str_starts_with(const std::string& s, const char* strStart)
{
    if (!strStart || !*strStart || s.empty())
        return false;
    const size_t nLength = strlen(strStart);
    if (s.size() < nLength)
        return false;
    return std::equal(strStart, strStart + nLength, s.cbegin());
}

/**
 * Check if the string s starts with the given string strStart (case insensitive).
 * c++20 standard implements starts_with for std::string.
 * 
 * \param s - string to check
 * \param strStart - a null-terminated string to search for
 * \return true is string s starts with the provided string, false otherwise
 */
static bool str_istarts_with(const std::string& s, const char* strStart)
{
    if (!strStart || !*strStart || s.empty())
        return false;
    const size_t nLength = strlen(strStart);
    if (s.size() < nLength)
        return false;
    return std::equal(strStart, strStart + nLength, s.cbegin(),
        [](char a, char b)
        {
			return std::tolower(a) == std::tolower(b);
        });
}

/**
 * Check if the string s ends with the given suffix.
 * c++20 standard implements ends_with for std::string.
 * 
 * \param s - string to check
 * \param suffix - a null-terminated string to search for
 * \return true is string s ends with the provided suffix, false otherwise
 */
static bool str_ends_with(const std::string& s, const char* suffix)
{
    if (!suffix || !*suffix || s.empty())
        return false;
    const size_t nLength = strlen(suffix);
    if (s.size() < nLength)
        return false;
    return s.substr(s.size() - nLength).compare(suffix) == 0;
}

/**
 * Append new field to the string with the specified delimiter.
 * 
 * \param str - string to append field to
 * \param szField - a null-terminated field to add
 * \param szDelimiter - a null-terminated delimiter to add if string is not empty
 */
static void str_append_field(std::string& str, const char* szField, const char* szDelimiter)
{
    if (!str.empty() && szDelimiter && !str_ends_with(str, szDelimiter))
        str += szDelimiter;
    if (szField)
        str += szField;
}

/**
 * Split string s with delimiter chDelimiter into vector v.
 * 
 * \param v - output vector of strings
 * \param s - input string
 * \param chDelimiter  - string parts delimiter
 */
static void str_split(v_strings &v, const std::string &s, const char chDelimiter)
{
    v.clear();
    std::string::size_type posStart = 0;
    for (std::string::size_type posEnd = 0; (posEnd = s.find(chDelimiter, posEnd)) != std::string::npos; ++posEnd)
    {
        v.emplace_back(s.substr(posStart, posEnd - posStart));
        posStart = posEnd + 1;
    }
    v.emplace_back(s.substr(posStart));
}

/**
 * Split string s with any separators in szSeparators into vector v.
 * 
 * \param v - output vector of strings
 * \param s - input string
 * \param szSeparators  - string separators
 * \param bCompressTokens - if true - adjacent separators are merged together
 */
static void str_split(v_strings &v, const std::string &s, const char *szSeparators, const bool bCompressTokens = false)
{
    v.clear();
    std::string sToken;
    bool bSepState = false;
    for (const auto& ch : s)
    {
        if (strchr(szSeparators, ch))
        {
            if (bSepState && bCompressTokens)
                continue;
            bSepState = true;
            v.emplace_back(sToken);
            sToken.clear();
        }
        else
        {
            sToken += ch;
            bSepState = false;
        }
    }
    if (!bSepState)
        v.emplace_back(sToken);
}

/**
 * Split string s with delimiter chDelimiter into vector v.
 * 
 * \param v - output vector of strings
 * \param s - input string
 * \param chDelimiter  - string parts delimiter
 */
static void str_split(std::set<std::string> &strSet, const std::string &s, const char chDelimiter)
{
    strSet.clear();
    std::string::size_type posStart = 0;
    for (std::string::size_type posEnd = 0; (posEnd = s.find(chDelimiter, posEnd)) != std::string::npos; ++posEnd)
    {
        strSet.emplace(s.substr(posStart, posEnd - posStart));
        posStart = posEnd + 1;
    }
    strSet.emplace(s.substr(posStart));
}

/**
 * Join string vector to string with delimiter.
 * 
 * \param v - input vector of strings
 * \param chDelimiter - delimiter to use
 * \return joined string
 */
static std::string str_join(const v_strings& v, const char chDelimiter)
{
    std::string s;
    // calculate reserve for string s
    size_t nReserve = 0;
    for (const auto& str : v)
        nReserve += str.size() + 1;
    s.reserve(nReserve);
    for (const auto& str : v)
    {
        if (!s.empty())
            s += chDelimiter;
        s += str;
    }
    return s;
}

/**
 * Join string vector to string with const char * delimiter.
 * 
 * \param v - input vector of strings
 * \param szDelimiter - string delimiter to use
 * \return joined string
 */
static std::string str_join(const v_strings& v, const char* szDelimiter)
{
    std::string s;
    // calculate reserve for string s
    size_t nReserve = 0;
    size_t nDelimSize = strlen(SAFE_SZ(szDelimiter));
    for (const auto& str : v)
        nReserve += str.size() + nDelimSize;
    s.reserve(nReserve);
    for (const auto& str : v)
    {
        if (!s.empty())
            s += SAFE_SZ(szDelimiter);
        s += str;
    }
    return s;
}

