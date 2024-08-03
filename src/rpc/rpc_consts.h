#pragma once
// Copyright (c) 2021-2024 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

// rpc response object keys
constexpr auto RPC_KEY_RESULT					 = "result";
constexpr auto RPC_KEY_ERROR_MESSAGE			 = "errorMessage";
constexpr auto RPC_KEY_STATUS					 = "status";
constexpr auto RPC_KEY_ALIAS					 = "alias";
constexpr auto RPC_KEY_TXID						 = "txid";
constexpr auto RPC_KEY_OUTPUT_INDEX				 = "outputIndex";
constexpr auto RPC_KEY_PASTELID					 = "pastelid";
constexpr auto RPC_KEY_LEGROAST					 = "legRoastKey";
constexpr auto RPC_KEY_CODE						 = "code";
constexpr auto RPC_KEY_MESSAGE					 = "message";
constexpr auto RPC_KEY_MESSAGE_DETAILS			 = "messageDetails";	
constexpr auto RPC_KEY_KEY						 = "key";
constexpr auto RPC_KEY_PRIVKEY					 = "privKey";
constexpr auto RPC_KEY_HEIGHT					 = "height";
constexpr auto RPC_KEY_CHAIN_DEFLATOR_FACTOR     = "feeDeflatorFactor";
constexpr auto RPC_KEY_FEE_ADJUSTMENT_MULTIPLIER = "feeAdjustmentMultiplier";
constexpr auto RPC_KEY_GLOBAL_FEE_MULTIPLIER     = "globalFeeMultiplier";
constexpr auto RPC_KEY_AMOUNT					 = "amount";
constexpr auto RPC_KEY_AMOUNT_PATOSHIS			 = "amountPat";

// rpc response object key values
constexpr auto RPC_RESULT_FAILED				= "failed";
constexpr auto RPC_RESULT_SUCCESS				= "successful";

// rpc API methods
constexpr auto RPC_API_GETBLOCKDELTAS			= "getblockdeltas";
constexpr auto RPC_API_GETBLOCKHASHES			= "getblockhashes";
constexpr auto RPC_API_GETADDRESSBALANCE		= "getaddressbalance";
constexpr auto RPC_API_GETADDRESSDELTAS		    = "getaddressdeltas";
constexpr auto RPC_API_GETADDRESSMEMPOOL		= "getaddressmempool";
constexpr auto RPC_API_GETADDRESSTXIDS			= "getaddresstxids";
constexpr auto RPC_API_GETADDRESSUTXOS			= "getaddressutxos";
constexpr auto RPC_API_GETADDRESSUTXOSEXTRA		= "getaddressutxosextra";
constexpr auto RPC_API_GETSPENTINFO				= "getspentinfo";
constexpr auto RPC_API_SCANBURNTRANSACTIONS 	= "scanburntransactions";

inline const char *get_rpc_result(const bool bSucceeded) noexcept { return bSucceeded ? RPC_RESULT_SUCCESS : RPC_RESULT_FAILED; }