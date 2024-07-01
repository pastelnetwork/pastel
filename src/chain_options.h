#pragma once
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <atomic>

#include <amount.h>

/** Default for -txexpirydelta, in number of blocks */
constexpr uint32_t DEFAULT_TX_EXPIRY_DELTA = 20;
/** Default for -minrelaytxfee, minimum relay fee for transactions */
constexpr unsigned int DEFAULT_MIN_RELAY_TX_FEE = 30;
/** The number of blocks within expiry height when a tx is considered to be expiring soon */
constexpr uint32_t TX_EXPIRING_SOON_THRESHOLD = 3;
/**
 * Block files containing a block-height within MIN_BLOCKS_TO_KEEP of chainActive.Tip() 
 * will not be pruned...
 */
constexpr uint32_t MIN_BLOCKS_TO_KEEP = 288;
/** The default checklevel for block db validation */	
constexpr uint32_t DEFAULT_BLOCKDB_CHECKLEVEL = 3;
constexpr uint32_t DEFAULT_BLOCKDB_CHECKBLOCKS = MIN_BLOCKS_TO_KEEP;

constexpr int FORK_BLOCK_LIMIT = MIN_BLOCKS_TO_KEEP;
// expiration time in secs for the fork switch entry in the fork-switch-tracker
constexpr time_t FORK_SWITCH_TRACKER_EXPIRATION_TIME_SECS = 5 * 60;
constexpr size_t MAX_FAILED_FORK_SWITCHES = 3;

// START insightexplorer
extern std::atomic_bool fInsightExplorer;

void SetInsightExplorer(const bool fEnable);
// The following flags enable specific indices (DB tables), but are not exposed as
// separate command-line options; instead they are enabled by experimental feature "-insightexplorer"
// and are always equal to the overall controlling flag, fInsightExplorer.

// Maintain a full address index, used to query for the balance, txids and unspent outputs for addresses
extern std::atomic_bool fAddressIndex;

// Maintain a full spent index, used to query the spending txid and input index for an outpoint
extern std::atomic_bool fSpentIndex;

// Maintain a full timestamp index, used to query for blocks within a time range
extern std::atomic_bool fTimestampIndex;

// Maintain a full burn tx index, used to query for burn txs
extern std::atomic_bool fBurnTxIndex;

// END insightexplorer

class CChainOptions
{
public:
	CChainOptions() noexcept;

	uint32_t expiryDelta = DEFAULT_TX_EXPIRY_DELTA;

	/** Fees smaller than this (in patoshi) are considered zero fee (for relaying and mining) */
	CFeeRate minRelayTxFee;
};

extern CChainOptions gl_ChainOptions;