// Copyright (c) 2023-2024 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .
#include <thread>
#include <future>
#include <queue>

#include <utils/sync.h>
#include <main.h>
#include <wallet/wallet.h>
#include <init.h>
#include <wallet/missing_txs.h>

using namespace std;

using txid_queue_t = queue<uint256>;

txid_queue_t ScanChainSegment(const CBlockIndex* pStartingBlockIndex, const bool bFixWalletTxs, const uint32_t nBlocksToScan, 
	bool bTipStartingBlock, const bool bTxOnlyInvolvingMe)
{
	const auto &consensusParams = Params().GetConsensus();
	txid_queue_t missing_txs;
	auto pBlockIndex = pStartingBlockIndex;
	size_t nMissingTxs = 0;
	LogFnPrintf("Scanning blocks %d..%d for missing wallet transactions", 
				pStartingBlockIndex->nHeight - nBlocksToScan, pStartingBlockIndex->nHeight);
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
			{
				LOCK(pwalletMain->cs_wallet);
				if (pwalletMain->mapWallet.count(txid))
					continue;
			}
			bool bProcessedMissingTx = false;

			{
				LOCK(pwalletMain->cs_wallet);
				if (bFixWalletTxs)
				{

					if (bTxOnlyInvolvingMe)
						bProcessedMissingTx = pwalletMain->AddToWalletIfInvolvingMe(tx, &block, false);
					else
						bProcessedMissingTx = pwalletMain->AddTxToWallet(tx, &block, false);
				}
				else
				{
					if (bTxOnlyInvolvingMe)
						bProcessedMissingTx = pwalletMain->IsTxInvolvingMe(tx);
					else
						bProcessedMissingTx = true;
				}
			}
			if (bProcessedMissingTx)
			{
				missing_txs.push(txid);
				++nMissingTxs;
			}
		}
		pBlockIndex = pBlockIndex->pprev;
		bTipStartingBlock = false;
	}
	LogFnPrintf("Processed blocks %d..%d, %s %zu missing wallet txs", 
		pStartingBlockIndex->nHeight - nBlocksToScan, pStartingBlockIndex->nHeight, 
		bFixWalletTxs ? "added" : "found", nMissingTxs);
	return missing_txs;
}

UniValue ScanWalletForMissingTransactions(const uint32_t nStartingHeight, const bool bFixWalletTxs, const bool bTxOnlyInvolvingMe)
{
	unsigned int nNumThreads = thread::hardware_concurrency();
	if (nNumThreads == 0)
		nNumThreads = 1;
	const uint32_t nBlockCount = gl_nChainHeight - nStartingHeight + 1;
	uint32_t nBlocksPerThread = 0;
	if (nBlockCount < nNumThreads)
	{
		nNumThreads = 1;
		nBlocksPerThread = nBlockCount;
	} else
		nBlocksPerThread = nBlockCount / nNumThreads;

	bool bTipStartingBlock = true;
	vector<future<txid_queue_t>> futures;
	futures.reserve(nNumThreads);
	// each future will process nBlocksPerThread blocks (one segment)

	LogFnPrintf("Scanning%s %u blocks for missing wallet transactions in %d threads (%u blocks per thread)",
		bFixWalletTxs ? " and fixing" : "", nBlockCount, nNumThreads, nBlocksPerThread);
	LOCK(cs_main);
	auto pBlockIndex = chainActive.Tip();
	if (pBlockIndex)
	{
		bool bAllBlocksProcessed = false;
		for (size_t i = 0; i < nNumThreads; ++i)
		{
			// If this is the last thread, assign any remaining blocks to it
			if (i == nNumThreads - 1)
				nBlocksPerThread += nBlockCount % nNumThreads;
			const auto pStartingBlockIndex = pBlockIndex;
			for (uint32_t j = 0; j < nBlocksPerThread; ++j)
			{
				const int nCurrentHeight = pBlockIndex->nHeight;
				pBlockIndex = pBlockIndex->pprev;
				if (!pBlockIndex && nCurrentHeight > 0)
					throw runtime_error(strprintf(
						"Previous block index is not defined for height %d", nCurrentHeight));
				if (nCurrentHeight == nStartingHeight)
				{
					bAllBlocksProcessed = true;
					break;
				}
			}
			futures.push_back(async(launch::async, ScanChainSegment, pStartingBlockIndex, bFixWalletTxs, nBlocksPerThread, bTipStartingBlock, bTxOnlyInvolvingMe));
			bTipStartingBlock = false;
			if (bAllBlocksProcessed)
				break;
		}
	}

	// Wait for all futures and collect the results
	UniValue ret(UniValue::VARR);
	size_t nProcessedTxs = 0;
	for (auto& f : futures)
	{
		auto missingTxQueue = f.get();
		while (!missingTxQueue.empty())
		{
			ret.push_back(missingTxQueue.front().ToString());
			missingTxQueue.pop();
			++nProcessedTxs;
		}
	}
	if (nProcessedTxs && pwalletMain)
	{
		LogFnPrintf("Processed %zu wallet transactions", nProcessedTxs);
		LOCK(pwalletMain->cs_wallet);
		pwalletMain->Flush(false);
	}
	return ret;
}
