// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <chain_options.h>

using namespace std;

// insightexplorer
atomic_bool fInsightExplorer = false;  
atomic_bool fAddressIndex = false;
atomic_bool fSpentIndex = true;
atomic_bool fTimestampIndex = false;
atomic_bool fFundsTransferIndex = false;

CChainOptions gl_ChainOptions;

CChainOptions::CChainOptions() noexcept :
	minRelayTxFee(DEFAULT_MIN_RELAY_TX_FEE)
{}

