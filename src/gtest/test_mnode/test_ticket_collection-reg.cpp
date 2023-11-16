// Copyright (c) 2022-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <utils/utilstrencodings.h>
#include <mnode/tickets/collection-reg.h>
#include <mnode/mnode-controller.h>

#include <test_mnode/test_data.h>

using namespace std;
using namespace testing;

class PTestCollRegTicket_parsetkt : 
	public CollectionRegTicket,
	public TestWithParam<tuple<
							string, // nft_ticket - not base64-encoded
							bool,   // expected result
							string, // expected error substring
							function<void(const PTestCollRegTicket_parsetkt &p)> // validation function
						>>
{
public:
	static void SetUpTestSuite()
	{
		masterNodeCtrl.TicketGreenAddress = TEST_GREEN_ADDRESS;
	}

	static void TearDownTestSuite()
	{
		masterNodeCtrl.TicketGreenAddress.clear();
	}
};

TEST_P(PTestCollRegTicket_parsetkt, parse_collection_ticket)
{
	const auto& params = GetParam();
	m_sCollectionTicket = get<0>(params);
	const bool bExpectedResult = get<1>(params);
	const auto& sExpectedError = get<2>(params);
	const auto& fnValidateResult = get<3>(params);

	string error;
	try
	{
		parse_collection_ticket();
		EXPECT_TRUE(bExpectedResult) << "collection_ticket was successfully parsed, expected failure";
		fnValidateResult(*this);
	}
	catch (const exception& ex)
	{
		error = ex.what();
		EXPECT_FALSE(bExpectedResult) << "collection_ticket parsing failed with [" << error << "]";
	}
	if (!bExpectedResult)
		EXPECT_NE(error.find(sExpectedError), string::npos);
}

INSTANTIATE_TEST_SUITE_P(test_collection_reg, PTestCollRegTicket_parsetkt, Values(
	make_tuple( // valid v1 collection_ticket (invalid royalty fee)
		strprintf(R"({
				"collection_ticket_version": 1,
				"collection_name": "%s",
				"item_type": "nft",
				"creator": "%s",
				"blocknum": %u,
				"block_hash": "%s",
				"list_of_pastelids_of_authorized_contributors": [
					"%s", "%s"
				],
				"collection_final_allowed_block_height": %u,
				"max_collection_entries": %u,
				"collection_item_copy_count": %u,
				"royalty": %f,
				"green": true,
				"app_ticket": ""
		   })", TEST_COLLECTION_NAME, TEST_CREATOR_ID, TEST_BLOCK_NUM, TEST_BLOCK_HASH, TEST_CREATOR_ID, TEST_USER_ID,
			TEST_COLLECTION_FINAL_ALLOWED_BLOCK_HEIGHT, TEST_MAX_ALLOWED_COLLECTION_ENTRIES, TEST_COLLECTION_ITEM_COPY_COUNT, TEST_ROYALTY_FEE), true, "", 
		[](const PTestCollRegTicket_parsetkt &p)
		{
			EXPECT_STREQ(p.getCreatorPastelID_param().c_str(), TEST_CREATOR_ID);
			EXPECT_EQ(p.getItemType(), COLLECTION_ITEM_TYPE::NFT);
			EXPECT_STREQ(p.getItemTypeStr().c_str(), "nft");
			EXPECT_STREQ(p.getName().c_str(), TEST_COLLECTION_NAME);	
			EXPECT_EQ(p.getCreatorHeight(), TEST_BLOCK_NUM);
			EXPECT_STREQ(p.getTopBlockHash().c_str(), TEST_BLOCK_HASH);
			EXPECT_EQ(p.getCollectionFinalAllowedBlockHeight(), TEST_COLLECTION_FINAL_ALLOWED_BLOCK_HEIGHT);
            EXPECT_EQ(p.getMaxCollectionEntries(), TEST_MAX_ALLOWED_COLLECTION_ENTRIES);
			EXPECT_EQ(p.getItemCopyCount(), TEST_COLLECTION_ITEM_COPY_COUNT);
			EXPECT_EQ(p.getRoyalty(), TEST_ROYALTY_FEE);
			EXPECT_TRUE(p.hasGreenFee());
			EXPECT_STREQ(p.getGreenAddress().c_str(), TEST_GREEN_ADDRESS);
            EXPECT_TRUE(p.IsAuthorizedContributor(TEST_CREATOR_ID));
            EXPECT_TRUE(p.IsAuthorizedContributor(TEST_USER_ID));
            EXPECT_FALSE(p.IsAuthorizedContributor("abcd"));

			string error;
			EXPECT_FALSE(p.ValidateFees(error));
			EXPECT_TRUE(!error.empty());
		}),
	make_tuple(
		"{}", false, "key 'collection_ticket_version' not found", [](const PTestCollRegTicket_parsetkt& p) {}),
	make_tuple( // unsupported property
		R"({ "collection_ticket_version": 1,
			 "unknown_ticket_property": "abcd"
		})", false, "Found unsupported property 'unknown_ticket_property'", [](const PTestCollRegTicket_parsetkt& p) {}),
	make_tuple( // missing required property
		R"({ "collection_ticket_version": 1,
			 "creator": "123",
			 "blocknum": 1,
			 "block_hash": "aaaa",
			 "royalty": 0.2,
			 "app_ticket": ""
		})", false, "Missing required properties", [](const PTestCollRegTicket_parsetkt& p) {})
));

