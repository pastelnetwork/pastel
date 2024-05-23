// Copyright (c) 2015-2018 The Zcash developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <algorithm>
#include <stdexcept>

#include <zcash/util.h>


v_uint8 convertIntToVectorLE(const uint64_t val_int)
{
    v_uint8 vBytes(sizeof(uint64_t));
    for (size_t i = 0; i < sizeof(uint64_t); i++)
        vBytes[i] = static_cast<uint8_t>(val_int >> (i * 8));
    return vBytes;
}

// Convert bytes into boolean vector. (MSB to LSB)
v_bools convertBytesVectorToVector(const v_uint8& bytes)
{
    v_bools ret;
    ret.resize(bytes.size() * 8);

    unsigned char c;
    for (size_t i = 0; i < bytes.size(); i++)
    {
        c = bytes.at(i);
        for (size_t j = 0; j < 8; j++)
            ret.at((i*8)+j) = (c >> (7-j)) & 1;
    }

    return ret;
}

// Convert boolean vector (big endian) to integer
uint64_t convertVectorToInt(const v_bools& v)
{
    if (v.size() > 64)
        throw std::length_error ("boolean vector can't be larger than 64 bits");

    uint64_t result = 0;
    for (size_t i = 0; i < v.size(); i++)
    {
        if (v.at(i))
            result |= (uint64_t)1 << ((v.size() - 1) - i);
    }

    return result;
}
