// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <array>

#include <univalue.h>

#include <primitives/block.h>
#include <primitives/transaction.h>
#include <main.h>
#include <httpserver.h>
#include <rpc/server.h>
#include <streams.h>
#include <sync.h>
#include <txmempool.h>
#include <utilstrencodings.h>
#include <version.h>
#include <str_utils.h>
#include <enum_util.h>
#include <vector_types.h>

using namespace std;

static constexpr size_t MAX_GETUTXOS_OUTPOINTS = 15; //allow a max of 15 outpoints to be queried at once

enum class RetFormat : uint32_t
{
    UNDEF = 0,
    BINARY,
    HEX,
    JSON,

    COUNT
};

typedef struct _RF_NAME
{
    const RetFormat rf;
    const char* name;
} RF_NAME;

static constexpr auto to_index(const RetFormat rf)
{
    return static_cast<std::underlying_type_t<RetFormat>>(rf);
}

static constexpr std::array<RF_NAME, to_index(RetFormat::COUNT)> rf_names =
{{
    { RetFormat::UNDEF,     ""    },
    { RetFormat::BINARY,    "bin" },
    { RetFormat::HEX,       "hex" },
    { RetFormat::JSON,      "json"}
}};

struct CCoin
{
    uint32_t nTxVer{}; // Don't call this nVersion, that name has a special meaning inside IMPLEMENT_SERIALIZE
    uint32_t nHeight{};
    CTxOut out;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(nTxVer);
        READWRITE(nHeight);
        READWRITE(out);
    }
};

extern void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry);
extern UniValue blockToJSON(const CBlock& block, const CBlockIndex* blockindex, bool txDetails = false);
extern UniValue mempoolInfoToJSON();
extern UniValue mempoolToJSON(bool fVerbose = false);
extern void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex);
extern UniValue blockheaderToJSON(const CBlockIndex* blockindex);

static bool RESTERR(HTTPRequest* req, enum HTTPStatusCode status, string message)
{
    req->WriteHeader("Content-Type", "text/plain");
    req->WriteReply(to_integral_type(status), message + "\r\n");
    return false;
}

static enum RetFormat ParseDataFormat(v_strings& vParams, const string& strReq)
{
    str_split(vParams, strReq, '.');
    if (vParams.size() > 1)
    {
        for (const auto &param : rf_names)
            if (vParams[1] == param.name)
                return param.rf;
    }
    return rf_names[0].rf;
}

static string AvailableDataFormatsString()
{
    string formats;
    for (const auto &param : rf_names)
        if (strlen(param.name) > 0)
        {
            formats.append(".");
            formats.append(param.name);
            formats.append(", ");
        }

    if (!formats.empty())
        return formats.substr(0, formats.length() - 2);

    return formats;
}

static bool ParseHashStr(const string& strReq, uint256& v)
{
    if (!IsHex(strReq) || (strReq.size() != 64))
        return false;

    v.SetHex(strReq);
    return true;
}

static bool CheckWarmup(HTTPRequest* req)
{
    string statusmessage;
    if (RPCIsInWarmup(&statusmessage))
         return RESTERR(req, HTTPStatusCode::SERVICE_UNAVAILABLE, "Service temporarily unavailable: " + statusmessage);
    return true;
}

static bool rest_headers(HTTPRequest* req, const string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    v_strings vParams;
    const RetFormat rf = ParseDataFormat(vParams, strURIPart);
    v_strings vPath;
    if (vParams.empty())
        return false;
    str_split(vPath, vParams[0], '/');

    if (vPath.size() != 2)
        return RESTERR(req, HTTPStatusCode::BAD_REQUEST, "No header count specified. Use /rest/headers/<count>/<hash>.<ext>.");

    long count = strtol(vPath[0].c_str(), nullptr, 10);
    if (count < 1 || count > 2000)
        return RESTERR(req, HTTPStatusCode::BAD_REQUEST, "Header count out of range: " + vPath[0]);

    string hashStr = vPath[1];
    uint256 hash;
    if (!ParseHashStr(hashStr, hash))
        return RESTERR(req, HTTPStatusCode::BAD_REQUEST, "Invalid hash: " + hashStr);

    vector<const CBlockIndex *> vHeaders;
    vHeaders.reserve(count);
    {
        LOCK(cs_main);
        auto it = mapBlockIndex.find(hash);
        const CBlockIndex *pindex = (it != mapBlockIndex.end()) ? it->second : nullptr;
        while (pindex && chainActive.Contains(pindex))
        {
            vHeaders.push_back(pindex);
            if (vHeaders.size() == static_cast<size_t>(count))
                break;
            pindex = chainActive.Next(pindex);
        }
    }

    CDataStream ssHeader(SER_NETWORK, PROTOCOL_VERSION);
    for (const auto pindex : vHeaders)
        ssHeader << pindex->GetBlockHeader();

    switch (rf)
    {
        case RetFormat::BINARY: {
            string binaryHeader = ssHeader.str();
            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), binaryHeader);
            return true;
        }

        case RetFormat::HEX: {
            string strHex = HexStr(ssHeader.begin(), ssHeader.end()) + "\n";
            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strHex);
            return true;
        }
        case RetFormat::JSON: {
            UniValue jsonHeaders(UniValue::VARR);
            for (const auto pindex : vHeaders)
                jsonHeaders.push_back(blockheaderToJSON(pindex));
            string strJSON = jsonHeaders.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strJSON);
            return true;
        }
        default: {
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, "output format not found (available: .bin, .hex)");
        }
    }

    // not reached
    return true; // continue to process further HTTP reqs on this cxn
}

static bool rest_block(HTTPRequest* req,
                       const string& strURIPart,
                       bool showTxDetails)
{
    if (!CheckWarmup(req))
        return false;
    v_strings params;
    const RetFormat rf = ParseDataFormat(params, strURIPart);

    string hashStr = params[0];
    uint256 hash;
    if (!ParseHashStr(hashStr, hash))
        return RESTERR(req, HTTPStatusCode::BAD_REQUEST, "Invalid hash: " + hashStr);

    CBlock block;
    CBlockIndex* pblockindex = nullptr;
    {
        LOCK(cs_main);
        if (mapBlockIndex.count(hash) == 0)
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, hashStr + " not found");

        pblockindex = mapBlockIndex[hash];
        if (fHavePruned && !(pblockindex->nStatus & BLOCK_HAVE_DATA) && pblockindex->nTx > 0)
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, hashStr + " not available (pruned data)");

        if (!ReadBlockFromDisk(block, pblockindex, Params().GetConsensus()))
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, hashStr + " not found");
    }

    CDataStream ssBlock(SER_NETWORK, PROTOCOL_VERSION);
    ssBlock << block;

    switch (rf)
    {
        case RetFormat::BINARY: {
            string binaryBlock = ssBlock.str();
            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), binaryBlock);
            return true;
        }

        case RetFormat::HEX: {
            string strHex = HexStr(ssBlock.begin(), ssBlock.end()) + "\n";
            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strHex);
            return true;
        }

        case RetFormat::JSON: {
            UniValue objBlock = blockToJSON(block, pblockindex, showTxDetails);
            string strJSON = objBlock.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strJSON);
            return true;
        }

        default:
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, "output format not found (available: " + AvailableDataFormatsString() + ")");
    }

    // not reached
    return true; // continue to process further HTTP reqs on this cxn
}

static bool rest_block_extended(HTTPRequest* req, const string& strURIPart)
{
    return rest_block(req, strURIPart, true);
}

static bool rest_block_notxdetails(HTTPRequest* req, const string& strURIPart)
{
    return rest_block(req, strURIPart, false);
}

// A bit of a hack - dependency on a function defined in rpc/blockchain.cpp
UniValue getblockchaininfo(const UniValue& params, bool fHelp);

static bool rest_chaininfo(HTTPRequest* req, const string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    v_strings vParams;
    const RetFormat rf = ParseDataFormat(vParams, strURIPart);

    switch (rf)
    {
        case RetFormat::JSON: {
            UniValue rpcParams(UniValue::VARR);
            UniValue chainInfoObject = getblockchaininfo(rpcParams, false);
            string strJSON = chainInfoObject.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strJSON);
            return true;
        }

        default:
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, "output format not found (available: json)");
    }

    // not reached
    return true; // continue to process further HTTP reqs on this cxn
}

static bool rest_mempool_info(HTTPRequest* req, const string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    v_strings vParams;
    const RetFormat rf = ParseDataFormat(vParams, strURIPart);

    switch (rf)
    {
        case RetFormat::JSON: {
            UniValue mempoolInfoObject = mempoolInfoToJSON();

            string strJSON = mempoolInfoObject.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strJSON);
            return true;
        }

        default:
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, "output format not found (available: json)");
    }

    // not reached
    return true; // continue to process further HTTP reqs on this cxn
}

static bool rest_mempool_contents(HTTPRequest* req, const string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    v_strings vParams;
    const RetFormat rf = ParseDataFormat(vParams, strURIPart);

    switch (rf)
    {
        case RetFormat::JSON: {
            UniValue mempoolObject = mempoolToJSON(true);

            string strJSON = mempoolObject.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strJSON);
            return true;
        }

        default:
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, "output format not found (available: json)");
    }

    // not reached
    return true; // continue to process further HTTP reqs on this cxn
}

static bool rest_tx(HTTPRequest* req, const string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    v_strings vParams;
    const RetFormat rf = ParseDataFormat(vParams, strURIPart);

    const string hashStr = vParams[0];
    uint256 hash;
    if (!ParseHashStr(hashStr, hash))
        return RESTERR(req, HTTPStatusCode::BAD_REQUEST, "Invalid hash: " + hashStr);

    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, true))
        return RESTERR(req, HTTPStatusCode::NOT_FOUND, hashStr + " not found");

    CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
    ssTx << tx;

    switch (rf)
    {
        case RetFormat::BINARY: {
            string binaryTx = ssTx.str();
            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), binaryTx);
            return true;
        }

        case RetFormat::HEX: {
            string strHex = HexStr(ssTx.begin(), ssTx.end()) + "\n";
            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strHex);
            return true;
        }

        case RetFormat::JSON: {
            UniValue objTx(UniValue::VOBJ);
            TxToJSON(tx, hashBlock, objTx);
            string strJSON = objTx.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strJSON);
            return true;
        }

        default:
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, "output format not found (available: " + AvailableDataFormatsString() + ")");
    }

    // not reached
    return true; // continue to process further HTTP reqs on this cxn
}

static bool rest_getutxos(HTTPRequest* req, const string& strURIPart)
{
    if (!CheckWarmup(req))
        return false;
    v_strings vParams;
    enum RetFormat rf = ParseDataFormat(vParams, strURIPart);

    v_strings vUriParts;
    if (!vParams.empty() && vParams[0].length() > 1)
        str_split(vUriParts, vParams[0].substr(1), '/');

    // throw exception in case of an empty request
    string strRequestMutable = req->ReadBody();
    if (strRequestMutable.empty() && vUriParts.empty())
        return RESTERR(req, HTTPStatusCode::INTERNAL_SERVER_ERROR, "Error: empty request");

    bool fInputParsed = false;
    bool fCheckMemPool = false;
    v_outpoints vOutPoints;

    // parse/deserialize input
    // input-format = output-format, rest/getutxos/bin requires binary input, gives binary output, ...

    if (!vUriParts.empty())
    {

        //inputs is sent over URI scheme (/rest/getutxos/checkmempool/txid1-n/txid2-n/...)
        if (vUriParts[0] == "checkmempool")
            fCheckMemPool = true;

        for (size_t i = (fCheckMemPool) ? 1 : 0; i < vUriParts.size(); i++)
        {
            uint256 txid;
            int32_t nOutput;
            string strTxid = vUriParts[i].substr(0, vUriParts[i].find("-"));
            string strOutput = vUriParts[i].substr(vUriParts[i].find("-")+1);

            if (!ParseInt32(strOutput, &nOutput) || !IsHex(strTxid))
                return RESTERR(req, HTTPStatusCode::INTERNAL_SERVER_ERROR, "Parse error");

            txid.SetHex(strTxid);
            vOutPoints.push_back(COutPoint(txid, (uint32_t)nOutput));
        }

        if (vOutPoints.empty())
            return RESTERR(req, HTTPStatusCode::INTERNAL_SERVER_ERROR, "Error: empty request");
        fInputParsed = true;
    }

    switch (rf)
    {
        case RetFormat::HEX: {
            // convert hex to bin, continue then with bin part
            auto strRequestV = ParseHex(strRequestMutable);
            strRequestMutable.assign(strRequestV.begin(), strRequestV.end());
            break;
        }

        case RetFormat::BINARY: {
            try {
                //deserialize only if user sent a request
                if (strRequestMutable.size() > 0)
                {
                    if (fInputParsed) //don't allow sending input over URI and HTTP RAW DATA
                        return RESTERR(req, HTTPStatusCode::INTERNAL_SERVER_ERROR, "Combination of URI scheme inputs and raw post data is not allowed");

                    CDataStream oss(SER_NETWORK, PROTOCOL_VERSION);
                    oss << strRequestMutable;
                    oss >> fCheckMemPool;
                    oss >> vOutPoints;
                }
            } catch (const ios_base::failure& ) {
                // abort in case of unreadable binary data
                return RESTERR(req, HTTPStatusCode::INTERNAL_SERVER_ERROR, "Parse error");
            }
            break;
        }

        case RetFormat::JSON: {
            if (!fInputParsed)
                return RESTERR(req, HTTPStatusCode::INTERNAL_SERVER_ERROR, "Error: empty request");
            break;
        }

        default:
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, "output format not found (available: " + AvailableDataFormatsString() + ")");
    }

    // limit max outpoints
    if (vOutPoints.size() > MAX_GETUTXOS_OUTPOINTS)
        return RESTERR(req, HTTPStatusCode::INTERNAL_SERVER_ERROR, strprintf("Error: max outpoints exceeded (max: %d, tried: %d)", MAX_GETUTXOS_OUTPOINTS, vOutPoints.size()));

    // check spentness and form a bitmap (as well as a JSON capable human-readable string representation)
    v_uint8 bitmap;
    vector<CCoin> outs;
    string bitmapStringRepresentation;
    v_bools hits(vOutPoints.size());
    {
        LOCK2(cs_main, mempool.cs);

        CCoinsView viewDummy;
        CCoinsViewCache view(&viewDummy);

        CCoinsViewCache& viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);

        if (fCheckMemPool)
            view.SetBackend(viewMempool); // switch cache backend to db+mempool in case user likes to query mempool

        size_t i = 0;
        for (const auto &outpoint : vOutPoints)
        {
            CCoins coins;
            const uint256 &hash = outpoint.hash;
            if (view.GetCoins(hash, coins))
            {
                mempool.pruneSpent(hash, coins);
                if (coins.IsAvailable(outpoint.n))
                {
                    hits[i] = true;
                    // Safe to index into vout here because IsAvailable checked if it's off the end of the array, or if
                    // n is valid but points to an already spent output (IsNull).
                    CCoin coin;
                    coin.nTxVer = coins.nVersion;
                    coin.nHeight = coins.nHeight;
                    coin.out = coins.vout.at(outpoint.n);
                    assert(!coin.out.IsNull());
                    outs.push_back(coin);
                }
            }

            bitmapStringRepresentation.append(hits[i] ? "1" : "0"); // form a binary string representation (human-readable for json output)
            i++;
        }
    }
    copy(hits.cbegin(), hits.cend(), back_inserter(bitmap));


    switch (rf)
    {
        case RetFormat::BINARY: {
            // serialize data
            // use exact same output as mentioned in Bip64
            CDataStream ssGetUTXOResponse(SER_NETWORK, PROTOCOL_VERSION);
            ssGetUTXOResponse << chainActive.Height() << chainActive.Tip()->GetBlockHash() << bitmap << outs;
            string ssGetUTXOResponseString = ssGetUTXOResponse.str();

            req->WriteHeader("Content-Type", "application/octet-stream");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), ssGetUTXOResponseString);
            return true;
        }

        case RetFormat::HEX: {
            CDataStream ssGetUTXOResponse(SER_NETWORK, PROTOCOL_VERSION);
            ssGetUTXOResponse << chainActive.Height() << chainActive.Tip()->GetBlockHash() << bitmap << outs;
            string strHex = HexStr(ssGetUTXOResponse.begin(), ssGetUTXOResponse.end()) + "\n";

            req->WriteHeader("Content-Type", "text/plain");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strHex);
            return true;
        }

        case RetFormat::JSON: {
            UniValue objGetUTXOResponse(UniValue::VOBJ);

            // pack in some essentials
            // use more or less the same output as mentioned in Bip64
            objGetUTXOResponse.pushKV("chainHeight", chainActive.Height());
            objGetUTXOResponse.pushKV("chaintipHash", chainActive.Tip()->GetBlockHash().GetHex());
            objGetUTXOResponse.pushKV("bitmap", bitmapStringRepresentation);

            UniValue utxos(UniValue::VARR);
            for (const auto& coin : outs)
            {
                UniValue utxo(UniValue::VOBJ);
                utxo.pushKV("txvers", (int32_t)coin.nTxVer);
                utxo.pushKV("height", (int32_t)coin.nHeight);
                utxo.pushKV("value", ValueFromAmount(coin.out.nValue));

                // include the script in a json output
                UniValue o(UniValue::VOBJ);
                ScriptPubKeyToJSON(coin.out.scriptPubKey, o, true);
                utxo.pushKV("scriptPubKey", o);
                utxos.push_back(utxo);
            }
            objGetUTXOResponse.pushKV("utxos", utxos);

            // return json string
            string strJSON = objGetUTXOResponse.write() + "\n";
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(to_integral_type(HTTPStatusCode::OK), strJSON);
            return true;
        }

        default:
            return RESTERR(req, HTTPStatusCode::NOT_FOUND, "output format not found (available: " + AvailableDataFormatsString() + ")");
    }

    // not reached
    return true; // continue to process further HTTP reqs on this cxn
}

static const struct {
    const char* prefix;
    bool (*handler)(HTTPRequest* req, const string& strReq);
} uri_prefixes[] = {
      {"/rest/tx/", rest_tx},
      {"/rest/block/notxdetails/", rest_block_notxdetails},
      {"/rest/block/", rest_block_extended},
      {"/rest/chaininfo", rest_chaininfo},
      {"/rest/mempool/info", rest_mempool_info},
      {"/rest/mempool/contents", rest_mempool_contents},
      {"/rest/headers/", rest_headers},
      {"/rest/getutxos", rest_getutxos},
};

bool StartREST()
{
    for (unsigned int i = 0; i < ARRAYLEN(uri_prefixes); i++)
        RegisterHTTPHandler(uri_prefixes[i].prefix, false, uri_prefixes[i].handler);
    return true;
}

void InterruptREST()
{
}

void StopREST()
{
    for (unsigned int i = 0; i < ARRAYLEN(uri_prefixes); i++)
        UnregisterHTTPHandler(uri_prefixes[i].prefix, false);
}
