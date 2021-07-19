#pragma once
// Copyright (c) 2021 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using v_strings = std::vector<std::string>;
using v_uint8 = std::vector<uint8_t>;
using v_bytes = std::vector<std::byte>;

/**
 * Convert string to byte vector.
 * This function is using memcpy and ~60 times faster than using v.assign(s.cbegin(), s.cend())
 * 
 * \param s - string to convert
 * \return - byte vector
 */
inline v_uint8 string_to_vector(const std::string &s) noexcept
{
    v_uint8 v;
    v.resize(s.size());
    memcpy(v.data(), s.data(), s.size());
    return v;
}

inline void string_to_vector(const std::string &s, v_uint8 &v) noexcept
{
    v.resize(s.size());
    memcpy(v.data(), s.data(), s.size());
}

inline void append_string_to_vector(const std::string &s, v_uint8 &v) noexcept
{
    const size_t nSize = v.size();
    v.resize(nSize + s.size());
    memcpy(v.data() + nSize, s.data(), s.size());
}

inline std::string vector_to_string(const v_uint8 &v)
{
    std::string s;
    s.resize(v.size());
    memcpy(s.data(), v.data(), v.size());
    return s;
}
