#pragma once
// Copyright (c) 2021 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// rpc response object keys
constexpr auto RPC_KEY_RESULT					= "result";
constexpr auto RPC_KEY_ERROR_MESSAGE			= "errorMessage";
constexpr auto RPC_KEY_STATUS					= "status";
constexpr auto RPC_KEY_ALIAS					= "alias";

// rpc response object key values
constexpr auto RPC_RESULT_FAILED				= "failed";
constexpr auto RPC_RESULT_SUCCESS				= "successful";

inline const char *get_rpc_result(const bool bSucceeded) noexcept { return bSucceeded ? RPC_RESULT_SUCCESS : RPC_RESULT_FAILED; }