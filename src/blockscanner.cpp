// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include <utils/sync.h>
#include <blockscanner.h>
#include <main.h>

using namespace std;

CBlockScanner::CBlockScanner(const uint256& hashBlockStart)
{
    LOCK(cs_main);

    auto pindex = chainActive.Tip();
    while (pindex)
    {
        const auto &diskBlockPos = pindex->GetBlockPos();
        auto it = m_mapBlockFiles.find(diskBlockPos.nFile);
        if (it == m_mapBlockFiles.end())
		{
            m_mapBlockFiles[diskBlockPos.nFile] = v_uint32();

            auto &vOffsets = m_mapBlockFiles[diskBlockPos.nFile];
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

        if (pindex->GetBlockHash() == hashBlockStart)
            break;

        pindex = pindex->pprev;
    }

    // sort offsets in ascending order
    for (auto& [nFile, vOffsets] : m_mapBlockFiles)
		sort(vOffsets.begin(), vOffsets.end());
}

CBlockScanner::~CBlockScanner()
{
    // clear this first - it has vOffsets pointers to m_mapBlockFiles
	m_vTasks.clear();
	m_mapBlockFiles.clear();
}

void CBlockScanner::execute(const string &sThreadPrefix, const BlockScannerTaskHandler &taskHandler)
{
    CServiceThreadGroup threadGroup;
    string error;
    const auto &consensusParams = Params().GetConsensus();

    unsigned int nNumThreads = thread::hardware_concurrency();
	if (nNumThreads == 0)
		nNumThreads = 1;
    if (nNumThreads > BLOCK_SCANNER_MAX_THREADS)
		nNumThreads = BLOCK_SCANNER_MAX_THREADS;

    for (auto& [nFile, vOffsets] : m_mapBlockFiles)
    {
        // wait for threads to finish if we have reached the maximum number of threads
        if (threadGroup.size() >= nNumThreads)
            threadGroup.join_all();

        // split the offsets into smaller chunks if there are too many
        if (vOffsets.size() > BLOCK_SCANNER_MAX_OFFSETS_PER_THREAD)
        {
            string sThreadName;
            for (size_t i = 0; i < vOffsets.size(); i += BLOCK_SCANNER_MAX_OFFSETS_PER_THREAD)
            {
                m_vTasks.emplace_back(make_unique<BlockScannerTask>(nFile, vOffsets, i, 
                    BLOCK_SCANNER_MAX_OFFSETS_PER_THREAD, consensusParams, nullptr));
                const size_t nTaskIndex = m_vTasks.size() - 1;
                sThreadName = strprintf("%s-%d-%zu", sThreadPrefix, nFile, nTaskIndex);
                if (threadGroup.add_func_thread(error, sThreadName.c_str(), 
                    [taskHandler, nTaskIndex, this]() { taskHandler(this->m_vTasks[nTaskIndex].get()); }) == INVALID_THREAD_OBJECT_ID)
                    throw runtime_error(error);
			}
        }
        else
        {
            m_vTasks.emplace_back(make_unique<BlockScannerTask>(nFile, vOffsets, 0, 
                vOffsets.size(), consensusParams, nullptr));
            const size_t nTaskIndex = m_vTasks.size() - 1;
            if (threadGroup.add_func_thread(error, strprintf("%s-%d", sThreadPrefix, nFile).c_str(),
                [taskHandler, nTaskIndex, this]() { taskHandler(this->m_vTasks[nTaskIndex].get()); }) == INVALID_THREAD_OBJECT_ID)
                throw runtime_error(error);
        }
    }
    threadGroup.join_all();
}