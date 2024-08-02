// Copyright (c) 2024 The Pastel Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <regex>
#include <limits>

#include <utils/tinyformat.h>
#include <utils/str_utils.h>
#include <chain.h>
#include <rpc/chain-rpc-utils.h>
#include <rpc/protocol.h>
#include <rpc/rpc-utils.h>
#include <main.h>

using namespace std;

/**
 * Parse parameter that may contain block hash or block height.
 * Block height can be defined in a string format or as a number.
 * 
 * \param paramValue - rpc parameter value (block hash or block height)
 * \return block hash or block height
 */
variant<uint32_t, uint256> rpc_get_block_hash_or_height(const UniValue& paramValue)
{
    if (paramValue.isNull())
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Block hash or height parameter is required");

	if (!paramValue.isStr() && !paramValue.isNum())
		throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block hash or height parameter type");

    const uint32_t nCurrentHeight = gl_nChainHeight;

    uint32_t nBlockHeight;
    uint256 blockHash;
    string sParamValue;
    
    if (paramValue.isStr())
    {
        sParamValue = paramValue.get_str();
        trim(sParamValue);

        // check if height is supplied as a string parameter
        if (sParamValue.size() <= numeric_limits<uint32_t>::digits10)
        {
            // stoi allows characters, whereas we want to be strict
            regex r("[[:digit:]]+");
            if (!regex_match(sParamValue, r))
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid block height parameter [%s]", sParamValue));

            if (!str_to_uint32_check(sParamValue.c_str(), sParamValue.size(), nBlockHeight))
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid block height parameter [%s]", sParamValue));

            if (nBlockHeight > nCurrentHeight)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Block height %u out of range [0..%u]", nBlockHeight, nCurrentHeight));

            return nBlockHeight;
        }
        // hash is supplied as a string parameter
        string error;
        if (!parse_uint256(error, blockHash, sParamValue, "block hash parameter"))
			throw JSONRPCError(RPC_INVALID_PARAMETER, error);

        return blockHash;
    }

    // height is supplied as a number parameter
	nBlockHeight = paramValue.get_int();
	if (nBlockHeight > nCurrentHeight)
		throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Block height %u out of range [0..%u]", nBlockHeight, nCurrentHeight));

	return nBlockHeight;
}

uint32_t rpc_parse_height_param(const UniValue& param)
{
    uint32_t nChainHeight = gl_nChainHeight;
    const int64_t nHeight = get_long_number(param);
    if (nHeight < 0 || nHeight >= numeric_limits<uint32_t>::max())
        throw JSONRPCError(RPC_INVALID_PARAMETER, 
            "<height> parameter cannot be negative or greater than " + to_string(numeric_limits<uint32_t>::max()));
    if (nHeight != 0)
        nChainHeight = static_cast<uint32_t>(nHeight);
	return nChainHeight;
}

uint32_t rpc_get_height_param(const UniValue& params, size_t no)
{
    return (params.size() > no) ? rpc_parse_height_param(params[no]) : gl_nChainHeight.load();
}

height_range_opt_t rpc_get_height_range(const UniValue& params)
{
    uint32_t nStartHeight = 0;
    uint32_t nEndHeight = 0;
    if (params[0].isObject())
    {
        const auto &startValue = find_value(params[0].get_obj(), "start");
        const auto &endValue = find_value(params[0].get_obj(), "end");

        // If either is not specified, the other is ignored.
        if (!startValue.isNull() && !endValue.isNull())
        {
            nStartHeight = rpc_parse_height_param(startValue);
            nEndHeight = rpc_parse_height_param(endValue);

            if (nEndHeight < nStartHeight)
            {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                    "End value is expected to be greater than or equal to start");
            }
        }
    }

    const uint32_t nChainHeight = gl_nChainHeight;
    if (nStartHeight > nChainHeight || nEndHeight > nChainHeight)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Start or end is outside chain range");

    return make_optional<height_range_t>(nStartHeight, nEndHeight);
}

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