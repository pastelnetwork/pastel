// Copyright (c) 2022-2023 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <random>
#include <algorithm>

#include <gtest/gtest.h>
#include <scope_guard.hpp>

#ifdef ENABLE_WALLET
#include <init.h>
#include <key_io.h>
#include <transaction_builder.h>
#include <mnode/tickets/username-change.h>

#include <test_mnode/mock_p2fms_txbuilder.h>
#include <test_mnode/mock_wallet.h>
#include <pastel_gtest_main.h>

using namespace std;
using namespace testing;

class Test_CP2FMS_TX_Builder : 
	public MockP2FMS_TxBuilder,
	public Test
{
public:
	Test_CP2FMS_TX_Builder() :
		MockP2FMS_TxBuilder()
	{}

	static void SetUpTestCase()
	{
		gl_pPastelTestEnv->InitializeChainTest(ChainNetwork::REGTEST);
        UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_OVERWINTER, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
        UpdateNetworkUpgradeParameters(Consensus::UpgradeIndex::UPGRADE_SAPLING, Consensus::NetworkUpgrade::ALWAYS_ACTIVE);
	}

    static void TearDownTestSuite()
    {
        gl_pPastelTestEnv->FinalizeChainTest();
    }

	void SetUp() override
	{
		m_pwalletMain = pwalletMain;
		pwalletMain = &m_MockedWallet;
		ON_CALL(*this, CreateP2FMSScripts).WillByDefault([this]()
		{
			return this->CP2FMS_TX_Builder::CreateP2FMSScripts();
		});
		ON_CALL(*this, PreprocessAndValidate).WillByDefault([this]()
		{
			return this->CP2FMS_TX_Builder::PreprocessAndValidate();
		});
		ON_CALL(*this, BuildTransaction).WillByDefault([this](CMutableTransaction& tx_out)
		{
			return this->CP2FMS_TX_Builder::BuildTransaction(tx_out);
		});
	}

	void TearDown() override
	{
		pwalletMain = m_pwalletMain;
	}

protected:
	MockWallet m_MockedWallet;
	CWallet *m_pwalletMain;
	vector<COutput> m_vTestCoins;

	string generateTransparentAddress(CKeyID &keyID)
	{
		KeyIO keyIO(m_chainParams);
		CPubKey newKey;
		EXPECT_TRUE(m_pwalletMain->GetKeyFromPool(newKey));
		keyID = newKey.GetID();
		return keyIO.EncodeDestination(keyID);
	}

	void addTestCoins(const CAmount nInputInPSL, const v_amounts& vOutputAmountsInPSL)
	{
		const auto& consensusParams = m_chainParams.GetConsensus();
		CKeyID tKeyID;
		string sTAddr = generateTransparentAddress(tKeyID);
		CTxDestination tAddrIn = tKeyID;
		auto scriptPubKey = GetScriptForDestination(tAddrIn);
		auto builder = TransactionBuilder(consensusParams, 1, m_pwalletMain);
		builder.SetFee(0);
		builder.AddTransparentInput(COutPoint(), scriptPubKey, nInputInPSL * COIN);
		for (const auto& outputAmountInPSL : vOutputAmountsInPSL)
		{
			string sTAddrOut = generateTransparentAddress(tKeyID);
			CTxDestination tAddrOut = tKeyID;
			builder.AddTransparentOutput(tAddrOut, outputAmountInPSL * COIN);
		}
		auto tx = builder.Build().GetTxOrThrow();
		CWalletTx* wtx = new CWalletTx(m_pwalletMain, tx);
		for (uint32_t i = 0; i < vOutputAmountsInPSL.size(); ++i)
			m_vTestCoins.emplace_back(wtx, static_cast<int>(i), 6 * 24, true);
	}

	void clearTestCoins()
	{
		for (size_t i = 0; i < m_vTestCoins.size(); ++i)
		{
			for (size_t j = i + 1; j < m_vTestCoins.size(); ++j)
			{
				if (m_vTestCoins[j].tx == m_vTestCoins[i].tx)
					m_vTestCoins[j].tx = nullptr; // prevent double free
			}
			safe_delete_obj(m_vTestCoins[i].tx);
		}
		m_vTestCoins.clear();
	}
};

TEST_F(Test_CP2FMS_TX_Builder, PreprocessAndValidate_Success)
{
	CChangeUsernameTicket ticket;
	ticket.setUserName("test");
	m_DataStream << ticket;

	EXPECT_CALL(*this, CreateP2FMSScripts)
		.WillRepeatedly(Invoke(this, &MockP2FMS_TxBuilder::Call_CreateP2FMSScripts));
	EXPECT_CALL(m_MockedWallet, IsLocked())
		.WillRepeatedly(Return(false));
	EXPECT_CALL(*this, PreprocessAndValidate)
		.WillRepeatedly(Invoke(this, &MockP2FMS_TxBuilder::Call_PreprocessAndValidate));

	EXPECT_TRUE(PreprocessAndValidate());

	// success with valid funding address
	CKeyID keyID;
	m_sFundingAddress = generateTransparentAddress(keyID);

	EXPECT_FALSE(m_bUseFundingAddress);
	EXPECT_TRUE(PreprocessAndValidate());
	EXPECT_TRUE(m_bUseFundingAddress);
}

TEST_F(Test_CP2FMS_TX_Builder, PreprocessAndValidate_Failure)
{
	CChangeUsernameTicket ticket;
	ticket.setUserName("test");

	EXPECT_CALL(*this, PreprocessAndValidate)
		.WillRepeatedly(Invoke(this, &MockP2FMS_TxBuilder::Call_PreprocessAndValidate));

	// no wallet defined
	pwalletMain = nullptr;
	EXPECT_FALSE(PreprocessAndValidate());
	EXPECT_TRUE(!m_error.empty());

	// wallet is locked
	pwalletMain = &m_MockedWallet;
	m_error.clear();
	EXPECT_CALL(m_MockedWallet, IsLocked()).WillOnce(Return(true));
	EXPECT_FALSE(PreprocessAndValidate());
	EXPECT_TRUE(!m_error.empty());

	// empty input stream
	m_error.clear();
	m_DataStream.clear();
	EXPECT_CALL(m_MockedWallet, IsLocked()).WillRepeatedly(Return(false));
	EXPECT_FALSE(PreprocessAndValidate());
	EXPECT_TRUE(!m_error.empty());

	// 0-size P2FMS scripts
	m_error.clear();
	m_DataStream << ticket;
	EXPECT_CALL(*this, CreateP2FMSScripts()).WillOnce(Return(0));
	EXPECT_FALSE(PreprocessAndValidate());
	EXPECT_TRUE(!m_error.empty());

	// no P2FMS scripts generated
	EXPECT_CALL(*this, CreateP2FMSScripts()).WillOnce(Return(100));
	m_vOutScripts.clear();
	EXPECT_FALSE(PreprocessAndValidate());
	EXPECT_TRUE(!m_error.empty());

	// invalid funding address
	m_error.clear();
	EXPECT_CALL(*this, CreateP2FMSScripts())
		.WillOnce(Invoke(this, &MockP2FMS_TxBuilder::Call_CreateP2FMSScripts));
	m_sFundingAddress = "invalid_address";
	EXPECT_FALSE(PreprocessAndValidate());
	EXPECT_TRUE(!m_error.empty());
}

TEST_F(Test_CP2FMS_TX_Builder, build)
{
	CChangeUsernameTicket ticket;
	ticket.setUserName("test");
	m_DataStream << ticket;

	EXPECT_CALL(*this, CreateP2FMSScripts())
		.WillRepeatedly(Invoke(this, &MockP2FMS_TxBuilder::Call_CreateP2FMSScripts));
	EXPECT_CALL(*this, PreprocessAndValidate)
		.WillRepeatedly(Invoke(this, &MockP2FMS_TxBuilder::Call_PreprocessAndValidate));
	EXPECT_CALL(*this, BuildTransaction)
		.WillRepeatedly(Invoke(this, &MockP2FMS_TxBuilder::Call_BuildTransaction));
	EXPECT_CALL(*this, SignTransaction)
		.WillRepeatedly(Return(true));
	EXPECT_CALL(m_MockedWallet, IsLocked())
		.WillRepeatedly(Return(false));

	CFeeRate savedPayTxFee = payTxFee;
    auto guardPayTxFee = sg::make_scope_guard([&]() noexcept 
    {
        payTxFee = savedPayTxFee;
    });
	payTxFee = CFeeRate(1000, 1000); // 1000 patoshis per 1000 bytes

	EXPECT_CALL(m_MockedWallet, AvailableCoins(_, _, _, _, _, _, _))
		.WillRepeatedly(Invoke([this] (std::vector<COutput>& vCoins, bool, const CCoinControl*, bool, bool, int, bool)
		{
			vCoins = m_vTestCoins;
		}));

	string error;
	CMutableTransaction txOut;
	{
		// enough funds in one output:
		//   1) first full pass
		//   2) 2nd pass just checks tx fee can be squeezed in
		addTestCoins(1000, {{ 1000 }});
		m_nPriceInPSL = 100;
		EXPECT_TRUE(build(error, txOut));
		EXPECT_EQ(1, m_vSelectedOutputs.size());
		clearTestCoins();
	}

	{
		// funds in 3 outputs + 1 output for tx fee
		//   1) first full pass (3 outputs)
		//   2) 2nd pass adds 4th output for tx fee
		addTestCoins(1050, {{ 300, 100, 200, 50, 400 }});
		m_nPriceInPSL = 600;
		EXPECT_TRUE(build(error, txOut));
		EXPECT_EQ(4, m_vSelectedOutputs.size());
		clearTestCoins();
	}

	{
		payTxFee = CFeeRate(1 * COIN, 1000); // 1 PSL per 1000 bytes
		maxTxFee = 50 * COIN;
		addTestCoins(140, {{ 50, 30, 20, 40 }});
		m_nPriceInPSL = 50;
		m_DataStream.clear();
		// 2nd pass)
		//   - tx size = 46574 + 2 (signatures per input) * 72 (signature size) = 46718 bytes
		//   - tx fee = 46718 * 100 = 4'671'800 ~46 PSL, need 4th output to cover tx fee
		// 3rd pass)
		//   - tx size = 46738 + 4 (signatures per input) * 72 (signature size) = 47026 bytes
		//   - tx fee = 47026 * 100 = 4'702'600 ~47 PSL
		ticket.setUserName(string(20000, 'a'));
		m_DataStream << ticket;
		EXPECT_TRUE(build(error, txOut));
		EXPECT_EQ(4, m_vSelectedOutputs.size());
		CAmount nTotalOut = 0;
		for (const auto &out: txOut.vout)
			nTotalOut += out.nValue;
		// outputs should have 50 PSL + tx fee (~47 PSL)
		EXPECT_EQ(140 * COIN - 4'702'600, nTotalOut);
		clearTestCoins();
	}
}

TEST_F(Test_CP2FMS_TX_Builder, SignTransaction)
{
	EXPECT_CALL(*this, CreateP2FMSScripts())
		.WillRepeatedly(Invoke(this, &MockP2FMS_TxBuilder::Call_CreateP2FMSScripts));
	EXPECT_CALL(*this, PreprocessAndValidate)
		.WillRepeatedly(Invoke(this, &MockP2FMS_TxBuilder::Call_PreprocessAndValidate));
	EXPECT_CALL(*this, BuildTransaction)
		.WillRepeatedly(Invoke(this, &MockP2FMS_TxBuilder::Call_BuildTransaction));
	EXPECT_CALL(m_MockedWallet, IsLocked())
		.WillRepeatedly(Return(false));
	EXPECT_CALL(*this, SignTransaction(_))
		.WillRepeatedly(
			DoAll(
				InvokeWithoutArgs([this]{ pwalletMain = m_pwalletMain; }),
				Invoke(this, &MockP2FMS_TxBuilder::Call_SignTransaction)
			));

	CChangeUsernameTicket ticket;
	ticket.setUserName("test");
	m_DataStream << ticket;

	CFeeRate savedPayTxFee = payTxFee;
    auto guardPayTxFee = sg::make_scope_guard([&]() noexcept 
    {
        payTxFee = savedPayTxFee;
    });
	payTxFee = CFeeRate(1000, 1000); // 1000 patoshis per 1000 bytes

	EXPECT_CALL(m_MockedWallet, AvailableCoins(_, _, _, _, _, _, _))
		.WillRepeatedly(Invoke([this] (std::vector<COutput>& vCoins, bool, const CCoinControl*, bool, bool, int, bool)
		{
			vCoins = m_vTestCoins;
		}));

	string error;
	CMutableTransaction txOut;
	{
		// generate 100 random outputs
		constexpr size_t nOutputs = 100;
		v_amounts vAmounts;
		vAmounts.resize(nOutputs);
		random_device rd;
		mt19937 gen(rd());
		uniform_int_distribution<> distrib(1, 100);
		generate(vAmounts.begin(), vAmounts.end(), [&] { return distrib(gen); });
		CAmount nTotalInPSL = accumulate(vAmounts.cbegin(), vAmounts.cend(), CAmount(0));
		
		addTestCoins(nTotalInPSL, vAmounts);
		m_nPriceInPSL = nTotalInPSL * 9 / 10;

		EXPECT_TRUE(build(error, txOut));

		clearTestCoins();
	}
}
#endif // ENABLE_WALLET
