#pragma once
// Copyright (c) 2021-2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <cstddef>

enum class MN_FEE: uint32_t
{
	StorageFeePerMB = 0,		// data storage fee per MB
	TicketChainStorageFeePerKB, // ticket blockchain storage fee per KB
    SenseComputeFee,			// flat sense compute fee
    SenseProcessingFeePerMB,	// sense processing fee per MB

	COUNT
};

enum class GetTopMasterNodeStatus: int
{
    SUCCEEDED = 0,              // successfully got top masternodes
    SUCCEEDED_FROM_HISTORY = 1, // successfully got top masternodes from historical top mn data
    MN_NOT_SYNCED = -1,         // masternode is not synced
    BLOCK_NOT_FOUND = -2,       // block not found
    GET_MN_SCORES_FAILED = -3,  // failed to get masternode scores
    NOT_ENOUGH_MNS = -4,        // not enough top masternodes
    HISTORY_NOT_FOUND = -5,	    // historical top mn data not found
};

enum class MNCacheItem : uint8_t
{
    MN_LIST = 0,                // mns
    SEEN_MN_BROADCAST,          // seen
    SEEN_MN_PING,			    // seen
    RECOVERY_REQUESTS,		    // recovery
    RECOVERY_GOOD_REPLIES,	    // recovery
    ASKED_US_FOR_MN_LIST,	    // asked
    WE_ASKED_FOR_MN_LIST,       // asked
    WE_ASKED_FOR_MN_LIST_ENTRY, // asked
    HISTORICAL_TOP_MNS, 	    // top-mns  

    COUNT
};

static constexpr int DATASTREAM_VERSION = 1;

// used to define block hash to use in masternode ping
static constexpr uint32_t MN_PING_HEIGHT_OFFSET = 12;
static constexpr uint32_t MN_PING_HEIGHT_EXPIRATION = 24;
// 7 days, 1 block per 2.5 minutes -> 4032
static constexpr uint32_t MAX_IN_PROCESS_COLLECTION_TICKET_AGE = 7 * 24 * static_cast<uint32_t>(60 / 2.5);
static constexpr int64_t SN_ELIGIBILITY_CHECK_DELAY_SECS = 5 * 60;
static constexpr int SN_ELIGIBILITY_LAST_SEEN_TIME_SECS = 150;
static constexpr uint32_t MN_RECOVERY_LOOKBACK_BLOCKS = 100;

static constexpr int DSEG_UPDATE_SECONDS        = 3 * 60 * 60;

static constexpr int LAST_PAID_SCAN_BLOCKS      = 100;

static constexpr int MIN_POSE_PROTO_VERSION     = 70203;
static constexpr int MAX_POSE_CONNECTIONS       = 10;
static constexpr int MAX_POSE_RANK              = 10;
static constexpr int MAX_POSE_BLOCKS            = 10;

static constexpr size_t MNB_RECOVERY_QUORUM_TOTAL   = 10;
static constexpr size_t MNB_RECOVERY_QUORUM_REQUIRED   = 6;
static constexpr int MNB_RECOVERY_MAX_ASK_ENTRIES   = 10;
static constexpr int MNB_RECOVERY_WAIT_SECONDS      = 60;
static constexpr int MNB_RECOVERY_RETRY_SECONDS     = 3 * 60 * 60;

static constexpr auto MNCACHE_FILENAME = "mncache.dat";
static constexpr auto MNCACHE_CACHE_MAGIC_STR = "magicMasternodeCache";
static constexpr auto MNCACHE_SERIALIZATION_VERSION_PREFIX = "CMasternodeMan-Version-";
static constexpr uint32_t MNCACHE_VERSION_OLD = 7;
static constexpr uint32_t MNCACHE_VERSION_PROTECTED = 8;

