// Copyright (c) 2023-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <stdexcept>
#include <atomic>

#include <utils/sync.h>
#include <utils/svc_thread.h>
#include <main.h>
#include <rpc/protocol.h>
#include <rpc/coin-supply.h>

using namespace std;

void ProcessBlockFile(int nFile, v_uint32& vOffsets, atomic<CAmount> &totalCoinSupplyRef)
{
    const auto& consensusParams = Params().GetConsensus();
    CAmount nTotalCoinSupply = 0;

    // sort offsets in ascending order
    std::sort(vOffsets.begin(), vOffsets.end());

    // read blocks from disk and process
    for (const auto& nOffset : vOffsets)
    {
        CDiskBlockPos blockPos(nFile, nOffset);
        CBlock block;
        if (!ReadBlockFromDisk(block, blockPos, consensusParams))
            throw JSONRPCError(RPC_MISC_ERROR, "Failed to get total coin supply. ReadBlockFromDisk failed");

        if (block.vtx.empty())
			continue;
        const auto &tx = block.vtx[0];
        if (!tx.IsCoinBase())
            continue;

        for (const auto &out : tx.vout)
            nTotalCoinSupply += out.nValue;
    }
    vOffsets.clear();
    totalCoinSupplyRef.fetch_add(nTotalCoinSupply, memory_order_relaxed);
}

UniValue getTotalCoinSupply(const UniValue& params, bool fHelp)
{
    if (fHelp || !params.empty())
        throw runtime_error(
R"(get-total-coin-supply

Returns the total supply of coins as of current active chain height.
)");

    static constexpr auto MSG_GET_TOTAL_SUPPLY_FAILED = "Failed to get total coin supply.";
    static constexpr auto MAX_THREADS = 5U;

    uint32_t nCurrentHeight = gl_nChainHeight;
    LogFnPrintf("Calculating total coin supply for the height=%d...", nCurrentHeight);
   
    static constexpr auto VOFFSET_VECTOR_RESERVE = 2000U;
    unordered_map<int, v_uint32> mapBlockFiles;

    {
        LOCK(cs_main);

        auto pindex = chainActive.Tip();
        while (pindex)
        {
            const auto &diskBlockPos = pindex->GetBlockPos();
            auto it = mapBlockFiles.find(diskBlockPos.nFile);
            if (it == mapBlockFiles.end())
			{
                mapBlockFiles[diskBlockPos.nFile] = v_uint32();

                auto &vOffsets = mapBlockFiles[diskBlockPos.nFile];
                vOffsets.reserve(VOFFSET_VECTOR_RESERVE);
                vOffsets.push_back(diskBlockPos.nPos);
			}
            else
            {
                auto &vOffsets = it->second;
                if (vOffsets.capacity() - vOffsets.size() == 0)
                    vOffsets.reserve(vOffsets.capacity() + VOFFSET_VECTOR_RESERVE);
				vOffsets.push_back(diskBlockPos.nPos);
			}

            pindex = pindex->pprev;
        }
    }

    atomic<CAmount> nTotalCoinSupply = 0;
    CServiceThreadGroup threadGroup;
    string error;
    for (auto &[nFile, vOffsets] : mapBlockFiles)
	{
        if (threadGroup.size() >= MAX_THREADS)
            threadGroup.join_all();

        threadGroup.add_func_thread(error, strprintf("coin-supply-%d", nFile).c_str(),
            bind(&ProcessBlockFile, nFile, ref(vOffsets), ref(nTotalCoinSupply)));
	}
    threadGroup.join_all();
    LogFnPrintf("Total coin supply for the height=%d is %.5f", nCurrentHeight, GetTruncatedPSLAmount(nTotalCoinSupply));

    UniValue retObj(UniValue::VOBJ);
    retObj.pushKV("totalCoinSupply", GetTruncatedPSLAmount(nTotalCoinSupply.load()));
    retObj.pushKV("totalCoinSupplyPat", nTotalCoinSupply.load());
    retObj.pushKV("height", static_cast<uint64_t>(nCurrentHeight));
    return retObj;
}
