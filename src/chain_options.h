#pragma once
// Copyright (c) 2018-2023 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>

#include <amount.h>

/** Default for -txexpirydelta, in number of blocks */
static constexpr uint32_t DEFAULT_TX_EXPIRY_DELTA = 20;
/** Default for -minrelaytxfee, minimum relay fee for transactions */
static constexpr unsigned int DEFAULT_MIN_RELAY_TX_FEE = 100;
/** The number of blocks within expiry height when a tx is considered to be expiring soon */
static constexpr uint32_t TX_EXPIRING_SOON_THRESHOLD = 3;

class CChainOptions
{
public:
	CChainOptions() noexcept;

	uint32_t expiryDelta = DEFAULT_TX_EXPIRY_DELTA;

	/** Fees smaller than this (in patoshi) are considered zero fee (for relaying and mining) */
	CFeeRate minRelayTxFee;
};

extern CChainOptions gl_ChainOptions;