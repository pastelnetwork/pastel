// Copyright (c) 2023 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#include <thread>
#include <future>
#include <queue>

#include <sync.h>
#include <main.h>
#include <wallet/wallet.h>
#include <init.h>
#include <wallet/missing_txs.h>

using namespace std;

using txid_queue_t = queue<uint256>;

txid_queue_t ScanChainSegment(const CBlockIndex* pStartingBlockIndex, const bool bFixWalletTxs, const uint32_t nBlocksToScan, bool bTipStartingBlock)
{
	const auto &consensusParams = Params().GetConsensus();
	txid_queue_t missing_txs;
	const CBlockIndex *pBlockIndex = pStartingBlockIndex;
	for (uint32_t i = 0; i < nBlocksToScan && pBlockIndex; ++i)
	{
		CBlock block;
		if (!ReadBlockFromDisk(block, pBlockIndex, consensusParams))
			throw runtime_error(strprintf(
				"Failed to read block at height %d", pBlockIndex->nHeight));
		for (const auto& tx : block.vtx)
		{
			const uint256& txid = tx.GetHash();
			if (bTipStartingBlock && tx.IsCoinBase())
				continue;
			if (!pwalletMain->mapWallet.count(txid))
			{
				missing_txs.push(txid);
				if (bFixWalletTxs && !pwalletMain->AddToWalletIfInvolvingMe(tx, &block, false))
					throw runtime_error(strprintf(
						"Failed to add transaction %s to wallet", txid.ToString()));
			}
		}
		pBlockIndex = pBlockIndex->pprev;
		bTipStartingBlock = false;
	}
	return missing_txs;
}

UniValue ScanWalletForMissingTransactions(const uint32_t nStartingHeight, const bool bFixWalletTxs)
{
	unsigned int nNumThreads = thread::hardware_concurrency();
	if (nNumThreads == 0)
		nNumThreads = 1;
	const uint32_t nBlockCount = gl_nChainHeight - nStartingHeight + 1;
	uint32_t nBlocksPerThread = nBlockCount / nNumThreads;	

	bool bTipStartingBlock = true;
	vector<future<txid_queue_t>> futures;
	futures.reserve(nNumThreads);
	// each future will process nBlocksPerThread blocks (one segment)

	LOCK2(cs_main, pwalletMain->cs_wallet);
	auto pBlockIndex = chainActive.Tip();
	if (pBlockIndex)
	{
		for (size_t i = 0; i < nNumThreads; ++i)
		{
			const auto pStartingBlockIndex = pBlockIndex;
			for (uint32_t j = 0; j < nBlocksPerThread; ++j)
			{
				const int nCurrentHeight = pBlockIndex->nHeight;
				pBlockIndex = pBlockIndex->pprev;
				if (!pBlockIndex && nCurrentHeight > 0)
					throw runtime_error(strprintf(
						"Previous block index is not defined for height %d", nCurrentHeight));
			}
			// If this is the last thread, assign any remaining blocks to it
			if (i == nNumThreads - 1)
				nBlocksPerThread += nBlockCount % nNumThreads;
			futures.push_back(async(launch::async, ScanChainSegment, pStartingBlockIndex, nBlocksPerThread, bFixWalletTxs, bTipStartingBlock));
			bTipStartingBlock = false;
		}
	}

	// Wait for all futures and collect the results
	UniValue ret(UniValue::VARR);
	for (auto& f : futures)
	{
		auto missingTxQueue = f.get();
		while (!missingTxQueue.empty())
		{
			ret.push_back(missingTxQueue.front().ToString());
			missingTxQueue.pop();
		}
	}
	return ret;
}
