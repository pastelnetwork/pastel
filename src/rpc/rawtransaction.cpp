// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2018-2022 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include <stdint.h>
#include <variant>

#include <univalue.h>

#include <consensus/upgrades.h>
#include <consensus/validation.h>
#include <core_io.h>
#include <init.h>
#include <deprecation.h>
#include <key_io.h>
#include <keystore.h>
#include <main.h>
#include <merkleblock.h>
#include <net.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <script/script.h>
#include <script/script_error.h>
#include <script/sign.h>
#include <script/standard.h>
#include <uint256.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#endif

using namespace std;

void ScriptPubKeyToJSON(const CScript& scriptPubKey, UniValue& out, bool fIncludeHex)
{
    txnouttype type;
    vector<CTxDestination> addresses;
    int nRequired;

    out.pushKV("asm", ScriptToAsmStr(scriptPubKey));
    if (fIncludeHex)
        out.pushKV("hex", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

    if (!ExtractDestinations(scriptPubKey, type, addresses, nRequired)) {
        out.pushKV("type", GetTxnOutputType(type));
        return;
    }

    out.pushKV("reqSigs", nRequired);
    out.pushKV("type", GetTxnOutputType(type));

    KeyIO keyIO(Params());
    UniValue a(UniValue::VARR);
    for (const auto& addr : addresses)
        a.push_back(keyIO.EncodeDestination(addr));
    out.pushKV("addresses", a);
}

UniValue TxShieldedSpendsToJSON(const CTransaction& tx) {
    UniValue vdesc(UniValue::VARR);
    for (const SpendDescription& spendDesc : tx.vShieldedSpend) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("cv", spendDesc.cv.GetHex());
        obj.pushKV("anchor", spendDesc.anchor.GetHex());
        obj.pushKV("nullifier", spendDesc.nullifier.GetHex());
        obj.pushKV("rk", spendDesc.rk.GetHex());
        obj.pushKV("proof", HexStr(spendDesc.zkproof.begin(), spendDesc.zkproof.end()));
        obj.pushKV("spendAuthSig", HexStr(spendDesc.spendAuthSig.begin(), spendDesc.spendAuthSig.end()));
        vdesc.push_back(obj);
    }
    return vdesc;
}

UniValue TxShieldedOutputsToJSON(const CTransaction& tx) {
    UniValue vdesc(UniValue::VARR);
    for (const OutputDescription& outputDesc : tx.vShieldedOutput) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("cv", outputDesc.cv.GetHex());
        obj.pushKV("cmu", outputDesc.cm.GetHex());
        obj.pushKV("ephemeralKey", outputDesc.ephemeralKey.GetHex());
        obj.pushKV("encCiphertext", HexStr(outputDesc.encCiphertext.begin(), outputDesc.encCiphertext.end()));
        obj.pushKV("outCiphertext", HexStr(outputDesc.outCiphertext.begin(), outputDesc.outCiphertext.end()));
        obj.pushKV("proof", HexStr(outputDesc.zkproof.begin(), outputDesc.zkproof.end()));
        vdesc.push_back(obj);
    }
    return vdesc;
}

void TxToJSON(const CTransaction& tx, const uint256 hashBlock, UniValue& entry)
{
    const uint256 &txid = tx.GetHash();
    entry.pushKV("txid", txid.GetHex());
    entry.pushKV("size", static_cast<uint64_t>(::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION)));
    entry.pushKV("overwintered", tx.fOverwintered);
    entry.pushKV("version", tx.nVersion);
    if (tx.fOverwintered) {
        entry.pushKV("versiongroupid", HexInt(tx.nVersionGroupId));
    }
    entry.pushKV("locktime", (int64_t)tx.nLockTime);
    if (tx.fOverwintered) {
        entry.pushKV("expiryheight", (int64_t)tx.nExpiryHeight);
    }
    entry.pushKV("hex", EncodeHexTx(tx));

    KeyIO keyIO(Params());
    UniValue vin(UniValue::VARR);
    for (const auto& txin : tx.vin)
    {
        UniValue in(UniValue::VOBJ);
        if (tx.IsCoinBase())
            in.pushKV("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
        else
        {
            in.pushKV("txid", txin.prevout.hash.GetHex());
            in.pushKV("vout", (int64_t)txin.prevout.n);
            UniValue o(UniValue::VOBJ);
            o.pushKV("asm", ScriptToAsmStr(txin.scriptSig, true));
            o.pushKV("hex", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
            in.pushKV("scriptSig", move(o));

            // Add address and value info if spentindex enabled
            CSpentIndexValue spentInfo;
            CSpentIndexKey spentKey(txin.prevout.hash, txin.prevout.n);
            if (fSpentIndex && GetSpentIndex(spentKey, spentInfo)) {
                in.pushKV("value", ValueFromAmount(spentInfo.patoshis));
                in.pushKV("valuePat", spentInfo.patoshis);

                auto dest = DestFromAddressHash(spentInfo.addressType, spentInfo.addressHash);
                if (IsValidDestination(dest))
                    in.pushKV("address", keyIO.EncodeDestination(dest));
            }
        }
        in.pushKV("sequence", (int64_t)txin.nSequence);
        vin.push_back(move(in));
    }
    entry.pushKV("vin", vin);
    UniValue vout(UniValue::VARR);
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        const CTxOut& txout = tx.vout[i];
        UniValue out(UniValue::VOBJ);
        out.pushKV("value", ValueFromAmount(txout.nValue));
        out.pushKV("valuePat", txout.nValue);
        out.pushKV("n", (int64_t)i);
        UniValue o(UniValue::VOBJ);
        ScriptPubKeyToJSON(txout.scriptPubKey, o, true);
        out.pushKV("scriptPubKey", o);

        // Add spent information if spentindex is enabled
        CSpentIndexValue spentInfo;
        CSpentIndexKey spentKey(txid, i);
        if (fSpentIndex && GetSpentIndex(spentKey, spentInfo)) {
            out.pushKV("spentTxId", spentInfo.txid.GetHex());
            out.pushKV("spentIndex", (int)spentInfo.inputIndex);
            out.pushKV("spentHeight", spentInfo.blockHeight);
        }
        vout.push_back(out);
    }
    entry.pushKV("vout", vout);

    if (tx.fOverwintered && tx.nVersion >= SAPLING_TX_VERSION) {
        entry.pushKV("valueBalance", ValueFromAmount(tx.valueBalance));
        entry.pushKV("valueBalancePat", tx.valueBalance);
        UniValue vspenddesc = TxShieldedSpendsToJSON(tx);
        entry.pushKV("vShieldedSpend", vspenddesc);
        UniValue voutputdesc = TxShieldedOutputsToJSON(tx);
        entry.pushKV("vShieldedOutput", voutputdesc);
        if (!(vspenddesc.empty() && voutputdesc.empty())) {
            entry.pushKV("bindingSig", HexStr(tx.bindingSig.begin(), tx.bindingSig.end()));
        }
    }

    if (!hashBlock.IsNull()) {
        entry.pushKV("blockhash", hashBlock.GetHex());
        const auto mi = mapBlockIndex.find(hashBlock);
        if (mi != mapBlockIndex.cend() && mi->second)
        {
            const auto pindex = mi->second;
            if (chainActive.Contains(pindex))
            {
                entry.pushKV("height", pindex->nHeight);
                entry.pushKV("confirmations", 1 + chainActive.Height() - pindex->nHeight);
                entry.pushKV("time", pindex->GetBlockTime());
                entry.pushKV("blocktime", pindex->GetBlockTime());
            }
            else
            {
                entry.pushKV("height", -1);
                entry.pushKV("confirmations", 0);
            }
        }
    }
}

UniValue getrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
        throw runtime_error(
R"(getrawtransaction "txid" ( verbose "blockhash")

NOTE: By default this function only works sometimes. This is when the tx is in the mempool
or there is an unspent output in the utxo for this transaction. To make it always work,
you need to maintain a transaction index, using the -txindex command line option.

NOTE: If "blockhash" is not provided and the -txindex option is not enabled, then this call only
works for mempool transactions. If either "blockhash" is provided or the -txindex option is
enabled, it also works for blockchain transactions. If the block which contains the transaction
is known, its hash can be provided even for nodes without -txindex. Note that if a blockhash is
provided, only that block will be searched and if the transaction is in the mempool or other
blocks, or if this node does not have the given block available, the transaction will not be found.

Return the raw transaction data.

If verbose=0, returns a string that is serialized, hex-encoded data for 'txid'.
If verbose is non-zero, returns an Object with information about 'txid'.

Arguments:
1. "txid"      (string, required) The transaction id
2. verbose     (numeric, optional, default=0) If 0, return a string, other return a json object

Result (if verbose is not set or set to 0):
  "data"      (string) The serialized, hex-encoded data for 'txid'

Result (if verbose > 0):
{
  "in_active_chain": b, (bool) Whether specified block is in the active chain or not (only present with explicit "blockhash" argument)
  "hex" : "data",       (string) The serialized, hex-encoded data for 'txid'
  "txid" : "id",        (string) The transaction id (same as provided)
  "version" : n,        (numeric) The version
  "locktime" : ttt,     (numeric) The lock time
  "expiryheight" : ttt, (numeric, optional) The block height after which the transaction expires
  "vin" : [             (array of json objects)
     {
       "txid": "id",    (string) The transaction id
       "vout": n,       (numeric)
       "scriptSig": {   (json object) The script
         "asm": "asm",  (string) asm
         "hex": "hex"   (string) hex
       },
       "sequence": n    (numeric) The script sequence number
     }
     ,...
  ],
  "vout" : [                     (array of json objects)
     {
       "value" : x.xxx,          (numeric) The value in )" + CURRENCY_UNIT + R"(
       "n" : n,                  (numeric) index
       "scriptPubKey" : {        (json object)
         "asm" : "asm",          (string) the asm
         "hex" : "hex",          (string) the hex
         "reqSigs" : n,          (numeric) The required sigs
         "type" : "pubkeyhash",  (string) The type, eg 'pubkeyhash'
         "addresses" : [         (json array of string)
           "z-address"           (string) Pastel address
           ,...
         ]
       }
     }
     ,...
  ],
  "blockhash" : "hash",          (string) the block hash
  "confirmations" : n,           (numeric) The confirmations
  "time" : ttt,                  (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT)
  "blocktime" : ttt              (numeric) The block time in seconds since epoch (Jan 1 1970 GMT)
}

 Examples:
)" + HelpExampleCli("getrawtransaction", "\"mytxid\"")
   + HelpExampleCli("getrawtransaction", "\"mytxid\" 1")
   + HelpExampleRpc("getrawtransaction", "\"mytxid\", 1")
   + HelpExampleCli("getrawtransaction", "\"mytxid\" 0 \"myblockhash\"")
   + HelpExampleCli("getrawtransaction", "\"mytxid\" 1 \"myblockhash\"")
);

    LOCK(cs_main);

    bool in_active_chain = true;
    uint256 hash = ParseHashV(params[0], "parameter 1");
    CBlockIndex* blockindex = nullptr;

    bool fVerbose = false;
    if (params.size() > 1)
        fVerbose = (params[1].get_int() != 0);

    if (params.size() > 2)
    {
        uint256 blockhash = ParseHashV(params[2], "parameter 3");
        if (!blockhash.IsNull())
        {
            auto it = mapBlockIndex.find(blockhash);
            if (it == mapBlockIndex.cend())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block hash not found");
            blockindex = it->second;
            in_active_chain = chainActive.Contains(blockindex);
        }
    }

    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(hash, tx, Params().GetConsensus(), hashBlock, true, nullptr, blockindex))
    {
        string errmsg;
        if (blockindex) {
            if (!(blockindex->nStatus & BLOCK_HAVE_DATA)) {
                throw JSONRPCError(RPC_MISC_ERROR, "Block not available");
            }
            errmsg = "No such transaction found in the provided block";
        } else {
            errmsg = fTxIndex
              ? "No such mempool or blockchain transaction"
              : "No such mempool transaction. Use -txindex to enable blockchain transaction queries";
        }
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, errmsg + ". Use gettransaction for wallet transactions.");
    }

    string strHex = EncodeHexTx(tx);

    if (!fVerbose)
        return strHex;

    UniValue result(UniValue::VOBJ);
    if (blockindex)
       result.pushKV("in_active_chain", in_active_chain);
    result.pushKV("hex", strHex);
    TxToJSON(tx, hashBlock, result);
    return result;
}

UniValue gettxoutproof(const UniValue& params, bool fHelp)
{
    if (fHelp || (params.size() != 1 && params.size() != 2))
        throw runtime_error(
R"(gettxoutproof ["txid",...] ( blockhash )

Returns a hex-encoded proof that "txid" was included in a block.

NOTE: By default this function only works sometimes. This is when there is an
unspent output in the utxo for this transaction. To make it always work,
you need to maintain a transaction index, using the -txindex command line option or
specify the block in which the transaction is included in manually (by blockhash).

Return the raw transaction data.

Arguments:
1. "txids"       (string) A json array of txids to filter
    [
      "txid"     (string) A transaction hash
      ,...
    ]
2. "block hash"  (string, optional) If specified, looks for txid in the block with this hash

Result:
  "data"         (string) A string that is a serialized, hex-encoded data for the proof.

Examples:
)"
+ HelpExampleCli("gettxoutproof", "")
+ HelpExampleRpc("gettxoutproof", "")
);

    set<uint256> setTxids;
    uint256 oneTxid;
    UniValue txids = params[0].get_array();
    for (size_t idx = 0; idx < txids.size(); idx++) {
        const UniValue& txid = txids[idx];
        if (txid.get_str().length() != 64 || !IsHex(txid.get_str()))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid txid ")+txid.get_str());
        uint256 hash(uint256S(txid.get_str()));
        if (setTxids.count(hash))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated txid: ")+txid.get_str());
       setTxids.insert(hash);
       oneTxid = hash;
    }

    LOCK(cs_main);

    CBlockIndex* pblockindex = nullptr;

    uint256 hashBlock;
    if (params.size() > 1)
    {
        hashBlock = uint256S(params[1].get_str());
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found");
        pblockindex = mapBlockIndex[hashBlock];
    } else {
        CCoins coins;
        if (pcoinsTip->GetCoins(oneTxid, coins) && coins.nHeight > 0 && coins.nHeight <= chainActive.Height())
            pblockindex = chainActive[coins.nHeight];
    }

    const auto& consensusParams = Params().GetConsensus();
    if (!pblockindex)
    {
        CTransaction tx;
        if (!GetTransaction(oneTxid, tx, consensusParams, hashBlock, false) || hashBlock.IsNull())
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not yet in block");
        if (!mapBlockIndex.count(hashBlock))
            throw JSONRPCError(RPC_INTERNAL_ERROR, "Transaction index corrupt");
        pblockindex = mapBlockIndex[hashBlock];
    }

    CBlock block;
    if (!ReadBlockFromDisk(block, pblockindex, consensusParams))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Can't read block from disk");

    unsigned int ntxFound = 0;
    for (const auto&tx : block.vtx)
    {
        if (setTxids.count(tx.GetHash()))
            ntxFound++;
    }
    if (ntxFound != setTxids.size())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "(Not all) transactions not found in specified block");

    CDataStream ssMB(SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock mb(block, setTxids);
    ssMB << mb;
    string strHex = HexStr(ssMB.begin(), ssMB.end());
    return strHex;
}

UniValue verifytxoutproof(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(verifytxoutproof "proof"

Verifies that a proof points to a transaction in a block, returning the transaction it commits to
and throwing an RPC error if the block is not in our best chain

Arguments:
1. "proof"    (string, required) The hex-encoded proof generated by gettxoutproof

Result:
["txid"]      (array, strings) The txid(s) which the proof commits to, or empty array if the proof is invalid

Examples:
)"
+ HelpExampleCli("verifytxoutproof", "\"proof\"")
+ HelpExampleRpc("verifytxoutproof", "\"proof\"")
);

    CDataStream ssMB(ParseHexV(params[0], "proof"), SER_NETWORK, PROTOCOL_VERSION);
    CMerkleBlock merkleBlock;
    ssMB >> merkleBlock;

    UniValue res(UniValue::VARR);

    v_uint256 vMatch;
    if (merkleBlock.txn.ExtractMatches(vMatch) != merkleBlock.header.hashMerkleRoot)
        return res;

    LOCK(cs_main);

    if (!mapBlockIndex.count(merkleBlock.header.GetHash()) || !chainActive.Contains(mapBlockIndex[merkleBlock.header.GetHash()]))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Block not found in chain");

    for (const auto& hash : vMatch)
        res.push_back(hash.GetHex());
    return res;
}

UniValue createrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
R"(createrawtransaction [{"txid":"id", "vout":n},...] {"address":amount,...} ( locktime ) ( expiryheight )

Create a transaction spending the given inputs and sending to the given addresses.
Returns hex-encoded raw transaction.
Note that the transaction's inputs are not signed, and
it is not stored in the wallet or transmitted to the network.

Arguments:
1. "transactions"        (string, required) A json array of json objects
     [
       {
         "txid":"id",    (string, required) The transaction id
         "vout":n        (numeric, required) The output number
         "sequence":n    (numeric, optional) The sequence number
       }
       ,...
     ]
2. "addresses"           (string, required) a json object with addresses as keys and amounts as values
    {
      "address": x.xxx   (numeric, required) The key is the Pastel address, the value is the )" + CURRENCY_UNIT + R"( amount
      ,...
    }
3. locktime              (numeric, optional, default=0) Raw locktime. Non-0 value also locktime-activates inputs
4. expiryheight          (numeric, optional, default=nextblockheight+)" + strprintf("%d", DEFAULT_TX_EXPIRY_DELTA) + R"() Expiry height of transaction (if Overwinter is active)

Result:
  "transaction"          (string) hex string of the transaction

Examples:
)" + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\" \"{\\\"address\\\":0.01}\"")
   + HelpExampleRpc("createrawtransaction", "\"[{\\\"txid\\\":\\\"myid\\\",\\\"vout\\\":0}]\", \"{\\\"address\\\":0.01}\"")
);

    LOCK(cs_main);
    RPCTypeCheck(params, {UniValue::VARR, UniValue::VOBJ, UniValue::VNUM, UniValue::VNUM}, true);
    if (params[0].isNull() || params[1].isNull())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, arguments 1 and 2 must be non-null");

    UniValue inputs = params[0].get_array();
    UniValue sendTo = params[1].get_obj();

    uint32_t nextBlockHeight = 0;
    {
        LOCK(cs_main);
        nextBlockHeight = chainActive.Height() + 1;
    }
    CMutableTransaction rawTx = CreateNewContextualCMutableTransaction(
        Params().GetConsensus(), nextBlockHeight);

    if (params.size() > 2 && !params[2].isNull()) {
        int64_t nLockTime = params[2].get_int64();
        if (nLockTime < 0 || nLockTime > numeric_limits<uint32_t>::max())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, locktime out of range");
        rawTx.nLockTime = static_cast<uint32_t>(nLockTime);
    }
    
    if (params.size() > 3 && !params[3].isNull())
    {
        if (NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UpgradeIndex::UPGRADE_OVERWINTER))
        {
            const int64_t nExpiryHeight = params[3].get_int64();
            if (nExpiryHeight < 0 || nExpiryHeight >= TX_EXPIRY_HEIGHT_THRESHOLD) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, expiryheight must be nonnegative and less than %d.", TX_EXPIRY_HEIGHT_THRESHOLD));
            }
            // DoS mitigation: reject transactions expiring soon
            if (nExpiryHeight != 0 && nextBlockHeight + TX_EXPIRING_SOON_THRESHOLD > nExpiryHeight) {
                throw JSONRPCError(RPC_INVALID_PARAMETER,
                    strprintf("Invalid parameter, expiryheight should be at least %d to avoid transaction expiring soon",
                    nextBlockHeight + TX_EXPIRING_SOON_THRESHOLD));
            }
            rawTx.nExpiryHeight = static_cast<uint32_t>(nExpiryHeight);
        } else {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expiryheight can only be used if Overwinter is active when the transaction is mined");
        }
    }

    for (size_t idx = 0; idx < inputs.size(); idx++)
    {
        const UniValue& input = inputs[idx];
        const UniValue& o = input.get_obj();

        uint256 txid = ParseHashO(o, "txid");

        const UniValue& vout_v = find_value(o, "vout");
        if (!vout_v.isNum())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, missing vout key");
        int nOutput = vout_v.get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        uint32_t nSequence = (rawTx.nLockTime ? numeric_limits<uint32_t>::max() - 1 : numeric_limits<uint32_t>::max());

        // set the sequence number if passed in the parameters object
        const UniValue& sequenceObj = find_value(o, "sequence");
        if (sequenceObj.isNum())
            nSequence = sequenceObj.get_int();

        CTxIn in(COutPoint(txid, nOutput), CScript(), nSequence);

        rawTx.vin.push_back(in);
    }

    KeyIO keyIO(Params());
    set<CTxDestination> destinations;
    vector<string> addrList = sendTo.getKeys();
    for (const string& name_ : addrList) {
        CTxDestination destination = keyIO.DecodeDestination(name_);
        if (!IsValidDestination(destination)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Pastel address: ") + name_);
        }

        if (!destinations.insert(destination).second) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ") + name_);
        }

        CScript scriptPubKey = GetScriptForDestination(destination);
        CAmount nAmount = AmountFromValue(sendTo[name_]);

        CTxOut out(nAmount, scriptPubKey);
        rawTx.vout.push_back(out);
    }

    return EncodeHexTx(rawTx);
}

UniValue decoderawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(decoderawtransaction "hexstring"

Return a JSON object representing the serialized, hex-encoded transaction.

Arguments:
1. "hex"      (string, required) The transaction hex string

Result:
{
  "txid" : "id",          (string) The transaction id
  "size" : n,             (numeric) The transaction size
  "overwintered" : bool   (boolean) The Overwintered flag
  "version" : n,          (numeric) The version
  "versiongroupid":"hex"  (string, optional) The version group id (Overwintered txs)
  "locktime" : ttt,       (numeric) The lock time
  "expiryheight" : n,     (numeric, optional) Last valid block height for mining transaction (Overwintered txs)
  "vin" : [               (array of json objects)
     {
       "txid": "id",      (string) The transaction id
       "vout": n,         (numeric) The output number
       "scriptSig": {     (json object) The script
         "asm": "asm",    (string) asm
         "hex": "hex"     (string) hex
       },
       "sequence": n      (numeric) The script sequence number
     }
     ,...
  ],
  "vout" : [             (array of json objects)
     {
       "value" : x.xxx,          (numeric) The value in )" + CURRENCY_UNIT + R"(
       "n" : n,                  (numeric) index
       "scriptPubKey" : {        (json object)
         "asm" : "asm",          (string) the asm
         "hex" : "hex",          (string) the hex
         "reqSigs" : n,          (numeric) The required sigs
         "type" : "pubkeyhash",  (string) The type, eg 'pubkeyhash'
         "addresses" : [         (json array of string)
           "Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY"   (string) Pastel t-address
           ,...
         ]
       }
     }
     ,...
  ],
}

Examples:
)"+ HelpExampleCli("decoderawtransaction", "\"hexstring\"")
  + HelpExampleRpc("decoderawtransaction", "\"hexstring\"")
);

    LOCK(cs_main);
    RPCTypeCheck(params, {UniValue::VSTR});

    CTransaction tx;

    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    UniValue result(UniValue::VOBJ);
    TxToJSON(tx, uint256(), result);

    return result;
}

UniValue decodescript(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(decodescript "hex"

Decode a hex-encoded script.

Arguments:
1. "hex"     (string) the hex encoded script

Result:
{
  "asm":"asm",     (string) Script public key
  "hex":"hex",     (string) hex encoded public key
  "type":"type",   (string) The output type
  "reqSigs": n,    (numeric) The required signatures
  "addresses": [   (json array of strings)
     "address"     (string) Pastel address
     ,...
  ],
  "p2sh","address" (string) script address
}

Examples:
)" + HelpExampleCli("decodescript", "\"hexstring\"")
   + HelpExampleRpc("decodescript", "\"hexstring\"")
);

    LOCK(cs_main);
    RPCTypeCheck(params, {UniValue::VSTR});

    UniValue r(UniValue::VOBJ);
    CScript script;
    if (params[0].get_str().size() > 0){
        vector<unsigned char> scriptData(ParseHexV(params[0], "argument"));
        script = CScript(scriptData.begin(), scriptData.end());
    } else {
        // Empty scripts are valid
    }
    ScriptPubKeyToJSON(script, r, false);

    KeyIO keyIO(Params());
    r.pushKV("p2sh", keyIO.EncodeDestination(CScriptID(script)));
    return r;
}

/** Pushes a JSON object for script verification or signing errors to vErrorsRet. */
static void TxInErrorToJSON(const CTxIn& txin, UniValue& vErrorsRet, const string& strMessage)
{
    UniValue entry(UniValue::VOBJ);
    entry.pushKV("txid", txin.prevout.hash.ToString());
    entry.pushKV("vout", (uint64_t)txin.prevout.n);
    entry.pushKV("scriptSig", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
    entry.pushKV("sequence", (uint64_t)txin.nSequence);
    entry.pushKV("error", strMessage);
    vErrorsRet.push_back(entry);
}

UniValue signrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 5)
        throw runtime_error(
R"(signrawtransaction "hexstring" ( [{"txid":"id","vout":n,"scriptPubKey":"hex","redeemScript":"hex"},...] ["privatekey1",...] sighashtype )

Sign inputs for raw transaction (serialized, hex-encoded).
The second optional argument (may be null) is an array of previous transaction outputs that
this transaction depends on but may not yet be in the block chain.
The third optional argument (may be null) is an array of base58-encoded private
keys that, if given, will be the only keys used to sign the transaction.
)"
#ifdef ENABLE_WALLET
            + HelpRequiringPassphrase() + "\n"
#endif
R"(
Arguments:
1. "hexstring"     (string, required) The transaction hex string
2. "prevtxs"       (string, optional) An json array of previous dependent transaction outputs
     [               (json array of json objects, or 'null' if none provided)
       {
         "txid":"id",             (string, required) The transaction id
         "vout":n,                (numeric, required) The output number
         "scriptPubKey": "hex",   (string, required) script key
         "redeemScript": "hex",   (string, required for P2SH) redeem script
         "amount": value          (numeric, required) The amount spent
       }
       ,...
    ]
3. "privatekeys"     (string, optional) A json array of base58-encoded private keys for signing
    [                (json array of strings, or 'null' if none provided)
      "privatekey"   (string) private key in base58-encoding
      ,...
    ]
4. "sighashtype"     (string, optional, default=ALL) The signature hash type. Must be one of
       "ALL"
       "NONE"
       "SINGLE"
       "ALL|ANYONECANPAY"
       "NONE|ANYONECANPAY"
       "SINGLE|ANYONECANPAY"
5.  "branchid"       (string, optional) The hex representation of the consensus branch id to sign with.
    This can be used to force signing with consensus rules that are ahead of the node's current height.

Result:
{
  "hex" : "value",           (string) The hex-encoded raw transaction with signature(s)
  "complete" : true|false,   (boolean) If the transaction has a complete set of signatures
  "errors" : [               (json array of objects) Script verification errors (if there are any)
    {
      "txid" : "hash",       (string) The hash of the referenced, previous transaction
      "vout" : n,            (numeric) The index of the output to spent and used as input
      "scriptSig" : "hex",   (string) The hex-encoded signature script
      "sequence" : n,        (numeric) Script sequence number
      "error" : "text"       (string) Verification or signing error related to the input
    }
    ,...
  ]
}

Examples:
)"  + HelpExampleCli("signrawtransaction", "\"myhex\"")
    + HelpExampleRpc("signrawtransaction", "\"myhex\"")
);

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : nullptr);
#else
    LOCK(cs_main);
#endif
    RPCTypeCheck(params, {UniValue::VSTR, UniValue::VARR, UniValue::VARR, UniValue::VSTR, UniValue::VSTR}, true);

    v_uint8 txData(ParseHexV(params[0], "argument 1"));
    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    vector<CMutableTransaction> txVariants;
    while (!ssData.empty()) {
        try {
            CMutableTransaction tx;
            ssData >> tx;
            txVariants.push_back(tx);
        }
        catch (const exception&) {
            throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
        }
    }

    if (txVariants.empty())
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Missing transaction");

    // mergedTx will end up with all the signatures; it
    // starts as a clone of the rawtx:
    CMutableTransaction mergedTx(txVariants[0]);

    // Fetch previous transactions (inputs):
    CCoinsView viewDummy;
    CCoinsViewCache view(&viewDummy);
    {
        LOCK(mempool.cs);
        CCoinsViewCache &viewChain = *pcoinsTip;
        CCoinsViewMemPool viewMempool(&viewChain, mempool);
        view.SetBackend(viewMempool); // temporarily switch cache backend to db+mempool view

        for (const auto& txin : mergedTx.vin)
        {
            const uint256& prevHash = txin.prevout.hash;
            CCoins coins;
            view.AccessCoins(prevHash); // this is certainly allowed to fail
        }

        view.SetBackend(viewDummy); // switch back to avoid locking mempool for too long
    }

    KeyIO keyIO(Params());

    bool fGivenKeys = false;
    CBasicKeyStore tempKeystore;
    if (params.size() > 2 && !params[2].isNull())
    {
        fGivenKeys = true;
        UniValue keys = params[2].get_array();
        string sKeyError;
        for (size_t idx = 0; idx < keys.size(); idx++)
        {
            const UniValue k = keys[idx];
            const CKey key = keyIO.DecodeSecret(k.get_str(), sKeyError);
            if (!key.IsValid())
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, tfm::format("Invalid private key, %s", sKeyError.c_str()));
            tempKeystore.AddKey(key);
        }
    }
#ifdef ENABLE_WALLET
    else if (pwalletMain)
        EnsureWalletIsUnlocked();
#endif

    // Add previous txouts given in the RPC call:
    if (params.size() > 1 && !params[1].isNull())
    {
        UniValue prevTxs = params[1].get_array();
        for (size_t idx = 0; idx < prevTxs.size(); idx++)
        {
            const UniValue& p = prevTxs[idx];
            if (!p.isObject())
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "expected object with {\"txid'\",\"vout\",\"scriptPubKey\"}");

            UniValue prevOut = p.get_obj();

            RPCTypeCheckObj(prevOut, 
                {
                    {"txid", UniValue::VSTR},
                    {"vout", UniValue::VNUM},
                    {"scriptPubKey", UniValue::VSTR}
                });

            uint256 txid = ParseHashO(prevOut, "txid");

            int nOut = find_value(prevOut, "vout").get_int();
            if (nOut < 0)
                throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "vout must be positive");

            vector<unsigned char> pkData(ParseHexO(prevOut, "scriptPubKey"));
            CScript scriptPubKey(pkData.begin(), pkData.end());

            {
                CCoinsModifier coins = view.ModifyCoins(txid);
                if (coins->IsAvailable(nOut) && coins->vout[nOut].scriptPubKey != scriptPubKey) {
                    string err("Previous output scriptPubKey mismatch:\n");
                    err = err + ScriptToAsmStr(coins->vout[nOut].scriptPubKey) + "\nvs:\n"+
                        ScriptToAsmStr(scriptPubKey);
                    throw JSONRPCError(RPC_DESERIALIZATION_ERROR, err);
                }
                if ((unsigned int)nOut >= coins->vout.size())
                    coins->vout.resize(nOut+1);
                coins->vout[nOut].scriptPubKey = scriptPubKey;
                coins->vout[nOut].nValue = 0;
                if (prevOut.exists("amount")) {
                    coins->vout[nOut].nValue = AmountFromValue(find_value(prevOut, "amount"));
                }
            }

            // if redeemScript given and not using the local wallet (private keys
            // given), add redeemScript to the tempKeystore so it can be signed:
            if (fGivenKeys && scriptPubKey.IsPayToScriptHash()) {
                RPCTypeCheckObj(prevOut,
                    {
                        {"txid", UniValue::VSTR},
                        {"vout", UniValue::VNUM},
                        {"scriptPubKey", UniValue::VSTR},
                        {"redeemScript", UniValue::VSTR}
                    });
                UniValue v = find_value(prevOut, "redeemScript");
                if (!v.isNull()) {
                    vector<unsigned char> rsData(ParseHexV(v, "redeemScript"));
                    CScript redeemScript(rsData.begin(), rsData.end());
                    tempKeystore.AddCScript(redeemScript);
                }
            }
        }
    }

#ifdef ENABLE_WALLET
    const CKeyStore& keystore = ((fGivenKeys || !pwalletMain) ? tempKeystore : *pwalletMain);
#else
    const CKeyStore& keystore = tempKeystore;
#endif

    uint8_t nHashType = to_integral_type(SIGHASH::ALL);
    if (params.size() > 3 && !params[3].isNull())
    {
        static unordered_map<string, uint8_t> mapSigHashValues =
            {
                {                 "ALL", to_integral_type(SIGHASH::ALL)},
                {    "ALL|ANYONECANPAY", enum_or(SIGHASH::ALL, SIGHASH::ANYONECANPAY)},
                {                "NONE", to_integral_type(SIGHASH::NONE)},
                {   "NONE|ANYONECANPAY", enum_or(SIGHASH::NONE, SIGHASH::ANYONECANPAY)},
                {              "SINGLE", to_integral_type(SIGHASH::SINGLE)},
                { "SINGLE|ANYONECANPAY", enum_or(SIGHASH::SINGLE, SIGHASH::ANYONECANPAY)}
            };
        string strHashType = params[3].get_str();
        if (mapSigHashValues.count(strHashType))
            nHashType = mapSigHashValues[strHashType];
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid sighash param");
    }

    const bool fHashSingle = ((nHashType & ~to_integral_type(SIGHASH::ANYONECANPAY)) == to_integral_type(SIGHASH::SINGLE));
    // Use the approximate release height if it is greater so offline nodes 
    // have a better estimation of the current height and will be more likely to
    // determine the correct consensus branch ID.  Regtest mode ignores release height.
    unsigned int chainHeight = chainActive.Height() + 1;
    if (!Params().IsRegTest())
        chainHeight = max(chainHeight, APPROX_RELEASE_HEIGHT);
    // Grab the current consensus branch ID
    auto consensusBranchId = CurrentEpochBranchId(chainHeight, Params().GetConsensus());

    if (params.size() > 4 && !params[4].isNull()) {
        consensusBranchId = ParseHexToUInt32(params[4].get_str());
        if (!IsConsensusBranchId(consensusBranchId)) {
            throw runtime_error(params[4].get_str() + " is not a valid consensus branch id");
        }
    } 
    
    // Script verification errors
    UniValue vErrors(UniValue::VARR);

    // Use CTransaction for the constant parts of the
    // transaction to avoid rehashing.
    const CTransaction txConst(mergedTx);
    // Sign what we can:
    for (unsigned int i = 0; i < mergedTx.vin.size(); i++)
    {
        CTxIn& txin = mergedTx.vin[i];
        const CCoins* coins = view.AccessCoins(txin.prevout.hash);
        if (!coins || !coins->IsAvailable(txin.prevout.n)) {
            TxInErrorToJSON(txin, vErrors, "Input not found or already spent");
            continue;
        }
        const CScript& prevPubKey = coins->vout[txin.prevout.n].scriptPubKey;
        const CAmount& amount = coins->vout[txin.prevout.n].nValue;

        SignatureData sigdata;
        // Only sign SIGHASH_SINGLE if there's a corresponding output:
        if (!fHashSingle || (i < mergedTx.vout.size()))
            ProduceSignature(MutableTransactionSignatureCreator(&keystore, &mergedTx, i, amount, nHashType), prevPubKey, sigdata, consensusBranchId);

        // ... and merge in other signatures:
        for (const auto& txv : txVariants)
            sigdata = CombineSignatures(prevPubKey, TransactionSignatureChecker(&txConst, i, amount), sigdata, DataFromTransaction(txv, i), consensusBranchId);

        UpdateTransaction(mergedTx, i, sigdata);

        ScriptError serror = SCRIPT_ERR_OK;
        if (!VerifyScript(txin.scriptSig, prevPubKey, STANDARD_SCRIPT_VERIFY_FLAGS, TransactionSignatureChecker(&txConst, i, amount), consensusBranchId, &serror))
            TxInErrorToJSON(txin, vErrors, ScriptErrorString(serror));
    }
    bool fComplete = vErrors.empty();

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(mergedTx));
    result.pushKV("complete", fComplete);
    if (!vErrors.empty())
        result.pushKV("errors", vErrors);
    return result;
}

UniValue sendrawtransaction(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
R"(sendrawtransaction "hexstring" ( allowhighfees )
Submits raw transaction (serialized, hex-encoded) to local node and network.
Also see createrawtransaction and signrawtransaction calls.

Arguments:
1. "hexstring"    (string, required) The hex string of the raw transaction)
2. allowhighfees  (boolean, optional, default=false) Allow high fees

Result:
hex\"             (string) The transaction hash in hex

Examples:
Create a transaction
)" + HelpExampleCli("createrawtransaction", "\"[{\\\"txid\\\" : \\\"mytxid\\\",\\\"vout\\\":0}]\" \"{\\\"myaddress\\\":0.01}\"") + R"(
Sign the transaction, and get back the hex
)" + HelpExampleCli("signrawtransaction", "\"myhex\"") + R"(
Send the transaction (signed hex)
)" + HelpExampleCli("sendrawtransaction", "\"signedhex\"") + R"(
As a json rpc call
)" + HelpExampleRpc("sendrawtransaction", "\"signedhex\"")
);

    LOCK(cs_main);
    RPCTypeCheck(params, {UniValue::VSTR, UniValue::VBOOL});

    // parse hex string from parameter
    CTransaction tx;
    if (!DecodeHexTx(tx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");
    const auto &txid = tx.GetHash();

    auto chainparams = Params();

    // DoS mitigation: reject transactions expiring soon
    if (tx.nExpiryHeight > 0)
    {
        uint32_t nextBlockHeight = chainActive.Height() + 1;
        if (NetworkUpgradeActive(nextBlockHeight, chainparams.GetConsensus(), Consensus::UpgradeIndex::UPGRADE_OVERWINTER))
        {
            if (nextBlockHeight + TX_EXPIRING_SOON_THRESHOLD > tx.nExpiryHeight) {
                throw JSONRPCError(RPC_TRANSACTION_REJECTED,
                    strprintf("tx-expiring-soon: expiryheight is %u but should be at least %d to avoid transaction expiring soon",
                    tx.nExpiryHeight,
                    nextBlockHeight + TX_EXPIRING_SOON_THRESHOLD));
            }
        }
    }

    bool fOverrideFees = false;
    if (params.size() > 1)
        fOverrideFees = params[1].get_bool();

    CCoinsViewCache &view = *pcoinsTip;
    const CCoins* existingCoins = view.AccessCoins(txid);
    bool fHaveMempool = mempool.exists(txid);
    bool fHaveChain = existingCoins && existingCoins->nHeight < 1000000000;
    if (!fHaveMempool && !fHaveChain) {
        // push to local node and sync with wallets
        CValidationState state;
        bool fMissingInputs;
        if (!AcceptToMemoryPool(chainparams, mempool, state, tx, false, &fMissingInputs, !fOverrideFees)) {
            if (state.IsInvalid()) {
                throw JSONRPCError(RPC_TRANSACTION_REJECTED, strprintf("%i: %s", state.GetRejectCode(), state.GetRejectReason()));
            } else {
                if (fMissingInputs) {
                    throw JSONRPCError(RPC_TRANSACTION_ERROR, "Missing inputs");
                }
                throw JSONRPCError(RPC_TRANSACTION_ERROR, state.GetRejectReason());
            }
        }
    } else if (fHaveChain) {
        throw JSONRPCError(RPC_TRANSACTION_ALREADY_IN_CHAIN, "transaction already in block chain");
    }
    RelayTransaction(tx);

    return txid.GetHex();
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "rawtransactions",    "getrawtransaction",      &getrawtransaction,      true  },
    { "rawtransactions",    "createrawtransaction",   &createrawtransaction,   true  },
    { "rawtransactions",    "decoderawtransaction",   &decoderawtransaction,   true  },
    { "rawtransactions",    "decodescript",           &decodescript,           true  },
    { "rawtransactions",    "sendrawtransaction",     &sendrawtransaction,     false },
    { "rawtransactions",    "signrawtransaction",     &signrawtransaction,     false }, /* uses wallet if enabled */

    { "blockchain",         "gettxoutproof",          &gettxoutproof,          true  },
    { "blockchain",         "verifytxoutproof",       &verifytxoutproof,       true  },
};

void RegisterRawTransactionRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
