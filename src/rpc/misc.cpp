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
#include <net.h>
#include <netbase.h>
#include <timedata.h>
#include <txmempool.h>
#include <txdb/txdb.h>
#include <chain_options.h>
#include <script/scripttype.h>
#include <rpc/server.h>
#include <rpc/chain-rpc-utils.h>
#include <rpc/rpc_consts.h>
#include <rpc/rpc-utils.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
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
    obj.pushKV("blocks",        gl_nChainHeight.load());
    obj.pushKV("timeoffset",    GetTimeOffset());
    obj.pushKV("connections",   gl_NodeManager.GetNodeCount());
    obj.pushKV("proxy",         (proxy.IsValid() ? proxy.proxy.ToStringIPPort() : string()));
    obj.pushKV("difficulty",    GetDifficulty());
    obj.pushKV("chain",         Params().NetworkIDString());
#ifdef ENABLE_WALLET
    if (pwalletMain)
    {
        obj.pushKV("keypoololdest", pwalletMain->GetOldestKeyPoolTime());
        obj.pushKV("keypoolsize",   pwalletMain->GetKeyPoolSize());
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
            obj.pushKV("vAddresses", a);
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
    auto nRequired = get_long_number(params[0]);
    const UniValue& keys = params[1].get_array();

    // Gather public keys
    if (nRequired < 1)
        throw runtime_error("a multisignature address must require at least one key to redeem");
    if ((int)keys.size() < nRequired)
        throw runtime_error(
            strprintf("not enough keys supplied "
                      "(got %u keys, but need at least %d to redeem)", keys.size(), nRequired));
    if (keys.size() > 16)
        throw runtime_error("Number of vAddresses involved in the multisignature address creation > 16\nReduce the number");

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
    CScript result = GetScriptForMultisig(static_cast<int>(nRequired), pubkeys);

    if (result.size() > MAX_SCRIPT_ELEMENT_SIZE)
        throw runtime_error(
                strprintf("redeemScript exceeds size limit: %u > %u", result.size(), MAX_SCRIPT_ELEMENT_SIZE));

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
1. nrequired    (numeric, required) The number of required signatures out of the n keys or vAddresses.
2. "keys"       (string, required) A json array of keys which are Pastel vAddresses or hex-encoded public keys
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

Create a multisig address from 2 vAddresses
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
    obj.pushKV("used", stats.used);
    obj.pushKV("free", stats.free);
    obj.pushKV("total", stats.total);
    obj.pushKV("locked", stats.locked);
    obj.pushKV("chunks_used", stats.chunks_used);
    obj.pushKV("chunks_free", stats.chunks_free);
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

// insightexplorer
static address_opt_t getAddressFromParams(const UniValue& params, const string &name)
{
    const auto &addressValue = find_value(params.get_obj(), name);
	if (!addressValue.isStr())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Invalid %s parameter", name));

    KeyIO keyIO(Params());
    string sAddress = addressValue.get_str();
    auto address = keyIO.DecodeDestination(sAddress);
    uint160 addressHash;
    ScriptType addressType = ScriptType::UNKNOWN;
    if (!GetTxDestinationHash(address, addressHash, addressType))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    return make_optional<address_t>(move(addressHash), addressType);
}

static bool getAddressesFromParams(const UniValue& params, address_vector_t &vAddresses)
{
    unordered_map<string, bool> vParamAddresses;
    if (params[0].isStr())
        vParamAddresses[params[0].get_str()] = true;
    else if (params[0].isObject())
    {
        const auto &addressValues = find_value(params[0].get_obj(), "addresses");
        if (!addressValues.isArray())
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                "Addresses is expected to be an array");
        }
        for (const auto& it : addressValues.getValues())
            vParamAddresses[it.get_str()] = true;
    } else
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");

    vAddresses.reserve(vParamAddresses.size());

    KeyIO keyIO(Params());
    for (const auto& it : vParamAddresses)
    {
        auto address = keyIO.DecodeDestination(it.first);
        uint160 addressHash;
        ScriptType type = ScriptType::UNKNOWN;
        if (!GetTxDestinationHash(address, addressHash, type))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
        vAddresses.emplace_back(addressHash, type);
    }
    return true;
}

UniValue getaddressmempool(const UniValue& params, bool fHelp)
{
    string disabledMsg = rpcDisabledInsightExplorerHelpMsg(RPC_API_GETADDRESSMEMPOOL);

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getaddressmempool {"addresses": [taddr, ...]}

Returns all mempool deltas for an address.)" + disabledMsg + R"(
Arguments:
{
  "addresses":
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
            + HelpExampleCli(RPC_API_GETADDRESSMEMPOOL, "'{\"addresses\": [\"tPp3pfmLi57S8qoccfWnn2o4tXyoQ23wVSp\"]}'")
            + HelpExampleRpc(RPC_API_GETADDRESSMEMPOOL, "{\"addresses\": [\"tPp3pfmLi57S8qoccfWnn2o4tXyoQ23wVSp\"]}")
        );

    rpcDisabledThrowMsg(fInsightExplorer, RPC_API_GETADDRESSMEMPOOL);

    address_vector_t vAddresses;

    if (!getAddressesFromParams(params, vAddresses))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");

    vector<pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>> vIndexes;
    mempool.getAddressIndex(vAddresses, vIndexes);
    std::sort(vIndexes.begin(), vIndexes.end(),
        [](const pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>& a,
           const pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>& b) -> bool {
               return a.second.time < b.second.time;
           });

    UniValue result(UniValue::VARR);
    result.reserve(vIndexes.size());

    string sAddress;
    for (const auto& it : vIndexes)
    {
        sAddress.clear();
        if (!getAddressFromIndex(it.first.type, it.first.addressHash, sAddress))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");

        UniValue delta(UniValue::VOBJ);
        delta.pushKV("address", std::move(sAddress));
        delta.pushKV(RPC_KEY_TXID, it.first.txid.GetHex());
        delta.pushKV("index", it.first.index);
        delta.pushKV("patoshis", it.second.amount);
        delta.pushKV("timestamp", it.second.time);
        if (it.second.amount < 0)
        {
            delta.pushKV("prevtxid", it.second.prevhash.GetHex());
            delta.pushKV("prevout", it.second.prevout);
        }
        result.push_back(std::move(delta));
    }
    return result;
}

// Parse an address list then fetch the corresponding addressindex information.
static void getAddressesInHeightRange(
    const UniValue& params,
    const height_range_opt_t &height_range,
    address_vector_t& vAddresses,
    address_index_vector_t &vAddressIndex)
{
    if (!getAddressesFromParams(params, vAddresses))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    
    for (const auto& [addressHash, addressType] : vAddresses)
    {
        if (!GetAddressIndex(addressHash, addressType, vAddressIndex, height_range))
        {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                "No information available for address");
        }
    }
}

// insightexplorer
UniValue getaddresstxids(const UniValue& params, bool fHelp)
{
    string disabledMsg = rpcDisabledInsightExplorerHelpMsg(RPC_API_GETADDRESSTXIDS);

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getaddresstxids {"addresses": ["taddr", ...], ("start": n), ("end": n)}

Returns the transaction ids for given transparent addresses within the given (inclusive)
block height range, default is the full blockchain.

Returned txids are in the order they appear in blocks, which
ensures that they are topologically sorted (i.e. parent txids will appear before child txids).
)" + disabledMsg + R"(
Arguments:
{
  "addresses":
    [
      "taddr"  (string) The base58check encoded address
      ,...
    ]
  "start" (number, optional) The start block height
  "end"   (number, optional) The end block height
}
(or)
  "address" (string) The base58check encoded address

Result:
[
  "txid"  (string) The transaction id
  ,...
]

Examples:
)" + HelpExampleCli(RPC_API_GETADDRESSTXIDS, R"('{"addresses": ["PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n"], "start": 1000, "end": 2000}')") +
     HelpExampleRpc(RPC_API_GETADDRESSTXIDS, R"({"addresses": ["PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n"], "start": 1000, "end": 2000})")
    );

    rpcDisabledThrowMsg(fInsightExplorer, RPC_API_GETADDRESSTXIDS);

    const auto height_range = rpc_get_height_range(params);

    address_vector_t vAddresses;
    address_index_vector_t vAddressIndex;
    getAddressesInHeightRange(params, height_range, vAddresses, vAddressIndex);

    // This is an ordered set, sorted by (height, txindex) so result also sorted by height.
    set<tuple<uint32_t, uint32_t, string>> txids;

    for (const auto& [index_key, amount] : vAddressIndex)
    {
        const uint32_t height = index_key.blockHeight;
        const uint32_t txindex = index_key.txindex;
        string txid = index_key.txid.GetHex();
        // Duplicate entries (two addresses in same tx) are suppressed
        txids.emplace(height, txindex, std::move(txid));
    }
    UniValue result(UniValue::VARR);
    result.reserve(txids.size());
    for (const auto& it : txids)
    {
        // only push the txid, not the height
        result.push_back(get<2>(it));
    }

    return result;
}

// insightexplorer
UniValue getaddressbalance(const UniValue& params, bool fHelp)
{
    string disabledMsg = rpcDisabledInsightExplorerHelpMsg(RPC_API_GETADDRESSBALANCE);

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getaddressbalance {"addresses": ["taddr", ...]}

Returns the balance for addresses.
)" + disabledMsg + R"(
Arguments:
{
  "addresses":
    [
      "address"  (string) The base58check encoded address
      ,...
    ]
}
(or)
"address"  (string) The base58check encoded address

Result:
{
  "addresses":
    [
      {
        "address"     (string)  The base58check encoded address
        "balance"     (string)  (string) The current balance of the address in )" + MINOR_CURRENCY_UNIT + R"(
      }, ...
    ],
  "balance"  (string) The total current balance in )" + MINOR_CURRENCY_UNIT + R"(on all addresses in the request
  "received"  (string) The total number of )" + MINOR_CURRENCY_UNIT + R"( received (including change) by all addresses in the request
}

Examples:
)" + HelpExampleCli(RPC_API_GETADDRESSBALANCE, R"('{"addresses": ["tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ"]}')") +
     HelpExampleRpc(RPC_API_GETADDRESSBALANCE, R"({"addresses": ["tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ"]})")
    );

    rpcDisabledThrowMsg(fInsightExplorer, RPC_API_GETADDRESSBALANCE);

    address_vector_t vAddresses;
    address_index_vector_t vAddressIndex;
    // this method doesn't take start and end block height params, so set
    // to zero (full range, entire blockchain)
    getAddressesInHeightRange(params, nullopt, vAddresses, vAddressIndex);

    CAmount balance = 0;
    CAmount received = 0;
    unordered_map<string, CAmount> addressesMap;
    string sAddress;
    for (const auto& [key, amount] : vAddressIndex)
    {
        if (amount > 0)
            received += amount;

        balance += amount;

        sAddress.clear();
        if (!getAddressFromIndex(key.type, key.addressHash, sAddress))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");

        addressesMap[sAddress] += amount;
    }

    UniValue addresses(UniValue::VARR);
    addresses.reserve(addressesMap.size());
    for (const auto& [sAddress, amount] : addressesMap)
    {
        UniValue addr_obj(UniValue::VOBJ);
        addr_obj.pushKV("address", sAddress);
        addr_obj.pushKV("balance", amount);
        addresses.push_back(std::move(addr_obj));
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("addresses", std::move(addresses));
    result.pushKV("balance", balance);
    result.pushKV("received", received);
    return result;
}

// insightexplorer
UniValue getaddressdeltas(const UniValue& params, bool fHelp)
{
    string disabledMsg = rpcDisabledInsightExplorerHelpMsg(RPC_API_GETADDRESSDELTAS);

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getaddressdeltas {"addresses": ["taddr", ...], ("start": n), ("end": n), ("chainInfo": true|false)}

Returns all changes for an address.

Returns information about all changes to the given transparent addresses within the given (inclusive)
block height range, default is the full blockchain.
)" + disabledMsg + R"(
Arguments:
{
  "addresses":
    [
      "address" (string) The base58check encoded address
      ,...
    ]
  "start"       (number, optional) The start block height
  "end"         (number, optional) The end block height
  "chainInfo"   (boolean, optional, default=false) Include chain info in results, only applies if start and end specified
}
(or)
"address"       (string) The base58check encoded address

Result:
[
  {
    "patoshis"  (number) The difference of )" + MINOR_CURRENCY_UNIT + R"(
    "txid"      (string) The related txid
    "index"     (number) The related input or output index
    "height"    (number) The block height
    "address"   (string) The base58check encoded address
  }, ...
]

(or, if chainInfo is true):

{
  "deltas":
    [
      {
        "patoshis"    (number) The difference of )" + MINOR_CURRENCY_UNIT + R"(
        "txid"        (string) The related txid
        "index"       (number) The related input or output index
        "height"      (number) The block height
        "address"     (string)  The address base58check encoded
      }, ...
    ],
  "start":
    {
      "hash"          (string)  The start block hash
      "height"        (numeric) The height of the start block
    }
  "end":
    {
      "hash"          (string)  The end block hash
      "height"        (numeric) The height of the end block
    }
}

Examples:
)" + HelpExampleCli(RPC_API_GETADDRESSDELTAS, R"('{"addresses": ["tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ"], "start": 1000, "end": 2000, "chainInfo": true}')") +
     HelpExampleRpc(RPC_API_GETADDRESSDELTAS, R"({"addresses": ["tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ"], "start": 1000, "end": 2000, "chainInfo": true})")
    );

    rpcDisabledThrowMsg(fInsightExplorer, RPC_API_GETADDRESSDELTAS);

    const auto height_range = rpc_get_height_range(params);

    address_vector_t vAddresses;
    address_index_vector_t vAddressIndex;
    getAddressesInHeightRange(params, height_range, vAddresses, vAddressIndex);

    bool includeChainInfo = false;
    if (params[0].isObject())
    {
        UniValue chainInfo = find_value(params[0].get_obj(), "chainInfo");
        if (!chainInfo.isNull())
            includeChainInfo = get_bool_value(chainInfo);
    }

    UniValue deltas(UniValue::VARR);
    deltas.reserve(vAddressIndex.size());
    string sAddress;
    for (const auto& [key, amount] : vAddressIndex)
    {
        sAddress.clear();
        if (!getAddressFromIndex(key.type, key.addressHash, sAddress))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");

        UniValue delta(UniValue::VOBJ);
        delta.pushKV("address", move(sAddress));
        delta.pushKV("blockindex", key.txindex);
        delta.pushKV(RPC_KEY_HEIGHT, key.blockHeight);
        delta.pushKV("index", key.index);
        delta.pushKV("patoshis", amount);
        delta.pushKV(RPC_KEY_TXID, key.txid.GetHex());
        deltas.push_back(delta);
    }

    UniValue result(UniValue::VOBJ);

    uint32_t start = height_range ? height_range.value().first : 0;
    uint32_t end = height_range ? height_range.value().second : 0;
    if (!(includeChainInfo && start > 0 && end > 0))
        return deltas;

    UniValue startInfo(UniValue::VOBJ);
    UniValue endInfo(UniValue::VOBJ);
    {
        LOCK(cs_main);  // for chainActive
        if (start > gl_nChainHeight || end > gl_nChainHeight)
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Start or end is outside chain range");

        startInfo.pushKV("hash", chainActive[start]->GetBlockHash().GetHex());
        endInfo.pushKV("hash", chainActive[end]->GetBlockHash().GetHex());
    }
    startInfo.pushKV(RPC_KEY_HEIGHT, start);
    endInfo.pushKV(RPC_KEY_HEIGHT, end);

    result.pushKV("deltas", deltas);
    result.pushKV("start", startInfo);
    result.pushKV("end", endInfo);

    return result;
}

// insightexplorer
UniValue getUtxosDataWithSender(const address_vector_t& vDestAddresses, const height_range_opt_t& height_range,
    const address_t& senderAddress, bool bJustSendersAddress, bool bScanMemPoolTxs)
{
    funds_transfer_vector_t vFundsTransfers;
    for (const auto& [addressHashTo, addressTypeTo] : vDestAddresses)
    {
        if (!GetFundsTransferIndex(senderAddress.first, senderAddress.second, addressHashTo, addressTypeTo,
            vFundsTransfers, height_range))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Failed to get information from funds transfer index");
    }

    const auto &chainparams = Params();
    KeyIO keyIO(chainparams);
    UniValue utxos(UniValue::VARR);
    utxos.reserve(vFundsTransfers.size());

    string sAddressTo;
    string sAddressFrom = keyIO.EncodeDestination(DestFromAddressHash(senderAddress.second, senderAddress.first));
    for (const auto& [key, value] : vFundsTransfers)
    {
        auto unspentValue = GetAddressUnspent(key.addressHashTo, key.addressTypeTo, key.txid, value.nOutputIndex);
        if (!unspentValue)
            continue;

        UniValue output(UniValue::VOBJ);
        sAddressTo.clear();
        if (!getAddressFromIndex(key.addressTypeTo, key.addressHashTo, sAddressTo))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");

        output.pushKV("address", sAddressTo);
        output.pushKV(RPC_KEY_TXID, key.txid.GetHex());
        output.pushKV(RPC_KEY_OUTPUT_INDEX, value.nOutputIndex);
        output.pushKV("patoshis", value.nOutputValue);
        output.pushKV(RPC_KEY_HEIGHT, key.blockHeight);
        output.pushKV("senderAddress", sAddressFrom);

        if (!bJustSendersAddress) {
            UniValue vin(UniValue::VARR);
            vin.reserve(value.vInputIndex.size());
            for (const auto &[prevoutHash, prevoutN, nAmount]: value.vInputIndex) {
                UniValue in(UniValue::VOBJ);
                in.pushKV(RPC_KEY_TXID, prevoutHash.GetHex());
                in.pushKV(RPC_KEY_OUTPUT_INDEX, prevoutN);
                in.pushKV("patoshis", nAmount);
                vin.push_back(std::move(in));
            }
            output.pushKV("senders", std::move(vin));
        }

		utxos.push_back(std::move(output));
    }
    if (bScanMemPoolTxs)
    {
        address_t addressFrom, addressTo;

        LOCK2(cs_main, mempool.cs);
        for (const auto& entry : mempool.mapTx)
        {
            const auto& tx = entry.GetTx();
            if (tx.IsCoinBase())
                continue;

            uint32_t nTxOut = 0;
            for (const auto& txOut : tx.vout)
            {
                ++nTxOut;
                CTxDestination addressToDest;
                if (!ExtractDestination(txOut.scriptPubKey, addressToDest))
                    continue;

                if (!GetTxDestinationHash(addressToDest, addressTo.first, addressTo.second))
                    continue;

                // check if this address is in the list of destination addresses
                if (find(vDestAddresses.begin(), vDestAddresses.end(), addressTo) == vDestAddresses.end())
					continue;

                for (const auto& txIn : tx.vin)
                {
                    if (txIn.prevout.IsNull())
                        continue;

                    CTransaction prevTx;
					uint256 hashInputBlock;
					if (!GetTransaction(txIn.prevout.hash, prevTx, chainparams.GetConsensus(), hashInputBlock, true))
						continue;

					if (prevTx.vout.size() <= txIn.prevout.n)
						continue;

                    CTxDestination addressFromDest;
					const auto &prevTxOut = prevTx.vout[txIn.prevout.n];
					if (!ExtractDestination(prevTxOut.scriptPubKey, addressFromDest))
						continue;

                    if (!GetTxDestinationHash(addressFromDest, addressFrom.first, addressFrom.second))
                        continue;

					if (addressFrom != senderAddress)
						continue;

                    UniValue output(UniValue::VOBJ);
                    output.pushKV("address", keyIO.EncodeDestination(addressToDest));
                    output.pushKV(RPC_KEY_TXID, tx.GetHash().GetHex());
                    output.pushKV(RPC_KEY_OUTPUT_INDEX, nTxOut - 1);
                    output.pushKV("patoshis", txOut.nValue);
                    output.pushKV(RPC_KEY_HEIGHT, -1);
                    output.pushKV("senderAddress", keyIO.EncodeDestination(addressFromDest));

                    if (!bJustSendersAddress) {
                        UniValue vin(UniValue::VARR);
                        UniValue in(UniValue::VOBJ);
                        in.pushKV(RPC_KEY_TXID, txIn.prevout.hash.GetHex());
                        in.pushKV(RPC_KEY_OUTPUT_INDEX, txIn.prevout.n);
                        in.pushKV("patoshis", prevTxOut.nValue);
                        vin.push_back(std::move(in));
                        output.pushKV("senders", std::move(vin));
                    }

                    utxos.push_back(std::move(output));
                }
            }
        }
    }
    return utxos;
}

UniValue getUtxosData(const address_vector_t &vDestAddresses, const height_range_opt_t &height_range, 
                      const address_opt_t &senderAddress, bool bIncludeSender, bool bJustSendersAddress,
                      bool bScanMemPoolTxs, const string &sStatus)
{
    if (senderAddress)
        return getUtxosDataWithSender(vDestAddresses, height_range, senderAddress.value(),
                                      bJustSendersAddress, bScanMemPoolTxs);

    address_unspent_vector_t vUnspentOutputs;
    for (const auto& [addressHash, addressType] : vDestAddresses)
    {
        if (!GetAddressUnspent(addressHash, addressType, vUnspentOutputs))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "No information available for address");
    }
    std::sort(vUnspentOutputs.begin(), vUnspentOutputs.end(),
        [](const CAddressUnspentDbEntry& a, const CAddressUnspentDbEntry& b) -> bool {
            return a.second.blockHeight < b.second.blockHeight;
        });

    if (sStatus != "all")
    {
        address_unspent_vector_t vUnspentOutputsSpending;
        v_uint256 vtxid;
        mempool.queryHashes(vtxid);
        for (auto txid : vtxid) {
            CTransaction tx;
            uint256 hashBlock;
            CBlockIndex *blockindex = nullptr;
            if (!GetTransaction(txid, tx, Params().GetConsensus(), hashBlock, true, nullptr, blockindex)) {
                continue;
                //TODO ERROR
            }
            for (auto txin : tx.vin) {
                if (txin.prevout.IsNull())
                    continue;
                if (sStatus == "unspent")
                    vUnspentOutputs.erase(std::remove_if(vUnspentOutputs.begin(), vUnspentOutputs.end(),
                        [&txin](const CAddressUnspentDbEntry& entry) {
                            return entry.first.txid == txin.prevout.hash && entry.first.index == txin.prevout.n;
                        }), vUnspentOutputs.end());
                else if (sStatus == "spending") {
                    auto it = std::find_if(vUnspentOutputs.begin(), vUnspentOutputs.end(),
                        [&txin](const CAddressUnspentDbEntry& entry) {
                            return entry.first.txid == txin.prevout.hash && entry.first.index == txin.prevout.n;
                        });
                    if (it != vUnspentOutputs.end())
                        vUnspentOutputsSpending.push_back(*it);
                }
            }
        }
        if (sStatus == "spending")
            vUnspentOutputs = std::move(vUnspentOutputsSpending);
    }


    UniValue utxos(UniValue::VARR);
    utxos.reserve(vUnspentOutputs.size());

    string sAddress;
    for (const auto& [key, value] : vUnspentOutputs)
    {
        if (height_range && value.blockHeight < height_range.value().first)
            continue;

        UniValue output(UniValue::VOBJ);
        sAddress.clear();
        if (!getAddressFromIndex(key.type, key.addressHash, sAddress))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");

        output.pushKV("address", sAddress);
        output.pushKV(RPC_KEY_TXID, key.txid.GetHex());
        output.pushKV(RPC_KEY_OUTPUT_INDEX, key.index);
        if (!bJustSendersAddress)
            output.pushKV("script", HexStr(value.script.begin(), value.script.end()));
        output.pushKV("patoshis", value.patoshis);
        output.pushKV(RPC_KEY_HEIGHT, value.blockHeight);

        if (bIncludeSender)
        {
            CTransaction tx;
            uint256 hashBlock;
            CBlockIndex *blockindex = nullptr;
            KeyIO keyIO(Params());
            UniValue vin(UniValue::VARR);
            if (!GetTransaction(key.txid, tx, Params().GetConsensus(), hashBlock, true, nullptr, blockindex)) {
                continue;
                //TODO ERROR
            }
            bool bFoundSenders = false;
            for (const auto &txin: tx.vin)
            {
                UniValue in(UniValue::VOBJ);
                if (tx.IsCoinBase())
                {
                    if (bJustSendersAddress)
                        continue;
                    in.pushKV("coinbase", HexStr(txin.scriptSig.begin(), txin.scriptSig.end()));
                }
                else {
                    if (!bJustSendersAddress)
                    {
                        in.pushKV(RPC_KEY_TXID, txin.prevout.hash.GetHex());
                        in.pushKV(RPC_KEY_OUTPUT_INDEX, txin.prevout.n);
                    }

                    // Add address and value info if spentindex enabled
                    if (fSpentIndex)
                    {
                        CSpentIndexValue spentInfo;
                        CSpentIndexKey spentKey(txin.prevout.hash, txin.prevout.n);
                        {
                            LOCK(cs_main);
                            if (!GetSpentIndex(spentKey, spentInfo))
                                continue;
                        }
                        if (!bJustSendersAddress)
                            in.pushKV("patoshis", spentInfo.patoshis);
                        auto dest = DestFromAddressHash(spentInfo.addressType, spentInfo.addressHash);
                        if (IsValidDestination(dest))
                        {
                            auto senderAddress = keyIO.EncodeDestination(dest);
                            bFoundSenders = true;
                            if (bJustSendersAddress)
                                vin.push_back(senderAddress);
                            else
                                in.pushKV("address", senderAddress);
                        }
                    }
                }
                if (!bJustSendersAddress)
                    vin.push_back(std::move(in));
            }

            output.pushKV("senders", std::move(vin));
        }
        utxos.push_back(std::move(output));
    }
    return utxos;
}

UniValue getaddressutxos(const UniValue& params, bool fHelp)
{
    string disabledMsg = rpcDisabledInsightExplorerHelpMsg(RPC_API_GETADDRESSUTXOS);

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getaddressutxos {"addresses": ["taddr", ...], ("chainInfo": true|false), ("status": "all"|"unspent"|"spending")}

Returns all unspent outputs for an address.
)" + disabledMsg + R"(
Arguments:
{
  "addresses":
    [
      "address"  (string) The base58check encoded address
      ,...
    ],
  "chainInfo",  (boolean, optional, default=false) Include chain info with results
  "status"  (string, optional, default=all) Spend status of UTXO. Options: "all" - all UTXOs are included, "unspent" - excludes UTXOs in the unconfirmed transactions, "spending" - only UTXOs in the unconfirmed transactions
}
(or)
"address"  (string) The base58check encoded address

Result
[
  {
    "address"  (string) The address base58check encoded
    "txid"  (string) The output txid
    "height"  (number) The block height
    "outputIndex"  (number) The output index
    "script"  (string) The script hex encoded
    "patoshis"  (number) The number of )" + MINOR_CURRENCY_UNIT + R"( of the output
  }, ...
]

(or, if chainInfo is true):

{
  "utxos":
    [
      {
        "address"     (string)  The address base58check encoded
        "txid"        (string)  The output txid
        "height"      (number)  The block height
        "outputIndex" (number)  The output index
        "script"      (string)  The script hex encoded
        "patoshis"    (number)  The number of )" + MINOR_CURRENCY_UNIT + R"( of the output
      }, ...
    ],
  "hash"              (string)  The block hash
  "height"            (numeric) The block height
}

Examples:
)" + HelpExampleCli(RPC_API_GETADDRESSUTXOS, R"('{"addresses": ["tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ"], "chainInfo": true}')") +
     HelpExampleRpc(RPC_API_GETADDRESSUTXOS, R"({"addresses": ["tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ"], "chainInfo": true})")
    );

    rpcDisabledThrowMsg(fInsightExplorer, RPC_API_GETADDRESSUTXOS);

    bool includeChainInfo = false;
    string sStatus = "all";
    if (params[0].isObject())
    {
        UniValue chainInfo = find_value(params[0].get_obj(), "chainInfo");
        if (!chainInfo.isNull())
            includeChainInfo = get_bool_value(chainInfo);
        UniValue status = find_value(params[0].get_obj(), "status");
        if (!status.isNull())
        {
            sStatus = status.get_str();
            if (sStatus != "all" && sStatus != "unspent" && sStatus != "spending")
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid status parameter");
        }
    }
    address_vector_t vDestAddresses;
    if (!getAddressesFromParams(params, vDestAddresses))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");

    UniValue utxos = getUtxosData(vDestAddresses, nullopt, nullopt, false, false, false, sStatus);

    if (!includeChainInfo)
        return utxos;

    UniValue result(UniValue::VOBJ);
    result.pushKV("utxos", utxos);

    {
        LOCK(cs_main);  // for chainActive
        result.pushKV("hash", chainActive.Tip()->GetBlockHash().GetHex());
        result.pushKV(RPC_KEY_HEIGHT, gl_nChainHeight.load());
    }
    return result;
}

// insightexplorer
UniValue getaddressutxosextra(const UniValue& params, bool fHelp)
{
    string disabledMsg = rpcDisabledInsightExplorerHelpMsg(RPC_API_GETADDRESSUTXOSEXTRA);

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getaddressutxosextra {"addresses": ["taddr", ...], ("simple": true|false), ("minHeight": n)}

Returns all unspent outputs for an address including inputs for the transaction (vin).
)" + disabledMsg + R"(
Arguments:
{
  "addresses":
    [
      "address"  (string) The base58check encoded address
      ,...
    ],
  "simple"    (boolean, optional, default=false) Do not include full info about inputs with results
  "minHeight" (number, optional, default=0)      The minimum block height to include
  "sender"    (string, optional, default='')     Filter output by sender address
  "mempool"   (boolean, optional, default=false) Include mempool transactions
}

Result
[
  {
    "address"     (string) The address base58check encoded
    "txid"        (string) The output txid
    "height"      (number) The block height
    "outputIndex" (number) The output index
    "script"      (string) The script hex encoded
    "patoshis"    (number) The number of )" + MINOR_CURRENCY_UNIT + R"( of the output
    "senders"     (array, optional) The inputs for the transaction
  }, ...
]

Where "senders" is an array of objects with the following fields:
[
    {
      "txid"        (string) The input txid
      "outputIndex" (number) The output index
      "address"     (string) The base58check encoded address
      "patoshis"    (number) The number of )" + MINOR_CURRENCY_UNIT + R"( of the input
    }, ...
]
OR, if input is coinbase:
[
    "coinbase"  (string) The coinbase hex encoded
    ...
]
OR, if "simple" is true:
[
    "address"  (string) The base58check encoded address
    ...
]

Examples:
)" + HelpExampleCli(RPC_API_GETADDRESSUTXOSEXTRA, R"('{"addresses": ["tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ"], "simple": true, "minHeight": 1000}')") +
     HelpExampleRpc(RPC_API_GETADDRESSUTXOSEXTRA, R"({"addresses": ["tmYXBYJj1K7vhejSec5osXK2QsGa5MTisUQ"]})"));

    rpcDisabledThrowMsg(fInsightExplorer, RPC_API_GETADDRESSUTXOS);

    bool bSimpleInfo = false;
    bool bScanMempoolTxs = false;
    height_range_opt_t height_range;
    address_opt_t senderAddress;
    KeyIO keyIO(Params());
    if (params[0].isObject())
    {
        UniValue simple = find_value(params[0].get_obj(), "simple");
        if (!simple.isNull())
            bSimpleInfo = get_bool_value(simple);
        UniValue minHeight = find_value(params[0].get_obj(), "minHeight");
        if (!minHeight.isNull())
        {
            int64_t nParamMinHeight = get_long_number(minHeight);
            rpc_check_unsigned_param<uint32_t>("minHeight", nParamMinHeight);
            uint32_t nStartHeight = static_cast<uint32_t>(nParamMinHeight);
            height_range = make_pair(nStartHeight, gl_nChainHeight.load());
        }
        UniValue sender = find_value(params[0].get_obj(), "sender");
        if (!sender.isNull())
        {
            uint160 addressHash;
            ScriptType addressType;
            string sSenderAddress = sender.get_str();
            const CTxDestination dest = keyIO.DecodeDestination(sSenderAddress);
            if (!GetTxDestinationHash(dest, addressHash, addressType))
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pastel send address");
            senderAddress = make_pair(addressHash, addressType);
        }
        UniValue mempool = find_value(params[0].get_obj(), "mempool");
        if (!mempool.isNull())
			bScanMempoolTxs = get_bool_value(mempool);
    }
    address_vector_t vDestAddresses;
    if (!getAddressesFromParams(params, vDestAddresses))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pastel destination address");

    UniValue utxos = getUtxosData(vDestAddresses, height_range, senderAddress, true, bSimpleInfo, bScanMempoolTxs, "all");
    return utxos;
}

// insightexplorer
UniValue getspentinfo(const UniValue& params, bool fHelp)
{
    string disabledMsg = rpcDisabledInsightExplorerHelpMsg(RPC_API_GETSPENTINFO);

if (fHelp || params.size() != 1 || !params[0].isObject())
    throw runtime_error(
R"(getspentinfo {"txid": "txid", "index": n}

Returns the txid and index where an output is spent.
)" + disabledMsg + R"(
Arguments:
{
  "txid"   (string) The hex string of the transaction id
  "index"  (number) The vout (output) index
}

Result:
{
  "txid"   (string) The transaction id
  "index"  (number) The spending (vin, input) index
  ,...
}

Examples:
)" + HelpExampleCli(RPC_API_GETSPENTINFO, R"('{"txid": "33990288fb116981260be1de10b8c764f997674545ab14f9240f00346333b780", "index": 4}')") +
     HelpExampleRpc(RPC_API_GETSPENTINFO, R"({"txid": "33990288fb116981260be1de10b8c764f997674545ab14f9240f00346333b780", "index": 4})")
    );

    rpcDisabledThrowMsg(fInsightExplorer, RPC_API_GETSPENTINFO);

    const auto &txidValue = find_value(params[0].get_obj(), RPC_KEY_TXID);
    const auto &indexValue = find_value(params[0].get_obj(), "index");

    if (!txidValue.isStr())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid txid, must be a string");
    if (!indexValue.isNum())
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid index, must be an integer");
    uint256 txid = ParseHashV(txidValue, RPC_KEY_TXID);
    int64_t nParamOutputIndex = get_long_number(indexValue);
    rpc_check_unsigned_param<uint32_t>("index", nParamOutputIndex);
    uint32_t outputIndex = static_cast<uint32_t>(nParamOutputIndex);

    CSpentIndexKey key(txid, outputIndex);
    CSpentIndexValue value;

    {
        LOCK(cs_main);
        if (!GetSpentIndex(key, value))
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unable to get spent info");
    }
    UniValue obj(UniValue::VOBJ);
    obj.pushKV(RPC_KEY_TXID, value.txid.GetHex());
    obj.pushKV("index", value.inputIndex);
    obj.pushKV(RPC_KEY_HEIGHT, value.blockHeight);

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

    /* insight explorer */
    /* Address index */
    { "addressindex",       "getaddresstxids",        &getaddresstxids,        false }, /* insight explorer */
    { "addressindex",       "getaddressbalance",      &getaddressbalance,      false }, /* insight explorer */
    { "addressindex",       "getaddressdeltas",       &getaddressdeltas,       false }, /* insight explorer */
    { "addressindex",       "getaddressutxos",        &getaddressutxos,        false }, /* insight explorer */
    { "addressindex",       "getaddressutxosextra",   &getaddressutxosextra,   false }, /* insight explorer */
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
