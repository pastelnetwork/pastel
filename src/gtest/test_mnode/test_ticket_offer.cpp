// Copyright (c) 2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <mnode/tickets/offer.h>

using namespace std;
using namespace testing;

class PTestOfferTicket_height_validation :
	public COfferTicket, 
	public TestWithParam<tuple<uint32_t, uint32_t, uint32_t, OFFER_TICKET_STATE>>
{
public:
	PTestOfferTicket_height_validation() = default;
};

TEST_P(PTestOfferTicket_height_validation, test)
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

INSTANTIATE_TEST_SUITE_P(test_offer, PTestOfferTicket_height_validation, Values(
	make_tuple(0, 0, 125, OFFER_TICKET_STATE::NOT_DEFINED),
	make_tuple(115, 0, 114, OFFER_TICKET_STATE::NOT_ACTIVE),
	make_tuple(115, 0, 115, OFFER_TICKET_STATE::NOT_ACTIVE),
	make_tuple(115, 0, 116, OFFER_TICKET_STATE::ACTIVE),
	make_tuple(0, 120, 119, OFFER_TICKET_STATE::ACTIVE),
	make_tuple(0, 120, 120, OFFER_TICKET_STATE::EXPIRED),
	make_tuple(0, 120, 121, OFFER_TICKET_STATE::EXPIRED),
	make_tuple(115, 120, 120, OFFER_TICKET_STATE::EXPIRED),
	make_tuple(115, 120, 125, OFFER_TICKET_STATE::EXPIRED),
	make_tuple(120, 130, 125, OFFER_TICKET_STATE::ACTIVE),
	make_tuple(130, 140, 125, OFFER_TICKET_STATE::NOT_ACTIVE),
	make_tuple(120, 125, 125, OFFER_TICKET_STATE::EXPIRED)
	));
