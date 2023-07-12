#pragma once
// Copyright (c) 2017 Pieter Wuille
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Bech32 is a string encoding format used in newer address types.
// The output consists of a human-readable part (alphanumeric), a
// separator character (1), and a base32 data section, the last
// 6 characters of which are a checksum.
//
// For more information, see BIP 173.
#include <cstdint>

#include <vector_types.h>

namespace bech32
{

/** Encode a Bech32 string. Returns the empty string in case of failure. */
std::string Encode(const std::string& hrp, const v_uint8& values);

/** Decode a Bech32 string. Returns (hrp, data). Empty hrp means failure. */
std::pair<std::string, v_uint8> Decode(const std::string& str);

} // namespace bech32
