// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <chain_options.h>

CChainOptions gl_ChainOptions;

CChainOptions::CChainOptions() noexcept :
	minRelayTxFee(DEFAULT_MIN_RELAY_TX_FEE)
{}

