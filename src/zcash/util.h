#pragma once
// Copyright (c) 2015-2018 The Zcash developers
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <vector>
#include <cstdint>

#include <utils/vector_types.h>

v_uint8 convertIntToVectorLE(const uint64_t val_int);
std::vector<bool> convertBytesVectorToVector(const v_uint8& bytes);
uint64_t convertVectorToInt(const v_bools& v);

