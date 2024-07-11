// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#pragma once
#include <memory>
#include <functional>
#include <unordered_map>

#include <utils/vector_types.h>
#include <consensus/params.h>

typedef struct _BlockScannerTask
{
    _BlockScannerTask(const int nBlockFile, const v_uint32 &vBlockOffsets, 
        const size_t blockOffsetIndexStart, const size_t blockOffsetIndexCount, 
        const Consensus::Params& consensusParams, void *pTaskParam) noexcept :
        nBlockFile(nBlockFile), vBlockOffsets(vBlockOffsets),
        nBlockOffsetIndexStart(blockOffsetIndexStart), nBlockOffsetIndexCount(blockOffsetIndexCount),
        consensusParams(consensusParams), pTaskParam(pTaskParam)
    {}

    int nBlockFile;
    const v_uint32 &vBlockOffsets;
    size_t nBlockOffsetIndexStart;
    size_t nBlockOffsetIndexCount;
    const Consensus::Params& consensusParams;
    void *pTaskParam;
} BlockScannerTask;

typedef std::function<void(BlockScannerTask *)> BlockScannerTaskHandler;

class CBlockScanner
{
public:
    CBlockScanner(const uint256 &hashBlockStart);
    ~CBlockScanner();
    void execute(const std::string &sThreadPrefix, const BlockScannerTaskHandler &taskHandler);

private:
    static constexpr size_t VOFFSET_VECTOR_RESERVE = 2000U;
    static constexpr size_t BLOCK_SCANNER_MAX_THREADS = 7U;
    static constexpr size_t BLOCK_SCANNER_MAX_OFFSETS_PER_THREAD = 10000U;

    std::vector<std::unique_ptr<BlockScannerTask>> m_vTasks;
    std::unordered_map<int, v_uint32> m_mapBlockFiles;
};

