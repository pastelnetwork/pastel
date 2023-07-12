// Copyright (c) 2018-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#pragma once

#include <vector>

#include <gmock/gmock.h>

#ifdef ENABLE_WALLET
#include <wallet/wallet.h>

class MockWallet : public CWallet
{
public:
	MOCK_METHOD(void, AvailableCoins, (std::vector<COutput>& vCoins,
		bool fOnlyConfirmed,
		const CCoinControl* coinControl,
		bool fIncludeZeroValue,
		bool fIncludeCoinBase,
		int exactCoins,
		bool fIncludeLocked), (const, override));
	MOCK_METHOD(bool, IsCrypted, (), (const, noexcept, override));
	MOCK_METHOD(bool, IsLocked, (), (const, noexcept, override));
};

#endif // ENABLE_WALLET
