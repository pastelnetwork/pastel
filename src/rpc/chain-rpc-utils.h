#pragma once
// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <variant>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include <univalue.h>

#include <consensus/params.h>
#include <utils/vector_types.h>
#include <utils/uint256.h>
#include <utils/svc_thread.h>
#include <chain_options.h>

using block_id_t = std::variant<uint32_t, uint256>;
block_id_t rpc_get_block_hash_or_height(const UniValue& paramValue);
uint32_t rpc_parse_height_param(const UniValue& param);
uint32_t rpc_get_height_param(const UniValue& params, size_t no);
height_range_opt_t rpc_get_height_range(const UniValue& params);

