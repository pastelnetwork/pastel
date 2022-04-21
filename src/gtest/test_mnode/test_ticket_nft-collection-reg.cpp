// Copyright (c) 2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <utilstrencodings.h>
#include <mnode/tickets/nft-collection-reg.h>
#include <mnode/mnode-controller.h>

using namespace std;
using namespace testing;

constexpr auto TEST_GREEN_ADDRESS = "tPj5BfCrLfLpuviSJrD3B1yyWp3XkgtFjb6";

class PTestNFTCollRegTicket_parsetkt : 
	public CNFTCollectionRegTicket,
	public TestWithParam<tuple<
	string, // nft_ticket - not base64-encoded
	bool,   // expected result
	string, // expected error substring
	function<void(const PTestNFTCollRegTicket_parsetkt &p)> // validation function
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

TEST_P(PTestNFTCollRegTicket_parsetkt, parse_nft_collection_ticket)
{
	const auto& params = GetParam();
	m_sNFTCollectionTicket = EncodeBase64(get<0>(params));
	const bool bExpectedResult = get<1>(params);
	const auto& sExpectedError = get<2>(params);
	const auto& fnValidateResult = get<3>(params);

	string error;
	try
	{
		parse_nft_collection_ticket();
		EXPECT_TRUE(bExpectedResult) << "nft_collection_ticket was successfully parsed, expected failure";
		fnValidateResult(*this);
	}
	catch (const exception& ex)
	{
		error = ex.what();
		EXPECT_FALSE(bExpectedResult) << "nft_collection_ticket parsing failed with [" << error << "]";
	}
	if (!bExpectedResult)
		EXPECT_NE(error.find(sExpectedError), string::npos);
}

constexpr auto TEST_CREATOR_ID = "jXYW94ge4vXUSTMyT3o86H7Pp2PAmd2UUgkUZSUTVRB16GNRNYwNgqHZqFC6zWwixghjZuVBeYrCdNXWvpGhTW";
constexpr auto TEST_USER_ID = "jXYqZNPj21RVnwxnEJ654wEdzi7GZTZ5LAdiotBmPrF7pDMkpX1JegDMQZX55WZLkvy9fxNpZcbBJuE8QYUqBF";
constexpr uint32_t TEST_BLOCK_NUM = 123;
constexpr auto TEST_BLOCK_HASH = "03135e4e147a737b4bbd9928156280aab25eefbd2358a0f928487b635f3d329b";
constexpr uint32_t TEST_NFT_MAX_COUNT = 120;
constexpr uint32_t TEST_NFT_COPY_COUNT = 10;
constexpr uint32_t TEST_CLOSING_HEIGHT = 500;
constexpr float TEST_ROYALTY_FEE = 0.25;
constexpr auto TEST_COLLECTION_NAME = "Test NFT Collection Name";

INSTANTIATE_TEST_SUITE_P(test_nft_collection_reg, PTestNFTCollRegTicket_parsetkt, Values(
	make_tuple( // valid v1 nft_collection_ticket (invalid royalty fee)
		strprintf(R"({
				"nft_collection_ticket_version": 1,
				"nft_collection_name": "%s",
				"creator": "%s",
				"blocknum": %u,
				"block_hash": "%s",
				"permitted_users": [
					"%s", "%s"
				],
				"closing_height": %u,
				"nft_max_count": %u,
				"nft_copy_count": %u,
				"royalty": %f,
				"green": true,
				"app_ticket": ""
		   })", TEST_COLLECTION_NAME, TEST_CREATOR_ID, TEST_BLOCK_NUM, TEST_BLOCK_HASH, TEST_CREATOR_ID, TEST_USER_ID,
			TEST_CLOSING_HEIGHT, TEST_NFT_MAX_COUNT, TEST_NFT_COPY_COUNT, TEST_ROYALTY_FEE), true, "", 
		[](const PTestNFTCollRegTicket_parsetkt &p)
		{
			EXPECT_STREQ(p.getCreatorPastelID_param().c_str(), TEST_CREATOR_ID);
			EXPECT_EQ(p.getCreatorHeight(), TEST_BLOCK_NUM);
			EXPECT_STREQ(p.getTopBlockHash().c_str(), TEST_BLOCK_HASH);
			EXPECT_EQ(p.getClosingHeight(), TEST_CLOSING_HEIGHT);
			EXPECT_EQ(p.getMaxNFTCount(), TEST_NFT_MAX_COUNT);
			EXPECT_EQ(p.getNFTCopyCount(), TEST_NFT_COPY_COUNT);
			EXPECT_EQ(p.getRoyalty(), TEST_ROYALTY_FEE);
			EXPECT_TRUE(p.hasGreenFee());
			EXPECT_STREQ(p.getGreenAddress().c_str(), TEST_GREEN_ADDRESS);
			EXPECT_TRUE(p.IsUserPermitted(TEST_CREATOR_ID));
			EXPECT_TRUE(p.IsUserPermitted(TEST_USER_ID));
			EXPECT_FALSE(p.IsUserPermitted("abcd"));

			string error;
			EXPECT_FALSE(p.ValidateFees(error));
			EXPECT_TRUE(!error.empty());
		}),
	make_tuple(
		"{}", false, "key 'nft_collection_ticket_version' not found", [](const PTestNFTCollRegTicket_parsetkt& p) {}),
	make_tuple( // unsupported property
		R"({ "nft_collection_ticket_version": 1,
			 "unknown_ticket_property": "abcd"
		})", false, "Found unsupported property 'unknown_ticket_property'", [](const PTestNFTCollRegTicket_parsetkt& p) {}),
	make_tuple( // missing required property
		R"({ "nft_collection_ticket_version": 1,
			 "creator": "123",
			 "blocknum": 1,
			 "block_hash": "aaaa",
			 "royalty": 0.2,
			 "app_ticket": ""
		})", false, "Missing required properties", [](const PTestNFTCollRegTicket_parsetkt& p) {})
));

