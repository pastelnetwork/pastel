#pragma once
// Copyright (c) 2023-2024 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#include <univalue.h>

UniValue ScanWalletForMissingTransactions(const uint32_t nStartingHeight, 
	const bool bFixWalletTxs = false, const bool bTxOnlyInvolvingMe = true);
