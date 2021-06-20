// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php.

#include "clientversion.h"
#include "init.h"
#include "key_io.h"
#include "main.h"
#include "net.h"
#include "netbase.h"
#include "rpc/server.h"
#include "timedata.h"
#include "txmempool.h"
#include "util.h"
#ifdef ENABLE_WALLET
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#endif

#include <stdint.h>
#include <variant>

#include <boost/assign/list_of.hpp>

#include <univalue.h>

#include "zcash/Address.hpp"

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
            "getinfo\n"
            "Returns an object containing various state info.\n"
            "\nResult:\n"
            "{\n"
            "  \"version\": xxxxx,           (numeric) the server version\n"
            "  \"protocolversion\": xxxxx,   (numeric) the protocol version\n"
            "  \"walletversion\": xxxxx,     (numeric) the wallet version\n"
            "  \"balance\": xxxxxxx,         (numeric) the total Pastel balance of the wallet\n"
            "  \"blocks\": xxxxxx,           (numeric) the current number of blocks processed in the server\n"
            "  \"timeoffset\": xxxxx,        (numeric) the time offset\n"
            "  \"connections\": xxxxx,       (numeric) the number of connections\n"
            "  \"proxy\": \"host:port\",     (string, optional) the proxy used by the server\n"
            "  \"difficulty\": xxxxxx,       (numeric) the current difficulty\n"
            "  \"testnet\": true|false,      (boolean) if the server is using testnet or not\n"
            "  \"keypoololdest\": xxxxxx,    (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool\n"
            "  \"keypoolsize\": xxxx,        (numeric) how many new keys are pre-generated\n"
            "  \"unlocked_until\": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked\n"
            "  \"paytxfee\": x.xxxx,         (numeric) the transaction fee set in " + CURRENCY_UNIT + "/kB\n"
            "  \"relayfee\": x.xxxx,         (numeric) minimum relay fee for non-free transactions in " + CURRENCY_UNIT + "/kB\n"
            "  \"errors\": \"...\"           (string) any error messages\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getinfo", "")
            + HelpExampleRpc("getinfo", "")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
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
    obj.pushKV("blocks",        (int)chainActive.Height());
    obj.pushKV("timeoffset",    GetTimeOffset());
    obj.pushKV("connections",   (int)vNodes.size());
    obj.pushKV("proxy",         (proxy.IsValid() ? proxy.proxy.ToStringIPPort() : string()));
    obj.pushKV("difficulty",    (double)GetDifficulty());
    obj.pushKV("testnet",       Params().TestnetToBeDeprecatedFieldRPC());
#ifdef ENABLE_WALLET
    if (pwalletMain) {
        obj.pushKV("keypoololdest", pwalletMain->GetOldestKeyPoolTime());
        obj.pushKV("keypoolsize",   (int)pwalletMain->GetKeyPoolSize());
    }
    if (pwalletMain && pwalletMain->IsCrypted())
        obj.pushKV("unlocked_until", nWalletUnlockTime);
    obj.pushKV("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK()));
#endif
    obj.pushKV("relayfee",      ValueFromAmount(::minRelayTxFee.GetFeePerK()));
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
        if (pwalletMain && pwalletMain->GetCScript(scriptID, subscript)) {
            std::vector<CTxDestination> addresses;
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
            "validateaddress \"zcashaddress\"\n"
            "\nReturn information about the given Pastel address.\n"
            "\nArguments:\n"
            "1. \"zcashaddress\"     (string, required) The Pastel address to validate\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\" : true|false,         (boolean) If the address is valid or not. If not, this is the only property returned.\n"
            "  \"address\" : \"zcashaddress\",   (string) The Pastel address validated\n"
            "  \"scriptPubKey\" : \"hex\",       (string) The hex encoded scriptPubKey generated by the address\n"
            "  \"ismine\" : true|false,          (boolean) If the address is yours or not\n"
            "  \"isscript\" : true|false,        (boolean) If the key is a script\n"
            "  \"pubkey\" : \"publickeyhex\",    (string) The hex value of the raw public key\n"
            "  \"iscompressed\" : true|false,    (boolean) If the address is compressed\n"
            "  \"account\" : \"account\"         (string) DEPRECATED. The account associated with the address, \"\" is the default account\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
            + HelpExampleRpc("validateaddress", "\"1PSSGeFHDnKNxiEyFrD1wcEaHr9hrQDDWc\"")
        );

#ifdef ENABLE_WALLET
    LOCK2(cs_main, pwalletMain ? &pwalletMain->cs_wallet : NULL);
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
        std::string currentAddress = keyIO.EncodeDestination(dest);
        ret.pushKV("address", currentAddress);

        CScript scriptPubKey = GetScriptForDestination(dest);
        ret.pushKV("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));

#ifdef ENABLE_WALLET
        isminetype mine = pwalletMain ? IsMine(*pwalletMain, dest) : ISMINE_NO;
        ret.pushKV("ismine", (mine & ISMINE_SPENDABLE) ? true : false);
        ret.pushKV("iswatchonly", (mine & ISMINE_WATCH_ONLY) ? true: false);
        UniValue detail = std::visit(DescribeAddressVisitor(), dest);
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

    UniValue operator()(const libzcash::SproutPaymentAddress &zaddr) const {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("type", "sprout");
        obj.pushKV("payingkey", zaddr.a_pk.GetHex());
        obj.pushKV("transmissionkey", zaddr.pk_enc.GetHex());
#ifdef ENABLE_WALLET
        if (pwalletMain) {
            obj.pushKV("ismine", HaveSpendingKeyForPaymentAddress(pwalletMain)(zaddr));
        }
#endif
        return obj;
    }

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
            "z_validateaddress \"zaddr\"\n"
            "\nReturn information about the given z address.\n"
            "\nArguments:\n"
            "1. \"zaddr\"     (string, required) The z address to validate\n"
            "\nResult:\n"
            "{\n"
            "  \"isvalid\" : true|false,      (boolean) If the address is valid or not. If not, this is the only property returned.\n"
            "  \"address\" : \"zaddr\",         (string) The z address validated\n"
            "  \"type\" : \"xxxx\",             (string) \"sprout\" or \"sapling\"\n"
            "  \"ismine\" : true|false,       (boolean) If the address is yours or not\n"
            "  \"payingkey\" : \"hex\",         (string) [sprout] The hex value of the paying key, a_pk\n"
            "  \"transmissionkey\" : \"hex\",   (string) [sprout] The hex value of the transmission key, pk_enc\n"
            "  \"diversifier\" : \"hex\",       (string) [sapling] The hex value of the diversifier, d\n"
            "  \"diversifiedtransmissionkey\" : \"hex\", (string) [sapling] The hex value of pk_d\n"

            "}\n"
            "\nExamples:\n"
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
        UniValue detail = std::visit(DescribePaymentAddressVisitor(), address);
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

    std::vector<CPubKey> pubkeys;
    pubkeys.resize(keys.size());
    for (unsigned int i = 0; i < keys.size(); i++)
    {
        const std::string& ks = keys[i].get_str();
#ifdef ENABLE_WALLET
        // Case 1: Bitcoin address and we have full public key:
        CTxDestination dest = keyIO.DecodeDestination(ks);
        if (pwalletMain && IsValidDestination(dest)) {
            const CKeyID *keyID = std::get_if<CKeyID>(&dest);
            if (!keyID) {
                throw std::runtime_error(strprintf("%s does not refer to a key", ks));
            }
            CPubKey vchPubKey;
            if (!pwalletMain->GetPubKey(*keyID, vchPubKey)) {
                throw std::runtime_error(strprintf("no full public key for address %s", ks));
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
    {
        string msg = "createmultisig nrequired [\"key\",...]\n"
            "\nCreates a multi-signature address with n signature of m keys required.\n"
            "It returns a json object with the address and redeemScript.\n"

            "\nArguments:\n"
            "1. nrequired      (numeric, required) The number of required signatures out of the n keys or addresses.\n"
            "2. \"keys\"       (string, required) A json array of keys which are Pastel addresses or hex-encoded public keys\n"
            "     [\n"
            "       \"key\"    (string) Pastel address or hex-encoded public key\n"
            "       ,...\n"
            "     ]\n"

            "\nResult:\n"
            "{\n"
            "  \"address\":\"multisigaddress\",  (string) The value of the new multisig address.\n"
            "  \"redeemScript\":\"script\"       (string) The string value of the hex-encoded redemption script.\n"
            "}\n"

            "\nExamples:\n"
            "\nCreate a multisig address from 2 addresses\n"
            + HelpExampleCli("createmultisig", "2 \"[\\\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\\\",\\\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\\\"]\"") +
            "\nAs a json rpc call\n"
            + HelpExampleRpc("createmultisig", "2, \"[\\\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\\\",\\\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\\\"]\"")
        ;
        throw runtime_error(msg);
    }

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
            "verifymessage \"zcashaddress\" \"signature\" \"message\"\n"
            "\nVerify a signed message\n"
            "\nArguments:\n"
            "1. \"zcashaddress\"    (string, required) The Pastel address to use for the signature.\n"
            "2. \"signature\"       (string, required) The signature provided by the signer in base 64 encoding (see signmessage).\n"
            "3. \"message\"         (string, required) The message that was signed.\n"
            "\nResult:\n"
            "true|false   (boolean) If the signature is verified or not.\n"
            "\nExamples:\n"
            "\nUnlock the wallet for 30 seconds\n"
            + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") +
            "\nCreate the signature\n"
            + HelpExampleCli("signmessage", "\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\" \"my message\"") +
            "\nVerify the signature\n"
            + HelpExampleCli("verifymessage", "\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\" \"signature\" \"my message\"") +
            "\nAs json rpc\n"
            + HelpExampleRpc("verifymessage", "\"Ptor9ydHJuGpNWFAX3ZTu3bXevEhCaDVrsY\", \"signature\", \"my message\"")
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

    const CKeyID *keyID = std::get_if<CKeyID>(&destination);
    if (!keyID) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    }

    bool fInvalid = false;
    vector<unsigned char> vchSig = DecodeBase64(strSign.c_str(), &fInvalid);

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
            "setmocktime timestamp\n"
            "\nSet the local time to given timestamp (-regtest only)\n"
            "\nArguments:\n"
            "1. timestamp  (integer, required) Unix seconds-since-epoch timestamp\n"
            "   Pass 0 to go back to using the system time."
        );

    if (!Params().MineBlocksOnDemand())
        throw runtime_error("setmocktime for regression testing (-regtest mode) only");

    // cs_vNodes is locked and node send/receive times are updated
    // atomically with the time change to prevent peers from being
    // disconnected because we think we haven't communicated with them
    // in a long time.
    LOCK2(cs_main, cs_vNodes);

    RPCTypeCheck(params, boost::assign::list_of(UniValue::VNUM));
    SetMockTime(params[0].get_int64());

    uint64_t t = GetTime();
    for (auto pnode : vNodes)
        pnode->nLastSend = pnode->nLastRecv = t;

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
            "getmemoryinfo\n"
            "Returns an object containing information about memory usage.\n"
            "\nResult:\n"
            "{\n"
            "  \"locked\": {               (json object) Information about locked memory manager\n"
            "    \"used\": xxxxx,          (numeric) Number of bytes used\n"
            "    \"free\": xxxxx,          (numeric) Number of bytes available in current arenas\n"
            "    \"total\": xxxxxxx,       (numeric) Total number of bytes managed\n"
            "    \"locked\": xxxxxx,       (numeric) Amount of bytes that succeeded locking. If this number is smaller than total, locking pages failed at some point and key data could be swapped to disk.\n"
            "    \"chunks_used\": xxxxx,   (numeric) Number allocated chunks\n"
            "    \"chunks_free\": xxxxx,   (numeric) Number unused chunks\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getmemoryinfo", "")
            + HelpExampleRpc("getmemoryinfo", "")
        );
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("locked", RPCLockedMemoryInfo());
    return obj;
}

// insightexplorer
static bool getAddressFromIndex(
    CScript::ScriptType type, const uint160 &hash, std::string &address)
{
    KeyIO keyIO(Params());
    if (type == CScript::ScriptType::P2SH) {
        address = keyIO.EncodeDestination(CScriptID(hash));
    } else if (type == CScript::ScriptType::P2PKH) {
        address = keyIO.EncodeDestination(CKeyID(hash));
    } else {
        return false;
    }
    return true;
}

// This function accepts an address and returns in the output parameters
// the version and raw bytes for the RIPEMD-160 hash.
static bool getIndexKey(const CTxDestination& dest, uint160& hashBytes, CScript::ScriptType& type)
{
    if (!IsValidDestination(dest)) {
        return false;
    }
    if (IsKeyDestination(dest)) {
        auto x = std::get_if<CKeyID>(&dest);
        memcpy(&hashBytes, x->begin(), 20);
        type = CScript::ScriptType::P2PKH;
        return true;
    }
    if (IsScriptDestination(dest)) {
        auto x = std::get_if<CScriptID>(&dest);
        memcpy(&hashBytes, x->begin(), 20);
        type = CScript::ScriptType::P2SH;
        return true;
    }
    return false;
}

// insightexplorer
static bool getAddressesFromParams(
    const UniValue& params,
    std::vector<std::pair<uint160, CScript::ScriptType>> &addresses)
{
    std::vector<std::string> param_addresses;
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

    } else {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }

    KeyIO keyIO(Params());
    for (const auto& it : param_addresses) {
        CTxDestination address = keyIO.DecodeDestination(it);
        uint160 hashBytes;
        CScript::ScriptType type = CScript::ScriptType::UNKNOWN;
        if (!getIndexKey(address, hashBytes, type)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address" + params[0].get_str());
        }
        addresses.push_back(std::make_pair(std::move(hashBytes), std::move(type)));
    }
    return true;
}

UniValue getaddressmempool(const UniValue& params, bool fHelp)
{
    const std::string enableArg = "insightexplorer";
    const bool fEnableGetAddressMempool = fExperimentalMode && fInsightExplorer;
    std::string disabledMsg = "";
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

    std::vector<std::pair<uint160, CScript::ScriptType>> addresses;

    if (!getAddressesFromParams(params, addresses)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");
    }
    std::vector<std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>> indexes;
    mempool.getAddressIndex(addresses, indexes);
    std::sort(indexes.begin(), indexes.end(),
        [](const std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>& a,
           const std::pair<CMempoolAddressDeltaKey, CMempoolAddressDelta>& b) -> bool {
               return a.second.time < b.second.time;
           });

    UniValue result(UniValue::VARR);

    for (const auto& it : indexes) {
        std::string address;
        if (!getAddressFromIndex(it.first.type, it.first.addressBytes, address)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Unknown address type");
        }
        UniValue delta(UniValue::VOBJ);
        delta.pushKV("address", std::move(address));
        delta.pushKV("txid", it.first.txhash.GetHex());
        delta.pushKV("index", (int)it.first.index);
        delta.pushKV("patoshis", it.second.amount);
        delta.pushKV("timestamp", it.second.time);
        if (it.second.amount < 0) {
            delta.pushKV("prevtxid", it.second.prevhash.GetHex());
            delta.pushKV("prevout", (int)it.second.prevout);
        }
        result.push_back(std::move(delta));
    }
    return result;
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

    /* Address index */
    { "addressindex",       "getaddressmempool",      &getaddressmempool,      true  }, /* insight explorer */

    /* Not shown in help */
    { "hidden",             "setmocktime",            &setmocktime,            true  },
};

void RegisterMiscRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
