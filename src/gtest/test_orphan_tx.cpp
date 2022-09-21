// Copyright (c) 2022 The Pastel developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <random>

#include <gmock/gmock.h>

#include <orphan-tx.h>
#include <key.h>
#include <keystore.h>

using namespace std;
using namespace testing;

class TestOrphanTxManager : 
	public COrphanTxManager,
	public Test
{
public:
	TestOrphanTxManager() : 
		COrphanTxManager()
	{
		m_key.MakeNewKey(true);
		m_keystore.AddKey(m_key);
	}

	MOCK_METHOD(bool, AcceptOrphanTxToMemPool, (const CChainParams& chainparams, CValidationState& state, const CTransaction& orphanTx, bool& fMissingInputs), (const, override));

protected:
	CTransaction CreateTx(const uint256& txIn)
	{
		CMutableTransaction tx;
		tx.vin.resize(1);
		tx.vin[0].prevout.n = 0;
		tx.vin[0].prevout.hash = txIn;
		tx.vin[0].scriptSig << OP_1;
		tx.vout.resize(1);
		tx.vout[0].nValue =m_nAmount++ * CENT;
		tx.vout[0].scriptPubKey = GetScriptForDestination(m_key.GetPubKey().GetID());
		return tx;
	}

	/**
	 * Create tx tree with the specified number of layers.
	 * tx1 -+-> tx2 -+-> tx4...
     *      |        |-> tx5...
     *      |
     *      +-> tx3 -+-> tx6...
     *               +-> tx7...
	 * 
	 * \param parentTxId - parent tx
	 * \param nLayerCount - number of layers
	 * \param nChildTxCount - number of child transactions
	 */
	size_t CreateTestOrphanTxTree(const uint256 & parentTxId, const size_t nLayerCount, const size_t nChildTxCount)
	{ 
		vector<v_uint256> vTxLayer;
		vTxLayer.resize(nLayerCount);
		vTxLayer[0].push_back(parentTxId);
		size_t nCount = 0;
		for (size_t i = 1; i < nLayerCount; ++i)
		{
			const auto& vPrev = vTxLayer[i - 1];
			auto& v = vTxLayer[i];
			v.reserve(vPrev.size() * nChildTxCount);
			for (const auto& txid : vPrev)
			{
				for (size_t j = 0; j < nChildTxCount; ++j)
				{
					CTransaction tx = CreateTx(txid);
					v.push_back(tx.GetHash());
					EXPECT_TRUE(AddOrphanTx(tx, 1));
				}
			}
			nCount += v.size();
		}
		return nCount;
	}

private:
	CKey m_key;
	CBasicKeyStore m_keystore;
	int m_nAmount {0};
};

TEST_F(TestOrphanTxManager, ProcessOrphanTxs)
{
	SelectParams(ChainNetwork::REGTEST);

	uint256 origin = GetRandHash();
	const size_t nTxCount = CreateTestOrphanTxTree(origin, 7, 3);
	EXPECT_EQ(nTxCount, m_mapOrphanTransactions.size());

	EXPECT_CALL(*this, AcceptOrphanTxToMemPool)
		.Times(static_cast<int>(nTxCount))
		.WillRepeatedly(Return(true));
	CRollingBloomFilter recentRejects(120000, 0.000001);
	ProcessOrphanTxs(Params(), origin, recentRejects);
	// all orphan txs should be processed
	EXPECT_TRUE(m_mapOrphanTransactions.empty());
	EXPECT_TRUE(m_mapOrphanTransactionsByPrev.empty());
}
