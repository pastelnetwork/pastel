#pragma once
// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <variant>
#include <cstdint>
#include <tuple>

#include <univalue.h>

#include <utils/uint256.h>
#include <utils/svc_thread.h>

std::variant<uint32_t, uint256> rpc_get_block_hash_or_height(const UniValue& paramValue);
uint32_t rpc_parse_height_param(const UniValue& param);
uint32_t rpc_get_height_param(const UniValue& params, size_t no);
std::tuple<uint32_t, uint32_t> rpc_get_height_range(const UniValue& params);

