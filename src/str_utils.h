#pragma once
// Copyright (c) 2021 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <string>
#include <algorithm>

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
 * \param s
 */
static inline std::string &lowercase(std::string &s)
{
    std::transform(s.cbegin(), s.cend(), s.begin(), [](const auto ch) { return std::tolower(ch); });
    return s;
}

/**
 * uppercase string in-place.
 *
 * \param s
 */
static inline std::string & uppercase(std::string& s)
{
    std::transform(s.cbegin(), s.cend(), s.begin(), [](const auto ch) { return std::toupper(ch); });
    return s;
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
