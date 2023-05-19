// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include <stdint.h>

#include <univalue.h>

#include <amount.h>
#include <chainparams.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <core_io.h>
#ifdef ENABLE_MINING
#include <crypto/equihash.h>
#endif
#include <init.h>
#include <key_io.h>
#include <main.h>
#include <metrics.h>
#include <miner.h>
#include <net.h>
#include <pow.h>
#include <rpc/server.h>
#include <txmempool.h>
#include <util.h>
#include <validationinterface.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif
#include <mnode/mnode-controller.h>

using namespace std;

/**
 * Return average network hashes per second based on the last 'lookup' blocks,
 * or over the difficulty averaging window if 'lookup' is nonpositive.
 * If 'height' is nonnegative, compute the estimate at the time when a given block was found.
 */
int64_t GetNetworkHashPS(int lookup, int height)
{
    CBlockIndex *pb = chainActive.Tip();

    if (height >= 0 && height < chainActive.Height())
        pb = chainActive[height];

    if (!pb || !pb->nHeight)
        return 0;

    // If lookup is nonpositive, then use difficulty averaging window.
    if (lookup <= 0)
        lookup = static_cast<int>(Params().GetConsensus().nPowAveragingWindow);

    // If lookup is larger than chain, then set it to chain length.
    if (lookup > pb->nHeight)
        lookup = pb->nHeight;

    CBlockIndex *pb0 = pb;
    int64_t minTime = pb0->GetBlockTime();
    int64_t maxTime = minTime;
    for (int i = 0; i < lookup; i++) {
        pb0 = pb0->pprev;
        int64_t time = pb0->GetBlockTime();
        minTime = std::min(time, minTime);
        maxTime = std::max(time, maxTime);
    }

    // In case there's a situation where minTime == maxTime, we don't want a divide by zero exception.
    if (minTime == maxTime)
        return 0;

    arith_uint256 workDiff = pb->nChainWork - pb0->nChainWork;
    int64_t timeDiff = maxTime - minTime;

    return (int64_t)(workDiff.getdouble() / timeDiff);
}

UniValue getlocalsolps(const UniValue& params, bool fHelp)
{
    if (fHelp)
        throw runtime_error(
R"(getlocalsolps

Returns the average local solutions per second since this node was started.
This is the same information shown on the metrics screen (if enabled).

Result:
xxx.xxxxx     (numeric) Solutions per second average

Examples:
)" + HelpExampleCli("getlocalsolps", "")
   + HelpExampleRpc("getlocalsolps", ""));

    LOCK(cs_main);
    return GetLocalSolPS();
}

UniValue getnetworksolps(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
R"(getnetworksolps ( blocks height )

Returns the estimated network solutions per second based on the last n blocks.
Pass in [blocks] to override # of blocks, -1 specifies over difficulty averaging window.
Pass in [height] to estimate the network speed at the time when a certain block was found.

Arguments:
1. blocks     (numeric, optional, default=120) The number of blocks, or -1 for blocks over difficulty averaging window.
2. height     (numeric, optional, default=-1) To estimate at the time of the given height.

Result:
x             (numeric) Solutions per second estimated

Examples:
)" + HelpExampleCli("getnetworksolps", "")
   + HelpExampleRpc("getnetworksolps", ""));

    LOCK(cs_main);
    return GetNetworkHashPS(params.size() > 0 ? params[0].get_int() : 120, params.size() > 1 ? params[1].get_int() : -1);
}

UniValue getnetworkhashps(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
R"(getnetworkhashps ( blocks height )

DEPRECATED - left for backwards-compatibility. Use getnetworksolps instead.

Returns the estimated network solutions per second based on the last n blocks.
Pass in [blocks] to override # of blocks, -1 specifies over difficulty averaging window.
Pass in [height] to estimate the network speed at the time when a certain block was found.

Arguments:
1. blocks     (numeric, optional, default=120) The number of blocks, or -1 for blocks over difficulty averaging window.
2. height     (numeric, optional, default=-1) To estimate at the time of the given height.

Result:
x             (numeric) Solutions per second estimated

Examples:
)" + HelpExampleCli("getnetworkhashps", "")
   + HelpExampleRpc("getnetworkhashps", ""));

    LOCK(cs_main);
    return GetNetworkHashPS(params.size() > 0 ? params[0].get_int() : 120, params.size() > 1 ? params[1].get_int() : -1);
}

#ifdef ENABLE_MINING
UniValue getgenerate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getgenerate

Return if the server is set to generate coins or not. The default is false.
It is set with the command line argument -gen (or pastel.conf setting gen)
It can also be set with the setgenerate call.

Result:
true|false      (boolean) If the server is set to generate coins or not

Examples:
)"  + HelpExampleCli("getgenerate", "")
    + HelpExampleRpc("getgenerate", ""));

    LOCK(cs_main);
    return GetBoolArg("-gen", false);
}

UniValue generate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 1)
        throw runtime_error(
R"(generate numblocks

Mine blocks immediately (before the RPC call returns)

Note: this function can only be used on the regtest network

Arguments:
1. numblocks    (numeric) How many blocks are generated immediately.

Result:
[ blockhashes ] (array) hashes of blocks generated

Examples:
Generate 11 blocks
)" + HelpExampleCli("generate", "11")
   + HelpExampleRpc("generate", "11")
);

    if (GetArg("-mineraddress", "").empty()) {
#ifdef ENABLE_WALLET
        if (!pwalletMain) {
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Wallet disabled and -mineraddress not set");
        }
#else
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "pasteld compiled without wallet and -mineraddress not set");
#endif
    }
    if (!Params().MineBlocksOnDemand())
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "This method can only be used on regtest");

    int nGenerate = params[0].get_int();
#ifdef ENABLE_WALLET
    CReserveKey reservekey(pwalletMain);
#endif

    const uint32_t nHeightStart = gl_nChainHeight;
    const uint32_t nHeightEnd = nHeightStart + nGenerate;
    uint32_t nHeight = nHeightStart;
    unsigned int nExtraNonce = 0;
    UniValue blockHashes(UniValue::VARR);
    const auto& chainparams = Params();
    const auto& consensusParams = chainparams.GetConsensus();
    unsigned int n = consensusParams.nEquihashN;
    unsigned int k = consensusParams.nEquihashK;
    while (nHeight < nHeightEnd)
    {
#ifdef ENABLE_WALLET
        std::unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlockWithKey(reservekey, chainparams));
#else
        std::unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlockWithKey(chainparams));
#endif
        if (!pblocktemplate.get())
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Wallet keypool empty");
        CBlock *pblock = &pblocktemplate->block;
        {
            LOCK(cs_main);
            IncrementExtraNonce(pblock, chainActive.Tip(), nExtraNonce);
        }

        // Hash state
        crypto_generichash_blake2b_state eh_state;
        EhInitialiseState(n, k, eh_state);

        // I = the block header minus nonce and solution.
        CEquihashInput I{*pblock};
        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
        ss << I;

        // H(I||...
        crypto_generichash_blake2b_update(&eh_state, (unsigned char*)&ss[0], ss.size());

        while (true)
        {
            // Yes, there is a chance every nonce could fail to satisfy the -regtest
            // target -- 1 in 2^(2^256). That ain't gonna happen
            pblock->nNonce = ArithToUint256(UintToArith256(pblock->nNonce) + 1);

            // H(I||V||...
            crypto_generichash_blake2b_state curr_state;
            curr_state = eh_state;
            crypto_generichash_blake2b_update(&curr_state,
                                              pblock->nNonce.begin(),
                                              pblock->nNonce.size());

            // (x_1, x_2, ...) = A(I, V, n, k)
            std::function<bool(v_uint8)> validBlock =
                [&pblock](v_uint8 soln) {
                pblock->nSolution = soln;
                solutionTargetChecks.increment();
                return CheckProofOfWork(pblock->GetHash(), pblock->nBits, Params().GetConsensus());
            };
            bool found = EhBasicSolveUncancellable(n, k, curr_state, validBlock);
            ehSolverRuns.increment();
            if (found) {
                goto endloop;
            }
        }
endloop:
        CValidationState state;
        if (!ProcessNewBlock(state, chainparams, nullptr, pblock, true, nullptr))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "ProcessNewBlock, block not accepted");
        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());
    }
    return blockHashes;
}

UniValue setgenerate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
R"(setgenerate generate ( genproclimit )

Set 'generate' true or false to turn generation on or off.
Generation is limited to 'genproclimit' processors, -1 is unlimited.
See the getgenerate call for the current setting.

Arguments:
1. generate         (boolean, required) Set to true to turn on generation, off to turn off.
2. genproclimit     (numeric, optional) Set the processor limit for when generation is on. Can be -1 for unlimited.

Examples:
Set the generation on with a limit of one processor
)" + HelpExampleCli("setgenerate", "true 1") + R"(
Check the setting
)" + HelpExampleCli("getgenerate", "") + R"(
Turn off generation
)" + HelpExampleCli("setgenerate", "false") + R"(
Using json rpc
)" + HelpExampleRpc("setgenerate", "true, 1")
);

    if (GetArg("-mineraddress", "").empty()) {
#ifdef ENABLE_WALLET
        if (!pwalletMain)
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Wallet disabled and -mineraddress not set");
#else
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "pasteld compiled without wallet and -mineraddress not set");
#endif
    }
    const auto& chainparams = Params();
    if (chainparams.MineBlocksOnDemand())
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Use the generate method instead of setgenerate on this network");

    bool fGenerate = true;
    if (params.size() > 0)
        fGenerate = params[0].get_bool();

    int nGenProcLimit = -1;
    if (params.size() > 1)
    {
        nGenProcLimit = params[1].get_int();
        if (nGenProcLimit == 0)
            fGenerate = false;
    }

    mapArgs["-gen"] = (fGenerate ? "1" : "0");
    mapArgs ["-genproclimit"] = itostr(nGenProcLimit);
#ifdef ENABLE_WALLET
    GenerateBitcoins(fGenerate, pwalletMain, nGenProcLimit, chainparams);
#else
    GenerateBitcoins(fGenerate, nGenProcLimit, chainparams);
#endif

    return NullUniValue;
}
#endif

UniValue getmininginfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getmininginfo

Returns a json object containing mining-related information.

Result:
{
  "blocks": nnn,             (numeric) The current block
  "currentblocksize": nnn,   (numeric) The last block size
  "currentblocktx": nnn,     (numeric) The last block transaction
  "difficulty": xxx.xxxxx    (numeric) The current difficulty
  "errors": "..."            (string) Current errors
  "generate": true|false     (boolean) If the generation is on or off (see getgenerate or setgenerate calls)
  "genproclimit": n          (numeric) The processor limit for generation. -1 if no generation. (see getgenerate or setgenerate calls)
  "localsolps": xxx.xxxxx    (numeric) The average local solution rate in Sol/s since this node was started
  "networksolps": x          (numeric) The estimated network solution rate in Sol/s
  "pooledtx": n              (numeric) The size of the mem pool
  "testnet": true|false      (boolean) If using testnet or not
  "chain": "xxxx",          (string) current network name as defined in BIP70 (main, test, regtest)
}

Examples:
)" + HelpExampleCli("getmininginfo", "")
   + HelpExampleRpc("getmininginfo", ""));


    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("blocks",           (int)chainActive.Height());
    obj.pushKV("currentblocksize", (uint64_t)nLastBlockSize);
    obj.pushKV("currentblocktx",   (uint64_t)nLastBlockTx);
    obj.pushKV("difficulty",       (double)GetNetworkDifficulty());
    obj.pushKV("errors",           GetWarnings("statusbar"));
    obj.pushKV("genproclimit",     (int)GetArg("-genproclimit", -1));
    obj.pushKV("localsolps"  ,     getlocalsolps(params, false));
    obj.pushKV("networksolps",     getnetworksolps(params, false));
    obj.pushKV("networkhashps", getnetworksolps(params, false));
    obj.pushKV("testnet",          Params().TestnetToBeDeprecatedFieldRPC());
    obj.pushKV("chain",            Params().NetworkIDString());
#ifdef ENABLE_MINING
    obj.pushKV("generate",         getgenerate(params, false));
#endif
    return obj;
}


// NOTE: Unlike wallet RPC (which use BTC values), mining RPCs follow GBT (BIP 22) in using patoshi amounts
UniValue prioritisetransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
R"(prioritisetransaction <txid> <priority delta> <fee delta>

Accepts the transaction into mined blocks at a higher (or lower) priority

Arguments:
1. "txid"         (string, required) The transaction id.
2. priority delta (numeric, required) The priority to add or subtract.
                  The transaction selection algorithm considers the tx as it would have a higher priority.
                  (priority of a transaction is calculated: coinage * value_in_patoshis / txsize)
3. fee delta      (numeric, required) The fee value (in patoshis) to add (or subtract, if negative).
                  The fee is not actually paid, only the algorithm for selecting transactions into a block
                  considers the transaction as it would have paid a higher (or lower) fee.

Result:
  true            (boolean) Returns true

Examples:
)" + HelpExampleCli("prioritisetransaction", "\"txid\" 0.0 10000")
   + HelpExampleRpc("prioritisetransaction", "\"txid\", 0.0, 10000"));

    LOCK(cs_main);

    uint256 hash = ParseHashStr(params[0].get_str(), "txid");
    CAmount nAmount = params[2].get_int64();

    mempool.PrioritiseTransaction(hash, params[0].get_str(), params[1].get_real(), nAmount);
    return true;
}


// NOTE: Assumes a conclusive result; if result is inconclusive, it must be handled by caller
static UniValue BIP22ValidationResult(const CValidationState& state)
{
    if (state.IsValid())
        return NullUniValue;

    std::string strRejectReason = state.GetRejectReason();
    if (state.IsError())
        throw JSONRPCError(RPC_VERIFY_ERROR, strRejectReason);
    if (state.IsInvalid())
    {
        if (strRejectReason.empty())
            return "rejected";
        return strRejectReason;
    }
    // Should be impossible
    return "valid?";
}

UniValue getblocktemplate(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(getblocktemplate ( "jsonrequestobject" )

If the request parameters include a 'mode' key, that is used to explicitly select between the default 'template' request or a 'proposal'.
It returns data needed to construct a block to work on.
See https://en.bitcoin.it/wiki/BIP_0022 for full specification.

Arguments:
1. "jsonrequestobject"       (string, optional) A json object in the following spec
     {
       "mode": "template"    (string, optional) This must be set to "template" or omitted
       "capabilities":[      (array, optional) A list of strings
           "support"         (string) client side supported feature, 'longpoll', 'coinbasetxn', 'coinbasevalue', 'proposal', 'serverlist', 'workid'
           ,...
         ]
     }

Result:
{
  "version" : n,                   (numeric) The block version
  "previousblockhash" : "xxxx",    (string) The hash of current highest block
  "finalsaplingroothash" : "xxxx", (string) The hash of the final sapling root
  "transactions" : [               (array) contents of non-coinbase transactions that should be included in the next block
      {
         "data" : "xxxx",          (string) transaction data encoded in hexadecimal (byte-for-byte)
         "hash" : "xxxx",          (string) hash/id encoded in little-endian hexadecimal
         "depends" : [             (array) array of numbers
             n                     (numeric) transactions before this one (by 1-based index in 'transactions' list) that must be present in the final block if this one is
             ,...
         ],
         "fee": n,                 (numeric) difference in value between transaction inputs and outputs (in Patoshis); for coinbase transactions, this is a negative Number of the total collected block fees (ie, not including the block subsidy); if key is not present, fee is unknown and clients MUST NOT assume there isn't one
         "sigops" : n,             (numeric) total number of SigOps, as counted for purposes of block limits; if key is not present, sigop count is unknown and clients MUST NOT assume there aren't any
         "required" : true|false   (boolean) if provided and true, this transaction must be in the final block
      }
      ,...
  ],
  "coinbasetxn" : { ... },         (json object) information for coinbase transaction
  "target" : "xxxx",               (string) The hash target
  "mintime" : xxx,                 (numeric) The minimum timestamp appropriate for next block time in seconds since epoch (Jan 1 1970 GMT)
  "mutable" : [                    (array of string) list of ways the block template may be changed
     "value"                       (string) A way the block template may be changed, e.g. 'time', 'transactions', 'prevblock'
     ,...
  ],
  "noncerange" : "00000000ffffffff", (string) A range of valid nonces
  "sigoplimit" : n,                  (numeric) limit of sigops in blocks
  "sizelimit" : n,                   (numeric) limit of block size
  "curtime" : ttt,                   (numeric) current timestamp in seconds since epoch (Jan 1 1970 GMT)
  "bits" : "xxx",                    (string) compressed target of next block
  "height" : n                       (numeric) The height of the next block
  "masternode" : {                   (json object) required masternode payee that must be included in the next block
      "payee" : "xxxx",              (string) payee address
      "script" : "xxxx",             (string) payee scriptPubKey
      "amount": n                    (numeric) required amount to pay
  },
  "governance" : {                   (json object) required governance payee that must be included in the next block, can be empty
      "payee" : "xxxx",              (string) payee address
      "script" :+ "xxxx",            (string) payee scriptPubKey
      "amount": n                    (numeric) required amount to pay
  }
}

Examples:
)" + HelpExampleCli("getblocktemplate", "")
   + HelpExampleRpc("getblocktemplate", ""));

    LOCK(cs_main);

    // Wallet or miner address is required because we support coinbasetxn
    if (GetArg("-mineraddress", "").empty()) {
#ifdef ENABLE_WALLET
        if (!pwalletMain)
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Wallet disabled and -mineraddress not set");
#else
        throw JSONRPCError(RPC_METHOD_NOT_FOUND, "pasteld compiled without wallet and -mineraddress not set");
#endif
    }

    const auto& chainparams = Params();
    std::string strMode = "template";
    UniValue lpval = NullUniValue;
    // TODO: Re-enable coinbasevalue once a specification has been written
    bool coinbasetxn = true;
    if (params.size() > 0)
    {
        const UniValue& oparam = params[0].get_obj();
        const UniValue& modeval = find_value(oparam, "mode");
        if (modeval.isStr())
            strMode = modeval.get_str();
        else if (modeval.isNull())
        {
            /* Do nothing */
        }
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        lpval = find_value(oparam, "longpollid");

        if (strMode == "proposal")
        {
            const UniValue& dataval = find_value(oparam, "data");
            if (!dataval.isStr())
                throw JSONRPCError(RPC_TYPE_ERROR, "Missing data String key for proposal");

            CBlock block;
            if (!DecodeHexBlk(block, dataval.get_str()))
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

            uint256 hash = block.GetHash();
            BlockMap::iterator mi = mapBlockIndex.find(hash);
            if (mi != mapBlockIndex.end()) {
                CBlockIndex *pindex = mi->second;
                if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                    return "duplicate";
                if (pindex->nStatus & BLOCK_FAILED_MASK)
                    return "duplicate-invalid";
                return "duplicate-inconclusive";
            }

            CBlockIndex* const pindexPrev = chainActive.Tip();
            // TestBlockValidity only supports blocks built on the current Tip
            if (block.hashPrevBlock != pindexPrev->GetBlockHash())
                return "inconclusive-not-best-prevblk";

            CValidationState state;
            TestBlockValidity(state, chainparams, block, pindexPrev, false, true);
            return BIP22ValidationResult(state);
        }
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    if (!chainparams.IsRegTest() && vNodes.empty())
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Pastel is not connected!");

    if (fnIsInitialBlockDownload(chainparams.GetConsensus()))
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Pastel is downloading blocks...");

//PASTEL
    CScript payee;
    if (!chainparams.IsRegTest() && !masterNodeCtrl.masternodeSync.IsWinnersListSynced()
        && !masterNodeCtrl.masternodePayments.GetBlockPayee(chainActive.Height() + 1, payee))
            throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "Pastel Core is downloading masternode winners...");
//PASTEL

    static unsigned int nTransactionsUpdatedLast;

    if (!lpval.isNull())
    {
        // Wait to respond until either the best block changes, OR a minute has passed and there are more transactions
        uint256 hashWatchedChain;
        unsigned int nTransactionsUpdatedLastLP;

        if (lpval.isStr())
        {
            // Format: <hashBestChain><nTransactionsUpdatedLast>
            std::string lpstr = lpval.get_str();

            hashWatchedChain.SetHex(lpstr.substr(0, 64));
            nTransactionsUpdatedLastLP = strtoul(lpstr.substr(64).c_str(), nullptr, 10);
        }
        else
        {
            // NOTE: Spec does not specify behaviour for non-string longpollid, but this makes testing easier
            hashWatchedChain = chainActive.Tip()->GetBlockHash();
            nTransactionsUpdatedLastLP = nTransactionsUpdatedLast;
        }

        // Release the wallet and main lock while waiting
        LEAVE_CRITICAL_SECTION(cs_main);
        {
            auto checktxtime = chrono::system_clock::now() + 1min;

            unique_lock<mutex> lock(csBestBlock);
            while (chainActive.Tip()->GetBlockHash() == hashWatchedChain && IsRPCRunning())
            {
                if (cvBlockChange.wait_until(lock, checktxtime) == cv_status::timeout)
                {
                    // Timeout: Check transactions for update
                    if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLastLP)
                        break;
                    checktxtime += 10s;
                }
            }
        }
        ENTER_CRITICAL_SECTION(cs_main);

        if (!IsRPCRunning())
            throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "Shutting down");
        // TODO: Maybe recheck connections/IBD and (if something wrong) send an expires-immediately template to stop miners?
    }

    // Update block
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static CBlockTemplate* pblocktemplate;
    if (pindexPrev != chainActive.Tip() ||
        (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 5))
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = nullptr;

        // Store the pindexBest used before CreateNewBlockWithKey, to avoid races
        nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
        CBlockIndex* pindexPrevNew = chainActive.Tip();
        nStart = GetTime();

        // Create new block
        if(pblocktemplate)
        {
            delete pblocktemplate;
            pblocktemplate = nullptr;
        }
#ifdef ENABLE_WALLET
        CReserveKey reservekey(pwalletMain);
        pblocktemplate = CreateNewBlockWithKey(reservekey, chainparams);
#else
        pblocktemplate = CreateNewBlockWithKey(chainparams);
#endif
        if (!pblocktemplate)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Need to update only after we know CreateNewBlockWithKey succeeded
        pindexPrev = pindexPrevNew;
    }
    CBlock* pblock = &pblocktemplate->block; // pointer for convenience

    // Update nTime
    UpdateTime(pblock, Params().GetConsensus(), pindexPrev);
    pblock->nNonce = uint256();

    UniValue aCaps(UniValue::VARR); aCaps.push_back("proposal");

    UniValue txCoinbase = NullUniValue;
    UniValue transactions(UniValue::VARR);
    map<uint256, int64_t> setTxIndex;
    int i = 0;
    for (const auto& tx : pblock->vtx)
    {
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase() && !coinbasetxn) //-V560
            continue;

        UniValue entry(UniValue::VOBJ);

        entry.pushKV("data", EncodeHexTx(tx));

        entry.pushKV("hash", txHash.GetHex());

        UniValue deps(UniValue::VARR);
        for (const auto &in : tx.vin)
        {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.pushKV("depends", deps);

        int index_in_template = i - 1;
        entry.pushKV("fee", pblocktemplate->vTxFees[index_in_template]);
        entry.pushKV("sigops", pblocktemplate->vTxSigOps[index_in_template]);

        if (tx.IsCoinBase()) {
            entry.pushKV("required", true);
            txCoinbase = entry;
        } else {
            transactions.push_back(entry);
        }
    }

    UniValue aux(UniValue::VOBJ);
    aux.pushKV("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end()));

    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);

    static UniValue aMutable(UniValue::VARR);
    if (aMutable.empty())
    {
        aMutable.push_back("time");
        aMutable.push_back("transactions");
        aMutable.push_back("prevblock");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("capabilities", aCaps);
    result.pushKV("version", pblock->nVersion);
    result.pushKV("previousblockhash", pblock->hashPrevBlock.GetHex());
    result.pushKV("finalsaplingroothash", pblock->hashFinalSaplingRoot.GetHex());
    result.pushKV("transactions", transactions);
    if (coinbasetxn) { //-V547
        assert(txCoinbase.isObject());
        result.pushKV("coinbasetxn", txCoinbase);
    } else {
        result.pushKV("coinbaseaux", aux);
        result.pushKV("coinbasevalue", (int64_t)pblock->vtx[0].vout[0].nValue);
    }
    result.pushKV("longpollid", chainActive.Tip()->GetBlockHash().GetHex() + i64tostr(nTransactionsUpdatedLast));
    result.pushKV("target", hashTarget.GetHex());
    result.pushKV("mintime", (int64_t)pindexPrev->GetMedianTimePast()+1);
    result.pushKV("mutable", aMutable);
    result.pushKV("noncerange", "00000000ffffffff");
    result.pushKV("sigoplimit", (int64_t)MAX_BLOCK_SIGOPS);
    result.pushKV("sizelimit", (int64_t)MAX_BLOCK_SIZE);
    result.pushKV("curtime", pblock->GetBlockTime());
    result.pushKV("bits", strprintf("%08x", pblock->nBits));
    result.pushKV("height", (int64_t)(pindexPrev->nHeight) + 1);

    //PASTEL-->
    //MN payment
    KeyIO keyIO(Params());
    UniValue masternodeObj(UniValue::VOBJ);
    if(pblock->txoutMasternode != CTxOut()) {
        CTxDestination dest;
        ExtractDestination(pblock->txoutMasternode.scriptPubKey, dest);
        std::string address = keyIO.EncodeDestination(dest);
        masternodeObj.pushKV("payee", address);
        masternodeObj.pushKV("script", HexStr(pblock->txoutMasternode.scriptPubKey.begin(), pblock->txoutMasternode.scriptPubKey.end()));
        masternodeObj.pushKV("amount", pblock->txoutMasternode.nValue);
    }
    result.pushKV("masternodeinfo", masternodeObj);
    //Governance payment
    UniValue governanceObj(UniValue::VOBJ);
    if(pblock->txoutGovernance != CTxOut()) {
        CTxDestination dest;
        ExtractDestination(pblock->txoutGovernance.scriptPubKey, dest);
        std::string address = keyIO.EncodeDestination(dest);
        governanceObj.pushKV("payee", address);
        governanceObj.pushKV("script", HexStr(pblock->txoutGovernance.scriptPubKey.begin(), pblock->txoutGovernance.scriptPubKey.end()));
        governanceObj.pushKV("amount", pblock->txoutGovernance.nValue);
    }
    result.pushKV("governanceinfo", governanceObj);
    //<--PASTEL

    return result;
}

class submitblock_StateCatcher : public CValidationInterface
{
public:
    uint256 hash;
    bool found;
    CValidationState state;

    submitblock_StateCatcher(const uint256 &hashIn) : hash(hashIn), found(false), state() {};

protected:
    void BlockChecked(const CBlock& block, const CValidationState& stateIn) override
    {
        if (block.GetHash() != hash)
            return;
        found = true;
        state = stateIn;
    };
};

UniValue submitblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
R"(submitblock "hexdata" ( "jsonparametersobject" )

Attempts to submit new block to network.
The 'jsonparametersobject' parameter is currently ignored.
See https://en.bitcoin.it/wiki/BIP_0022 for full specification.

Arguments
1. "hexdata"                (string, required) the hex-encoded block data to submit
2. "jsonparametersobject"   (string, optional) object of optional parameters
    {
      "workid" : "id"       (string, optional) if the server provided a workid, it MUST be included with submissions
    }

Result:
"duplicate" - node already has valid copy of block
"duplicate-invalid" - node already has block, but it is invalid
"duplicate-inconclusive" - node already has block but has not validated it
"inconclusive" - node has not validated the block, it may not be on the node's current best chain
"rejected" - block was rejected as invalid
For more information on submitblock parameters and results, see: https://github.com/bitcoin/bips/blob/master/bip-0022.mediawiki#block-submission

Examples:
)"
+ HelpExampleCli("submitblock", "\"mydata\"")
+ HelpExampleRpc("submitblock", "\"mydata\"")
);

    CBlock block;
    if (!DecodeHexBlk(block, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");

    uint256 hash = block.GetHash();
    bool fBlockPresent = false;
    {
        LOCK(cs_main);
        const auto mi = mapBlockIndex.find(hash);
        if (mi != mapBlockIndex.cend())
        {
            CBlockIndex *pindex = mi->second;
            if (pindex->IsValid(BLOCK_VALID_SCRIPTS))
                return "duplicate";
            if (pindex->nStatus & BLOCK_FAILED_MASK)
                return "duplicate-invalid";
            // Otherwise, we might only have the header - process the block before returning
            fBlockPresent = true;
        }
    }

    CValidationState state;
    submitblock_StateCatcher sc(block.GetHash());
    RegisterValidationInterface(&sc);
    bool fAccepted = ProcessNewBlock(state, Params(), nullptr, &block, true, nullptr);
    UnregisterValidationInterface(&sc);
    if (fBlockPresent)
    {
        if (fAccepted && !sc.found)
            return "duplicate-inconclusive";
        return "duplicate";
    }
    if (fAccepted)
    {
        if (!sc.found)
            return "inconclusive";
        state = sc.state;
    }
    return BIP22ValidationResult(state);
}

UniValue estimatefee(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(estimatefee nblocks

Estimates the approximate fee per kilobyte
needed for a transaction to begin confirmation
within nblocks blocks.

Arguments:
1. nblocks     (numeric)

Result:
n :    (numeric) estimated fee-per-kilobyte

-1.0 is returned if not enough transactions and
blocks have been observed to make an estimate.

Example:
)"
+ HelpExampleCli("estimatefee", "6")
);

    RPCTypeCheck(params, {UniValue::VNUM});

    int nBlocks = params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    CFeeRate feeRate = mempool.estimateFee(nBlocks);
    if (feeRate == CFeeRate(0))
        return -1.0;

    return ValueFromAmount(feeRate.GetFeePerK());
}

UniValue estimatepriority(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(estimatepriority nblocks

Estimates the approximate priority
a zero-fee transaction needs to begin confirmation
within nblocks blocks.

Arguments:
1. nblocks     (numeric)

Result:
n :    (numeric) estimated priority

-1.0 is returned if not enough transactions and
blocks have been observed to make an estimate.

Example:
)" + HelpExampleCli("estimatepriority", "6")
);

    RPCTypeCheck(params, {UniValue::VNUM});

    int nBlocks = params[0].get_int();
    if (nBlocks < 1)
        nBlocks = 1;

    return mempool.estimatePriority(nBlocks);
}

UniValue getblocksubsidy(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(getblocksubsidy height

Returns block subsidy reward, taking into account the mining slow start, of block at index provided.

Arguments:
1. height         (numeric, optional) The block height.  If not provided, defaults to the current height of the chain.

Result:
{
  "miner" : x.xxx           (numeric) The mining reward amount in )" + CURRENCY_UNIT + R"(.
  "masternode" : x.xxx      (numeric) The masternode reward amount in )" + CURRENCY_UNIT + R"(.
  "governance" : x.xxx      (numeric) The governance reward amount in )" + CURRENCY_UNIT + R"(.
}

Examples:
)"
+ HelpExampleCli("getblocksubsidy", "1000")
+ HelpExampleRpc("getblocksubsidy", "1000")
);

    LOCK(cs_main);
    int nHeight = (params.size()==1) ? params[0].get_int() : chainActive.Height();
    if (nHeight < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CAmount nReward = GetBlockSubsidy(nHeight, Params().GetConsensus());

    CAmount nGovernancePayment = 0;
#ifdef GOVERNANCE_TICKETS
    if (!masterNodeCtrl.masternodeGovernance.mapTickets.empty())
        nGovernancePayment = masterNodeCtrl.masternodeGovernance.GetCurrentPaymentAmount(nHeight, nReward);
#endif // GOVERNANCE_TICKETS

    CAmount nMasternodePayment = 0;
    if (masterNodeCtrl.masternodePayments.mapMasternodeBlockPayees.count(nHeight))
        nMasternodePayment = masterNodeCtrl.masternodePayments.GetMasternodePayment(0, nReward);//same for any height currently

    UniValue result(UniValue::VOBJ);
    result.pushKV("miner", ValueFromAmount(nReward - nGovernancePayment - nMasternodePayment));
    result.pushKV("masternode", ValueFromAmount(nMasternodePayment));
#ifdef GOVERNANCE_TICKETS
    result.pushKV("governance", ValueFromAmount(nGovernancePayment));
#endif // GOVERNANCE_TICKETS
    return result;
}

UniValue getnextblocksubsidy(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 0)
        throw runtime_error(
R"(getnextblocksubsidy

Returns block subsidy rewards of the next block.

Result:
{
  "miner" : x.xxx           (numeric) The mining reward amount in )" + CURRENCY_UNIT + R"(.
  "masternode" : x.xxx      (numeric) The masternode reward amount in )" + CURRENCY_UNIT + R"(.
  "governance" : x.xxx      (numeric) The governance reward amount in )" + CURRENCY_UNIT + R"(.
}

Examples:
)"
+ HelpExampleCli("getblocksubsidy", "")
+ HelpExampleRpc("getblocksubsidy", "")
);
    
    LOCK(cs_main);
    int nHeight = chainActive.Height()+1;
    
    CAmount nReward = GetBlockSubsidy(nHeight, Params().GetConsensus());
    
    CAmount nGovernancePayment = 0;
#ifdef GOVERNANCE_TICKETS
    if (!masterNodeCtrl.masternodeGovernance.mapTickets.empty())
        nGovernancePayment = masterNodeCtrl.masternodeGovernance.GetCurrentPaymentAmount(nHeight, nReward);
#endif // GOVERNANCE_TICKETS

    CAmount nMasternodePayment = 0;
    if (masterNodeCtrl.masternodePayments.mapMasternodeBlockPayees.count(nHeight))
        nMasternodePayment = masterNodeCtrl.masternodePayments.GetMasternodePayment(0, nReward);//same for any height currently
    
    UniValue result(UniValue::VOBJ);
    result.pushKV("miner", ValueFromAmount(nReward - nGovernancePayment - nMasternodePayment));
    result.pushKV("masternode", ValueFromAmount(nMasternodePayment));
#ifdef GOVERNANCE_TICKETS
    result.pushKV("governance", ValueFromAmount(nGovernancePayment));
#endif // GOVERNANCE_TICKETS
    return result;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "mining",             "getlocalsolps",          &getlocalsolps,          true  },
    { "mining",             "getnetworksolps",        &getnetworksolps,        true  },
    { "mining",             "getnetworkhashps",       &getnetworkhashps,       true  },
    { "mining",             "getmininginfo",          &getmininginfo,          true  },
    { "mining",             "prioritisetransaction",  &prioritisetransaction,  true  },
    { "mining",             "getblocktemplate",       &getblocktemplate,       true  },
    { "mining",             "submitblock",            &submitblock,            true  },
    { "mining",             "getblocksubsidy",        &getblocksubsidy,        true  },
    { "mining",             "getnextblocksubsidy",    &getnextblocksubsidy,    true  },

#ifdef ENABLE_MINING
    { "generating",         "getgenerate",            &getgenerate,            true  },
    { "generating",         "setgenerate",            &setgenerate,            true  },
    { "generating",         "generate",               &generate,               true  },
#endif

    { "util",               "estimatefee",            &estimatefee,            true  },
    { "util",               "estimatepriority",       &estimatepriority,       true  },
};

void RegisterMiningRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
