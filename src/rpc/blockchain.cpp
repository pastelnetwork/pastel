// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stdint.h>
#include <regex>

#include <univalue.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <checkpoints.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <main.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <streams.h>
#include <sync.h>
#include <util.h>

using namespace std;

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);

double GetDifficultyINTERNAL(const CBlockIndex* blockindex, bool networkDifficulty)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (chainActive.Tip() == NULL)
            return 1.0;
        else
            blockindex = chainActive.Tip();
    }

    uint32_t bits;
    if (networkDifficulty) {
        bits = GetNextWorkRequired(blockindex, nullptr, Params().GetConsensus());
    } else {
        bits = blockindex->nBits;
    }

    uint32_t powLimit =
        UintToArith256(Params().GetConsensus().powLimit).GetCompact();
    int nShift = (bits >> 24) & 0xff;
    int nShiftAmount = (powLimit >> 24) & 0xff;

    double dDiff =
        (double)(powLimit & 0x00ffffff) / 
        (double)(bits & 0x00ffffff);

    while (nShift < nShiftAmount)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > nShiftAmount)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetDifficulty(const CBlockIndex* blockindex)
{
    return GetDifficultyINTERNAL(blockindex, false);
}

double GetNetworkDifficulty(const CBlockIndex* blockindex)
{
    return GetDifficultyINTERNAL(blockindex, true);
}

static UniValue ValuePoolDesc(
    const string &name,
    const optional<CAmount> chainValue,
    const optional<CAmount> valueDelta)
{
    UniValue rv(UniValue::VOBJ);
    rv.pushKV("id", name);
    rv.pushKV("monitored", (bool)chainValue);
    if (chainValue) {
        rv.pushKV("chainValue", ValueFromAmount(*chainValue));
        rv.pushKV("chainValuePat", *chainValue);
    }
    if (valueDelta) {
        rv.pushKV("valueDelta", ValueFromAmount(*valueDelta));
        rv.pushKV("valueDeltaPat", *valueDelta);
    }
    return rv;
}

UniValue blockheaderToJSON(const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", blockindex->GetBlockHash().GetHex());
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.pushKV("confirmations", confirmations);
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", blockindex->nVersion);
    result.pushKV("merkleroot", blockindex->hashMerkleRoot.GetHex());
    result.pushKV("finalsaplingroot", blockindex->hashFinalSaplingRoot.GetHex());
    result.pushKV("time", (int64_t)blockindex->nTime);
    result.pushKV("nonce", blockindex->nNonce.GetHex());
    result.pushKV("solution", HexStr(blockindex->nSolution));
    result.pushKV("bits", strprintf("%08x", blockindex->nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());
    return result;
}

UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", block.GetHash().GetHex());
    int confirmations = -1;
    // Only report confirmations if the block is on the main chain
    if (chainActive.Contains(blockindex))
        confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.pushKV("confirmations", confirmations);
    result.pushKV("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION));
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", block.nVersion);
    result.pushKV("merkleroot", block.hashMerkleRoot.GetHex());
    result.pushKV("finalsaplingroot", block.hashFinalSaplingRoot.GetHex());
    UniValue txs(UniValue::VARR);
    for (const auto &tx : block.vtx)
    {
        if(txDetails)
        {
            UniValue objTx(UniValue::VOBJ);
            TxToJSON(tx, uint256(), objTx);
            txs.push_back(objTx);
        }
        else
            txs.push_back(tx.GetHash().GetHex());
    }
    result.pushKV("tx", move(txs));
    result.pushKV("time", block.GetBlockTime());
    result.pushKV("nonce", block.nNonce.GetHex());
    result.pushKV("solution", HexStr(block.nSolution));
    result.pushKV("bits", strprintf("%08x", block.nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());
    result.pushKV("anchor", blockindex->hashFinalSproutRoot.GetHex());

    UniValue valuePools(UniValue::VARR);
    valuePools.push_back(ValuePoolDesc("sprout", blockindex->nChainSproutValue, blockindex->nSproutValue));
    valuePools.push_back(ValuePoolDesc("sapling", blockindex->nChainSaplingValue, blockindex->nSaplingValue));
    result.pushKV("valuePools", move(valuePools));

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());
    return result;
}

UniValue getblockcount(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getblockcount

Returns the number of blocks in the best valid block chain.

Result:
n    (numeric) The current block count

Examples:
)"
    + HelpExampleCli("getblockcount", "")
    + HelpExampleRpc("getblockcount", "")
);

    LOCK(cs_main);
    return chainActive.Height();
}

UniValue getbestblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getbestblockhash

Returns the hash of the best (tip) block in the longest block chain.

Result
"hex"      (string) the block hash hex encoded

Examples
)"
+ HelpExampleCli("getbestblockhash", "")
+ HelpExampleRpc("getbestblockhash", "")
);

    LOCK(cs_main);
    return chainActive.Tip()->GetBlockHash().GetHex();
}

UniValue getdifficulty(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getdifficulty

Returns the proof-of-work difficulty as a multiple of the minimum difficulty.

Result:
n.nnn       (numeric) the proof-of-work difficulty as a multiple of the minimum difficulty.

Examples:
)"
+ HelpExampleCli("getdifficulty", "")
+ HelpExampleRpc("getdifficulty", "")
);

    LOCK(cs_main);
    return GetNetworkDifficulty();
}

UniValue mempoolToJSON(bool fVerbose = false)
{
    if (fVerbose)
    {
        LOCK(mempool.cs);
        UniValue o(UniValue::VOBJ);
        o.reserve(mempool.mapTx.size());

        for (const auto& e : mempool.mapTx)
        {
            const uint256& hash = e.GetTx().GetHash();
            UniValue info(UniValue::VOBJ);
            info.reserve(7);
            info.pushKV("size", static_cast<uint64_t>(e.GetTxSize()));
            info.pushKV("fee", ValueFromAmount(e.GetFee()));
            info.pushKV("time", e.GetTime());
            info.pushKV("height", (int)e.GetHeight());
            info.pushKV("startingpriority", e.GetPriority(e.GetHeight()));
            info.pushKV("currentpriority", e.GetPriority(chainActive.Height()));
            const CTransaction& tx = e.GetTx();
            set<string> setDepends;
            for (const auto &txin : tx.vin)
            {
                if (mempool.exists_nolock(txin.prevout.hash))
                    setDepends.insert(txin.prevout.hash.ToString());
            }

            UniValue depends(UniValue::VARR);
            depends.reserve(setDepends.size());
            for (const auto& dep : setDepends)
                depends.push_back(dep);

            info.pushKV("depends", move(depends));
            o.pushKV(hash.ToString(), move(info));
        }
        return o;
    }
    v_uint256 vtxid;
    mempool.queryHashes(vtxid);

    UniValue a(UniValue::VARR);
    a.reserve(vtxid.size());
    for (const auto& hash : vtxid)
        a.push_back(hash.ToString());

    return a;
}

UniValue getrawmempool(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(getrawmempool ( verbose )

Returns all transaction ids in memory pool as a json array of string transaction ids.

Arguments:
1. verbose           (boolean, optional, default=false) true for a json object, false for array of transaction ids

Result: (for verbose = false):
[                     (json array of string)
  "transactionid"     (string) The transaction id
  ,...
]

Result: (for verbose = true):
{                           (json object)
  "transactionid" : {       (json object)
    "size" : n,             (numeric) transaction size in bytes
    "fee" : n,              (numeric) transaction fee in )" + CURRENCY_UNIT + R"(
    "time" : n,             (numeric) local time transaction entered pool in seconds since 1 Jan 1970 GMT
    "height" : n,           (numeric) block height when transaction entered pool
    "startingpriority" : n, (numeric) priority when transaction entered pool
    "currentpriority" : n,  (numeric) transaction priority now\n"
    "depends" : [           (array) unconfirmed transactions used as inputs for this transaction
        "transactionid",    (string) parent transaction id
       ... ]
  }, ...
}

Examples:
)"
+ HelpExampleCli("getrawmempool", "true")
+ HelpExampleRpc("getrawmempool", "true")
        );

    LOCK(cs_main);

    bool fVerbose = false;
    if (params.size() > 0)
        fVerbose = params[0].get_bool();

    return mempoolToJSON(fVerbose);
}

UniValue getblockhash(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getblockhash index

Returns hash of block in best-block-chain at index provided.

Arguments:
1. index         (numeric, required) The block index

Result:
  "hash"         (string) The block hash

Examples:
)"
+ HelpExampleCli("getblockhash", "1000")
+ HelpExampleRpc("getblockhash", "1000")
);

    LOCK(cs_main);

    int nHeight = params[0].get_int();
    if (nHeight < 0 || nHeight > chainActive.Height())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");

    CBlockIndex* pblockindex = chainActive[nHeight];
    return pblockindex->GetBlockHash().GetHex();
}

UniValue getblockheader(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
R"(getblockheader "hash" ( verbose )
If verbose is false, returns a string that is serialized, hex-encoded data for blockheader 'hash'.
If verbose is true, returns an Object with information about blockheader <hash>.

Arguments:
1. "hash"          (string, required) The block hash
2. verbose         (boolean, optional, default=true) true for a json object, false for the hex encoded data

Result (for verbose = true):
{
  "hash" : "hash",       (string) the block hash (same as provided)
  "confirmations" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain
  "height" : n,          (numeric) The block height or index
  "version" : n,         (numeric) The block version
  "merkleroot" : "xxxx", (string) The merkle root
  "finalsaplingroot" : "xxxx", (string) The root of the Sapling commitment tree after applying this block
  "time" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)
  "nonce" : n,           (numeric) The nonce
  "bits" : "1d00ffff",   (string) The bits
  "difficulty" : x.xxx,  (numeric) The difficulty
  "previousblockhash" : "hash",  (string) The hash of the previous block
  "nextblockhash" : "hash"       (string) The hash of the next block
}

Result (for verbose=false):
"data"             (string) A string that is serialized, hex-encoded data for block 'hash'.

Examples:
)"
+ HelpExampleCli("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
+ HelpExampleRpc("getblockheader", "\"00000000c937983704a73af28acdec37b049d214adbda81d7e2a3dd146f6ed09\"")
);

    LOCK(cs_main);

    string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    bool fVerbose = true;
    if (params.size() > 1)
        fVerbose = params[1].get_bool();

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (!fVerbose)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << pblockindex->GetBlockHeader();
        string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockheaderToJSON(pblockindex);
}

UniValue getblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
R"(getblock "hash|height" ( verbosity )
If verbosity is 0, returns a string that is serialized, hex-encoded data for the block.
If verbosity is 1, returns an Object with information about the block.
If verbosity is 2, returns an Object with information about the block and information about each transaction.

Arguments:
1. "hash|height"          (string, required) The block hash or height
2. verbosity              (numeric, optional, default=1) 0 for hex encoded data, 1 for a json object, and 2 for json object with transaction data

Result (for verbosity = 0):
"data"                    (string) A string that is serialized, hex-encoded data for the block.

Result (for verbosity = 1):
{
  "hash" : "hash",       (string) the block hash (same as provided hash)
  "confirmations" : n,   (numeric) The number of confirmations, or -1 if the block is not on the main chain
  "size" : n,            (numeric) The block size
  "height" : n,          (numeric) The block height or index (same as provided height)
  "version" : n,         (numeric) The block version
  "merkleroot" : "xxxx", (string) The merkle root
  "finalsaplingroot" : "xxxx", (string) The root of the Sapling commitment tree after applying this block
  "tx" : [               (array of string) The transaction ids
     "transactionid"     (string) The transaction id
     ,...
  ],
  "time" : ttt,          (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)
  "nonce" : n,           (numeric) The nonce
  "bits" : "1d00ffff",   (string) The bits
  "difficulty" : x.xxx,  (numeric) The difficulty
  "previousblockhash" : "hash",  (string) The hash of the previous block
  "nextblockhash" : "hash"       (string) The hash of the next block
}

Result (for verbosity = 2):
{
  ...,                     Same output as verbosity = 1.
  "tx" : [               (array of Objects) The transactions in the format of the getrawtransaction RPC. Different from verbosity = 1 "tx" result.
         ,...
  ],
  ,...                     Same output as verbosity = 1.
}

Examples:
)"
+ HelpExampleCli("getblock", "\"00000000febc373a1da2bd9f887b105ad79ddc26ac26c2b28652d64e5207c5b5\"")
+ HelpExampleRpc("getblock", "\"00000000febc373a1da2bd9f887b105ad79ddc26ac26c2b28652d64e5207c5b5\"")
+ HelpExampleCli("getblock", "12800")
+ HelpExampleRpc("getblock", "12800")
);

    LOCK(cs_main);

    string strHash = params[0].get_str();

    // If height is supplied, find the hash
    if (strHash.size() < (2 * sizeof(uint256))) {
        // stoi allows characters, whereas we want to be strict
        regex r("[[:digit:]]+");
        if (!regex_match(strHash, r)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block height parameter");
        }

        int nHeight = -1;
        try {
            nHeight = stoi(strHash);
        }
        catch (const exception &) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid block height parameter");
        }

        if (nHeight < 0 || nHeight > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Block height out of range");
        }
        strHash = chainActive[nHeight]->GetBlockHash().GetHex();
    }

    uint256 hash(uint256S(strHash));

    int verbosity = 1;
    if (params.size() > 1) {
        if(params[1].isNum()) {
            verbosity = params[1].get_int();
        } else {
            verbosity = params[1].get_bool() ? 1 : 0;
        }
    }

    if (verbosity < 0 || verbosity > 2) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Verbosity must be in range from 0 to 2");
    }

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if(!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    if (verbosity == 0)
    {
        CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
        ssBlock << block;
        string strHex = HexStr(ssBlock.begin(), ssBlock.end());
        return strHex;
    }

    return blockToJSON(block, pblockindex, verbosity >= 2);
}

UniValue gettxoutsetinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(gettxoutsetinfo

Returns statistics about the unspent transaction output set.
Note this call may take some time.

Result:
{
  "height":n,                (numeric) The current block height (index)
  "bestblock": "hex",        (string) the best block hash hex
  "transactions": n,         (numeric) The number of transactions
  "txouts": n,               (numeric) The number of output transactions
  "bytes_serialized": n,     (numeric) The serialized size
  "hash_serialized": "hash", (string) The serialized hash
  "total_amount": x.xxx      (numeric) The total amount
}

Examples:
)"
    + HelpExampleCli("gettxoutsetinfo", "")
    + HelpExampleRpc("gettxoutsetinfo", "")
);

    UniValue ret(UniValue::VOBJ);

    CCoinsStats stats;
    FlushStateToDisk();
    if (pcoinsTip->GetStats(stats)) {
        ret.pushKV("height", (int64_t)stats.nHeight);
        ret.pushKV("bestblock", stats.hashBlock.GetHex());
        ret.pushKV("transactions", (int64_t)stats.nTransactions);
        ret.pushKV("txouts", (int64_t)stats.nTransactionOutputs);
        ret.pushKV("bytes_serialized", (int64_t)stats.nSerializedSize);
        ret.pushKV("hash_serialized", stats.hashSerialized.GetHex());
        ret.pushKV("total_amount", ValueFromAmount(stats.nTotalAmount));
    }
    return ret;
}

UniValue gettxout(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 3)
        throw runtime_error(
R"(gettxout "txid" n ( includemempool )

Returns details about an unspent transaction output.

Arguments:
1. "txid"          (string, required) The transaction id
2. n               (numeric, required) vout value
3. includemempool  (boolean, optional) Whether to include the mempool

Result:
{
  "bestblock" : "hash",      (string) the block hash
  "confirmations" : n,       (numeric) The number of confirmations
  "value" : x.xxx,           (numeric) The transaction value in )" + CURRENCY_UNIT + R"(
  "valuePat" : xxxx,           (numeric) The transaction value in )" + MINOR_CURRENCY_UNIT + R"(
  "scriptPubKey" : {         (json object)
     "asm" : "code",         (string)
     "hex" : "hex",          (string)
     "reqSigs" : n,          (numeric) Number of required signatures
     "type" : "pubkeyhash",  (string) The type, eg pubkeyhash
     "addresses" : [         (array of string) array of Pastel addresses
        "zcashaddress"       (string) Pastel address
        ,...
     ]
  },
  "version" : n,             (numeric) The version
  "coinbase" : true|false    (boolean) Coinbase or not
}

Examples:
Get unspent transactions
)" + HelpExampleCli("listunspent", "") + R"(
View the details
)" + HelpExampleCli("gettxout", "\"txid\" 1") + R"(
As a json rpc call
)" + HelpExampleRpc("gettxout", "\"txid\", 1")
);

    LOCK(cs_main);

    UniValue ret(UniValue::VOBJ);

    string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    int n = params[1].get_int();
    bool fMempool = true;
    if (params.size() > 2)
        fMempool = params[2].get_bool();

    CCoins coins;
    if (fMempool) {
        LOCK(mempool.cs);
        CCoinsViewMemPool view(pcoinsTip, mempool);
        if (!view.GetCoins(hash, coins))
            return NullUniValue;
        mempool.pruneSpent(hash, coins); // TODO: this should be done by the CCoinsViewMemPool
    } else {
        if (!pcoinsTip->GetCoins(hash, coins))
            return NullUniValue;
    }
    if (n<0 || (unsigned int)n>=coins.vout.size() || coins.vout[n].IsNull())
        return NullUniValue;

    BlockMap::iterator it = mapBlockIndex.find(pcoinsTip->GetBestBlock());
    CBlockIndex *pindex = it->second;
    ret.pushKV("bestblock", pindex->GetBlockHash().GetHex());
    if ((unsigned int)coins.nHeight == MEMPOOL_HEIGHT)
        ret.pushKV("confirmations", 0);
    else
        ret.pushKV("confirmations", pindex->nHeight - coins.nHeight + 1);
    ret.pushKV("value", ValueFromAmount(coins.vout[n].nValue));
    ret.pushKV("valuePat", coins.vout[n].nValue);
    UniValue o(UniValue::VOBJ);
    ScriptPubKeyToJSON(coins.vout[n].scriptPubKey, o, true);
    ret.pushKV("scriptPubKey", o);
    ret.pushKV("version", coins.nVersion);
    ret.pushKV("coinbase", coins.fCoinBase);

    return ret;
}

UniValue verifychain(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
        throw runtime_error(
R"(verifychain ( checklevel numblocks )
Verifies blockchain database.

Arguments:
1. checklevel   (numeric, optional, 0-4, default=3) How thorough the block verification is.
2. numblocks    (numeric, optional, default=288, 0=all) The number of blocks to check.

Result:
 true|false       (boolean) Verified or not

Examples:
)"
    + HelpExampleCli("verifychain", "")
    + HelpExampleRpc("verifychain", "")
);

    LOCK(cs_main);

    int nCheckLevel = static_cast<int>(GetArg("-checklevel", 3));
    int nCheckDepth = static_cast<int>(GetArg("-checkblocks", 288));
    if (params.size() > 0)
        nCheckLevel = params[0].get_int();
    if (params.size() > 1)
        nCheckDepth = params[1].get_int();

    return CVerifyDB().VerifyDB(Params(), pcoinsTip, nCheckLevel, nCheckDepth);
}

/** Implementation of IsSuperMajority with better feedback */
static UniValue SoftForkMajorityDesc(int minVersion, CBlockIndex* pindex, int nRequired, const Consensus::Params& consensusParams)
{
    int nFound = 0;
    CBlockIndex* pstart = pindex;
    for (int i = 0; i < consensusParams.nMajorityWindow && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
            ++nFound;
        pstart = pstart->pprev;
    }

    UniValue rv(UniValue::VOBJ);
    rv.pushKV("status", nFound >= nRequired);
    rv.pushKV("found", nFound);
    rv.pushKV("required", nRequired);
    rv.pushKV("window", consensusParams.nMajorityWindow);
    return rv;
}

static UniValue SoftForkDesc(const string &name, int version, CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    UniValue rv(UniValue::VOBJ);
    rv.pushKV("id", name);
    rv.pushKV("version", version);
    rv.pushKV("enforce", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityEnforceBlockUpgrade, consensusParams));
    rv.pushKV("reject", SoftForkMajorityDesc(version, pindex, consensusParams.nMajorityRejectBlockOutdated, consensusParams));
    return rv;
}

static UniValue NetworkUpgradeDesc(const Consensus::Params& consensusParams, Consensus::UpgradeIndex idx, int height)
{
    UniValue rv(UniValue::VOBJ);
    const auto upgrade = NetworkUpgradeInfo[to_integral_type(idx)];
    rv.pushKV("name", upgrade.strName);
    const auto nActivationHeight = consensusParams.vUpgrades[to_integral_type(idx)].nActivationHeight;
    rv.pushKV("activationheight", nActivationHeight == Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT ? -1 : static_cast<int64_t>(nActivationHeight));
    switch (NetworkUpgradeState(height, consensusParams, idx))
    {
        case UpgradeState::UPGRADE_DISABLED:
            rv.pushKV("status", "disabled");
            break;

        case UpgradeState::UPGRADE_PENDING:
            rv.pushKV("status", "pending");
            break;

        case UpgradeState::UPGRADE_ACTIVE:
            rv.pushKV("status", "active");
            break;
    }
    rv.pushKV("info", upgrade.strInfo);
    return rv;
}

void NetworkUpgradeDescPushBack(
    UniValue& networkUpgrades,
    const Consensus::Params& consensusParams,
    Consensus::UpgradeIndex idx,
    const uint32_t height)
{
    // Network upgrades with an activation height of NO_ACTIVATION_HEIGHT are
    // hidden. This is used when network upgrade implementations are merged
    // without specifying the activation height.
    if (consensusParams.vUpgrades[to_integral_type(idx)].nActivationHeight != Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT)
    {
        networkUpgrades.pushKV(
            HexInt(NetworkUpgradeInfo[to_integral_type(idx)].nBranchId),
            NetworkUpgradeDesc(consensusParams, idx, height));
    }
}

UniValue getblockchaininfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getblockchaininfo
Returns an object containing various state info regarding block chain processing.
 
Note that when the chain tip is at the last block before a network upgrade activation,
consensus.chaintip != consensus.nextblock.

Result:
{
  "chain": "xxxx",        (string) current network name as defined in BIP70 (main, test, regtest)
  "blocks": xxxxxx,       (numeric) the current number of blocks processed in the server
  "headers": xxxxxx,      (numeric) the current number of headers we have validated
  "bestblockhash": "...", (string) the hash of the currently best block
  "difficulty": xxxxxx,   (numeric) the current difficulty
  "verificationprogress": xxxx, (numeric) estimate of verification progress [0..1]
  "chainwork": "xxxx"     (string) total amount of work in active chain, in hexadecimal
  "commitments": xxxxxx,  (numeric) the current number of note commitments in the commitment tree
  "softforks": [          (array) status of softforks in progress
     {
        "id": "xxxx",         (string) name of softfork
        "version": xx,        (numeric) block version
        "enforce": {          (object) progress toward enforcing the softfork rules for new-version blocks
           "status": xx,       (boolean) true if threshold reached
           "found": xx,        (numeric) number of blocks with the new version found
           "required": xx,     (numeric) number of blocks required to trigger
           "window": xx,       (numeric) maximum size of examined window of recent blocks
        },
        "reject": { ... }      (object) progress toward rejecting pre-softfork blocks (same fields as \"enforce\")
     }, ...
  ],
  "upgrades": {                (object) status of network upgrades
     "xxxx" : {                (string) branch ID of the upgrade
        "name": "xxxx",        (string) name of upgrade
        "activationheight": xxxxxx,  (numeric) block height of activation
        "status": "xxxx",      (string) status of upgrade
        "info": "xxxx",        (string) additional information about upgrade
     }, ...
  },
  "consensus": {               (object) branch IDs of the current and upcoming consensus rules
     "chaintip": "xxxxxxxx",   (string) branch ID used to validate the current chain tip
     "nextblock": "xxxxxxxx"   (string) branch ID that the next block will be validated under
  }
}

Examples:
)" + HelpExampleCli("getblockchaininfo", "")
   + HelpExampleRpc("getblockchaininfo", "")
);

    LOCK(cs_main);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("chain",                 Params().NetworkIDString());
    obj.pushKV("blocks",                (int)chainActive.Height());
    obj.pushKV("headers",               pindexBestHeader ? pindexBestHeader->nHeight : -1);
    obj.pushKV("bestblockhash",         chainActive.Tip()->GetBlockHash().GetHex());
    obj.pushKV("difficulty",            (double)GetNetworkDifficulty());
    obj.pushKV("verificationprogress",  Checkpoints::GuessVerificationProgress(Params().Checkpoints(), chainActive.Tip()));
    obj.pushKV("chainwork",             chainActive.Tip()->nChainWork.GetHex());
    obj.pushKV("pruned",                fPruneMode);

    SproutMerkleTree tree;
    pcoinsTip->GetSproutAnchorAt(pcoinsTip->GetBestAnchor(SPROUT), tree);
    obj.pushKV("commitments",           static_cast<uint64_t>(tree.size()));

    CBlockIndex* tip = chainActive.Tip();
    UniValue valuePools(UniValue::VARR);
    valuePools.push_back(ValuePoolDesc("sprout", tip->nChainSproutValue, nullopt));
    valuePools.push_back(ValuePoolDesc("sapling", tip->nChainSaplingValue, nullopt));
    obj.pushKV("valuePools",            valuePools);

    const Consensus::Params& consensusParams = Params().GetConsensus();
    UniValue softforks(UniValue::VARR);
    softforks.push_back(SoftForkDesc("bip34", 2, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip66", 3, tip, consensusParams));
    softforks.push_back(SoftForkDesc("bip65", 4, tip, consensusParams));
    obj.pushKV("softforks",             softforks);

    UniValue upgrades(UniValue::VOBJ);
    for (auto i = to_integral_type(Consensus::UpgradeIndex::UPGRADE_OVERWINTER); 
        i < to_integral_type(Consensus::UpgradeIndex::MAX_NETWORK_UPGRADES); ++i)
    {
        NetworkUpgradeDescPushBack(upgrades, consensusParams, Consensus::UpgradeIndex(i), tip->nHeight);
    }
    obj.pushKV("upgrades", upgrades);

    UniValue consensus(UniValue::VOBJ);
    consensus.pushKV("chaintip", HexInt(CurrentEpochBranchId(tip->nHeight, consensusParams)));
    consensus.pushKV("nextblock", HexInt(CurrentEpochBranchId(tip->nHeight + 1, consensusParams)));
    obj.pushKV("consensus", consensus);

    if (fPruneMode)
    {
        CBlockIndex *block = chainActive.Tip();
        while (block && block->pprev && (block->pprev->nStatus & BLOCK_HAVE_DATA))
            block = block->pprev;

        if (block)
            obj.pushKV("pruneheight", block->nHeight);
    }
    return obj;
}

/** Comparison function for sorting the getchaintips heads.  */
struct CompareBlocksByHeight
{
    bool operator()(const CBlockIndex* a, const CBlockIndex* b) const
    {
        /* Make sure that unequal blocks with the same height do not compare
           equal. Use the pointers themselves to make a distinction. */

        if (a->nHeight != b->nHeight)
          return (a->nHeight > b->nHeight);

        return a < b;
    }
};

UniValue getchaintips(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getchaintips

Return information about all known tips in the block tree,
 including the main chain as well as orphaned branches.

Result:
[
  {
    "height": xxxx,         (numeric) height of the chain tip
    "hash": "xxxx",         (string) block hash of the tip
    "branchlen": 0          (numeric) zero for main chain
    "status": "active"      (string) "active" for the main chain
  },
  {
    "height": xxxx,
    "hash": "xxxx",
    "branchlen": 1          (numeric) length of branch connecting the tip to the main chain
    "status": "xxxx"        (string) status of the chain (active, valid-fork, valid-headers, headers-only, invalid)
  }
]

Possible values for status:
1.  "invalid"               This branch contains at least one invalid block
2.  "headers-only"          Not all blocks for this branch are available, but the headers are valid
3.  "valid-headers"         All blocks are available for this branch, but they were never fully validated
4.  "valid-fork"            This branch is not part of the active chain, but is fully validated
5.  "active"                This is the tip of the active main chain, which is certainly valid

Examples:
)"
+ HelpExampleCli("getchaintips", "")
+ HelpExampleRpc("getchaintips", "")
);

    LOCK(cs_main);

    /* Build up a list of chain tips.  We start with the list of all
       known blocks, and successively remove blocks that appear as pprev
       of another block.  */
    set<const CBlockIndex*, CompareBlocksByHeight> setTips;
    for (const auto &[hash, blockIndex] : mapBlockIndex)
        setTips.insert(blockIndex);
    for (const auto& [hash, blockIndex] : mapBlockIndex)
    {
        const CBlockIndex* pprev = blockIndex->pprev;
        if (pprev)
            setTips.erase(pprev);
    }

    // Always report the currently active tip.
    setTips.insert(chainActive.Tip());

    /* Construct the output array.  */
    UniValue res(UniValue::VARR);
    for (const auto block : setTips)
    {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("height", block->nHeight);
        obj.pushKV("hash", block->phashBlock->GetHex());

        const int branchLen = block->nHeight - chainActive.FindFork(block)->nHeight;
        obj.pushKV("branchlen", branchLen);

        string status;
        if (chainActive.Contains(block)) {
            // This block is part of the currently active chain.
            status = "active";
        } else if (block->nStatus & BLOCK_FAILED_MASK) {
            // This block or one of its ancestors is invalid.
            status = "invalid";
        } else if (block->nChainTx == 0) {
            // This block cannot be connected because full block data for it or one of its parents is missing.
            status = "headers-only";
        } else if (block->IsValid(BLOCK_VALID_SCRIPTS)) {
            // This block is fully validated, but no longer part of the active chain. It was probably the active block once, but was reorganized.
            status = "valid-fork";
        } else if (block->IsValid(BLOCK_VALID_TREE)) {
            // The headers for this block are valid, but it has not been validated. It was probably never part of the most-work chain.
            status = "valid-headers";
        } else {
            // No clue.
            status = "unknown";
        }
        obj.pushKV("status", status);

        res.push_back(obj);
    }

    return res;
}

UniValue mempoolInfoToJSON()
{
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("size", (int64_t) mempool.size());
    ret.pushKV("bytes", (int64_t) mempool.GetTotalTxSize());
    ret.pushKV("usage", (int64_t) mempool.DynamicMemoryUsage());

    return ret;
}

UniValue getmempoolinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getmempoolinfo

Returns details on the active state of the TX memory pool.

Result:
{
  "size": xxxxx                (numeric) Current tx count
  "bytes": xxxxx               (numeric) Sum of all tx sizes
  "usage": xxxxx               (numeric) Total memory usage for the mempool
}

Examples:
)"
            + HelpExampleCli("getmempoolinfo", "")
            + HelpExampleRpc("getmempoolinfo", "")
        );

    return mempoolInfoToJSON();
}

UniValue invalidateblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(invalidateblock "hash"

Permanently marks a block as invalid, as if it violated a consensus rule.

Arguments:
1. hash   (string, required) the hash of the block to mark as invalid

Result:

Examples:
)"
+ HelpExampleCli("invalidateblock", "\"blockhash\"")
+ HelpExampleRpc("invalidateblock", "\"blockhash\"")
);

    string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;
    const auto& chainparams = Params();
    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        InvalidateBlock(state, chainparams, pblockindex);
    }

    if (state.IsValid())
        ActivateBestChain(state, chainparams);

    if (!state.IsValid())
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());

    return NullUniValue;
}

UniValue reconsiderblock(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(reconsiderblock "hash"

Removes invalidity status of a block and its descendants, reconsider them for activation.
This can be used to undo the effects of invalidateblock.

Arguments:
1. hash   (string, required) the hash of the block to reconsider

Result:

Examples:
)"
+ HelpExampleCli("reconsiderblock", "\"blockhash\"")
+ HelpExampleRpc("reconsiderblock", "\"blockhash\"")
);

    string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));
    CValidationState state;

    const auto& chainparams = Params();
    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

        CBlockIndex* pblockindex = mapBlockIndex[hash];
        ReconsiderBlock(state, pblockindex);
    }

    if (state.IsValid())
        ActivateBestChain(state, chainparams);

    if (!state.IsValid()) {
        throw JSONRPCError(RPC_DATABASE_ERROR, state.GetRejectReason());
    }

    return NullUniValue;
}

// insightexplorer
UniValue blockToDeltasJSON(const CBlock& block, const CBlockIndex* blockindex)
{
    UniValue result(UniValue::VOBJ);
    result.pushKV("hash", block.GetHash().GetHex());
    // Only report confirmations if the block is on the main chain
    if (!chainActive.Contains(blockindex))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block is an orphan");
    int confirmations = chainActive.Height() - blockindex->nHeight + 1;
    result.pushKV("confirmations", confirmations);
    result.pushKV("size", (int)::GetSerializeSize(block, SER_NETWORK, PROTOCOL_VERSION));
    result.pushKV("height", blockindex->nHeight);
    result.pushKV("version", block.nVersion);
    result.pushKV("merkleroot", block.hashMerkleRoot.GetHex());

    KeyIO keyIO(Params());
    UniValue deltas(UniValue::VARR);
    uint64_t i = 0;
    for (const auto &tx : block.vtx) {
        const uint256 txhash = tx.GetHash();

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("txid", txhash.GetHex());
        entry.pushKV("index", i);

        UniValue inputs(UniValue::VARR);
        if (!tx.IsCoinBase()) {
            uint64_t index = 0;
            for (const auto &input : tx.vin) {
                UniValue delta(UniValue::VOBJ);
                CSpentIndexValue spentInfo;
                CSpentIndexKey spentKey(input.prevout.hash, input.prevout.n);

                if (!GetSpentIndex(spentKey, spentInfo)) {
                    throw JSONRPCError(RPC_INTERNAL_ERROR, "Spent information not available");
                }
                CTxDestination dest = DestFromAddressHash(spentInfo.addressType, spentInfo.addressHash);
                if (IsValidDestination(dest)) {
                    delta.pushKV("address", keyIO.EncodeDestination(dest));
                }
                delta.pushKV("patoshis", -1 * spentInfo.patoshis);
                delta.pushKV("index", index);
                delta.pushKV("prevtxid", input.prevout.hash.GetHex());
                delta.pushKV("prevout", (int)input.prevout.n);

                inputs.push_back(move(delta));
                index ++;
            }
        }
        entry.pushKV("inputs", move(inputs));

        UniValue outputs(UniValue::VARR);
        
        uint64_t outIndex = 0;
        for (const auto &out : tx.vout) {
            UniValue delta(UniValue::VOBJ);
            const uint160 addrhash = out.scriptPubKey.AddressHash();
            CTxDestination dest;

            if (out.scriptPubKey.IsPayToScriptHash()) {
                dest = CScriptID(addrhash);
            } else if (out.scriptPubKey.IsPayToPublicKeyHash()) {
                dest = CKeyID(addrhash);
            }
            if (IsValidDestination(dest)) {
                delta.pushKV("address", keyIO.EncodeDestination(dest));
            }
            delta.pushKV("patoshis", out.nValue);
            delta.pushKV("index", outIndex);

            outputs.push_back(move(delta));
            outIndex ++;
        }
        entry.pushKV("outputs", move(outputs));
        deltas.push_back(move(entry));
        i ++;
    }
    result.pushKV("deltas", move(deltas));
    result.pushKV("time", block.GetBlockTime());
    result.pushKV("mediantime", (int64_t)blockindex->GetMedianTimePast());
    result.pushKV("nonce", block.nNonce.GetHex());
    result.pushKV("bits", strprintf("%08x", block.nBits));
    result.pushKV("difficulty", GetDifficulty(blockindex));
    result.pushKV("chainwork", blockindex->nChainWork.GetHex());

    if (blockindex->pprev)
        result.pushKV("previousblockhash", blockindex->pprev->GetBlockHash().GetHex());
    CBlockIndex *pnext = chainActive.Next(blockindex);
    if (pnext)
        result.pushKV("nextblockhash", pnext->GetBlockHash().GetHex());
    return result;
}

// insightexplorer
UniValue getblockdeltas(const UniValue& params, bool fHelp)
{
    const string enableArg = "insightexplorer";
    const bool enabled = fExperimentalMode && fInsightExplorer;
    string disabledMsg = "";
    if (!enabled) {
        disabledMsg = experimentalDisabledHelpMsg("getblockdeltas", enableArg);
    }
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getblockdeltas blockhash
Returns the txid and index where an output is spent.)"
            + disabledMsg +
R"(Arguments:
1. "hash"          (string, required) The block hash

Result:
{
  "hash": "hash",              (string) block ID
  "confirmations": n,          (numeric) number of confirmations
  "size": n,                   (numeric) block size in bytes
  "height": n,                 (numeric) block height
  "version": n,                (numeric) block version (e.g. 4)
  "merkleroot": "hash",        (string) block Merkle root
  "deltas": [
    {
      "txid": "hash",          (string) transaction ID
      "index": n,              (numeric) tx index in block
      "inputs": [
        {
          "address": "taddr",  (string) transparent address
          "patoshis": n,       (numeric) negative of spend amount
          "index": n,          (numeric) vin index
          "prevtxid": "hash",  (string) source utxo tx ID
          "prevout": n         (numeric) source utxo index
        }, ...
      ],
      "outputs": [
        {
          "address": "taddr",  (string) transparent address
          "patoshis": n,       (numeric) amount
          "index": n           (numeric) vout index
        }, ...
      ]
    }, ...
  ],
  "time": n,                   (numeric) The block version
  "mediantime": n,             (numeric) The most recent blocks' ave time
  "nonce": "hexstring",        (hex string) The nonce
  "bits": "hexstring",         (hex string) The bits
  "difficulty": ,              (numeric) the current difficulty
  "chainwork": "hexstring",    (hex string) total amount of work in active chain
  "previousblockhash": "hash", (hex string) The hash of the previous block
  "nextblockhash": "hash"      (hex string) The hash of the next block
}

Examples:)"
            + HelpExampleCli("getblockdeltas", "00227e566682aebd6a7a5b772c96d7a999cadaebeaf1ce96f4191a3aad58b00b")
            + HelpExampleRpc("getblockdeltas", "\"00227e566682aebd6a7a5b772c96d7a999cadaebeaf1ce96f4191a3aad58b00b\"")
        );

    if (!enabled) {
        throw JSONRPCError(RPC_MISC_ERROR, "Error: getblockdeltas is disabled. "
            "Run './pastel-cli help getblockdeltas' for instructions on how to enable this feature.");
    }

    string strHash = params[0].get_str();
    uint256 hash(uint256S(strHash));

    if (mapBlockIndex.count(hash) == 0)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");

    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hash];

    if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Block not available (pruned data)");

    if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    return blockToDeltasJSON(block, pblockindex);
}
static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "blockchain",         "getblockchaininfo",      &getblockchaininfo,      true  },
    { "blockchain",         "getbestblockhash",       &getbestblockhash,       true  },
    { "blockchain",         "getblockcount",          &getblockcount,          true  },
    { "blockchain",         "getblock",               &getblock,               true  },
    { "blockchain",         "getblockhash",           &getblockhash,           true  },
    { "blockchain",         "getblockheader",         &getblockheader,         true  },
    { "blockchain",         "getchaintips",           &getchaintips,           true  },
    { "blockchain",         "getdifficulty",          &getdifficulty,          true  },
    { "blockchain",         "getmempoolinfo",         &getmempoolinfo,         true  },
    { "blockchain",         "getrawmempool",          &getrawmempool,          true  },
    { "blockchain",         "gettxout",               &gettxout,               true  },
    { "blockchain",         "gettxoutsetinfo",        &gettxoutsetinfo,        true  },
    { "blockchain",         "verifychain",            &verifychain,            true  },

    // insightexplorer
    { "blockchain",         "getblockdeltas",         &getblockdeltas,         false },    
    
    /* Not shown in help */
    { "hidden",             "invalidateblock",        &invalidateblock,        true  },
    { "hidden",             "reconsiderblock",        &reconsiderblock,        true  },
};

void RegisterBlockchainRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
