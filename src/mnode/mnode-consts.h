#pragma once
// Copyright (c) 2021-2023 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>

enum class MN_FEE: uint32_t
{
	StorageFeePerMB = 0,		// data storage fee per MB
	TicketChainStorageFeePerKB, // ticket blockchain storage fee per KB
    SenseComputeFee,			// flat sense compute fee
    SenseProcessingFeePerMB,	// sense processing fee per MB

	COUNT
};

constexpr int DATASTREAM_VERSION = 1;
