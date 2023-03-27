// Copyright (c) 2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <mnode/tickets/ticket-types.h>

using namespace std;

TEST(ticket_types, GetCollectionItemType)
{
	string sItemType = GetCollectionItemType(COLLECTION_ITEM_TYPE::SENSE);
	EXPECT_EQ(sItemType, COLLECTION_ITEM_TYPE_SENSE);

	sItemType = GetCollectionItemType(COLLECTION_ITEM_TYPE::NFT);
	EXPECT_EQ(sItemType, COLLECTION_ITEM_TYPE_NFT);
}