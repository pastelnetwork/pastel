#pragma once
// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <variant>
#include <cstdint>
#include <functional>
#include <unordered_map>

#include <univalue.h>

#include <consensus/params.h>
#include <utils/vector_types.h>
#include <utils/uint256.h>
#include <utils/svc_thread.h>
#include <chain_options.h>

std::variant<uint32_t, uint256> rpc_get_block_hash_or_height(const UniValue& paramValue);
uint32_t rpc_parse_height_param(const UniValue& param);
uint32_t rpc_get_height_param(const UniValue& params, size_t no);
height_range_opt_t rpc_get_height_range(const UniValue& params);

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
