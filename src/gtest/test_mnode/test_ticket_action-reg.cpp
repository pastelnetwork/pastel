// Copyright (c) 2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <gtest/gtest.h>

#include <mnode/tickets/action-reg.h>

#include <pastel_gtest_main.h>
#include <test_mnode/test_data.h>

using namespace std;
using namespace testing;

class PTestActionRegTicket_parsetkt : 
	public CActionRegTicket,
	public TestWithParam<tuple<
		string, // action_ticket - not base64-encoded
		bool,   // expected result
		string, // expected error substring
		function<void(const PTestActionRegTicket_parsetkt &p)> // validation function
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

TEST_P(PTestActionRegTicket_parsetkt, parse_action_ticket)
{
	const auto& params = GetParam();
	m_sActionTicket = EncodeBase64(get<0>(params));
	const bool bExpectedResult = get<1>(params);
	const auto& sExpectedError = get<2>(params);
	const auto& fnValidateResult = get<3>(params);

	string error;
	try
	{
		parse_action_ticket();
		EXPECT_TRUE(bExpectedResult) << "action_ticket was successfully parsed, expected failure";
		fnValidateResult(*this);
	}
	catch (const exception& ex)
	{
		error = ex.what();
		EXPECT_FALSE(bExpectedResult) << "action_ticket parsing failed with [" << error << "]";
	}
	if (!bExpectedResult)
		EXPECT_NE(error.find(sExpectedError), string::npos);
}

INSTANTIATE_TEST_SUITE_P(test_action_reg, PTestActionRegTicket_parsetkt, Values(
	make_tuple( // valid v1 action_ticket
		strprintf(R"({
				"action_ticket_version": 1,
				"action_type": "sense",
				"caller": "%s",
				"blocknum": %u,
				"block_hash": "%s",
				"api_ticket": ""
		   })", TEST_CREATOR_ID, TEST_BLOCK_NUM, TEST_BLOCK_HASH), true, "", 
			[](const PTestActionRegTicket_parsetkt &p)
			{
				EXPECT_EQ(p.getTicketVersion(), 1u);
				EXPECT_STREQ(p.getCreatorPastelID_param().c_str(), TEST_CREATOR_ID);
				EXPECT_EQ(p.getCalledAtHeight(), TEST_BLOCK_NUM);
				EXPECT_STREQ(p.getTopBlockHash().c_str(), TEST_BLOCK_HASH);
			}),
	make_tuple(
		"{}", false, "key 'action_ticket_version' not found", [](const PTestActionRegTicket_parsetkt& p) {}),
	make_tuple( // unsupported property
		R"({ "action_ticket_version": 1,
			 "unknown_ticket_property": "abcd"
		})", false, "Found unsupported property 'unknown_ticket_property'", [](const PTestActionRegTicket_parsetkt& p) {}),
	make_tuple( // duplicate property - json implementation does not throw an error: uses second value
		R"({ "action_ticket_version": 1,
  		     "action_type": "sense",
			 "caller": "action_caller_1",
			 "caller": "action_caller_2",
			 "blocknum": 1,
			 "block_hash": "123",
			 "api_ticket": ""
		})", true, "", [](const PTestActionRegTicket_parsetkt& p)
			{
				EXPECT_STREQ(p.getCreatorPastelID_param().c_str(), "action_caller_2");
			}),
	make_tuple( // missing required property
		R"({ "action_ticket_version": 1,
			 "caller": "123",
			 "blocknum": 1,
			 "block_hash": "aaaa",
			 "api_ticket": ""
		})", false, "Missing required properties", [](const PTestActionRegTicket_parsetkt& p) {}),
	make_tuple( // valid v2 action_ticket - with collection_txid property
		strprintf(R"({
				"action_ticket_version": 2,
   		        "action_type": "sense",
				"caller": "%s",
				"blocknum": %u,
				"block_hash": "%s",
				"collection_txid": "%s",
				"api_ticket": ""
		   })", TEST_CREATOR_ID, TEST_BLOCK_NUM, TEST_BLOCK_HASH, TEST_COLLECTION_TXID), true, "", 
					[](const PTestActionRegTicket_parsetkt &p)
					{
						EXPECT_EQ(p.getTicketVersion(), 2u);
						EXPECT_STREQ(p.getCreatorPastelID_param().c_str(), TEST_CREATOR_ID);
						EXPECT_EQ(p.getCalledAtHeight(), TEST_BLOCK_NUM);
						EXPECT_STREQ(p.getTopBlockHash().c_str(), TEST_BLOCK_HASH);
						EXPECT_STREQ(p.getCollectionTxId().c_str(), TEST_COLLECTION_TXID);
					})
));

class TestActionRegTicket : 
	public CActionRegTicket,
	public Test
{
public:
	static void SetUpTestSuite()
	{
		gl_pPastelTestEnv->InitializeRegTest();
	}

	static void TearDownTestSuite()
	{
		gl_pPastelTestEnv->FinalizeRegTest();
	}
};

TEST_F(TestActionRegTicket, RetrieveCollectionTicket)
{
	bool bInvalidTxId = false;
	string error;

	// test invalid collection txid
	m_sCollectionTxid = "123";
	EXPECT_EQ(RetrieveCollectionTicket(error, bInvalidTxId), nullptr);
	EXPECT_TRUE(bInvalidTxId);
	EXPECT_TRUE(!error.empty());

	// valid collection txid
	error.clear();
	bInvalidTxId = false;
	m_sCollectionTxid = TEST_COLLECTION_TXID;
	EXPECT_EQ(RetrieveCollectionTicket(error, bInvalidTxId), nullptr);
	EXPECT_FALSE(bInvalidTxId);
	EXPECT_FALSE(error.empty());
}

