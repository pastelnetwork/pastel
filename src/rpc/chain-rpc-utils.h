#pragma once
// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <variant>
#include <univalue.h>

#include <utils/uint256.h>

std::variant<uint32_t, uint256> rpc_get_block_hash_or_height(const UniValue& paramValue);