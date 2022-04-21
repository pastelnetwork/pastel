// Copyright (c) 2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <utilstrencodings.h>
#include <mnode/tickets/nft-reg.h>
#include <mnode/mnode-controller.h>

using namespace std;
using namespace testing;

constexpr auto TEST_GREEN_ADDRESS = "tPj5BfCrLfLpuviSJrD3B1yyWp3XkgtFjb6";

class PTestNFTRegTicket_parsetkt : 
	public CNFTRegTicket,
	public TestWithParam<tuple<
		string, // nft_ticket - not base64-encoded
		bool,   // expected result
		string, // expected error substring
		function<void(const PTestNFTRegTicket_parsetkt &p)> // validation function
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

TEST_P(PTestNFTRegTicket_parsetkt, parse_nft_ticket)
{
	const auto& params = GetParam();
	m_sNFTTicket = EncodeBase64(get<0>(params));
	const bool bExpectedResult = get<1>(params);
	const auto& sExpectedError = get<2>(params);
	const auto& fnValidateResult = get<3>(params);

	string error;
	try
	{
		parse_nft_ticket();
		EXPECT_TRUE(bExpectedResult) << "nft_ticket was successfully parsed, expected failure";
		fnValidateResult(*this);
	}
	catch (const exception& ex)
	{
		error = ex.what();
		EXPECT_FALSE(bExpectedResult) << "nft_ticket parsing failed with [" << error << "]";
	}
	if (!bExpectedResult)
		EXPECT_NE(error.find(sExpectedError), string::npos);
}

constexpr auto TEST_CREATOR_ID = "jXYW94ge4vXUSTMyT3o86H7Pp2PAmd2UUgkUZSUTVRB16GNRNYwNgqHZqFC6zWwixghjZuVBeYrCdNXWvpGhTW";
constexpr uint32_t TEST_BLOCK_NUM = 123;
constexpr auto TEST_BLOCK_HASH = "03135e4e147a737b4bbd9928156280aab25eefbd2358a0f928487b635f3d329b";
constexpr auto TEST_COLLECTION_TXID = "0101010101010101010101010101010101010101010101010101010101010101";
constexpr uint32_t TEST_TOTAL_COPIES = 35;
constexpr float TEST_ROYALTY_FEE = 0.25;

INSTANTIATE_TEST_SUITE_P(test_nft_reg, PTestNFTRegTicket_parsetkt, Values(
	make_tuple( // valid v1 nft_ticket (invalid royalty fee)
		strprintf(R"({
				"nft_ticket_version": 1,
				"author": "%s",
				"blocknum": %u,
				"block_hash": "%s",
				"copies": %u,
				"royalty": %f,
				"green": true,
				"app_ticket": ""
		   })", TEST_CREATOR_ID, TEST_BLOCK_NUM, TEST_BLOCK_HASH, TEST_TOTAL_COPIES, TEST_ROYALTY_FEE), true, "", 
			[](const PTestNFTRegTicket_parsetkt &p)
			{
				EXPECT_EQ(p.getTicketVersion(), 1u);
				EXPECT_STREQ(p.getCreatorPastelID_param().c_str(), TEST_CREATOR_ID);
				EXPECT_EQ(p.getCreatorHeight(), TEST_BLOCK_NUM);
				EXPECT_STREQ(p.getTopBlockHash().c_str(), TEST_BLOCK_HASH);
				EXPECT_EQ(p.getTotalCopies(), TEST_TOTAL_COPIES);
				EXPECT_EQ(p.getRoyalty(), TEST_ROYALTY_FEE);
				EXPECT_TRUE(p.hasGreenFee());
				EXPECT_STREQ(p.getGreenAddress().c_str(), TEST_GREEN_ADDRESS);

				string error;
				EXPECT_FALSE(p.ValidateFees(error));
				EXPECT_TRUE(!error.empty());
			}),
	make_tuple(
		"{}", false, "key 'nft_ticket_version' not found", [](const PTestNFTRegTicket_parsetkt& p) {}),
	make_tuple( // unsupported property
		R"({ "nft_ticket_version": 1,
			 "unknown_ticket_property": "abcd"
		})", false, "Found unsupported property 'unknown_ticket_property'", [](const PTestNFTRegTicket_parsetkt& p) {}),
	make_tuple( // duplicate property - json implementation does not throw an error: uses second value
		R"({ "nft_ticket_version": 1,
			 "author": "nft_creator_1",
			 "author": "nft_creator_2",
			 "blocknum": 1,
			 "block_hash": "123",
			 "copies": 5,
			 "royalty": 0.1,
			 "green": true,
			 "app_ticket": ""
		})", true, "", [](const PTestNFTRegTicket_parsetkt& p)
			{
				EXPECT_STREQ(p.getCreatorPastelID_param().c_str(), "nft_creator_2");
			}),
	make_tuple( // missing required property
		R"({ "nft_ticket_version": 1,
			 "author": "123",
			 "blocknum": 1,
			 "block_hash": "aaaa",
			 "royalty": 0.2,
			 "app_ticket": ""
		})", false, "Missing required properties", [](const PTestNFTRegTicket_parsetkt& p) {}),
	make_tuple( // valid v2 nft_ticket - no optional royalty, green & copies properties
		strprintf(R"({
				"nft_ticket_version": 2,
				"author": "%s",
				"blocknum": %u,
				"block_hash": "%s",
				"nft_collection_txid": "%s",
				"app_ticket": ""
		   })", TEST_CREATOR_ID, TEST_BLOCK_NUM, TEST_BLOCK_HASH, TEST_COLLECTION_TXID), true, "", 
					[](const PTestNFTRegTicket_parsetkt &p)
					{
						EXPECT_EQ(p.getTicketVersion(), 2u);
						EXPECT_STREQ(p.getCreatorPastelID_param().c_str(), TEST_CREATOR_ID);
						EXPECT_EQ(p.getCreatorHeight(), TEST_BLOCK_NUM);
						EXPECT_STREQ(p.getTopBlockHash().c_str(), TEST_BLOCK_HASH);
						EXPECT_STREQ(p.getNFTCollectionTxId().c_str(), TEST_COLLECTION_TXID);
						EXPECT_EQ(p.getTotalCopies(), 0u);
						EXPECT_EQ(p.getRoyalty(), 0.0f);
						EXPECT_FALSE(p.hasGreenFee());
						EXPECT_TRUE(p.getGreenAddress().empty());
					})
));

class TestNFTRegTicket : 
	public CNFTRegTicket,
	public Test
{};

TEST_F(TestNFTRegTicket, RetrieveCollectionTicket)
{
	bool bInvalidTxId = false;
	string error;

	// test invalid collection txid
	m_sNFTCollectionTxid = "123";
	EXPECT_EQ(RetrieveCollectionTicket(error, bInvalidTxId), nullptr);
	EXPECT_TRUE(bInvalidTxId);
	EXPECT_TRUE(!error.empty());

	// valid collection txid
	error.clear();
	bInvalidTxId = false;
	m_sNFTCollectionTxid = TEST_COLLECTION_TXID;
	EXPECT_EQ(RetrieveCollectionTicket(error, bInvalidTxId), nullptr);
	EXPECT_FALSE(bInvalidTxId);
	EXPECT_TRUE(error.empty());
}
