// Copyright (c) 2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <mnode/tickets/nft-sell.h>

using namespace std;
using namespace testing;

class PTestNFTSellTicket_height_validation :
	public CNFTSellTicket, 
	public TestWithParam<tuple<uint32_t, uint32_t, uint32_t, SELL_TICKET_STATE>>
{
public:
	PTestNFTSellTicket_height_validation() = default;
};

TEST_P(PTestNFTSellTicket_height_validation, test)
{
	const auto& params = GetParam();
	const auto nValidAfter = get<0>(params);
	if (nValidAfter > 0)
		m_nValidAfter = nValidAfter;
	const auto nValidBefore = get<1>(params);
	if (nValidBefore > 0)
		m_nValidBefore = nValidBefore;
	const auto nCurrentHeight = get<2>(params);
	const auto state = checkValidState(nCurrentHeight);
	EXPECT_EQ(state, get<3>(params));
}

INSTANTIATE_TEST_SUITE_P(test_nft_sell, PTestNFTSellTicket_height_validation, Values(
	make_tuple(0, 0, 125, SELL_TICKET_STATE::NOT_DEFINED),
	make_tuple(115, 0, 114, SELL_TICKET_STATE::NOT_ACTIVE),
	make_tuple(115, 0, 115, SELL_TICKET_STATE::NOT_ACTIVE),
	make_tuple(115, 0, 116, SELL_TICKET_STATE::ACTIVE),
	make_tuple(0, 120, 119, SELL_TICKET_STATE::ACTIVE),
	make_tuple(0, 120, 120, SELL_TICKET_STATE::EXPIRED),
	make_tuple(0, 120, 121, SELL_TICKET_STATE::EXPIRED),
	make_tuple(115, 120, 120, SELL_TICKET_STATE::EXPIRED),
	make_tuple(115, 120, 125, SELL_TICKET_STATE::EXPIRED),
	make_tuple(120, 130, 125, SELL_TICKET_STATE::ACTIVE),
	make_tuple(130, 140, 125, SELL_TICKET_STATE::NOT_ACTIVE),
	make_tuple(120, 125, 125, SELL_TICKET_STATE::EXPIRED)
	));
