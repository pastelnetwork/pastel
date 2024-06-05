// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2024 The Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.
#include <cstdint>
#include <variant>

#include <univalue.h>

#include <utils/util.h>
#include <clientversion.h>
#include <init.h>
#include <key_io.h>
#include <main.h>
#include <net.h>
#include <netbase.h>
#include <rpc/server.h>
#include <timedata.h>
#include <txmempool.h>
#include <chain_options.h>
#include <script/scripttype.h>
#ifdef ENABLE_WALLET
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#endif
#include <netmsg/nodemanager.h>

#include <zcash/Address.hpp>

using namespace std;

/**
 * @note Do not add or change anything in the information returned by this
 * method. `getinfo` exists for backwards-compatibility only. It combines
 * information from wildly different sources in the program, which is a mess,
 * and is thus planned to be deprecated eventually.
 *
 * Based on the source of the information, new information should be added to:
 * - `getblockchaininfo`,
 * - `getnetworkinfo` or
 * - `getwalletinfo`
 *
 * Or alternatively, create a specific query method for the information.
 **/
UniValue getinfo(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getinfo

Returns an object containing various state info.

Result:
{
  "version": xxxxx,           (numeric) the server version
  "protocolversion": xxxxx,   (numeric) the protocol version
  "walletversion": xxxxx,     (numeric) the wallet version
  "balance": xxxxxxx,         (numeric) the total Pastel balance of the wallet
  "blocks": xxxxxx,           (numeric) the current number of blocks processed in the server
  "timeoffset": xxxxx,        (numeric) the time offset
  "connections": xxxxx,       (numeric) the number of connections
  "proxy": "host:port",       (string, optional) the proxy used by the server
  "difficulty": xxxxxx,       (numeric) the current difficulty
  "chain": "xxxx",          (string) current network name (mainnet, testnet, devnet, regtest)
  "keypoololdest": xxxxxx,    (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool
  "keypoolsize": xxxx,        (numeric) how many new keys are pre-generated
  "unlocked_until": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked
  "paytxfee": x.xxxx,         (numeric) the transaction fee set in )" + CURRENCY_UNIT + R"(/kB
  "relayfee": x.xxxx,         (numeric) minimum relay fee for non-free transactions in )" + CURRENCY_UNIT + R"(/kB
  "errors": "..."             (string) any error messages
}

Examples:
)"
+ HelpExampleCli("getinfo", "")
+ HelpExampleRpc("getinfo", "")
);

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : nullptr);
#else
    LOCK(cs_main);
#endif

    proxyType proxy;
    GetProxy(NET_IPV4, proxy);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("version", CLIENT_VERSION);
    obj.pushKV("protocolversion", PROTOCOL_VERSION);
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.pushKV("walletversion", pwalletMain->GetVersion());
        obj.pushKV("balance",       ValueFromAmount(pwalletMain->GetBalance()));
    }
#endif
    obj.pushKV("blocks",        static_cast<uint64_t>(gl_nChainHeight.load()));
    obj.pushKV("timeoffset",    GetTimeOffset());
    obj.pushKV("connections",   static_cast<uint64_t>(gl_NodeManager.GetNodeCount()));
    obj.pushKV("proxy",         (proxy.IsValid() ? proxy.proxy.ToStringIPPort() : string()));
    obj.pushKV("difficulty",    GetDifficulty());
    obj.pushKV("chain",         Params().NetworkIDString());
#ifdef ENABLE_WALLET
    if (pwalletMain)
    {
        obj.pushKV("keypoololdest", pwalletMain->GetOldestKeyPoolTime());
        obj.pushKV("keypoolsize",   static_cast<uint64_t>(pwalletMain->GetKeyPoolSize()));
    }
    if (pwalletMain && pwalletMain->IsCrypted())
        obj.pushKV("unlocked_until", nWalletUnlockTime);
    obj.pushKV("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK()));
#endif
    obj.pushKV("relayfee",      ValueFromAmount(gl_ChainOptions.minRelayTxFee.GetFeePerK()));
    obj.pushKV("errors",        GetWarnings("statusbar"));
    return obj;
}

#ifdef ENABLE_WALLET
class DescribeAddressVisitor
{
public:
    UniValue operator()(const CNoDestination &dest) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const CKeyID &keyID) const {
        UniValue obj(UniValue::VOBJ);
        CPubKey vchPubKey;
        obj.pushKV("isscript", false);
        if (pwalletMain && pwalletMain->GetPubKey(keyID, vchPubKey)) {
            obj.pushKV("pubkey", HexStr(vchPubKey));
            obj.pushKV("iscompressed", vchPubKey.IsCompressed());
        }
        return obj;
    }

    UniValue operator()(const CScriptID &scriptID) const {
        KeyIO keyIO(Params());
        UniValue obj(UniValue::VOBJ);
        CScript subscript;
        obj.pushKV("isscript", true);
        if (pwalletMain && pwalletMain->GetCScript(scriptID, subscript))
        {
            txdest_vector_t addresses;
            txnouttype whichType;
            int nRequired;
            ExtractDestinations(subscript, whichType, addresses, nRequired);
            obj.pushKV("script", GetTxnOutputType(whichType));
            obj.pushKV("hex", HexStr(subscript.begin(), subscript.end()));
            UniValue a(UniValue::VARR);
            for (const CTxDestination& addr : addresses) {
                a.push_back(keyIO.EncodeDestination(addr));
            }
            obj.pushKV("addresses", a);
            if (whichType == TX_MULTISIG)
                obj.pushKV("sigsrequired", nRequired);
        }
        return obj;
    }
};
#endif

UniValue validateaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(validateaddress "t-address"

Return information about the given Pastel address.

Arguments:
1. "t-address"     (string, required) The Pastel transparent address to validate

Result:
{
  "isvalid" : true|false,       (boolean) If the address is valid or not. If not, this is the only property returned.
  "address" : "t-address",      (string) The Pastel transparent address validated
  "scriptPubKey" : "hex",       (string) The hex encoded scriptPubKey generated by the address
  "ismine" : true|false,        (boolean) If the address is yours or not
  "isscript" : true|false,      (boolean) If the key is a script
  "pubkey" : "publickeyhex",    (string) The hex value of the raw public key
  "iscompressed" : true|false,  (boolean) If the address is compressed
  "account" : "account"         (string) DEPRECATED. The account associated with the address, "" is the default account
}

Examples:
)"
+ HelpExampleCli("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
+ HelpExampleRpc("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
);

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : nullptr);
#else
    LOCK(cs_main);
#endif

    KeyIO keyIO(Params());
    CTxDestination dest = keyIO.DecodeDestination(params[0].get_str());
    bool isValid = IsValidDestination(dest);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", isValid);
    if (isValid)
    {
        string currentAddress = keyIO.EncodeDestination(dest);
        ret.pushKV("address", currentAddress);

        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.pushKV("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

#ifdef ENABLE_WALLET
        isminetype mine = pwalletMain ? GetIsMine(*pwalletMain, dest) : isminetype::NO;
        ret.pushKV("ismine", IsMineSpendable(mine) ? true : false);
        ret.pushKV("iswatchonly", IsMineWatchOnly(mine) ? true: false);
        UniValue detail = visit(DescribeAddressVisitor(), dest);
        ret.pushKVs(detail);
        if (pwalletMain && pwalletMain->mapAddressBook.count(dest))
            ret.pushKV("account", pwalletMain->mapAddressBook[dest].name);
#endif
    }
    return ret;
}


class DescribePaymentAddressVisitor
{
public:
    UniValue operator()(const libzcash::InvalidEncoding &zaddr) const { return UniValue(UniValue::VOBJ); }

    UniValue operator()(const libzcash::SaplingPaymentAddress &zaddr) const {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("type", "sapling");
        obj.pushKV("diversifier", HexStr(zaddr.d));
        obj.pushKV("diversifiedtransmissionkey", zaddr.pk_d.GetHex());
#ifdef ENABLE_WALLET
        if (pwalletMain) {
            obj.pushKV("ismine", HaveSpendingKeyForPaymentAddress(pwalletMain)(zaddr));
        }
#endif
        return obj;
    }
};

UniValue z_validateaddress(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(z_validateaddress "zaddr"

Return information about the given z address.

Arguments:
1. "zaddr"     (string, required) The z address to validate

Result:
{
  "isvalid" : true|false,       (boolean) If the address is valid or not. If not, this is the only property returned.
  "address" : "zaddr",          (string) The z address validated
  "type" : "xxxx",              (string) "sprout" or "sapling"
  "ismine" : true|false,        (boolean) If the address is yours or not
  "payingkey" : "hex",          (string) [sprout] The hex value of the paying key, a_pk
  "transmissionkey" : "hex",    (string) [sprout] The hex value of the transmission key, pk_enc
  "diversifier" : "hex",        (string) [sapling] The hex value of the diversifier, d
  "diversifiedtransmissionkey" :"hex", (string) [sapling] The hex value of pk_d

}

Examples:
)"
+ HelpExampleCli("z_validateaddress", "\"PzWcy67ygestjagHaFZxjWxmawMeShmQWNPE8FNJp23pQS2twecwps5223ajUtN7iihxR4MmLDFQ19heHkBx5AKaDooS6aQ\"")
+ HelpExampleRpc("z_validateaddress", "\"PzWcy67ygestjagHaFZxjWxmawMeShmQWNPE8FNJp23pQS2twecwps5223ajUtN7iihxR4MmLDFQ19heHkBx5AKaDooS6aQ\"")
);

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain->cs_wallet);
#else
    LOCK(cs_main);
#endif

    KeyIO keyIO(Params());
    string strAddress = params[0].get_str();
    auto address = keyIO.DecodePaymentAddress(strAddress);
    bool isValid = IsValidPaymentAddress(address);

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("isvalid", isValid);
    if (isValid)
    {
        ret.pushKV("address", strAddress);
        UniValue detail = visit(DescribePaymentAddressVisitor(), address);
        ret.pushKVs(detail);
    }
    return ret;
}


/**
 * Used by addmultisigaddress / createmultisig:
 */
CScript _createmultisig_redeemScript(const UniValue& params)
{
    int nRequired = params[0].get_int();
    const UniValue& keys = params[1].get_array();

    // Gather public keys
    if (nRequired < 1)
        throw runtime_error("a multisignature address must require at least one key to redeem");
    if ((int)keys.size() < nRequired)
        throw runtime_error(
            strprintf("not enough keys supplied "
                      "(got %u keys, but need at least %d to redeem)", keys.size(), nRequired));
    if (keys.size() > 16)
        throw runtime_error("Number of addresses involved in the multisignature address creation > 16\nReduce the number");

    KeyIO keyIO(Params());

    vector<CPubKey> pubkeys;
    pubkeys.resize(keys.size());
    for (unsigned int i = 0; i < keys.size(); i++)
    {
        const string& ks = keys[i].get_str();
#ifdef ENABLE_WALLET
        // Case 1: Bitcoin address and we have full public key:
        CTxDestination dest = keyIO.DecodeDestination(ks);
        if (pwalletMain && IsValidDestination(dest)) {
            const CKeyID *keyID = get_if<CKeyID>(&dest);
            if (!keyID) {
                throw runtime_error(strprintf("%s does not refer to a key", ks));
            }
            CPubKey vchPubKey;
            if (!pwalletMain->GetPubKey(*keyID, vchPubKey)) {
                throw runtime_error(strprintf("no full public key for address %s", ks));
            }
            if (!vchPubKey.IsFullyValid())
                throw runtime_error(" Invalid public key: "+ks);
            pubkeys[i] = vchPubKey;
        }

        // Case 2: hex public key
        else
#endif
        if (IsHex(ks))
        {
            CPubKey vchPubKey(ParseHex(ks));
            if (!vchPubKey.IsFullyValid())
                throw runtime_error(" Invalid public key: "+ks);
            pubkeys[i] = vchPubKey;
        }
        else
        {
            throw runtime_error(" Invalid public key: "+ks);
        }
    }
    CScript result = GetScriptForMultisig(nRequired, pubkeys);

    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE)
        throw runtime_error(
                strprintf("redeemScript exceeds size limit: %d > %d", result.size(), MAX_SCRIPT_ELEMENT_SIZE));

    return result;
}

UniValue createmultisig(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() < 2 || params.size() > 2)
        throw runtime_error(
R"(createmultisig nrequired ["key",...]
    
Creates a multi-signature address with n signature of m keys required.
It returns a json object with the address and redeemScript.

Arguments:
1. nrequired    (numeric, required) The number of required signatures out of the n keys or addresses.
2. "keys"       (string, required) A json array of keys which are Pastel addresses or hex-encoded public keys
     [
       "key"    (string) Pastel address or hex-encoded public key
       ,...
     ]

Result:
{
  "address":"multisigaddress",  (string) The value of the new multisig address.
  "redeemScript":"script"       (string) The string value of the hex-encoded redemption script.
}

Examples:

Create a multisig address from 2 addresses
)" + HelpExampleCli("createmultisig", "2 \"[\\\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\\\",\\\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\\\"]\"") + R"(
As a json rpc call
)" + HelpExampleRpc("createmultisig", "2, \"[\\\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\\\",\\\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\\\"]\"")
);

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(params);
    CScriptID innerID(inner);

    KeyIO keyIO(Params());
    UniValue result(UniValue::VOBJ);
    result.pushKV("address", keyIO.EncodeDestination(innerID));
    result.pushKV("redeemScript", HexStr(inner.begin(), inner.end()));

    return result;
}

UniValue verifymessage(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
R"(verifymessage "t-address" "signature" "message"

Verify a signed message

Arguments:
1. "t-address"    (string, required) The Pastel transparent address to use for the signature.
2. "signature"    (string, required) The signature provided by the signer in base 64 encoding (see signmessage).
3. "message"      (string, required) The message that was signed.

Result:
true|false   (boolean) If the signature is verified or not.

Examples:
Unlock the wallet for 30 seconds
)" + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") + R"(
Create the signature
)" + HelpExampleCli("signmessage", "\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\" \"my message\"") + R"(
Verify the signature
)" + HelpExampleCli("verifymessage", "\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\" \"signature\" \"my message\"") + R"(
As json rpc
)" + HelpExampleRpc("verifymessage", "\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\", \"signature\", \"my message\"")
);

    LOCK(cs_main);

    string strAddress  = params[0].get_str();
    string strSign     = params[1].get_str();
    string strMessage  = params[2].get_str();

    KeyIO keyIO(Params());
    CTxDestination destination = keyIO.DecodeDestination(strAddress);
    if (!IsValidDestination(destination)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    const CKeyID *keyID = get_if<CKeyID>(&destination);
    if (!keyID) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    }

    bool fInvalid = false;
    auto vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

    if (fInvalid)
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Malformed base64 encoding");

    CHashWriter ss(SER_GETHASH, 0);
    ss << STR_MSG_MAGIC;
    ss << strMessage;

    CPubKey pubkey;
    if (!pubkey.RecoverCompact(ss.GetHash(), vchSig))
        return false;

    return (pubkey.GetID() == *keyID);
}

UniValue setmocktime(const UniValue& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(setmocktime timestamp

Set the local time to given timestamp (-regtest only)

Arguments:
1. timestamp  (integer, required) Unix seconds-since-epoch timestamp
   Pass 0 to go back to using the system time.

Examples:
)"
+ HelpExampleCli("setmocktime", "")
+ HelpExampleRpc("setmocktime", "")
);

    if (!Params().MineBlocksOnDemand())
        throw runtime_error("setmocktime for regression testing (-regtest mode) only");

    // cs_vNodes is locked and node send/receive times are updated
    // atomically with the time change to prevent peers from being
    // disconnected because we think we haven't communicated with them
    // in a long time.
    LOCK(cs_main);

    RPCTypeCheck(params, {UniValue::VNUM});
    SetMockTime(params[0].get_int64());

    gl_NodeManager.UpdateNodesSendRecvTime(GetTime());
    return NullUniValue;
}

static UniValue RPCLockedMemoryInfo()
{
    LockedPool::Stats stats = LockedPoolManager::Instance().stats();
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("used", uint64_t(stats.used));
    obj.pushKV("free", uint64_t(stats.free));
    obj.pushKV("total", uint64_t(stats.total));
    obj.pushKV("locked", uint64_t(stats.locked));
    obj.pushKV("chunks_used", uint64_t(stats.chunks_used));
    obj.pushKV("chunks_free", uint64_t(stats.chunks_free));
    return obj;
}

UniValue getmemoryinfo(const UniValue& params, bool fHelp)
{
    /* Please, avoid using the word "pool" here in the RPC interface or help,
     * as users will undoubtedly confuse it with the other "memory pool"
     */
    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getmemoryinfo

Returns an object containing information about memory usage.

Result:
{
  "locked": {               (json object) Information about locked memory manager
    "used": xxxxx,          (numeric) Number of bytes used
    "free": xxxxx,          (numeric) Number of bytes available in current arenas
    "total": xxxxxxx,       (numeric) Total number of bytes managed
    "locked": xxxxxx,       (numeric) Amount of bytes that succeeded locking. If this number is smaller than total, locking pages failed at some point and key data could be swapped to disk.
    "chunks_used": xxxxx,   (numeric) Number allocated chunks
    "chunks_free": xxxxx,   (numeric) Number unused chunks
  }
}

Examples:
)"
+ HelpExampleCli("getmemoryinfo", "")
+ HelpExampleRpc("getmemoryinfo", "")
);
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("locked", RPCLockedMemoryInfo());
    return obj;
}

// insightexplorer
static bool getAddressFromIndex(
    ScriptType type, const uint160 &hash, string &address)
{
    KeyIO keyIO(Params());
    if (type == ScriptType::P2SH) {
        address = keyIO.EncodeDestination(CScriptID(hash));
    } else if (type == ScriptType::P2PKH) {
        address = keyIO.EncodeDestination(CKeyID(hash));
    } else {
        return false;
    }
    return true;
}

// This function accepts an address and returns in the output parameters
// the version and raw bytes for the RIPEMD-160 hash.
static bool getIndexKey(const CTxDestination& dest, uint160& hashBytes, ScriptType& type)
{
    if (!IsValidDestination(dest))
        return false;
    if (IsKeyDestination(dest))
    {
        auto x = get_if<CKeyID>(&dest);
        memcpy(&hashBytes, x->begin(), 20);
        type = ScriptType::P2PKH;
        return true;
    }
    if (IsScriptDestination(dest))
    {
        auto x = get_if<CScriptID>(&dest);
        memcpy(&hashBytes, x->begin(), 20);
        type = ScriptType::P2SH;
        return true;
    }
    return false;
}

// insightexplorer
static bool getAddressesFromParams(
    const UniValue& params,
    vector<pair<uint160, ScriptType>> &addresses)
{
    v_strings param_addresses;
    if (params[0].isStr()) {
        param_addresses.push_back(params[0].get_str());
    } else if (params[0].isObject()) {
        UniValue addressValues = find_value(params[0].get_obj(), "addresses");
        if (!addressValues.isArray()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                "Addresses is expected to be an array");
        }
        for (const auto& it : addressValues.getValues()) {
            param_addresses.push_back(it.get_str());
        }
    } else
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");

    KeyIO keyIO(Params());
    for (const auto& it : param_addresses)
    {
        CTxDestination address = keyIO.DecodeDestination(it);
        uint160 hashBytes;
        ScriptType type = ScriptType::UNKNOWN;
        if (!getIndexKey(address, hashBytes, type))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address" + params[0].get_str());
        addresses.push_back(make_pair(move(hashBytes), move(type)));
    }
    return true;
}

UniValue getaddressmempool(const UniValue& params, bool fHelp)
{
    const string enableArg = "insightexplorer";
    const bool fEnableGetAddressMempool = fExperimentalMode && fInsightExplorer;
    string disabledMsg = "";
    if (!fEnableGetAddressMempool) {
        disabledMsg = experimentalDisabledHelpMsg("getaddressmempool", enableArg);
    }
    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getaddressmempool {addresses: [taddr, ...]}

Returns all mempool deltas for an address.)"
+ disabledMsg +
R"(Arguments:
{
  addresses:
    [
      address   (string) The base58check encoded address
      ,...
    ]
}
(or)
address   (string) The base58check encoded address
Result:
[
  {
    address     (string) The base58check encoded address
    txid        (string) The related txid
    index       (number) The related input or output index
    patoshis    (number) The difference of patoshis
    timestamp   (number) The time the transaction entered the mempool (seconds)
    prevtxid    (string) The previous txid (if spending)
    prevout     (string) The previous transaction output index (if spending)
  }
]

Examples:)"
            + HelpExampleCli("getaddressmempool", "'{\"addresses\": [\"tPp3pfmLi57S8qoccfWnn2o4tXyoQ23wVSp\"]}'")
            + HelpExampleRpc("getaddressmempool", "{\"addresses\": [\"tPp3pfmLi57S8qoccfWnn2o4tXyoQ23wVSp\"]}")
        );

    if (!fEnableGetAddressMempool) {
        throw JSONRPCError(RPC_MISC_ERROR, "Error: getaddressmempool is disabled. "
            "Run './pastel-cli help getaddressmempool' for instructions on how to enable this feature.");
    }

    vector<pair<uint160, ScriptType>> addresses;

    if (!getAddressesFromParams(params, addresses))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");

    vector<pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>> indexes;
    mempool.getAddressIndex(addresses, indexes);
    sort(indexes.begin(), indexes.end(),
        [](const pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>& a,
           const pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>& b) -> bool {
               return a.second.time < b.second.time;
           });

    UniValue result(UniValue::VARR);

    for (const auto& it : indexes) {
        string address;
        if (!getAddressFromIndex(it.first.type, it.first.addressBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }
        UniValue delta(UniValue::VOBJ);
        delta.pushKV("address", move(address));
        delta.pushKV("txid", it.first.txhash.GetHex());
        delta.pushKV("index", (int)it.first.index);
        delta.pushKV("patoshis", it.second.amount);
        delta.pushKV("timestamp", it.second.time);
        if (it.second.amount < 0) {
            delta.pushKV("prevtxid", it.second.prevhash.GetHex());
            delta.pushKV("prevout", (int)it.second.prevout);
        }
        result.push_back(move(delta));
    }
    return result;
}

// insightexplorer
UniValue getaddressutxos(const UniValue& params, bool fHelp)
{
    const bool fEnableInsightExplorer = fExperimentalMode && fInsightExplorer;
    std::string disabledMsg = "";
    if (!fEnableInsightExplorer) {
        disabledMsg = experimentalDisabledHelpMsg("getaddressutxos", {"insightexplorer"});
    }
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "getaddressutxos {\"addresses\": [\"taddr\", ...], (\"chainInfo\": true|false)}\n"
                "\nReturns all unspent outputs for an address.\n"
                + disabledMsg +
                "\nArguments:\n"
                "{\n"
                "  \"addresses\":\n"
                "    [\n"
                "      \"address\"  (string) The base58check encoded address\n"
                "      ,...\n"
                "    ],\n"
                "  \"chainInfo\"  (boolean, optional, default=false) Include chain info with results\n"
                "}\n"
                "(or)\n"
                "\"address\"  (string) The base58check encoded address\n"
                "\nResult\n"
                "[\n"
                "  {\n"
                "    \"address\"  (string) The address base58check encoded\n"
                "    \"txid\"  (string) The output txid\n"
                "    \"height\"  (number) The block height\n"
                "    \"outputIndex\"  (number) The output index\n"
                "    \"script\"  (string) The script hex encoded\n"
                "    \"satoshis\"  (number) The number of " + MINOR_CURRENCY_UNIT + " of the output\n"
                                                                                    "  }, ...\n"
                                                                                    "]\n\n"
                                                                                    "(or, if chainInfo is true):\n\n"
                                                                                    "{\n"
                                                                                    "  \"utxos\":\n"
                                                                                    "    [\n"
                                                                                    "      {\n"
                                                                                    "        \"address\"     (string)  The address base58check encoded\n"
                                                                                    "        \"txid\"        (string)  The output txid\n"
                                                                                    "        \"height\"      (number)  The block height\n"
                                                                                    "        \"outputIndex\" (number)  The output index\n"
                                                                                    "        \"script\"      (string)  The script hex encoded\n"
                                                                                    "        \"satoshis\"    (number)  The number of " + MINOR_CURRENCY_UNIT + " of the output\n"
                                                                                                                                                               "      }, ...\n"
                                                                                                                                                               "    ],\n"
                                                                                                                                                               "  \"hash\"              (string)  The block hash\n"
                                                                                                                                                               "  \"height\"            (numeric) The block height\n"
                                                                                                                                                               "}\n"
                                                                                                                                                               "\nExamples:\n"
                + HelpExampleCli("getaddressutxos", "'{\"addresses\": [\"tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ\"], \"chainInfo\": true}'")
                + HelpExampleRpc("getaddressutxos", "{\"addresses\": [\"tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ\"], \"chainInfo\": true}")
        );

    if (!fEnableInsightExplorer) {
        throw JSONRPCError(RPC_MISC_ERROR, "Error: getaddressutxos is disabled. "
                                           "Run './pastel-cli help getaddressutxos' for instructions on how to enable this feature.");
    }

    bool includeChainInfo = false;
    if (params[0].isObject()) {
        UniValue chainInfo = find_value(params[0].get_obj(), "chainInfo");
        if (!chainInfo.isNull()) {
            includeChainInfo = chainInfo.get_bool();
        }
    }
    std::vector<std::pair<uint160, ScriptType>> addresses;
    if (!getAddressesFromParams(params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    std::vector<CAddressUnspentDbEntry> unspentOutputs;
    for (const auto& it : addresses) {
        if (!GetAddressUnspent(it.first, it.second, unspentOutputs)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
        }
    }
    std::sort(unspentOutputs.begin(), unspentOutputs.end(),
              [](const CAddressUnspentDbEntry& a, const CAddressUnspentDbEntry& b) -> bool {
                  return a.second.blockHeight < b.second.blockHeight;
              });

    UniValue utxos(UniValue::VARR);
    for (const auto& it : unspentOutputs) {
        UniValue output(UniValue::VOBJ);
        std::string address;
        if (!getAddressFromIndex(it.first.type, it.first.hashBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        output.pushKV("address", address);
        output.pushKV("txid", it.first.txhash.GetHex());
        output.pushKV("outputIndex", (int)it.first.index);
        output.pushKV("script", HexStr(it.second.script.begin(), it.second.script.end()));
        output.pushKV("satoshis", it.second.satoshis);
        output.pushKV("height", it.second.blockHeight);
        utxos.push_back(output);
    }

    if (!includeChainInfo)
        return utxos;

    UniValue result(UniValue::VOBJ);
    result.pushKV("utxos", utxos);

    LOCK(cs_main);  // for chainActive
    result.pushKV("hash", chainActive.Tip()->GetBlockHash().GetHex());
    result.pushKV("height", (int)chainActive.Height());
    return result;
}


static void getHeightRange(const UniValue& params, int& start, int& end)
{
    start = 0;
    end = 0;
    if (params[0].isObject()) {
        UniValue startValue = find_value(params[0].get_obj(), "start");
        UniValue endValue = find_value(params[0].get_obj(), "end");
        // If either is not specified, the other is ignored.
        if (!startValue.isNull() && !endValue.isNull()) {
            start = startValue.get_int();
            end = endValue.get_int();
            if (start <= 0 || end <= 0) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                                   "Start and end are expected to be greater than zero");
            }
            if (end < start) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                                   "End value is expected to be greater than or equal to start");
            }
        }
    }

    LOCK(cs_main);  // for chainActive
    if (start > chainActive.Height() || end > chainActive.Height()) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Start or end is outside chain range");
    }
}

// Parse an address list then fetch the corresponding addressindex information.
static void getAddressesInHeightRange(
        const UniValue& params,
        int start, int end,
        std::vector<std::pair<uint160, ScriptType>>& addresses,
        std::vector<std::pair<CAddressIndexKey, CAmount>> &addressIndex)
{
    if (!getAddressesFromParams(params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    for (const auto& it : addresses) {
        if (!GetAddressIndex(it.first, it.second, addressIndex, start, end)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                               "No information available for address");
        }
    }
}

// insightexplorer
UniValue getaddressdeltas(const UniValue& params, bool fHelp)
{
    std::string disabledMsg = "";
    const bool fEnableInsightExplorer = fExperimentalMode && fInsightExplorer;
    if (!fEnableInsightExplorer) {
        disabledMsg = experimentalDisabledHelpMsg("getaddressdeltas", {"insightexplorer"});
    }
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "getaddressdeltas {\"addresses\": [\"taddr\", ...], (\"start\": n), (\"end\": n), (\"chainInfo\": true|false)}\n"
                "\nReturns all changes for an address.\n"
                "\nReturns information about all changes to the given transparent addresses within the given (inclusive)\n"
                "\nblock height range, default is the full blockchain.\n"
                + disabledMsg +
                "\nArguments:\n"
                "{\n"
                "  \"addresses\":\n"
                "    [\n"
                "      \"address\" (string) The base58check encoded address\n"
                "      ,...\n"
                "    ]\n"
                "  \"start\"       (number, optional) The start block height\n"
                "  \"end\"         (number, optional) The end block height\n"
                "  \"chainInfo\"   (boolean, optional, default=false) Include chain info in results, only applies if start and end specified\n"
                "}\n"
                "(or)\n"
                "\"address\"       (string) The base58check encoded address\n"
                "\nResult:\n"
                "[\n"
                "  {\n"
                "    \"satoshis\"  (number) The difference of " + MINOR_CURRENCY_UNIT + "\n"
                                                                                        "    \"txid\"      (string) The related txid\n"
                                                                                        "    \"index\"     (number) The related input or output index\n"
                                                                                        "    \"height\"    (number) The block height\n"
                                                                                        "    \"address\"   (string) The base58check encoded address\n"
                                                                                        "  }, ...\n"
                                                                                        "]\n\n"
                                                                                        "(or, if chainInfo is true):\n\n"
                                                                                        "{\n"
                                                                                        "  \"deltas\":\n"
                                                                                        "    [\n"
                                                                                        "      {\n"
                                                                                        "        \"satoshis\"    (number) The difference of " + MINOR_CURRENCY_UNIT + "\n"
                                                                                                                                                                      "        \"txid\"        (string) The related txid\n"
                                                                                                                                                                      "        \"index\"       (number) The related input or output index\n"
                                                                                                                                                                      "        \"height\"      (number) The block height\n"
                                                                                                                                                                      "        \"address\"     (string)  The address base58check encoded\n"
                                                                                                                                                                      "      }, ...\n"
                                                                                                                                                                      "    ],\n"
                                                                                                                                                                      "  \"start\":\n"
                                                                                                                                                                      "    {\n"
                                                                                                                                                                      "      \"hash\"          (string)  The start block hash\n"
                                                                                                                                                                      "      \"height\"        (numeric) The height of the start block\n"
                                                                                                                                                                      "    }\n"
                                                                                                                                                                      "  \"end\":\n"
                                                                                                                                                                      "    {\n"
                                                                                                                                                                      "      \"hash\"          (string)  The end block hash\n"
                                                                                                                                                                      "      \"height\"        (numeric) The height of the end block\n"
                                                                                                                                                                      "    }\n"
                                                                                                                                                                      "}\n"
                                                                                                                                                                      "\nExamples:\n"
                + HelpExampleCli("getaddressdeltas", "'{\"addresses\": [\"tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ\"], \"start\": 1000, \"end\": 2000, \"chainInfo\": true}'")
                + HelpExampleRpc("getaddressdeltas", "{\"addresses\": [\"tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ\"], \"start\": 1000, \"end\": 2000, \"chainInfo\": true}")
        );

    if (!fEnableInsightExplorer) {
        throw JSONRPCError(RPC_MISC_ERROR, "Error: getaddressdeltas is disabled. "
                                           "Run './pastel-cli help getaddressdeltas' for instructions on how to enable this feature.");
    }

    int start = 0;
    int end = 0;
    getHeightRange(params, start, end);

    std::vector<std::pair<uint160, int>> addresses;
    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;
    getAddressesInHeightRange(params, start, end, addresses, addressIndex);

    bool includeChainInfo = false;
    if (params[0].isObject()) {
        UniValue chainInfo = find_value(params[0].get_obj(), "chainInfo");
        if (!chainInfo.isNull()) {
            includeChainInfo = chainInfo.get_bool();
        }
    }

    UniValue deltas(UniValue::VARR);
    for (const auto& it : addressIndex) {
        std::string address;
        if (!getAddressFromIndex(it.first.type, it.first.hashBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }

        UniValue delta(UniValue::VOBJ);
        delta.pushKV("address", address);
        delta.pushKV("blockindex", (int)it.first.txindex);
        delta.pushKV("height", it.first.blockHeight);
        delta.pushKV("index", (int)it.first.index);
        delta.pushKV("satoshis", it.second);
        delta.pushKV("txid", it.first.txhash.GetHex());
        deltas.push_back(delta);
    }

    UniValue result(UniValue::VOBJ);

    if (!(includeChainInfo && start > 0 && end > 0)) {
        return deltas;
    }

    UniValue startInfo(UniValue::VOBJ);
    UniValue endInfo(UniValue::VOBJ);
    {
        LOCK(cs_main);  // for chainActive
        if (start > chainActive.Height() || end > chainActive.Height()) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Start or end is outside chain range");
        }
        startInfo.pushKV("hash", chainActive[start]->GetBlockHash().GetHex());
        endInfo.pushKV("hash", chainActive[end]->GetBlockHash().GetHex());
    }
    startInfo.pushKV("height", start);
    endInfo.pushKV("height", end);

    result.pushKV("deltas", deltas);
    result.pushKV("start", startInfo);
    result.pushKV("end", endInfo);

    return result;
}

// insightexplorer
UniValue getaddressbalance(const UniValue& params, bool fHelp)
{
    std::string disabledMsg = "";
    const bool fEnableInsightExplorer = fExperimentalMode && fInsightExplorer;
    if (!fEnableInsightExplorer) {
        disabledMsg = experimentalDisabledHelpMsg("getaddressbalance", {"insightexplorer"});
    }
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "getaddressbalance {\"addresses\": [\"taddr\", ...]}\n"
                "\nReturns the balance for addresses.\n"
                + disabledMsg +
                "\nArguments:\n"
                "{\n"
                "  \"addresses\":\n"
                "    [\n"
                "      \"address\"  (string) The base58check encoded address\n"
                "      ,...\n"
                "    ]\n"
                "}\n"
                "(or)\n"
                "\"address\"  (string) The base58check encoded address\n"
                "\nResult:\n"
                "{\n"
                "  \"balance\"  (string) The current balance in " + MINOR_CURRENCY_UNIT + "\n"
                                                                                          "  \"received\"  (string) The total number of " + MINOR_CURRENCY_UNIT + " received (including change)\n"
                                                                                                                                                                  "}\n"
                                                                                                                                                                  "\nExamples:\n"
                + HelpExampleCli("getaddressbalance", "'{\"addresses\": [\"tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ\"]}'")
                + HelpExampleRpc("getaddressbalance", "{\"addresses\": [\"tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ\"]}")
        );

    if (!fEnableInsightExplorer) {
        throw JSONRPCError(RPC_MISC_ERROR, "Error: getaddressbalance is disabled. "
                                           "Run './pastel-cli help getaddressbalance' for instructions on how to enable this feature.");
    }

    std::vector<std::pair<uint160, int>> addresses;
    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;
    // this method doesn't take start and end block height params, so set
    // to zero (full range, entire blockchain)
    getAddressesInHeightRange(params, 0, 0, addresses, addressIndex);

    CAmount balance = 0;
    CAmount received = 0;
    for (const auto& it : addressIndex) {
        if (it.second > 0) {
            received += it.second;
        }
        balance += it.second;
    }
    UniValue result(UniValue::VOBJ);
    result.pushKV("balance", balance);
    result.pushKV("received", received);
    return result;
}

// insightexplorer
UniValue getaddresstxids(const UniValue& params, bool fHelp)
{
    std::string disabledMsg = "";
    const bool fEnableInsightExplorer = fExperimentalMode && fInsightExplorer;
    if (!fEnableInsightExplorer) {
        disabledMsg = experimentalDisabledHelpMsg("getaddresstxids", {"insightexplorer"});
    }
    if (fHelp || params.size() != 1)
        throw runtime_error(
                "getaddresstxids {\"addresses\": [\"taddr\", ...], (\"start\": n), (\"end\": n)}\n"
                "\nReturns the txids for given transparent addresses within the given (inclusive)\n"
                "\nblock height range, default is the full blockchain.\n"
                "\nStarting v4.5.0, returned txids are in the order they appear in blocks, which \n"
                "\nensures that they are topologically sorted (i.e. parent txids will appear before child txids).\n"
                + disabledMsg +
                "\nArguments:\n"
                "{\n"
                "  \"addresses\":\n"
                "    [\n"
                "      \"taddr\"  (string) The base58check encoded address\n"
                "      ,...\n"
                "    ]\n"
                "  \"start\" (number, optional) The start block height\n"
                "  \"end\" (number, optional) The end block height\n"
                "}\n"
                "(or)\n"
                "\"address\"  (string) The base58check encoded address\n"
                "\nResult:\n"
                "[\n"
                "  \"transactionid\"  (string) The transaction id\n"
                "  ,...\n"
                "]\n"
                "\nExamples:\n"
                + HelpExampleCli("getaddresstxids", "'{\"addresses\": [\"tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ\"], \"start\": 1000, \"end\": 2000}'")
                + HelpExampleRpc("getaddresstxids", "{\"addresses\": [\"tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ\"], \"start\": 1000, \"end\": 2000}")
        );

    if (!fEnableInsightExplorer) {
        throw JSONRPCError(RPC_MISC_ERROR, "Error: getaddresstxids is disabled. "
                                           "Run './pastel-cli help getaddresstxids' for instructions on how to enable this feature.");
    }

    int start = 0;
    int end = 0;
    getHeightRange(params, start, end);

    std::vector<std::pair<uint160, int>> addresses;
    std::vector<std::pair<CAddressIndexKey, CAmount>> addressIndex;
    getAddressesInHeightRange(params, start, end, addresses, addressIndex);

    // This is an ordered set, sorted by (height,txindex) so result also sorted by height.
    std::set<std::tuple<int, int, std::string>> txids;

    for (const auto& it : addressIndex) {
        const int height = it.first.blockHeight;
        const int txindex = it.first.txindex;
        const std::string txid = it.first.txhash.GetHex();
        // Duplicate entries (two addresses in same tx) are suppressed
        txids.insert(std::make_tuple(height, txindex, txid));
    }
    UniValue result(UniValue::VARR);
    for (const auto& it : txids) {
        // only push the txid, not the height
        result.push_back(std::get<2>(it));
    }

    return result;
}

// insightexplorer
UniValue getspentinfo(const UniValue& params, bool fHelp)
{
    std::string disabledMsg = "";
    const bool fEnableInsightExplorer = fExperimentalMode && fInsightExplorer;
    if (!fEnableInsightExplorer) {
        disabledMsg = experimentalDisabledHelpMsg("getspentinfo", {"insightexplorer"});
    }
    if (fHelp || params.size() != 1 || !params[0].isObject())
        throw runtime_error(
                "getspentinfo {\"txid\": \"txidhex\", \"index\": n}\n"
                "\nReturns the txid and index where an output is spent.\n"
                + disabledMsg +
                "\nArguments:\n"
                "{\n"
                "  \"txid\"   (string) The hex string of the txid\n"
                "  \"index\"  (number) The vout (output) index\n"
                "}\n"
                "\nResult:\n"
                "{\n"
                "  \"txid\"   (string) The transaction id\n"
                "  \"index\"  (number) The spending (vin, input) index\n"
                "  ,...\n"
                "}\n"
                "\nExamples:\n"
                + HelpExampleCli("getspentinfo", "'{\"txid\": \"33990288fb116981260be1de10b8c764f997674545ab14f9240f00346333b780\", \"index\": 4}'")
                + HelpExampleRpc("getspentinfo", "{\"txid\": \"33990288fb116981260be1de10b8c764f997674545ab14f9240f00346333b780\", \"index\": 4}")
        );

    if (!fEnableInsightExplorer) {
        throw JSONRPCError(RPC_MISC_ERROR, "Error: getspentinfo is disabled. "
                                           "Run './pastel-cli help getspentinfo' for instructions on how to enable this feature.");
    }

    UniValue txidValue = find_value(params[0].get_obj(), "txid");
    UniValue indexValue = find_value(params[0].get_obj(), "index");

    if (!txidValue.isStr())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid txid, must be a string");
    if (!indexValue.isNum())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid index, must be an integer");
    uint256 txid = ParseHashV(txidValue, "txid");
    int outputIndex = indexValue.get_int();

    CSpentIndexKey key(txid, outputIndex);
    CSpentIndexValue value;

    {
        LOCK(cs_main);
        if (!GetSpentIndex(key, value)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to get spent info");
        }
    }
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("txid", value.txid.GetHex());
    obj.pushKV("index", (int)value.inputIndex);
    obj.pushKV("height", value.blockHeight);

    return obj;
}

static UniValue RPCLockedMemoryInfo()
{
    LockedPool::Stats stats = LockedPoolManager::Instance().stats();
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("used", uint64_t(stats.used));
    obj.pushKV("free", uint64_t(stats.free));
    obj.pushKV("total", uint64_t(stats.total));
    obj.pushKV("locked", uint64_t(stats.locked));
    obj.pushKV("chunks_used", uint64_t(stats.chunks_used));
    obj.pushKV("chunks_free", uint64_t(stats.chunks_free));
    return obj;
}

static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "control",            "getinfo",                &getinfo,                true  }, /* uses wallet if enabled */
    { "control",            "getmemoryinfo",          &getmemoryinfo,          true  },
    { "util",               "validateaddress",        &validateaddress,        true  }, /* uses wallet if enabled */
    { "util",               "z_validateaddress",      &z_validateaddress,      true  }, /* uses wallet if enabled */
    { "util",               "createmultisig",         &createmultisig,         true  },
    { "util",               "verifymessage",          &verifymessage,          true  },

    // START insightexplorer
    /* Address index */
    { "addressindex",       "getaddresstxids",        &getaddresstxids,        false }, /* insight explorer */
    { "addressindex",       "getaddressbalance",      &getaddressbalance,      false }, /* insight explorer */
    { "addressindex",       "getaddressdeltas",       &getaddressdeltas,       false }, /* insight explorer */
    { "addressindex",       "getaddressutxos",        &getaddressutxos,        false }, /* insight explorer */
    { "addressindex",       "getaddressmempool",      &getaddressmempool,      true  }, /* insight explorer */
    { "blockchain",         "getspentinfo",           &getspentinfo,           false }, /* insight explorer */
    // END insightexplorer

    /* Not shown in help */
    { "hidden",             "setmocktime",            &setmocktime,            true  },
};

void RegisterMiscRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
