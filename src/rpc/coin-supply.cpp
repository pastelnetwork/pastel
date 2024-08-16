// Copyright (c) 2023-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <stdexcept>
#include <atomic>

#include <utils/sync.h>
#include <utils/svc_thread.h>
#include <chain.h>
#include <main.h>
#include <blockscanner.h>
#include <rpc/rpc_consts.h>
#include <rpc/protocol.h>
#include <rpc/coin-supply.h>

using namespace std;

UniValue getTotalCoinSupply(const UniValue& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw runtime_error(
R"(get-total-coin-supply

Returns the total supply of coins as of current active chain height.
)");

    static constexpr auto MSG_GET_TOTAL_SUPPLY_FAILED = "Failed to get total coin supply.";

    uint32_t nCurrentHeight = gl_nChainHeight;
    LogFnPrintf("Calculating total coin supply for the height=%d...", nCurrentHeight);
   
    uint256 hash;
    CBlockScanner blockScanner(hash);

    atomic<CAmount> nTotalCoinSupply = 0;
    try
    {
        blockScanner.execute("coin-supply", [&](BlockScannerTask *pTask)
        {
            CAmount nTotalCoinSupplyLocal = 0;

            // read blocks from disk and process
            for (size_t i = pTask->nBlockOffsetIndexStart;
                i < min(pTask->nBlockOffsetIndexStart + pTask->nBlockOffsetIndexCount, pTask->vBlockOffsets.size()); ++i)
            {
                CDiskBlockPos blockPos(pTask->nBlockFile, pTask->vBlockOffsets[i]);
                CBlock block;
                if (!ReadBlockFromDisk(block, blockPos, pTask->consensusParams))
                    throw JSONRPCError(RPC_MISC_ERROR, "ReadBlockFromDisk failed");

                if (block.vtx.empty())
                    continue;
                const auto& tx = block.vtx[0];
                if (!tx.IsCoinBase())
                    continue;

                for (const auto& out : tx.vout)
                    nTotalCoinSupplyLocal += out.nValue;
            }
            nTotalCoinSupply.fetch_add(nTotalCoinSupplyLocal, memory_order_relaxed);
        });
    } catch (const std::exception& e)
    {
		throw JSONRPCError(RPC_MISC_ERROR, 
            strprintf("%s %s", MSG_GET_TOTAL_SUPPLY_FAILED, e.what()));
	}
    LogFnPrintf("Total coin supply for the height=%d is %.5f", 
        nCurrentHeight, GetTruncatedPSLAmount(nTotalCoinSupply));

    UniValue retObj(UniValue::VOBJ);
    retObj.pushKV("totalCoinSupply", GetTruncatedPSLAmount(nTotalCoinSupply.load()));
    retObj.pushKV("totalCoinSupplyPat", nTotalCoinSupply.load());
    retObj.pushKV(RPC_KEY_HEIGHT, nCurrentHeight);
    return retObj;
}
