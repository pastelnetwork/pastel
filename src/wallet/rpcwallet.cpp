// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2018-2021 Pastel Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://www.opensource.org/licenses/mit-license.php .

#include <stdint.h>
#include <numeric>
#include <optional>
#include <variant>

#include <univalue.h>

#include <amount.h>
#include <consensus/upgrades.h>
#include <consensus/consensus.h>
#include <core_io.h>
#include <init.h>
#include <key_io.h>
#include <main.h>
#include <net.h>
#include <netbase.h>
#include <rpc/server.h>
#include <timedata.h>
#include <transaction_builder.h>
#include <util.h>
#include <utilmoneystr.h>
#include <primitives/transaction.h>
#include <zcbenchmarks.h>
#include <script/interpreter.h>
#include <zcash/Address.hpp>
#include <utfcpp/utf8.h>
#include <utiltime.h>
#include <asyncrpcoperation.h>
#include <asyncrpcqueue.h>
#include <wallet/wallet.h>
#include <wallet/walletdb.h>
#include <wallet/asyncrpcoperation_mergetoaddress.h>
#include <wallet/asyncrpcoperation_sendmany.h>
#include <wallet/asyncrpcoperation_shieldcoinbase.h>

#include <sodium.h>

using namespace std;

using namespace libzcash;

const string ADDR_TYPE_SAPLING = "sapling";

int64_t nWalletUnlockTime;
static CCriticalSection cs_nWalletUnlockTime;

// Private method:
UniValue z_getoperationstatus_IMPL(const UniValue&, bool);

string HelpRequiringPassphrase()
{
    return pwalletMain && pwalletMain->IsCrypted()
        ? "\nRequires wallet passphrase to be set with walletpassphrase call."
        : "";
}

bool EnsureWalletIsAvailable(bool avoidException)
{
    if (!pwalletMain)
    {
        if (!avoidException)
            throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");
        else
            return false;
    }
    return true;
}

void EnsureWalletIsUnlocked()
{
    if (pwalletMain->IsLocked())
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Error: Please enter the wallet passphrase with walletpassphrase first.");
}

void WalletTxToJSON(const CWalletTx& wtx, UniValue& entry)
{
    int confirms = wtx.GetDepthInMainChain();

    string status = "waiting";
    entry.pushKV("confirmations", confirms);
    if (wtx.IsCoinBase())
        entry.pushKV("generated", true);
    if (confirms > 0)
    {
        entry.pushKV("blockhash", wtx.hashBlock.GetHex());
        entry.pushKV("blockindex", wtx.nIndex);
        entry.pushKV("blocktime", mapBlockIndex[wtx.hashBlock]->GetBlockTime());
        entry.pushKV("expiryheight", (int64_t)wtx.nExpiryHeight);
        status = "mined";
    }
    else
    {
        const int height = chainActive.Height();
        if (!IsExpiredTx(wtx, height) && IsExpiringSoonTx(wtx, height + 1))
            status = "expiringsoon";
        else if (IsExpiredTx(wtx, height))
            status = "expired";
    }
    entry.pushKV("status", status);

    const uint256 hash = wtx.GetHash();
    entry.pushKV("txid", hash.GetHex());

    UniValue conflicts(UniValue::VARR);
    for (const auto& conflict : wtx.GetConflicts())
        conflicts.push_back(conflict.GetHex());
    entry.pushKV("walletconflicts", conflicts);
    entry.pushKV("time", wtx.GetTxTime());
    entry.pushKV("timereceived", (int64_t)wtx.nTimeReceived);
    for (const auto &[key, value] : wtx.mapValue)
        entry.pushKV(key, value);
}

string AccountFromValue(const UniValue& value)
{
    string strAccount = value.get_str();
    if (strAccount != "")
        throw JSONRPCError(RPC_WALLET_ACCOUNTS_UNSUPPORTED, "Accounts are unsupported");
    return strAccount;
}

UniValue getnewaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(getnewaddress ( "account" )

Returns a new Pastel address for receiving payments.

Arguments:
1. "account"        (string, optional) DEPRECATED. If provided, it MUST be set to the empty string "" to represent the default account. Passing any other string will result in an error.

Result:
"address"    (string) The new Pastel address

Examples:
)"
    + HelpExampleCli("getnewaddress", "")
    + HelpExampleRpc("getnewaddress", "")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    string strAccount;
    if (params.size() > 0)
        strAccount = AccountFromValue(params[0]); //-V1048

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    // Generate a new key that is added to wallet
    CPubKey newKey;
    if (!pwalletMain->GetKeyFromPool(newKey))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");
    CKeyID keyID = newKey.GetID();

    pwalletMain->SetAddressBook(keyID, strAccount, "receive");

    KeyIO keyIO(Params());
    return keyIO.EncodeDestination(keyID);
}


CTxDestination GetAccountAddress(string strAccount, bool bForceNew=false)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);

    CAccount account;
    walletdb.ReadAccount(strAccount, account);

    bool bKeyUsed = false;

    // Check if the current key has been used
    if (account.vchPubKey.IsValid())
    {
        CScript scriptPubKey = GetScriptForDestination(account.vchPubKey.GetID());
        for (const auto &[txid, wtx] : pwalletMain->mapWallet)
        {
            for (const auto& txout : wtx.vout)
            {
                if (txout.scriptPubKey == scriptPubKey)
                {
                    bKeyUsed = true;
                    break;
                }
            }
        }
    }

    // Generate a new key
    if (!account.vchPubKey.IsValid() || bForceNew || bKeyUsed)
    {
        if (!pwalletMain->GetKeyFromPool(account.vchPubKey))
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

        pwalletMain->SetAddressBook(account.vchPubKey.GetID(), strAccount, "receive");
        walletdb.WriteAccount(strAccount, account);
    }

    return account.vchPubKey.GetID();
}

UniValue getaccountaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getaccountaddress "account"

DEPRECATED. Returns the current Pastel address for receiving payments to this account.

Arguments:
1. "account"       (string, required) MUST be set to the empty string "" to represent the default account. Passing any other string will result in an error.

Result:
"zcashaddress"     (string) The account Pastel address

Examples:
)"
+ HelpExampleCli("getaccountaddress", "")
+ HelpExampleCli("getaccountaddress", "\"\"")
+ HelpExampleCli("getaccountaddress", "\"myaccount\"")
+ HelpExampleRpc("getaccountaddress", "\"myaccount\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Parse the account first so we don't generate a key if there's an error
    string strAccount = AccountFromValue(params[0]);

    UniValue ret(UniValue::VSTR);

    KeyIO keyIO(Params());
    ret = keyIO.EncodeDestination(GetAccountAddress(strAccount));
    return ret;
}


UniValue getrawchangeaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(getrawchangeaddress

Returns a new Pastel address, for receiving change.
This is for use with raw transactions, NOT normal use.

Result:
"address"    (string) The address

Examples:
)"
+ HelpExampleCli("getrawchangeaddress", "")
+ HelpExampleRpc("getrawchangeaddress", "")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (!pwalletMain->IsLocked())
        pwalletMain->TopUpKeyPool();

    CReserveKey reservekey(pwalletMain);
    CPubKey vchPubKey;
    if (!reservekey.GetReservedKey(vchPubKey))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out, please call keypoolrefill first");

    reservekey.KeepKey();

    CKeyID keyID = vchPubKey.GetID();

    KeyIO keyIO(Params());
    return keyIO.EncodeDestination(keyID);
}


UniValue setaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
R"(setaccount "zcashaddress" "account"

DEPRECATED. Sets the account associated with the given address.

Arguments:
1. "zcashaddress"  (string, required) The Pastel address to be associated with an account.
2. "account"       (string, required) MUST be set to the empty string "" to represent the default account. Passing any other string will result in an error.

Examples:
)"
+ HelpExampleCli("setaccount", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" \"tabby\"")
+ HelpExampleRpc("setaccount", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\", \"tabby\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    KeyIO keyIO(Params());
    CTxDestination dest = keyIO.DecodeDestination(params[0].get_str());
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pastel address");

    string strAccount;
    if (params.size() > 1)
        strAccount = AccountFromValue(params[1]); //-V1048

    // Only add the account if the address is yours.
    if (IsMine(*pwalletMain, dest))
    {
        // Detect when changing the account of an address that is the 'unused current key' of another account:
        if (pwalletMain->mapAddressBook.count(dest)) {
            string strOldAccount = pwalletMain->mapAddressBook[dest].name;
            if (dest == GetAccountAddress(strOldAccount)) {
                GetAccountAddress(strOldAccount, true);
            }
        }
        pwalletMain->SetAddressBook(dest, strAccount, "receive");
    }
    else
        throw JSONRPCError(RPC_MISC_ERROR, "setaccount can only be used with own address");

    return NullUniValue;
}


UniValue getaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getaccount "zcashaddress"

DEPRECATED. Returns the account associated with the given address.

Arguments:
1. "zcashaddress"  (string, required) The Pastel address for account lookup.

Result:
"accountname"      (string) the account address

Examples:
)"
+ HelpExampleCli("getaccount", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\"")
+ HelpExampleRpc("getaccount", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    KeyIO keyIO(Params());
    CTxDestination dest = keyIO.DecodeDestination(params[0].get_str());
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pastel address");

    string strAccount;
    map<CTxDestination, CAddressBookData>::iterator mi = pwalletMain->mapAddressBook.find(dest);
    if (mi != pwalletMain->mapAddressBook.end() && !(*mi).second.name.empty()) {
        strAccount = (*mi).second.name;
    }
    return strAccount;
}


UniValue getaddressesbyaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(getaddressesbyaccount "account"

DEPRECATED. Returns the list of addresses for the given account.

Arguments:
1. "account"  (string, required) MUST be set to the empty string \"\" to represent the default account. Passing any other string will result in an error.

Result:
[                     (json array of string)
  "zcashaddress"  (string) a Pastel address associated with the given account
  ,...
]
Examples:
)"
+ HelpExampleCli("getaddressesbyaccount", "\"tabby\"")
+ HelpExampleRpc("getaddressesbyaccount", "\"tabby\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strAccount = AccountFromValue(params[0]);

    KeyIO keyIO(Params());
    // Find all addresses that have the given account
    UniValue ret(UniValue::VARR);
    for (const pair<CTxDestination, CAddressBookData>& item : pwalletMain->mapAddressBook) {
        const CTxDestination& dest = item.first;
        const string& strName = item.second.name;
        if (strName == strAccount) {
            ret.push_back(keyIO.EncodeDestination(dest));
        }
    }
    return ret;
}

static void SendMoney(const CTxDestination &address, const CAmount nValue, bool fSubtractFeeFromAmount, CWalletTx& wtxNew)
{
    CAmount curBalance = pwalletMain->GetBalance();

    // Check amount
    if (nValue <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nValue > curBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient funds");

    // Parse Pastel address
    CScript scriptPubKey = GetScriptForDestination(address);

    // Create and send the transaction
    CReserveKey reservekey(pwalletMain);
    CAmount nFeeRequired;
    string strError;
    vector<CRecipient> vecSend;
    int nChangePosRet = -1;
    vecSend.emplace_back(scriptPubKey, nValue, fSubtractFeeFromAmount);
    if (!pwalletMain->CreateTransaction(vecSend, wtxNew, reservekey, nFeeRequired, nChangePosRet, strError))
    {
        if (!fSubtractFeeFromAmount && nValue + nFeeRequired > pwalletMain->GetBalance())
            strError = strprintf("Error: This transaction requires a transaction fee of at least %s because of its amount, complexity, or use of recently received funds!", FormatMoney(nFeeRequired));
        throw JSONRPCError(RPC_WALLET_ERROR, strError);
    }
    if (!pwalletMain->CommitTransaction(wtxNew, reservekey))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");
}

UniValue sendtoaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 5)
        throw runtime_error(
R"(sendtoaddress "t-address" amount ( "comment" "comment-to" subtractfeefromamount )
Send an amount to a given address. The amount is a real and is rounded to the nearest 0.00000001
") + HelpRequiringPassphrase() + R"(

Arguments:
1. "t-address"   (string, required) The Pastel address to send to.
2. "amount"      (numeric, required) The amount in )" + CURRENCY_UNIT + R"( to send. eg 0.1
3. "comment"     (string, optional) A comment used to store what the transaction is for.
                            This is not part of the transaction, just kept in your wallet.
4. "comment-to"  (string, optional) A comment to store the name of the person or organization 
                             to which you're sending the transaction. This is not part of the 
                             transaction, just kept in your wallet.
5. subtractfeefromamount  (boolean, optional, default=false) The fee will be deducted from the amount being sent.
                             The recipient will receive less Pastel than you enter in the amount field.

Result:
"transactionid"  (string) The transaction id.

Examples:
)"  + HelpExampleCli("sendtoaddress", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" 0.1")
    + HelpExampleCli("sendtoaddress", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" 0.1 \"donation\" \"seans outpost\"")
    + HelpExampleCli("sendtoaddress", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" 0.1 \"\" \"\" true")
    + HelpExampleRpc("sendtoaddress", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\", 0.1, \"donation\", \"seans outpost\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    KeyIO keyIO(Params());
    const CTxDestination dest = keyIO.DecodeDestination(params[0].get_str());
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pastel address");

    // Amount
    const CAmount nAmount = AmountFromValue(params[1]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");

    // Wallet comments
    CWalletTx wtx;
    if (params.size() > 2 && !params[2].isNull() && !params[2].get_str().empty())
        wtx.mapValue["comment"] = params[2].get_str();
    if (params.size() > 3 && !params[3].isNull() && !params[3].get_str().empty())
        wtx.mapValue["to"]      = params[3].get_str();

    bool fSubtractFeeFromAmount = false;
    if (params.size() > 4)
        fSubtractFeeFromAmount = params[4].get_bool();

    EnsureWalletIsUnlocked();

    SendMoney(dest, nAmount, fSubtractFeeFromAmount, wtx);

    return wtx.GetHash().GetHex();
}

UniValue listaddressamounts(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;
    
    if (fHelp || params.size() > 2)
        throw runtime_error(
R"(listaddressamounts (includeEmpty ismineFilter)

Lists balance on each address

Arguments:
1. includeEmpty   (numeric, optional, default=false) Whether to include addresses with empty balance.
2. ismineFilter   (string, optional, default=all) Whether to include "all", "watchOnly" or "spendableOnly" addresses.

Result:
{
  "address":      (string)  The Pastel address
     amount,      (numeric) The amount in )" + CURRENCY_UNIT + R"(
  ...
}

Examples:
)"
    + HelpExampleCli("listaddressamounts", "")
    + HelpExampleCli("listaddressamounts", "true spendableOnly")
    + HelpExampleRpc("listaddressamounts", "")
    + HelpExampleRpc("listaddressamounts", "true spendableOnly")
);
    
    bool bIncludeEmpty = false;
    if (params.size() >= 1)
        bIncludeEmpty = params[0].get_bool();
    isminetype isMineFilter = isminetype::ALL;
    if (params.size() >= 2)
    {
        const string& s = params[1].get_str();
        isMineFilter = StrToIsMineType(s, isminetype::NO);
        if (isMineFilter == isminetype::NO)
            throw JSONRPCError(RPC_INVALID_PARAMETER, tfm::format("Invalid ismineFilter parameter [%s]. Supported values are '%s','%s','%s'", s, 
                    ISMINE_FILTERSTR_SPENDABLE_ONLY, ISMINE_FILTERSTR_WATCH_ONLY, ISMINE_FILTERSTR_ALL));
    }
    
    LOCK2(cs_main, pwalletMain->cs_wallet);
    
    UniValue jsonBalances(UniValue::VOBJ);
    KeyIO keyIO(Params());
    const auto balances = pwalletMain->GetAddressBalances(isMineFilter);
    for (const auto &[txDestination, amount] : balances)
    {
        if (!bIncludeEmpty && amount == 0)
            continue;
        jsonBalances.pushKV(keyIO.EncodeDestination(txDestination), ValueFromAmount(amount));
    }
    return jsonBalances;
}

UniValue listaddressgroupings(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp)
        throw runtime_error(
R"(listaddressgroupings

Lists groups of addresses which have had their common ownership
made public by common use as inputs or as the resulting change
in past transactions

Result:
[
  [
    [
      "zcashaddress",     (string) The Pastel address
      amount,             (numeric) The amount in )" + CURRENCY_UNIT + R"(
      "account"           (string, optional) The account (DEPRECATED)
    ]
    ,...
  ]
  ,...
]
Examples:
)"
+ HelpExampleCli("listaddressgroupings", "")
+ HelpExampleRpc("listaddressgroupings", "")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    KeyIO keyIO(Params());
    UniValue jsonGroupings(UniValue::VARR);
    auto balances = pwalletMain->GetAddressBalances(isminetype::ALL);
    for (const set<CTxDestination>& grouping : pwalletMain->GetAddressGroupings()) {
        UniValue jsonGrouping(UniValue::VARR);
        for (const CTxDestination& address : grouping)
        {
            UniValue addressInfo(UniValue::VARR);
            addressInfo.push_back(keyIO.EncodeDestination(address));
            addressInfo.push_back(ValueFromAmount(balances[address]));
            {
                if (pwalletMain->mapAddressBook.find(address) != pwalletMain->mapAddressBook.end()) {
                    addressInfo.push_back(pwalletMain->mapAddressBook.find(address)->second.name);
                }
            }
            jsonGrouping.push_back(addressInfo);
        }
        jsonGroupings.push_back(jsonGrouping);
    }
    return jsonGroupings;
}

UniValue signmessage(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 2)
        throw runtime_error(
R"(signmessage "t-addr" "message"

Sign a message with the private key of a t-addr)"
+ HelpRequiringPassphrase() + R"(

Arguments:
1. "t-addr"          (string, required) The transparent address to use for the private key.
2. "message"         (string, required) The message to create a signature of.

Result:
"signature"          (string) The signature of the message encoded in base 64

Examples:
Unlock the wallet for 30 seconds
)" + HelpExampleCli("walletpassphrase", "\"mypassphrase\" 30") + R"(
Create the signature
)" + HelpExampleCli("signmessage", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" \"my message\"") + R"(
Verify the signature
)" + HelpExampleCli("verifymessage", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" \"signature\" \"my message\"") + R"(
As json rpc
)" + HelpExampleRpc("signmessage", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\", \"my message\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    string strAddress = params[0].get_str();
    string strMessage = params[1].get_str();

    KeyIO keyIO(Params());
    CTxDestination dest = keyIO.DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid address");
    }

    const CKeyID *keyID = get_if<CKeyID>(&dest);
    if (!keyID) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to key");
    }

    CKey key;
    if (!pwalletMain->GetKey(*keyID, key)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key not available");
    }

    CHashWriter ss(SER_GETHASH, 0);
    ss << STR_MSG_MAGIC;
    ss << strMessage;

    vector<unsigned char> vchSig;
    if (!key.SignCompact(ss.GetHash(), vchSig))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Sign failed");

    return EncodeBase64(&vchSig[0], vchSig.size());
}

UniValue getreceivedbyaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
R"(getreceivedbyaddress "zcashaddress" ( minconf )

Returns the total amount received by the given Pastel address in transactions with at least minconf confirmations.

Arguments:
1. "zcashaddress"      (string, required) The Pastel address for transactions.
2. minconf             (numeric, optional, default=1) Only include transactions confirmed at least this many times.

Result:
amount   (numeric) The total amount in )" + CURRENCY_UNIT + R"( received at this address.

Examples:
The amount from transactions with at least 1 confirmation
)" + HelpExampleCli("getreceivedbyaddress", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\"") + R"(
The amount including unconfirmed transactions, zero confirmations
)" + HelpExampleCli("getreceivedbyaddress", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" 0") + R"(
The amount with at least 6 confirmations, very safe
)" + HelpExampleCli("getreceivedbyaddress", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" 6") + R"(
As a json rpc call
)" + HelpExampleRpc("getreceivedbyaddress", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\", 6")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    KeyIO keyIO(Params());
    // Bitcoin address
    CTxDestination dest = keyIO.DecodeDestination(params[0].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pastel address");
    }
    CScript scriptPubKey = GetScriptForDestination(dest);
    if (!IsMine(*pwalletMain, scriptPubKey))
        return ValueFromAmount(0);

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Tally
    CAmount nAmount = 0;
    for (const auto &[txid, wtx] : pwalletMain->mapWallet)
    {
        if (wtx.IsCoinBase() || !CheckFinalTx(wtx))
            continue;

        for (const auto& txout : wtx.vout)
        {
            if ((txout.scriptPubKey == scriptPubKey) && (wtx.GetDepthInMainChain() >= nMinDepth))
                nAmount += txout.nValue;
        }
    }

    return ValueFromAmount(nAmount);
}

UniValue getreceivedbyaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
R"(getreceivedbyaccount "account" ( minconf )

DEPRECATED. Returns the total amount received by addresses with <account> in transactions with at least [minconf] confirmations.

Arguments:

1. "account"      (string, required) MUST be set to the empty string "" to represent the default account. Passing any other string will result in an error.
2. minconf        (numeric, optional, default=1) Only include transactions confirmed at least this many times.

Result:
  amount          (numeric) The total amount in )" + CURRENCY_UNIT + R"( received for this account.

Examples:
Amount received by the default account with at least 1 confirmation
)" + HelpExampleCli("getreceivedbyaccount", "\"\"") + R"(
Amount received at the tabby account including unconfirmed amounts with zero confirmations
)" + HelpExampleCli("getreceivedbyaccount", "\"tabby\" 0") + R"(
The amount with at least 6 confirmation, very safe
)" + HelpExampleCli("getreceivedbyaccount", "\"tabby\" 6") + R"(
As a json rpc call
)" + HelpExampleRpc("getreceivedbyaccount", "\"tabby\", 6")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();

    // Get the set of pub keys assigned to account
    string strAccount = AccountFromValue(params[0]);
    set<CTxDestination> setAddress = pwalletMain->GetAccountAddresses(strAccount);

    // Tally
    CAmount nAmount = 0;
    for (const auto &[txid, wtx] : pwalletMain->mapWallet)
    {
        if (wtx.IsCoinBase() || !CheckFinalTx(wtx))
            continue;

        for (const auto& txout : wtx.vout)
        {
            CTxDestination address;
            if (ExtractDestination(txout.scriptPubKey, address) && 
                IsMine(*pwalletMain, address) && 
                setAddress.count(address))
                if (wtx.GetDepthInMainChain() >= nMinDepth)
                    nAmount += txout.nValue;
        }
    }

    return ValueFromAmount(nAmount);
}

CAmount GetAccountBalance(CWalletDB& walletdb, const string& strAccount, int nMinDepth, const isminetype& filter)
{
    // Tally wallet transactions
    CAmount nBalance = 0;
    for (const auto &[txid, wtx] : pwalletMain->mapWallet)
    {
        if (!CheckFinalTx(wtx) || wtx.GetBlocksToMaturity() > 0 || wtx.GetDepthInMainChain() < 0)
            continue;

        CAmount nReceived, nSent, nFee;
        wtx.GetAccountAmounts(strAccount, nReceived, nSent, nFee, filter);

        if (nReceived != 0 && wtx.GetDepthInMainChain() >= nMinDepth)
            nBalance += nReceived;
        nBalance -= nSent + nFee;
    }

    // Tally internal accounting entries
    nBalance += walletdb.GetAccountCreditDebit(strAccount);

    return nBalance;
}

CAmount GetAccountBalance(const string& strAccount, int nMinDepth, const isminetype& filter)
{
    CWalletDB walletdb(pwalletMain->strWalletFile);
    return GetAccountBalance(walletdb, strAccount, nMinDepth, filter);
}


UniValue getbalance(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw runtime_error(
R"(getbalance ( "account" minconf includeWatchonly )

Returns the server's total available balance.

Arguments:
1. "account"        (string, optional) DEPRECATED. If provided, it MUST be set to the empty string "" or to the string "*", either of which will give the total available balance. Passing any other string will result in an error.
2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.
3. includeWatchonly (bool, optional, default=false) Also include balance in watchonly addresses (see 'importaddress')

Result:
  amount            (numeric) The total amount in )" + CURRENCY_UNIT + R"( received for this account.

Examples:
The total amount in the wallet
)" + HelpExampleCli("getbalance", "") + R"(
The total amount in the wallet at least 5 blocks confirmed
)" + HelpExampleCli("getbalance", "\"*\" 6") + R"(
As a json rpc call
)" + HelpExampleRpc("getbalance", "\"*\", 6")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (params.size() == 0)
        return  ValueFromAmount(pwalletMain->GetBalance());

    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();
    isminetype filter = isminetype::SPENDABLE;
    if(params.size() > 2)
        if(params[2].get_bool())
            filter = isminetype::ALL;

    if (params[0].get_str() == "*")
    {
        // Calculate total balance a different way from GetBalance()
        // (GetBalance() sums up all unspent TxOuts)
        // getbalance and "getbalance * 1 true" should return the same number
        CAmount nBalance = 0;
        list<COutputEntry> listReceived;
        list<COutputEntry> listSent;
        for (const auto &[txid, wtx] : pwalletMain->mapWallet)
        {
            if (!CheckFinalTx(wtx) || wtx.GetBlocksToMaturity() > 0 || wtx.GetDepthInMainChain() < 0)
                continue;

            CAmount allFee;
            string strSentAccount;
            listReceived.clear();
            listSent.clear();
            wtx.GetAmounts(listReceived, listSent, allFee, strSentAccount, filter);
            if (wtx.GetDepthInMainChain() >= nMinDepth)
            {
                for (const auto& r : listReceived)
                    nBalance += r.amount;
            }
            for (const auto& s : listSent)
                nBalance -= s.amount;
            nBalance -= allFee;
        }
        return  ValueFromAmount(nBalance);
    }

    const string strAccount = AccountFromValue(params[0]);
    const CAmount nBalance = GetAccountBalance(strAccount, nMinDepth, filter);

    return ValueFromAmount(nBalance);
}

UniValue getunconfirmedbalance(const UniValue &params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 0)
        throw runtime_error(
                "getunconfirmedbalance\n"
                "Returns the server's total unconfirmed balance\n");

    LOCK2(cs_main, pwalletMain->cs_wallet);

    return ValueFromAmount(pwalletMain->GetUnconfirmedBalance());
}


UniValue movecmd(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 5)
        throw runtime_error(
R"(move "fromaccount" "toaccount" amount ( minconf "comment" )

DEPRECATED. Move a specified amount from one account in your wallet to another.

Arguments:
1. "fromaccount"   (string, required) MUST be set to the empty string "" to represent the default account. Passing any other string will result in an error.
2. "toaccount"     (string, required) MUST be set to the empty string "" to represent the default account. Passing any other string will result in an error.
3. amount          (numeric) Quantity of )" + CURRENCY_UNIT + R"( to move between accounts.
4. minconf         (numeric, optional, default=1) Only use funds with at least this many confirmations.
5. "comment"       (string, optional) An optional comment, stored in the wallet only.

Result:
true|false           (boolean) true if successful.

Examples:
Move 0.01 )" + CURRENCY_UNIT + R"( from the default account to the account named tabby
)" + HelpExampleCli("move", "\"\" \"tabby\" 0.01") + R"(
Move 0.01 )" + CURRENCY_UNIT + R"( timotei to akiko with a comment and funds have 6 confirmations
)" + HelpExampleCli("move", "\"timotei\" \"akiko\" 0.01 6 \"happy birthday!\"") + R"(
As a json rpc call
)" + HelpExampleRpc("move", "\"timotei\", \"akiko\", 0.01, 6, \"happy birthday!\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strFrom = AccountFromValue(params[0]);
    string strTo = AccountFromValue(params[1]);
    CAmount nAmount = AmountFromValue(params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
    if (params.size() > 3)
        // unused parameter, used to be nMinDepth, keep type-checking it though
        (void)params[3].get_int();
    string strComment;
    if (params.size() > 4)
        strComment = params[4].get_str();

    CWalletDB walletdb(pwalletMain->strWalletFile);
    if (!walletdb.TxnBegin())
        throw JSONRPCError(RPC_DATABASE_ERROR, "database error");

    int64_t nNow = GetAdjustedTime();

    // Debit
    CAccountingEntry debit;
    debit.nOrderPos = pwalletMain->IncOrderPosNext(&walletdb);
    debit.strAccount = strFrom;
    debit.nCreditDebit = -nAmount;
    debit.nTime = nNow;
    debit.strOtherAccount = strTo;
    debit.strComment = strComment;
    walletdb.WriteAccountingEntry(debit);

    // Credit
    CAccountingEntry credit;
    credit.nOrderPos = pwalletMain->IncOrderPosNext(&walletdb);
    credit.strAccount = strTo;
    credit.nCreditDebit = nAmount;
    credit.nTime = nNow;
    credit.strOtherAccount = strFrom;
    credit.strComment = strComment;
    walletdb.WriteAccountingEntry(credit);

    if (!walletdb.TxnCommit())
        throw JSONRPCError(RPC_DATABASE_ERROR, "database error");

    return true;
}


UniValue sendfrom(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 3 || params.size() > 6)
        throw runtime_error(
R"(sendfrom "fromaccount" "tozcashaddress" amount ( minconf "comment" "comment-to" )

DEPRECATED (use sendtoaddress). Sent an amount from an account to a Pastel address.
The amount is a real and is rounded to the nearest 0.00000001.)"
+ HelpRequiringPassphrase() + R"(

Arguments:
1. "fromaccount"        (string, required) MUST be set to the empty string "" to represent the default account. Passing any other string will result in an error.
2. "tozcashaddress"     (string, required) The Pastel address to send funds to.
3. amount               (numeric, required) The amount in )" + CURRENCY_UNIT + R"( (transaction fee is added on top).
4. minconf              (numeric, optional, default=1) Only use funds with at least this many confirmations.
5. "comment"            (string, optional) A comment used to store what the transaction is for.
                                     This is not part of the transaction, just kept in your wallet.
6. "comment-to"         (string, optional) An optional comment to store the name of the person or organization 
                                     to which you're sending the transaction. This is not part of the transaction, 
                                     it is just kept in your wallet.

Result:
"transactionid"         (string) The transaction id.

Examples:
Send 0.01 )" + CURRENCY_UNIT + R"( from the default account to the address, must have at least 1 confirmation
)" + HelpExampleCli("sendfrom", "\"\" \"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" 0.01") + R"(
Send 0.01 from the tabby account to the given address, funds must have at least 6 confirmations
)" + HelpExampleCli("sendfrom", "\"tabby\" \"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" 0.01 6 \"donation\" \"seans outpost\"") + R"(
As a json rpc call
)" + HelpExampleRpc("sendfrom", "\"tabby\", \"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\", 0.01, 6, \"donation\", \"seans outpost\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    KeyIO keyIO(Params());
    string strAccount = AccountFromValue(params[0]);
    CTxDestination dest = keyIO.DecodeDestination(params[1].get_str());
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Pastel address");
    }
    CAmount nAmount = AmountFromValue(params[2]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
    int nMinDepth = 1;
    if (params.size() > 3)
        nMinDepth = params[3].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (params.size() > 4 && !params[4].isNull() && !params[4].get_str().empty())
        wtx.mapValue["comment"] = params[4].get_str();
    if (params.size() > 5 && !params[5].isNull() && !params[5].get_str().empty())
        wtx.mapValue["to"]      = params[5].get_str();

    EnsureWalletIsUnlocked();

    // Check funds
    CAmount nBalance = GetAccountBalance(strAccount, nMinDepth, isminetype::SPENDABLE);
    if (nAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Account has insufficient funds");

    SendMoney(dest, nAmount, false, wtx);

    return wtx.GetHash().GetHex();
}


UniValue sendmany(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 5)
        throw runtime_error(
R"(sendmany "fromaccount" {"address":amount,...} ( minconf "comment" ["address",...] )

Send multiple times. Amounts are decimal numbers with at most 8 digits of precision.)"
+ HelpRequiringPassphrase() + R"(

Arguments:
1. "fromaccount"           (string, required) MUST be set to the empty string "" to represent the default account. Passing any other string will result in an error.
2. "amounts"               (string, required) A json object with addresses and amounts
    {
      "address":amount     (numeric) The Pastel address is the key, the numeric amount in )" + CURRENCY_UNIT + R"( is the value
      ,...
    }
3. minconf                 (numeric, optional, default=1) Only use the balance confirmed at least this many times.
4. "comment"               (string, optional) A comment
5. subtractfeefromamount   (string, optional) A json array with addresses.
                            The fee will be equally deducted from the amount of each selected address.
                            Those recipients will receive less Pastel than you enter in their corresponding amount field.
                            If no addresses are specified here, the sender pays the fee.
    [
      "address"            (string) Subtract fee from this address
      ,...
    ]

Result:
"transactionid"            (string) The transaction id for the send. Only 1 transaction is created regardless of 
                                    the number of addresses.

Examples:
Send two amounts to two different addresses:
)" + HelpExampleCli("sendmany", "\"\" \"{\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\":0.01,\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\":0.02}\"") + R"(
Send two amounts to two different addresses setting the confirmation and comment:
)" + HelpExampleCli("sendmany", "\"\" \"{\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\":0.01,\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\":0.02}\" 6 \"testing\"") + R"(
Send two amounts to two different addresses, subtract fee from amount:
)" + HelpExampleCli("sendmany", "\"\" \"{\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\":0.01,\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\":0.02}\" 1 \"\" \"[\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\",\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\"]\"") + R"(
As a json rpc call
)" + HelpExampleRpc("sendmany", "\"\", \"{\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\":0.01,\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\":0.02}\", 6, \"testing\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strAccount = AccountFromValue(params[0]);
    UniValue sendTo = params[1].get_obj();
    int nMinDepth = 1;
    if (params.size() > 2)
        nMinDepth = params[2].get_int();

    CWalletTx wtx;
    wtx.strFromAccount = strAccount;
    if (params.size() > 3 && !params[3].isNull() && !params[3].get_str().empty())
        wtx.mapValue["comment"] = params[3].get_str();

    UniValue subtractFeeFromAmount(UniValue::VARR);
    if (params.size() > 4)
        subtractFeeFromAmount = params[4].get_array();

    set<CTxDestination> destinations;
    vector<CRecipient> vecSend;

    KeyIO keyIO(Params());
    CAmount totalAmount = 0;
    vector<string> keys = sendTo.getKeys();
    for (const string& name_ : keys) {
        CTxDestination dest = keyIO.DecodeDestination(name_);
        if (!IsValidDestination(dest)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, string("Invalid Pastel address: ") + name_);
        }

        if (destinations.count(dest)) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ") + name_);
        }
        destinations.insert(dest);

        CScript scriptPubKey = GetScriptForDestination(dest);
        CAmount nAmount = AmountFromValue(sendTo[name_]);
        if (nAmount <= 0)
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid amount for send");
        totalAmount += nAmount;

        bool fSubtractFeeFromAmount = false;
        for (size_t idx = 0; idx < subtractFeeFromAmount.size(); idx++) {
            const UniValue& addr = subtractFeeFromAmount[idx];
            if (addr.get_str() == name_)
                fSubtractFeeFromAmount = true;
        }

        CRecipient recipient = {scriptPubKey, nAmount, fSubtractFeeFromAmount};
        vecSend.push_back(recipient);
    }

    EnsureWalletIsUnlocked();

    // Check funds
    CAmount nBalance = GetAccountBalance(strAccount, nMinDepth, isminetype::SPENDABLE);
    if (totalAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
                           strprintf("\"Account has insufficient funds: needs %s coins; has only %s coins spendable", FormatMoney(totalAmount), FormatMoney(nBalance) ) );

    // Send
    CReserveKey keyChange(pwalletMain);
    CAmount nFeeRequired = 0;
    int nChangePosRet = -1;
    string strFailReason;
    bool fCreated = pwalletMain->CreateTransaction(vecSend, wtx, keyChange, nFeeRequired, nChangePosRet, strFailReason);
    if (!fCreated)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, strFailReason);
    if (!pwalletMain->CommitTransaction(wtx, keyChange))
        throw JSONRPCError(RPC_WALLET_ERROR, "Transaction commit failed");

    return wtx.GetHash().GetHex();
}

// Defined in rpc/misc.cpp
extern CScript _createmultisig_redeemScript(const UniValue& params);

UniValue addmultisigaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 3)
    {
        throw runtime_error(
R"(addmultisigaddress nrequired ["key",...] ( "account" )

Add a nrequired-to-sign multisignature address to the wallet.
Each key is a Pastel address or hex-encoded public key.
If 'account' is specified (DEPRECATED), assign address to that account.

Arguments:
1. nrequired      (numeric, required) The number of required signatures out of the n keys or addresses.
2. "keysobject"   (string, required) A json array of Pastel addresses or hex-encoded public keys
     [
       "address"  (string) Pastel address or hex-encoded public key
       ...,
     ]
3. "account"      (string, optional) DEPRECATED. If provided, MUST be set to the empty string "" to represent the default account. Passing any other string will result in an error.

Result:
"zcashaddress"    (string) A Pastel address associated with the keys.

Examples:
Add a multisig address from 2 addresses
)" + HelpExampleCli("addmultisigaddress", "2 \"[\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\",\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\"]\"") + R"(
As json rpc call
)" + HelpExampleRpc("addmultisigaddress", "2, \"[\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\",\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\"]\"")
);
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strAccount;
    if (params.size() > 2)
        strAccount = AccountFromValue(params[2]); //-V1048

    // Construct using pay-to-script-hash:
    CScript inner = _createmultisig_redeemScript(params);
    CScriptID innerID(inner);
    pwalletMain->AddCScript(inner);

    pwalletMain->SetAddressBook(innerID, strAccount, "send");
    KeyIO keyIO(Params());
    return keyIO.EncodeDestination(innerID);
}


struct tallyitem
{
    CAmount nAmount;
    int nConf;
    v_uint256 txids;
    bool fIsWatchonly;
    tallyitem()
    {
        nAmount = 0;
        nConf = numeric_limits<int>::max();
        fIsWatchonly = false;
    }
};

UniValue ListReceived(const UniValue& params, bool fByAccounts)
{
    // Minimum confirmations
    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    // Whether to include empty accounts
    bool fIncludeEmpty = false;
    if (params.size() > 1)
        fIncludeEmpty = params[1].get_bool();

    isminetype filter = isminetype::SPENDABLE;
    if(params.size() > 2)
        if(params[2].get_bool())
            filter = isminetype::ALL;

    // Tally
    map<CTxDestination, tallyitem> mapTally;
    for (const auto &[txid, wtx] : pwalletMain->mapWallet)
    {
        if (wtx.IsCoinBase() || !CheckFinalTx(wtx))
            continue;

        int nDepth = wtx.GetDepthInMainChain();
        if (nDepth < nMinDepth)
            continue;

        for (const auto& txout : wtx.vout)
        {
            CTxDestination address;
            if (!ExtractDestination(txout.scriptPubKey, address))
                continue;

            const isminetype mine = GetIsMine(*pwalletMain, address);
            if (!IsMineType(mine, filter))
                continue;

            tallyitem& item = mapTally[address];
            item.nAmount += txout.nValue;
            item.nConf = min(item.nConf, nDepth);
            item.txids.push_back(wtx.GetHash());
            if (IsMineWatchOnly(mine))
                item.fIsWatchonly = true;
        }
    }

    KeyIO keyIO(Params());

    // Reply
    UniValue ret(UniValue::VARR);
    map<string, tallyitem> mapAccountTally;
    for (const auto& item : pwalletMain->mapAddressBook)
    {
        const CTxDestination& dest = item.first;
        const string& strAccount = item.second.name;
        auto it = mapTally.find(dest);
        if (it == mapTally.end() && !fIncludeEmpty)
            continue;

        CAmount nAmount = 0;
        int nConf = numeric_limits<int>::max();
        bool fIsWatchonly = false;
        if (it != mapTally.end())
        {
            nAmount = (*it).second.nAmount;
            nConf = (*it).second.nConf;
            fIsWatchonly = (*it).second.fIsWatchonly;
        }

        if (fByAccounts)
        {
            tallyitem& item = mapAccountTally[strAccount];
            item.nAmount += nAmount;
            item.nConf = min(item.nConf, nConf);
            item.fIsWatchonly = fIsWatchonly;
        }
        else
        {
            UniValue obj(UniValue::VOBJ);
            if(fIsWatchonly)
                obj.pushKV("involvesWatchonly", true);
            obj.pushKV("address",       keyIO.EncodeDestination(dest));
            obj.pushKV("account",       strAccount);
            obj.pushKV("amount",        ValueFromAmount(nAmount));
            obj.pushKV("confirmations", (nConf == numeric_limits<int>::max() ? 0 : nConf));
            UniValue transactions(UniValue::VARR);
            if (it != mapTally.end())
            {
                for (const auto& item : it->second.txids)
                    transactions.push_back(item.GetHex());
            }
            obj.pushKV("txids", transactions);
            ret.push_back(obj);
        }
    }

    if (fByAccounts)
    {
        for (const auto &[sAccount, tally] : mapAccountTally)
        {
            const CAmount nAmount = tally.nAmount;
            int nConf = tally.nConf;
            UniValue obj(UniValue::VOBJ);
            if (tally.fIsWatchonly)
                obj.pushKV("involvesWatchonly", true);
            obj.pushKV("account",       sAccount);
            obj.pushKV("amount",        ValueFromAmount(nAmount));
            obj.pushKV("confirmations", (nConf == numeric_limits<int>::max() ? 0 : nConf));
            ret.push_back(obj);
        }
    }

    return ret;
}

UniValue listreceivedbyaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw runtime_error(
R"(listreceivedbyaddress ( minconf includeempty includeWatchonly)

List balances by receiving address.

Arguments:
1. minconf          (numeric, optional, default=1) The minimum number of confirmations before payments are included.
2. includeempty     (numeric, optional, default=false) Whether to include addresses that haven't received any payments.
3. includeWatchonly (bool, optional, default=false) Whether to include watchonly addresses (see 'importaddress').

Result:
[
  {
    "involvesWatchonly" : true,        (bool) Only returned if imported addresses were involved in transaction
    "address" : "receivingaddress",    (string) The receiving address
    "account" : "accountname",         (string) DEPRECATED. The account of the receiving address. The default account is "".
    "amount" : x.xxx,                  (numeric) The total amount in )" + CURRENCY_UNIT + R"( received by the address
    "confirmations" : n                (numeric) The number of confirmations of the most recent transaction included
  }
  ,...
]

Examples:
)"
+ HelpExampleCli("listreceivedbyaddress", "")
+ HelpExampleCli("listreceivedbyaddress", "6 true")
+ HelpExampleRpc("listreceivedbyaddress", "6, true, true")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    return ListReceived(params, false);
}

UniValue listreceivedbyaccount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw runtime_error(
R"(listreceivedbyaccount ( minconf includeempty includeWatchonly)

DEPRECATED. List balances by account.

Arguments:
1. minconf          (numeric, optional, default=1) The minimum number of confirmations before payments are included.
2. includeempty     (boolean, optional, default=false) Whether to include accounts that haven't received any payments.
3. includeWatchonly (bool, optional, default=false) Whether to include watchonly addresses (see 'importaddress').

Result:
[
  {
    "involvesWatchonly" : true,   (bool) Only returned if imported addresses were involved in transaction
    "account" : "accountname",    (string) The account name of the receiving account
    "amount" : x.xxx,             (numeric) The total amount received by addresses with this account
    "confirmations" : n           (numeric) The number of confirmations of the most recent transaction included
  }
  ,...
]

Examples:
)"
+ HelpExampleCli("listreceivedbyaccount", "")
+ HelpExampleCli("listreceivedbyaccount", "6 true")
+ HelpExampleRpc("listreceivedbyaccount", "6, true, true")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    return ListReceived(params, true);
}

static void MaybePushAddress(UniValue & entry, const CTxDestination &dest)
{
    if (!IsValidDestination(dest))
        return;
    KeyIO keyIO(Params());
    entry.pushKV("address", keyIO.EncodeDestination(dest));
}

/**
 * List transactions based on the given criteria.
 *
 * \param  wtx        The wallet transaction.
 * \param  nMinDepth  The minimum confirmation depth.
 * \param  fLong      Whether to include the JSON version of the transaction.
 * \param  ret        The UniValue into which the result is stored.
 * \param  filter     The "is mine" filter.
 */
void ListTransactions(const CWalletTx& wtx, const string& strAccount, int nMinDepth, bool fLong, UniValue& ret, const isminetype& filter)
{
    CAmount nFee;
    string strSentAccount;
    list<COutputEntry> listReceived;
    list<COutputEntry> listSent;

    wtx.GetAmounts(listReceived, listSent, nFee, strSentAccount, filter);

    bool fAllAccounts = (strAccount == string("*"));
    bool involvesWatchonly = wtx.IsFromMe(isminetype::WATCH_ONLY);

    // Sent
    if ((!listSent.empty() || nFee != 0) && (fAllAccounts || strAccount == strSentAccount))
    {
        for (const auto& s: listSent)
        {
            UniValue entry(UniValue::VOBJ);
            if(involvesWatchonly || IsMineWatchOnly(::GetIsMine(*pwalletMain, s.destination)))
                entry.pushKV("involvesWatchonly", true);
            entry.pushKV("account", strSentAccount);
            MaybePushAddress(entry, s.destination);
            entry.pushKV("category", "send");
            entry.pushKV("amount", ValueFromAmount(-s.amount));
            entry.pushKV("amountPat", -s.amount);
            entry.pushKV("vout", s.vout);
            entry.pushKV("fee", ValueFromAmount(-nFee));
            if (fLong)
                WalletTxToJSON(wtx, entry);
            entry.pushKV("size", static_cast<uint64_t>(GetSerializeSize(static_cast<CTransaction>(wtx), SER_NETWORK, PROTOCOL_VERSION)));
            ret.push_back(entry);
        }
    }

    // Received
    if (!listReceived.empty() && wtx.GetDepthInMainChain() >= nMinDepth)
    {
        for (const auto& r : listReceived)
        {
            string account;
            if (pwalletMain->mapAddressBook.count(r.destination))
                account = pwalletMain->mapAddressBook[r.destination].name;
            if (fAllAccounts || (account == strAccount))
            {
                UniValue entry(UniValue::VOBJ);
                if(involvesWatchonly || IsMineWatchOnly(::GetIsMine(*pwalletMain, r.destination)))
                    entry.pushKV("involvesWatchonly", true);
                entry.pushKV("account", account);
                MaybePushAddress(entry, r.destination);
                if (wtx.IsCoinBase())
                {
                    if (wtx.GetDepthInMainChain() < 1)
                        entry.pushKV("category", "orphan");
                    else if (wtx.GetBlocksToMaturity() > 0)
                        entry.pushKV("category", "immature");
                    else
                        entry.pushKV("category", "generate");
                }
                else
                {
                    entry.pushKV("category", "receive");
                }
                entry.pushKV("amount", ValueFromAmount(r.amount));
                entry.pushKV("amountPat", r.amount);
                entry.pushKV("vout", r.vout);
                if (fLong)
                    WalletTxToJSON(wtx, entry);
                entry.pushKV("size", static_cast<uint64_t>(GetSerializeSize(static_cast<CTransaction>(wtx), SER_NETWORK, PROTOCOL_VERSION)));
                ret.push_back(move(entry));
            }
        }
    }
}

void AcentryToJSON(const CAccountingEntry& acentry, const string& strAccount, UniValue& ret)
{
    bool fAllAccounts = (strAccount == string("*"));

    if (fAllAccounts || acentry.strAccount == strAccount)
    {
        UniValue entry(UniValue::VOBJ);
        entry.pushKV("account", acentry.strAccount);
        entry.pushKV("category", "move");
        entry.pushKV("time", acentry.nTime);
        entry.pushKV("amount", ValueFromAmount(acentry.nCreditDebit));
        entry.pushKV("otheraccount", acentry.strOtherAccount);
        entry.pushKV("comment", acentry.strComment);
        ret.push_back(entry);
    }
}

UniValue listtransactions(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 4)
        throw runtime_error(
R"(listtransactions ( "account" count from includeWatchonly)
            
Returns up to 'count' most recent transactions skipping the first 'from' transactions for account 'account'.

Arguments:
1. "account"    (string, optional) DEPRECATED. The account name. Should be "*".
2. count          (numeric, optional, default=10) The number of transactions to return
3. from           (numeric, optional, default=0) The number of transactions to skip
4. includeWatchonly (bool, optional, default=false) Include transactions to watchonly addresses (see 'importaddress')

Result:
[
  {
    "account":"accountname",        (string) DEPRECATED. The account name associated with the transaction.
                                             It will be "" for the default account.
    "address":"zcashaddress",       (string) The Pastel address of the transaction. Not present for
                                             move transactions (category = move).
    "category":"send|receive|move", (string) The transaction category. 'move' is a local (off blockchain)
                                             transaction between accounts, and not associated with an address,
                                             transaction id or block. 'send' and 'receive' transactions are
                                             associated with an address, transaction id and block details
    "amount": x.xxx,                (numeric) The amount in " + CURRENCY_UNIT + ". This is negative for the 'send' category, and for the
                                              'move' category for moves outbound. It is positive for the 'receive' category,
                                              and for the 'move' category for inbound funds.
    "vout" : n,               (numeric) the vout value
    "fee": x.xxx,             (numeric) The amount of the fee in " + CURRENCY_UNIT + ". This is negative and only available for the
                                        'send' category of transactions.
    "confirmations": n,       (numeric) The number of confirmations for the transaction. Available for 'send' and
                                        'receive' category of transactions.
    "blockhash": "hashvalue", (string)  The block hash containing the transaction. Available for 'send' and 'receive'
                                        category of transactions.
    "blockindex": n,          (numeric) The block index containing the transaction. Available for 'send' and 'receive'
                                        category of transactions.
    "txid": "transactionid",  (string)  The transaction id. Available for 'send' and 'receive' category of transactions.
    "time": xxx,              (numeric) The transaction time in seconds since epoch (midnight Jan 1 1970 GMT).
    "timereceived": xxx,      (numeric) The time received in seconds since epoch (midnight Jan 1 1970 GMT). Available
                                        for 'send' and 'receive' category of transactions.
    "comment": "...",         (string)  If a comment is associated with the transaction.
    "otheraccount": "accountname",  (string) For the 'move' category of transactions, the account the funds came
                                             from (for receiving funds, positive amounts), or went to (for sending funds,
                                             negative amounts).
    "size": n,                (numeric) Transaction size in bytes
  }
]

Examples:
List the most recent 10 transactions in the systems:
)" + HelpExampleCli("listtransactions", "") + R"(
List transactions 100 to 120:
)" + HelpExampleCli("listtransactions", "\"*\" 20 100") + R"(
As a json rpc call:
)" + HelpExampleRpc("listtransactions", "\"*\", 20, 100")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string strAccount = "*";
    if (params.size() > 0)
        strAccount = params[0].get_str();
    size_t nCount = 10;
    // "count" parameter
    if (params.size() > 1)
    {
        const int nIntValue = params[1].get_int();
        if (nIntValue < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative 'count' parameter");
        nCount = static_cast<size_t>(nIntValue);
    }
    size_t nFrom = 0;
    if (params.size() > 2)
    {
        const int nIntValue = params[2].get_int();
        if (nIntValue < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Negative 'from' parameter");
        nFrom = static_cast<size_t>(nIntValue);
    }
    isminetype filter = isminetype::SPENDABLE;
    if(params.size() > 3)
        if(params[3].get_bool())
            filter = isminetype::ALL;

    UniValue ret(UniValue::VARR);

    list<CAccountingEntry> acentries;
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        CWallet::TxItems txOrdered = pwalletMain->OrderedTxItems(acentries, strAccount);

        // iterate backwards until we have nCount items to return:
        for (auto it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
        {
            CWalletTx *const pwtx = (*it).second.first;
            if (pwtx != 0)
                ListTransactions(*pwtx, strAccount, 0, true, ret, filter);
            CAccountingEntry *const pacentry = (*it).second.second;
            if (pacentry != 0)
                AcentryToJSON(*pacentry, strAccount, ret);

            if (ret.size() >= nCount + nFrom) 
                break;
        }
    }

    // ret is newest to oldest
    if (nFrom > ret.size())
        nFrom = ret.size();
    if (nFrom + nCount > ret.size())
        nCount = ret.size() - nFrom;

    auto arrTmp = ret.getValues();

    auto first = arrTmp.cbegin();
    advance(first, nFrom);
    auto last = arrTmp.cbegin();
    advance(last, nFrom + nCount);

    if (last != arrTmp.cend()) 
        arrTmp.erase(last, arrTmp.cend());
    if (first != arrTmp.cbegin()) 
        arrTmp.erase(arrTmp.cbegin(), first);

    reverse(arrTmp.begin(), arrTmp.end()); // Return oldest to newest

    ret.clear();
    ret.setArray();
    ret.push_backV(move(arrTmp));

    return ret;
}

UniValue listaccounts(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 2)
        throw runtime_error(
R"(listaccounts ( minconf includeWatchonly)

DEPRECATED. Returns Object that has account names as keys, account balances as values.

Arguments:
1. minconf          (numeric, optional, default=1) Only include transactions with at least this many confirmations
2. includeWatchonly (bool, optional, default=false) Include balances in watchonly addresses (see 'importaddress')

Result:
{                      (json object where keys are account names, and values are numeric balances
  "account": x.xxx,  (numeric) The property name is the account name, and the value is the total balance for the account.
  ...
}

Examples:
List account balances where there at least 1 confirmation
)" + HelpExampleCli("listaccounts", "") + R"(
List account balances including zero confirmation transactions
)" + HelpExampleCli("listaccounts", "0") + R"(
List account balances for 6 or more confirmations
)" + HelpExampleCli("listaccounts", "6") + R"(
As json rpc call
)" + HelpExampleRpc("listaccounts", "6")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();
    isminetype filter = isminetype::SPENDABLE;
    if(params.size() > 1)
        if(params[1].get_bool())
            filter = isminetype::ALL;

    map<string, CAmount> mapAccountBalances;
    for (const auto &[txDest, addressBookData] : pwalletMain->mapAddressBook)
    {
        if (IsMineType(GetIsMine(*pwalletMain, txDest), filter)) // This address belongs to me
            mapAccountBalances[addressBookData.name] = 0;
    }

    for (const auto &[txid, wtx] : pwalletMain->mapWallet)
    {
        CAmount nFee;
        string strSentAccount;
        list<COutputEntry> listReceived;
        list<COutputEntry> listSent;
        int nDepth = wtx.GetDepthInMainChain();
        if (wtx.GetBlocksToMaturity() > 0 || nDepth < 0)
            continue;
        wtx.GetAmounts(listReceived, listSent, nFee, strSentAccount, filter);
        mapAccountBalances[strSentAccount] -= nFee;
        for (const auto& s: listSent)
            mapAccountBalances[strSentAccount] -= s.amount;
        if (nDepth >= nMinDepth)
        {
            for (const auto& r : listReceived)
            {
                if (pwalletMain->mapAddressBook.count(r.destination))
                    mapAccountBalances[pwalletMain->mapAddressBook[r.destination].name] += r.amount;
                else
                    mapAccountBalances[""] += r.amount;
            }
        }
    }

    list<CAccountingEntry> acentries;
    CWalletDB(pwalletMain->strWalletFile).ListAccountCreditDebit("*", acentries);
    for (const auto &entry : acentries)
        mapAccountBalances[entry.strAccount] += entry.nCreditDebit;

    UniValue ret(UniValue::VOBJ);
    for (const auto &[hash, amount] : mapAccountBalances)
        ret.pushKV(hash, ValueFromAmount(amount));
    return ret;
}

UniValue listsinceblock(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp)
        throw runtime_error(
R"(listsinceblock ( "blockhash" target-confirmations includeWatchonly)

Get all transactions in blocks since block [blockhash], or all transactions if omitted

Arguments:
1. "blockhash"   (string, optional) The block hash to list transactions since
2. target-confirmations:    (numeric, optional) The confirmations required, must be 1 or more
3. includeWatchonly:        (bool, optional, default=false) Include transactions to watchonly addresses (see 'importaddress')

Result:
{
  "transactions": [
    "account":"accountname",    (string) DEPRECATED. The account name associated with the transaction. Will be "" for the default account.
    "address":"zcashaddress",   (string) The Pastel address of the transaction. Not present for move transactions (category = move).
    "category":"send|receive",  (string) The transaction category. 'send' has negative amounts, 'receive' has positive amounts.
    "amount": x.xxx,            (numeric) The amount in )" + CURRENCY_UNIT + R"(. This is negative for the 'send' category, and for the 'move' category for moves 
                                          outbound. It is positive for the 'receive' category, and for the 'move' category for inbound funds.
    "vout" : n,                 (numeric) the vout value
    "fee": x.xxx,               (numeric) The amount of the fee in )" + CURRENCY_UNIT + R"(. This is negative and only available for the 'send' category of transactions.
    "confirmations": n,         (numeric) The number of confirmations for the transaction. Available for 'send' and 'receive' category of transactions.
    "blockhash": "hashvalue",   (string) The block hash containing the transaction. Available for 'send' and 'receive' category of transactions.
    "blockindex": n,            (numeric) The block index containing the transaction. Available for 'send' and 'receive' category of transactions.
    "blocktime": xxx,           (numeric) The block time in seconds since epoch (1 Jan 1970 GMT).
    "txid": "transactionid",    (string) The transaction id. Available for 'send' and 'receive' category of transactions.
    "time": xxx,                (numeric) The transaction time in seconds since epoch (Jan 1 1970 GMT).
    "timereceived": xxx,        (numeric) The time received in seconds since epoch (Jan 1 1970 GMT). Available for 'send' and 'receive' category of transactions.
    "comment": "...",           (string) If a comment is associated with the transaction.
    "to": "...",                (string) If a comment to is associated with the transaction.
  ],
  "lastblock": "lastblockhash"  (string) The hash of the last block
}

Examples:
)"
+ HelpExampleCli("listsinceblock", "")
+ HelpExampleCli("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\" 6")
+ HelpExampleRpc("listsinceblock", "\"000000000000000bacf66f7497b7dc45ef753ee9a7d38571037cdb1a57f663ad\", 6")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    CBlockIndex *pindex = nullptr;
    int target_confirms = 1;
    isminetype filter = isminetype::SPENDABLE;

    if (params.size() > 0)
    {
        uint256 blockId;

        blockId.SetHex(params[0].get_str());
        BlockMap::iterator it = mapBlockIndex.find(blockId);
        if (it != mapBlockIndex.end())
            pindex = it->second;
    }

    if (params.size() > 1)
    {
        target_confirms = params[1].get_int();

        if (target_confirms < 1)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
    }

    if(params.size() > 2)
        if(params[2].get_bool())
            filter = isminetype::ALL;

    int depth = pindex ? (1 + chainActive.Height() - pindex->nHeight) : -1;

    UniValue transactions(UniValue::VARR);

    for (const auto &[txid, wtx] : pwalletMain->mapWallet)
    {
        if (depth == -1 || wtx.GetDepthInMainChain() < depth)
            ListTransactions(wtx, "*", 0, true, transactions, filter);
    }

    CBlockIndex *pblockLast = chainActive[chainActive.Height() + 1 - target_confirms];
    uint256 lastblock = pblockLast ? pblockLast->GetBlockHash() : uint256();

    UniValue ret(UniValue::VOBJ);
    ret.pushKV("transactions", transactions);
    ret.pushKV("lastblock", lastblock.GetHex());

    return ret;
}

UniValue gettransaction(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
R"(gettransaction "txid" ( includeWatchonly )
Get detailed information about in-wallet transaction <txid>

Arguments:
1. "txid"                   (string, required) The transaction id
2. "includeWatchonly"       (bool, optional, default=false) Whether to include watchonly addresses in balance calculation and details[]

Result:
{
  "status" : "mined|waiting|expiringsoon|expired", (string) The transaction status, can be 'mined', 'waiting', 'expiringsoon' or 'expired'
  "amount" : x.xxx,         (numeric) The transaction amount in )" + CURRENCY_UNIT + R"(
  "amountPat" : xxx,        (numeric) The amount in )" + MINOR_CURRENCY_UNIT + R"(
  "confirmations" : n,      (numeric) The number of confirmations
  "blockhash" : "hash",     (string) The block hash
  "blockindex" : xx,        (numeric) The block index
  "blocktime" : ttt,        (numeric) The time in seconds since epoch (1 Jan 1970 GMT)
  "txid" : "transactionid", (string) The transaction id.
  "time" : ttt,             (numeric) The transaction time in seconds since epoch (1 Jan 1970 GMT)
  "timereceived" : ttt,     (numeric) The time received in seconds since epoch (1 Jan 1970 GMT)
  "details" : [
    {
      "account" : "accountname",   (string) DEPRECATED. The account name involved in the transaction, can be "" for the default account.
      "address" : "zcashaddress",  (string) The Pastel address involved in the transaction
      "category" : "send|receive", (string) The category, either 'send' or 'receive'
      "amount" : x.xxx             (numeric) The amount in )" + CURRENCY_UNIT + R"(
      "amountPat" : xxx            (numeric) The amount in )" + MINOR_CURRENCY_UNIT + R"(
      "vout" : n,                  (numeric) the vout value
    }
    ,...
  ],
  "hex" : "data"                   (string) Raw data for transaction
}

Examples:
)"
    + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
    + HelpExampleCli("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\" true")
    + HelpExampleRpc("gettransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 hash;
    hash.SetHex(params[0].get_str());

    isminetype filter = isminetype::SPENDABLE;
    if(params.size() > 1)
        if(params[1].get_bool())
            filter = isminetype::ALL;

    UniValue entry(UniValue::VOBJ);
    if (!pwalletMain->mapWallet.count(hash))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    const CWalletTx& wtx = pwalletMain->mapWallet[hash];

    CAmount nCredit = wtx.GetCredit(filter);
    CAmount nDebit = wtx.GetDebit(filter);
    CAmount nNet = nCredit - nDebit;
    CAmount nFee = (wtx.IsFromMe(filter) ? wtx.GetValueOut() - nDebit : 0);

    entry.pushKV("amount", ValueFromAmount(nNet - nFee));
    entry.pushKV("amountPat", nNet - nFee);
    if (wtx.IsFromMe(filter))
        entry.pushKV("fee", ValueFromAmount(nFee));

    WalletTxToJSON(wtx, entry);

    UniValue details(UniValue::VARR);
    ListTransactions(wtx, "*", 0, false, details, filter);
    entry.pushKV("details", move(details));

    string strHex = EncodeHexTx(static_cast<CTransaction>(wtx));
    entry.pushKV("hex", strHex);

    return entry;
}


UniValue backupwallet(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(backupwallet "destination"

Safely copies wallet.dat to destination filename

Arguments:
1. "destination"   (string, required) The destination filename, saved in the directory set by -exportdir option.

Result:
"path"             (string) The full path of the destination file

Examples:
)"
+ HelpExampleCli("backupwallet", "\"backupdata\"")
+ HelpExampleRpc("backupwallet", "\"backupdata\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    fs::path exportdir;
    try {
        exportdir = GetExportDir();
    } catch (const runtime_error& e) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, e.what());
    }
    if (exportdir.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Cannot backup wallet until the -exportdir option has been set");
    }
    string unclean = params[0].get_str();
    string clean = SanitizeFilename(unclean);
    if (clean.compare(unclean) != 0) {
        throw JSONRPCError(RPC_WALLET_ERROR, strprintf("Filename is invalid as only alphanumeric characters are allowed.  Try '%s' instead.", clean));
    }
    fs::path exportfilepath = exportdir / clean;

    if (!BackupWallet(*pwalletMain, exportfilepath.string()))
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: Wallet backup failed!");

    return exportfilepath.string();
}


UniValue keypoolrefill(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(keypoolrefill ( newsize )

Fills the keypool.)"
+ HelpRequiringPassphrase() + R"(

Arguments:
1. newsize     (numeric, optional, default=100) The new keypool size

Examples:
)"
+ HelpExampleCli("keypoolrefill", "")
+ HelpExampleRpc("keypoolrefill", "")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // 0 is interpreted by TopUpKeyPool() as the default keypool size given by -keypool
    size_t kpSize = 0;
    if (params.size() > 0)
    {
        if (params[0].get_int() < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected valid size.");
        kpSize = static_cast<size_t>(params[0].get_int64());
    }

    EnsureWalletIsUnlocked();
    pwalletMain->TopUpKeyPool(static_cast<unsigned int>(kpSize));

    if (pwalletMain->GetKeyPoolSize() < kpSize)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error refreshing keypool.");

    return NullUniValue;
}


static void LockWallet(CWallet* pWallet)
{
    LOCK(cs_nWalletUnlockTime);
    nWalletUnlockTime = 0;
    pWallet->Lock();
}

UniValue walletpassphrase(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw runtime_error(
R"(walletpassphrase "passphrase" timeout

Stores the wallet decryption key in memory for 'timeout' seconds.
This is needed prior to performing transactions related to private keys such as sending Pastel

Arguments:
1. "passphrase"     (string, required) The wallet passphrase
2. timeout          (numeric, required) The time to keep the decryption key in seconds.

Note:
Issuing the walletpassphrase command while the wallet is already unlocked will set a new unlock
time that overrides the old one.

Examples:
nunlock the wallet for 60 seconds
)"
+ HelpExampleCli("walletpassphrase", "\"my pass phrase\" 60") + R"(
Lock the wallet again (before 60 seconds)
)" + HelpExampleCli("walletlock", "") + R"(
As json rpc call
)" + HelpExampleRpc("walletpassphrase", "\"my pass phrase\", 60")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrase was called.");

    // Note that the walletpassphrase is stored in params[0] which is not mlock()ed
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    // TODO: get rid of this .c_str() by implementing SecureString::operator=(string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    strWalletPass = params[0].get_str().c_str();

    if (strWalletPass.length() > 0)
    {
        if (!pwalletMain->Unlock(strWalletPass))
            throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");
    }
    else
        throw runtime_error(
R"(walletpassphrase <passphrase> <timeout>
Stores the wallet decryption key in memory for <timeout> seconds.)"
        );

    // No need to check return values, because the wallet was unlocked above
    pwalletMain->UpdateNullifierNoteMap();
    pwalletMain->TopUpKeyPool();

    int64_t nSleepTime = params[1].get_int64();
    LOCK(cs_nWalletUnlockTime);
    nWalletUnlockTime = GetTime() + nSleepTime;
    RPCRunLater("lockwallet", boost::bind(LockWallet, pwalletMain), nSleepTime);

    return NullUniValue;
}


UniValue walletpassphrasechange(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 2))
        throw runtime_error(
R"(walletpassphrasechange "oldpassphrase" "newpassphrase"

Changes the wallet passphrase from 'oldpassphrase' to 'newpassphrase'.

Arguments:
1. "oldpassphrase"      (string) The current passphrase
2. "newpassphrase"      (string) The new passphrase

Examples:
)"
+ HelpExampleCli("walletpassphrasechange", "\"old one\" \"new one\"")
+ HelpExampleRpc("walletpassphrasechange", "\"old one\", \"new one\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletpassphrasechange was called.");

    // TODO: get rid of these .c_str() calls by implementing SecureString::operator=(string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strOldWalletPass;
    strOldWalletPass.reserve(100);
    strOldWalletPass = params[0].get_str().c_str();

    SecureString strNewWalletPass;
    strNewWalletPass.reserve(100);
    strNewWalletPass = params[1].get_str().c_str();

    if (strOldWalletPass.length() < 1 || strNewWalletPass.length() < 1)
        throw runtime_error(
R"(walletpassphrasechange <oldpassphrase> <newpassphrase>
Changes the wallet passphrase from <oldpassphrase> to <newpassphrase>.)");

    if (!pwalletMain->ChangeWalletPassphrase(strOldWalletPass, strNewWalletPass))
        throw JSONRPCError(RPC_WALLET_PASSPHRASE_INCORRECT, "Error: The wallet passphrase entered was incorrect.");

    return NullUniValue;
}


UniValue walletlock(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (pwalletMain->IsCrypted() && (fHelp || params.size() != 0))
        throw runtime_error(
R"(walletlock

Removes the wallet encryption key from memory, locking the wallet.
After calling this method, you will need to call walletpassphrase again
before being able to call any methods which require the wallet to be unlocked.

Examples:
Set the passphrase for 2 minutes to perform a transaction
)" + HelpExampleCli("walletpassphrase", "\"my pass phrase\" 120") + R"(
Perform a send (requires passphrase set)
)" + HelpExampleCli("sendtoaddress", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" 1.0") + R"(
Clear the passphrase since we are done before 2 minutes is up
)" + HelpExampleCli("walletlock", "") + R"(
As json rpc call
)" + HelpExampleRpc("walletlock", "")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an unencrypted wallet, but walletlock was called.");

    {
        LOCK(cs_nWalletUnlockTime);
        pwalletMain->Lock();
        nWalletUnlockTime = 0;
    }

    return NullUniValue;
}


UniValue encryptwallet(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    string enableArg = "developerencryptwallet";
    auto fEnableWalletEncryption = fExperimentalMode && GetBoolArg("-" + enableArg, false);

    string strWalletEncryptionDisabledMsg;
    if (!fEnableWalletEncryption) {
        strWalletEncryptionDisabledMsg = experimentalDisabledHelpMsg("encryptwallet", enableArg);
    }

    if (!pwalletMain->IsCrypted() && (fHelp || params.size() != 1))
        throw runtime_error(
R"(encryptwallet "passphrase"
)" + strWalletEncryptionDisabledMsg + R"(
Encrypts the wallet with 'passphrase'. This is for first time encryption.
After this, any calls that interact with private keys such as sending or signing 
will require the passphrase to be set prior the making these calls.
Use the walletpassphrase call for this, and then walletlock call.
If the wallet is already encrypted, use the walletpassphrasechange call.
Note that this will shutdown the server.

Arguments:
1. "passphrase"    (string) The pass phrase to encrypt the wallet with. It must be at least 1 character, but should be long.

Examples:
Encrypt you wallet
)" + HelpExampleCli("encryptwallet", "\"my pass phrase\"") + R"(
Now set the passphrase to use the wallet, such as for signing or sending Pastel
)" + HelpExampleCli("walletpassphrase", "\"my pass phrase\"") + R"(
Now we can so something like sign
)" + HelpExampleCli("signmessage", "\"zcashaddress\" \"test message\"") + R"(
Now lock the wallet again by removing the passphrase
)" + HelpExampleCli("walletlock", "") + R"(
As a json rpc call
)" + HelpExampleRpc("encryptwallet", "\"my pass phrase\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (fHelp)
        return true;
    if (!fEnableWalletEncryption) {
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: wallet encryption is disabled.");
    }
    if (pwalletMain->IsCrypted())
        throw JSONRPCError(RPC_WALLET_WRONG_ENC_STATE, "Error: running with an encrypted wallet, but encryptwallet was called.");

    // TODO: get rid of this .c_str() by implementing SecureString::operator=(string)
    // Alternately, find a way to make params[0] mlock()'d to begin with.
    SecureString strWalletPass;
    strWalletPass.reserve(100);
    strWalletPass = params[0].get_str().c_str();

    if (strWalletPass.length() < 1)
        throw runtime_error(
R"(encryptwallet <passphrase>
Encrypts the wallet with <passphrase>.)");

    if (!pwalletMain->EncryptWallet(strWalletPass))
        throw JSONRPCError(RPC_WALLET_ENCRYPTION_FAILED, "Error: Failed to encrypt the wallet.");

    // BDB seems to have a bad habit of writing old data into
    // slack space in .dat files; that is bad if the old data is
    // unencrypted private keys. So:
    StartShutdown();
    return "wallet encrypted; Pastel server stopping, restart to run with encrypted wallet. The keypool has been flushed, you need to make a new backup.";
}

UniValue lockunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
R"(lockunspent unlock [{"txid":"txid","vout":n},...]

Updates list of temporarily unspendable outputs.
Temporarily lock (unlock=false) or unlock (unlock=true) specified transaction outputs.
A locked transaction output will not be chosen by automatic coin selection, when spending Pastel.
Locks are stored in memory only. Nodes start with zero locked outputs, and the locked output list
is always cleared (by virtue of process exit) when a node stops or fails.
Also see the listunspent call

Arguments:
1. unlock                (boolean, required) Whether to unlock (true) or lock (false) the specified transactions
2. "transactions"        (string, required) A json array of objects. Each object the txid (string) vout (numeric)
     [                          (json array of json objects)
       {
         "txid":"id",    (string) The transaction id
         "vout": n       (numeric) The output number
       }
       ,...
     ]

Result:
"true|false    (boolean) Whether the command was successful or not

Examples:
List the unspent transactions
)" + HelpExampleCli("listunspent", "") + R"(
Lock an unspent transaction
)" + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") + R"(
List the locked transactions
)" + HelpExampleCli("listlockunspent", "") + R"(
Unlock the transaction again
)" + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") + R"(
As a json rpc call
)" + HelpExampleRpc("lockunspent", "false, \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    if (params.size() == 1)
        RPCTypeCheck(params, {UniValue::VBOOL});
    else
        RPCTypeCheck(params, {UniValue::VBOOL, UniValue::VARR});

    bool fUnlock = params[0].get_bool();

    if (params.size() == 1) {
        if (fUnlock)
            pwalletMain->UnlockAllCoins();
        return true;
    }

    UniValue outputs = params[1].get_array();
    for (size_t idx = 0; idx < outputs.size(); idx++) {
        const UniValue& output = outputs[idx];
        if (!output.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");
        const UniValue& o = output.get_obj();

        RPCTypeCheckObj(o, {
            {"txid", UniValue::VSTR}, {"vout", UniValue::VNUM}});

        string txid = find_value(o, "txid").get_str();
        if (!IsHex(txid))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected hex txid");

        int nOutput = find_value(o, "vout").get_int();
        if (nOutput < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, vout must be positive");

        COutPoint outpt(uint256S(txid), nOutput);

        if (fUnlock)
            pwalletMain->UnlockCoin(outpt);
        else
            pwalletMain->LockCoin(outpt);
    }

    return true;
}

UniValue listlockunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 0)
        throw runtime_error(
R"(listlockunspent

Returns list of temporarily unspendable outputs.
See the lockunspent call to lock and unlock transactions for spending.

Result:
[
  {
    "txid" : "transactionid",     (string) The transaction id locked
    "vout" : n                    (numeric) The vout value
  }
  ,...
]

Examples:
List the unspent transactions
)" + HelpExampleCli("listunspent", "") + R"(
Lock an unspent transaction
)" + HelpExampleCli("lockunspent", "false \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") + R"(
List the locked transactions
)" + HelpExampleCli("listlockunspent", "") + R"(
Unlock the transaction again
)" + HelpExampleCli("lockunspent", "true \"[{\\\"txid\\\":\\\"a08e6907dbbd3d809776dbfc5d82e371b764ed838b5655e72f463568df1aadf0\\\",\\\"vout\\\":1}]\"") + R"(
As a json rpc call
)" + HelpExampleRpc("listlockunspent", "")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    vector<COutPoint> vOutpts;
    pwalletMain->ListLockedCoins(vOutpts);

    UniValue ret(UniValue::VARR);

    for (const auto &outpt : vOutpts)
    {
        UniValue o(UniValue::VOBJ);

        o.pushKV("txid", outpt.hash.GetHex());
        o.pushKV("vout", (int)outpt.n);
        ret.push_back(o);
    }

    return ret;
}

UniValue settxfee(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 1 || params.size() > 1)
        throw runtime_error(
R"(settxfee amount

Set the transaction fee per kB.

Arguments:
1. amount         (numeric, required) The transaction fee in )" + CURRENCY_UNIT + R"(/kB rounded to the nearest 0.00000001

Result
true|false        (boolean) Returns true if successful

Examples:
)"
+ HelpExampleCli("settxfee", "0.00001")
+ HelpExampleRpc("settxfee", "0.00001")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Amount
    CAmount nAmount = AmountFromValue(params[0]);

    payTxFee = CFeeRate(nAmount, 1000);
    return true;
}

UniValue getwalletinfo(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(getwalletinfo

Returns an object containing various wallet state info.

Result:
{
  "walletversion": xxxxx,     (numeric) the wallet version
  "balance": xxxxxxx,         (numeric) the total confirmed balance of the wallet in )" + CURRENCY_UNIT + R"(
  "unconfirmed_balance": xxx, (numeric) the total unconfirmed balance of the wallet in )" + CURRENCY_UNIT + R"(
  "immature_balance": xxxxxx, (numeric) the total immature balance of the wallet in )" + CURRENCY_UNIT + R"(
  "txcount": xxxxxxx,         (numeric) the total number of transactions in the wallet
  "keypoololdest": xxxxxx,    (numeric) the timestamp (seconds since GMT epoch) of the oldest pre-generated key in the key pool
  "keypoolsize": xxxx,        (numeric) how many new keys are pre-generated
  "unlocked_until": ttt,      (numeric) the timestamp in seconds since epoch (midnight Jan 1 1970 GMT) that the wallet is unlocked for transfers, or 0 if the wallet is locked
  "paytxfee": x.xxxx,         (numeric) the transaction fee configuration, set in )" + CURRENCY_UNIT + R"(/kB
  "seedfp": "uint256",        (string) the BLAKE2b-256 hash of the HD seed
}

Examples:
)"
+ HelpExampleCli("getwalletinfo", "")
+ HelpExampleRpc("getwalletinfo", "")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("walletversion", pwalletMain->GetVersion());
    obj.pushKV("balance",       ValueFromAmount(pwalletMain->GetBalance()));
    obj.pushKV("unconfirmed_balance", ValueFromAmount(pwalletMain->GetUnconfirmedBalance()));
    obj.pushKV("immature_balance",    ValueFromAmount(pwalletMain->GetImmatureBalance()));
    obj.pushKV("txcount",       static_cast<int64_t>(pwalletMain->mapWallet.size()));
    obj.pushKV("keypoololdest", pwalletMain->GetOldestKeyPoolTime());
    obj.pushKV("keypoolsize",   static_cast<int64_t>(pwalletMain->GetKeyPoolSize()));
    if (pwalletMain->IsCrypted())
        obj.pushKV("unlocked_until", nWalletUnlockTime);
    obj.pushKV("paytxfee",      ValueFromAmount(payTxFee.GetFeePerK()));
    uint256 seedFp = pwalletMain->GetHDChain().seedFp;
    if (!seedFp.IsNull())
         obj.pushKV("seedfp", seedFp.GetHex());
    return obj;
}

UniValue resendwallettransactions(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 0)
        throw runtime_error(
R"(resendwallettransactions

Immediately re-broadcast unconfirmed wallet transactions to all peers.
Intended only for testing; the wallet code periodically re-broadcasts
automatically.
Returns array of transaction ids that were re-broadcast.

Examples:
)"
+ HelpExampleCli("resendwallettransactions", "")
+ HelpExampleRpc("resendwallettransactions", "")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    v_uint256 txids = pwalletMain->ResendWalletTransactionsBefore(GetTime());
    UniValue result(UniValue::VARR);
    for (const auto& txid : txids)
        result.push_back(txid.ToString());
    return result;
}

UniValue listunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 3)
        throw runtime_error(
R"(listunspent ( minconf maxconf  ["address",...] )

Returns array of unspent transaction outputs (utxos)
with between minconf and maxconf (inclusive) confirmations.
Optionally filter to only include txouts paid to specified addresses.
Results are an array of Objects, each of which has:
{txid, vout, scriptPubKey, amount, confirmations}

Arguments:
1. minconf          (numeric, optional, default=1) The minimum confirmations to filter
2. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter
3. "addresses"      (string) A json array of Pastel addresses to filter
    [
      "address"     (string) Pastel address
      ,...
    ]

Result
[                   (array of json objects)
  {
    "txid" : "txid",          (string) the transaction id 
    "vout" : n,               (numeric) the vout value
    "generated" : true|false  (boolean) true if txout is a coinbase transaction output
    "address" : "address",    (string) the Pastel address
    "account" : "account",    (string) DEPRECATED. The associated account, or "" for the default account
    "scriptPubKey" : "key",   (string) the script key
    "amount" : x.xxx,         (numeric) the transaction amount in )" + CURRENCY_UNIT + R"(
    "confirmations" : n,      (numeric) The number of confirmations
    "redeemScript" : n        (string) The redeemScript if scriptPubKey is P2SH
    "spendable" : xxx         (bool) Whether we have the private keys to spend this output
  }
  ,...
]

Examples
)"
    + HelpExampleCli("listunspent", "")
    + HelpExampleCli("listunspent", "6 9999999 \"[\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\",\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\"]\"")
    + HelpExampleRpc("listunspent", "6, 9999999 \"[\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\",\\\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\\\"]\"")
);

    RPCTypeCheck(params, {UniValue::VNUM, UniValue::VNUM, UniValue::VARR});

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    int nMaxDepth = 9999999;
    if (params.size() > 1)
        nMaxDepth = params[1].get_int();

    KeyIO keyIO(Params());
    set<CTxDestination> destinations;
    if (params.size() > 2)
    {
        const UniValue &inputs = params[2].get_array();
        for (size_t idx = 0; idx < inputs.size(); idx++)
        {
            const UniValue& input = inputs[idx];
            CTxDestination dest = keyIO.DecodeDestination(input.get_str());

            if (!IsValidDestination(dest))
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, strprintf("Invalid Pastel address: %s", input.get_str()));

            if (!destinations.insert(dest).second)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Invalid parameter, duplicate address: %s", input.get_str()));
        }
    }

    UniValue results(UniValue::VARR);
    vector<COutput> vecOutputs;
    assert(pwalletMain != nullptr);
    LOCK2(cs_main, pwalletMain->cs_wallet);
    // get list of available COutputs
    pwalletMain->AvailableCoins(vecOutputs, false, nullptr, true);
    for (const auto& out : vecOutputs)
    {
        if (out.nDepth < nMinDepth || out.nDepth > nMaxDepth)
            continue;

        CTxDestination address;
        const auto& txOut = out.tx->vout[out.i];
        const CScript& scriptPubKey = txOut.scriptPubKey;
        const bool fValidAddress = ExtractDestination(scriptPubKey, address);

        // filter by destination address
        if (!destinations.empty() && (!fValidAddress || !destinations.count(address)))
            continue;

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("txid", out.tx->GetHash().GetHex());
        entry.pushKV("vout", out.i);
        entry.pushKV("generated", out.tx->IsCoinBase());

        if (fValidAddress)
        {
            entry.pushKV("address", keyIO.EncodeDestination(address));

            if (pwalletMain->mapAddressBook.count(address))
                entry.pushKV("account", pwalletMain->mapAddressBook[address].name);

            if (scriptPubKey.IsPayToScriptHash())
            {
                const CScriptID& hash = get<CScriptID>(address);
                CScript redeemScript;
                if (pwalletMain->GetCScript(hash, redeemScript))
                    entry.pushKV("redeemScript", HexStr(redeemScript.begin(), redeemScript.end()));
            }
        }

        entry.pushKV("scriptPubKey", HexStr(scriptPubKey.begin(), scriptPubKey.end()));
        entry.pushKV("amount", ValueFromAmount(txOut.nValue));
        entry.pushKV("confirmations", out.nDepth);
        entry.pushKV("spendable", out.fSpendable);
        results.push_back(move(entry));
    }

    return results;
}


UniValue z_listunspent(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 4)
        throw runtime_error(
R"(z_listunspent ( minconf maxconf includeWatchonly ["zaddr",...] )

Returns array of unspent shielded notes with between minconf and maxconf (inclusive) confirmations.
Optionally filter to only include notes sent to specified addresses.
When minconf is 0, unspent notes with zero confirmations are returned, even though they are not immediately spendable.
Results are an array of objects, each of which has:
  { txid, outindex, confirmations, address, amount, memo }

Arguments:
1. minconf          (numeric, optional, default=1) The minimum confirmations to filter
2. maxconf          (numeric, optional, default=9999999) The maximum confirmations to filter
3. includeWatchonly (bool, optional, default=false) Also include watchonly addresses (see 'z_importviewingkey')
4. "addresses"      (string) A json array of Sapling zaddrs to filter on.  Duplicate addresses not allowed.
    [
      "address"     (string) zaddr
      ,...
    ]"

Result
[                             (array of json object)
  {
    "txid" : "txid",          (string) the transaction id
    "outindex" (sapling) : n, (numeric) the output index
    "confirmations" : n,      (numeric) the number of confirmations
    "spendable" : true|false, (boolean) true if note can be spent by wallet, false if address is watchonly
    "address" : "address",    (string) the shielded address
    "amount": xxxxx,          (numeric) the amount of value in the note
    "memo": xxxxx,            (string) hexademical string representation of memo field
    "change": true|false,     (boolean) true if the address that received the note is also one of the sending addresses
  }
  ,...
]

Examples:
)"
    + HelpExampleCli("z_listunspent", "")
    + HelpExampleCli("z_listunspent", "6 9999999 false \"[\\\"Pzb8Ya6owSbT1EWKistVWFAEVXerZLi5nfuar8DqRZ2tkwHgvTP6GT8H6EaFf6wCnY7zwtbtnc7EcTGTfg9GdmNnV2xuYS3\\\",\\\"PzSSk8QJFqjo133DoFZvn9wwcCxt5RYeeLFJZRgws6xgJ3LroqRgXKNkhkG3ENmC8oe82UTr3PHcQB9mw7DSLXhyP6atQQ5\\\"]\"")
    + HelpExampleRpc("z_listunspent", "6 9999999 false \"[\\\"Pzb8Ya6owSbT1EWKistVWFAEVXerZLi5nfuar8DqRZ2tkwHgvTP6GT8H6EaFf6wCnY7zwtbtnc7EcTGTfg9GdmNnV2xuYS3\\\",\\\"PzSSk8QJFqjo133DoFZvn9wwcCxt5RYeeLFJZRgws6xgJ3LroqRgXKNkhkG3ENmC8oe82UTr3PHcQB9mw7DSLXhyP6atQQ5\\\"]\"")
);

    RPCTypeCheck(params, {UniValue::VNUM, UniValue::VNUM, UniValue::VBOOL, UniValue::VARR});

    int nMinDepth = 1;
    if (params.size() > 0) {
        nMinDepth = params[0].get_int();
    }
    if (nMinDepth < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum number of confirmations cannot be less than 0");
    }

    int nMaxDepth = 9999999;
    if (params.size() > 1) {
        nMaxDepth = params[1].get_int();
    }
    if (nMaxDepth < nMinDepth) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Maximum number of confirmations must be greater or equal to the minimum number of confirmations");
    }

    set<libzcash::PaymentAddress> zaddrs = {};

    bool fIncludeWatchonly = false;
    if (params.size() > 2) {
        fIncludeWatchonly = params[2].get_bool();
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    KeyIO keyIO(Params());
    // User has supplied zaddrs to filter on
    if (params.size() > 3) {
        UniValue addresses = params[3].get_array();
        if (addresses.size()==0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, addresses array is empty.");

        // Keep track of addresses to spot duplicates
        set<string> setAddress;

        // Sources
        for (const auto& o : addresses.getValues())
        {
            if (!o.isStr())
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected string");
            const string &address = o.get_str();
            auto zaddr = keyIO.DecodePaymentAddress(address);
            if (!IsValidPaymentAddress(zaddr))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, address is not a valid zaddr: ") + address);
            auto hasSpendingKey = visit(HaveSpendingKeyForPaymentAddress(pwalletMain), zaddr);
            if (!fIncludeWatchonly && !hasSpendingKey)
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, spending key for address does not belong to wallet: ") + address);
            zaddrs.insert(zaddr);

            if (setAddress.count(address))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ") + address);
            setAddress.insert(address);
        }
    }
    else {
        // User did not provide zaddrs, so use default i.e. all addresses
        // Sapling support
        set<libzcash::SaplingPaymentAddress> saplingzaddrs = {};
        pwalletMain->GetSaplingPaymentAddresses(saplingzaddrs);

        zaddrs.insert(saplingzaddrs.begin(), saplingzaddrs.end());
    }

    UniValue results(UniValue::VARR);

    if (zaddrs.size() > 0) {
        vector<SaplingNoteEntry> saplingEntries;
        pwalletMain->GetFilteredNotes(saplingEntries, zaddrs, nMinDepth, nMaxDepth, true, !fIncludeWatchonly, false);
        auto nullifierSet = pwalletMain->GetNullifiersForAddresses(zaddrs);

        for (const auto & entry : saplingEntries)
        {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("txid", entry.op.hash.ToString());
            obj.pushKV("outindex", (int)entry.op.n);
            obj.pushKV("confirmations", entry.confirmations);
            const bool hasSaplingSpendingKey = HaveSpendingKeyForPaymentAddress(pwalletMain)(entry.address);
            obj.pushKV("spendable", hasSaplingSpendingKey);
            obj.pushKV("address", keyIO.EncodePaymentAddress(entry.address));
            obj.pushKV("amount", ValueFromAmount(CAmount(entry.note.value()))); // note.value() is equivalent to plaintext.value()
            obj.pushKV("memo", HexStr(entry.memo));
            if (hasSaplingSpendingKey)
                obj.pushKV("change", pwalletMain->IsNoteSaplingChange(nullifierSet, entry.address, entry.op));
            results.push_back(move(obj));
        }
    }

    return results;
}


UniValue fundrawtransaction(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(fundrawtransaction "hexstring"

Add inputs to a transaction until it has enough in value to meet its out value.
This will not modify existing inputs, and will add one change output to the outputs.
Note that inputs which were signed may need to be resigned after completion since in/outputs have been added.
The inputs added will not be signed, use signrawtransaction for that.

Arguments:
1. "hexstring"      (string, required) The hex string of the raw transaction

Result:
{
  "hex": "value",   (string)  The resulting raw transaction (hex-encoded string)
  "fee":       n,   (numeric) The fee added to the transaction
  "changepos": n    (numeric) The position of the added change output, or -1
}

Examples:
Create a transaction with no inputs
)" + HelpExampleCli("createrawtransaction", "\"[]\" \"{\\\"myaddress\\\":0.01}\"") + R"(
Add sufficient unsigned inputs to meet the output value
)" + HelpExampleCli("fundrawtransaction", "\"rawtransactionhex\"") + R"(
Sign the transaction
)" + HelpExampleCli("signrawtransaction", "\"fundedtransactionhex\"") + R"(
Send the transaction
)" + HelpExampleCli("sendrawtransaction", "\"signedtransactionhex\"")
);

    RPCTypeCheck(params, {UniValue::VSTR});

    // parse hex string from parameter
    CTransaction origTx;
    if (!DecodeHexTx(origTx, params[0].get_str()))
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "TX decode failed");

    CMutableTransaction tx(origTx);
    CAmount nFee;
    string strFailReason;
    int nChangePos = -1;
    if(!pwalletMain->FundTransaction(tx, nFee, nChangePos, strFailReason))
        throw JSONRPCError(RPC_INTERNAL_ERROR, strFailReason);

    UniValue result(UniValue::VOBJ);
    result.pushKV("hex", EncodeHexTx(tx));
    result.pushKV("changepos", nChangePos);
    result.pushKV("fee", ValueFromAmount(nFee));

    return result;
}

UniValue zc_benchmark(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp)) {
        return NullUniValue;
    }

    if (fHelp || params.size() < 2) {
        throw runtime_error(
R"(zcbenchmark benchmarktype samplecount

Runs a benchmark of the selected type samplecount times,
returning the running times of each sample.

Output: [
  {
    "runningtime": runningtime
  },
  {
    "runningtime": runningtime
  }
  ...
]
)"
);
    }

    LOCK(cs_main);

    string benchmarktype = params[0].get_str();
    int samplecount = params[1].get_int();

    if (samplecount <= 0) {
        throw JSONRPCError(RPC_TYPE_ERROR, "Invalid samplecount");
    }

    vector<double> sample_times;
    for (int i = 0; i < samplecount; i++) {
        if (benchmarktype == "sleep") {
            sample_times.push_back(benchmark_sleep());
#ifdef ENABLE_MINING
        } else if (benchmarktype == "solveequihash") {
            if (params.size() < 3) {
                sample_times.push_back(benchmark_solve_equihash());
            } else {
                int nThreads = params[2].get_int();
                vector<double> vals = benchmark_solve_equihash_threaded(nThreads);
                sample_times.insert(sample_times.end(), vals.begin(), vals.end());
            }
#endif
        } else if (benchmarktype == "verifyequihash") {
            sample_times.push_back(benchmark_verify_equihash());
        } else if (benchmarktype == "validatelargetx") {
            // Number of inputs in the spending transaction that we will simulate
            int nInputs = 11130;
            if (params.size() >= 3) {
                nInputs = params[2].get_int();
            }
            sample_times.push_back(benchmark_large_tx(nInputs));
        } else if (benchmarktype == "connectblockslow") {
            if (!Params().IsRegTest())
                throw JSONRPCError(RPC_TYPE_ERROR, "Benchmark must be run in regtest mode");
            sample_times.push_back(benchmark_connectblock_slow());
        } else if (benchmarktype == "sendtoaddress") {
            if (!Params().IsRegTest())
                throw JSONRPCError(RPC_TYPE_ERROR, "Benchmark must be run in regtest mode");
            auto amount = AmountFromValue(params[2]);
            sample_times.push_back(benchmark_sendtoaddress(amount));
        } else if (benchmarktype == "loadwallet") {
            if (!Params().IsRegTest())
                throw JSONRPCError(RPC_TYPE_ERROR, "Benchmark must be run in regtest mode");
            sample_times.push_back(benchmark_loadwallet());
        } else if (benchmarktype == "listunspent") {
            sample_times.push_back(benchmark_listunspent());
        } else if (benchmarktype == "createsaplingspend") {
            sample_times.push_back(benchmark_create_sapling_spend());
        } else if (benchmarktype == "createsaplingoutput") {
            sample_times.push_back(benchmark_create_sapling_output());
        } else if (benchmarktype == "verifysaplingspend") {
            sample_times.push_back(benchmark_verify_sapling_spend());
        } else if (benchmarktype == "verifysaplingoutput") {
            sample_times.push_back(benchmark_verify_sapling_output());
        } else {
            throw JSONRPCError(RPC_TYPE_ERROR, "Invalid benchmarktype");
        }
    }

    UniValue results(UniValue::VARR);
    for (auto time : sample_times) {
        UniValue result(UniValue::VOBJ);
        result.pushKV("runningtime", time);
        results.push_back(result);
    }

    return results;
}

UniValue z_getnewaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    string defaultType = ADDR_TYPE_SAPLING;

    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(z_getnewaddress(type)

Returns a new shielded address for receiving payments.
With no arguments, returns a Sapling address.

Arguments:
1. "type"   (string, optional, default=")" + defaultType + R"(") The type of address. One of [")"
             + ADDR_TYPE_SAPLING + R"("].
Result:
   "zcashaddress"    (string) The new shielded address.
            
Examples:
)"
            + HelpExampleCli("z_getnewaddress", "")
            + HelpExampleCli("z_getnewaddress", ADDR_TYPE_SAPLING)
            + HelpExampleRpc("z_getnewaddress", "")
        );

    LOCK2(cs_main, pwalletMain->cs_wallet);

    EnsureWalletIsUnlocked();

    auto addrType = defaultType;
    if (params.size() > 0) {
        addrType = params[0].get_str();
    }

    KeyIO keyIO(Params());
    if (addrType == ADDR_TYPE_SAPLING)
        return keyIO.EncodePaymentAddress(pwalletMain->GenerateNewSaplingZKey());
    throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid address type");
}


UniValue z_listaddresses(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(z_listaddresses ( includeWatchonly )

Returns the list of Sapling shielded addresses belonging to the wallet.

Arguments:
1. includeWatchonly (bool, optional, default=false) Also include watchonly addresses (see 'z_importviewingkey')
Result:
[                   (json array of string)
  "zaddr"           (string) a zaddr belonging to the wallet
  ,...
]

Examples:
)"
+ HelpExampleCli("z_listaddresses", "")
+ HelpExampleRpc("z_listaddresses", "")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    bool fIncludeWatchonly = false;
    if (params.size() > 0) {
        fIncludeWatchonly = params[0].get_bool();
    }

    KeyIO keyIO(Params());
    UniValue ret(UniValue::VARR);
    {
        set<libzcash::SaplingPaymentAddress> addresses;
        pwalletMain->GetSaplingPaymentAddresses(addresses);
        for (auto addr : addresses) {
            if (fIncludeWatchonly || HaveSpendingKeyForPaymentAddress(pwalletMain)(addr)) {
                ret.push_back(keyIO.EncodePaymentAddress(addr));
            }
        }
    }
    return ret;
}

CAmount getBalanceTaddr(string transparentAddress, int minDepth=1, bool ignoreUnspendable=true) {
    set<CTxDestination> destinations;
    vector<COutput> vecOutputs;
    CAmount balance = 0;

    KeyIO keyIO(Params());
    if (transparentAddress.length() > 0) {
        CTxDestination taddr = keyIO.DecodeDestination(transparentAddress);
        if (!IsValidDestination(taddr)) {
            throw runtime_error("invalid transparent address");
        }
        destinations.insert(taddr);
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    pwalletMain->AvailableCoins(vecOutputs, false, nullptr, true);

    for (const auto& out : vecOutputs)
    {
        if (out.nDepth < minDepth) {
            continue;
        }

        if (ignoreUnspendable && !out.fSpendable) {
            continue;
        }

        if (destinations.size()) {
            CTxDestination address;
            if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address)) {
                continue;
            }

            if (!destinations.count(address)) {
                continue;
            }
        }

        CAmount nValue = out.tx->vout[out.i].nValue;
        balance += nValue;
    }
    return balance;
}

CAmount getBalanceZaddr(string address, int minDepth = 1, bool ignoreUnspendable=true) {
    CAmount balance = 0;
    vector<SaplingNoteEntry> saplingEntries;
    LOCK2(cs_main, pwalletMain->cs_wallet);
    pwalletMain->GetFilteredNotes(saplingEntries, address, minDepth, true, ignoreUnspendable);
    for (auto & entry : saplingEntries) {
        balance += CAmount(entry.note.value());
    }
    return balance;
}

// block info structure
struct txblock
{
    int height = 0;
    int index = -1;
    int64_t time = 0;

    txblock(const uint256 &hash)
    {
        if (pwalletMain->mapWallet.count(hash))
        {
            const auto& wtx = pwalletMain->mapWallet[hash];
            if (!wtx.hashBlock.IsNull())
                height = mapBlockIndex[wtx.hashBlock]->nHeight;
            index = wtx.nIndex;
            time = wtx.GetTxTime();
        }
    }
};


UniValue z_listreceivedbyaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size()==0 || params.size() >2)
        throw runtime_error(
R"(z_listreceivedbyaddress "address" ( minconf )
Return a list of amounts received by a zaddr belonging to the node's wallet.

Arguments:
1. "address"      (string) The private address.
2. minconf        (numeric, optional, default=1) Only include transactions confirmed at least this many times.

Result:
{
  "txid": txid",             (string) the transaction id.
  "amount": xxxxx,           (numeric) the amount of value in the note in )" + CURRENCY_UNIT + R"(.
  "amountPat" : xxxx         (numeric) the amount in )" + MINOR_CURRENCY_UNIT + R"(.
  "memo": xxxxx,             (string) hexadecimal string representation of memo field.
  "confirmations" : n,       (numeric) the number of confirmations.
  "blockheight": n,          (numeric) The block height containing the transaction.
  "blockindex": n,           (numeric) The block index containing the transaction.
  "blocktime": xxx,          (numeric) The transaction time in seconds since epoch (midnight Jan 1 1970 GMT).
  "jsindex" (sprout) : n,    (numeric) the joinsplit index.
  "jsoutindex" (sprout) : n, (numeric) the output index of the joinsplit.
  "outindex" (sapling) : n,  (numeric) the output index.
  "change": true|false,      (boolean) true if the address that received the note is also one of the sending addresses.
}

Examples:
)" + HelpExampleCli("z_listreceivedbyaddress", "\"Pzb8Ya6owSbT1EWKistVWFAEVXerZLi5nfuar8DqRZ2tkwHgvTP6GT8H6EaFf6wCnY7zwtbtnc7EcTGTfg9GdmNnV2xuYS3\"")
   + HelpExampleRpc("z_listreceivedbyaddress", "\"Pzb8Ya6owSbT1EWKistVWFAEVXerZLi5nfuar8DqRZ2tkwHgvTP6GT8H6EaFf6wCnY7zwtbtnc7EcTGTfg9GdmNnV2xuYS3\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int nMinDepth = 1;
    if (params.size() > 1)
        nMinDepth = params[1].get_int();
    if (nMinDepth < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum number of confirmations cannot be less than 0");

    // Check that the from address is valid.
    auto fromaddress = params[0].get_str();

    KeyIO keyIO(Params());
    auto zaddr = keyIO.DecodePaymentAddress(fromaddress);
    if (!IsValidPaymentAddress(zaddr))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid zaddr.");

    // Visitor to support Sprout and Sapling addrs
    if (!visit(PaymentAddressBelongsToWallet(pwalletMain), zaddr))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "From address does not belong to this node, zaddr spending key or viewing key not found.");

    UniValue result(UniValue::VARR);
    vector<SaplingNoteEntry> saplingEntries;
    pwalletMain->GetFilteredNotes(saplingEntries, fromaddress, nMinDepth, false, false);

    set<pair<PaymentAddress, uint256>> nullifierSet;
    auto hasSpendingKey = visit(HaveSpendingKeyForPaymentAddress(pwalletMain), zaddr);
    if (hasSpendingKey) {
        nullifierSet = pwalletMain->GetNullifiersForAddresses({zaddr});
    }

    if (get_if<libzcash::SaplingPaymentAddress>(&zaddr))
    {
        for (SaplingNoteEntry & entry : saplingEntries) {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("txid", entry.op.hash.ToString());
            obj.pushKV("amount", ValueFromAmount(CAmount(entry.note.value())));
            obj.pushKV("amountPat", CAmount(entry.note.value()));
            obj.pushKV("memo", HexStr(entry.memo));
            obj.pushKV("outindex", (int)entry.op.n);
            obj.pushKV("confirmations", entry.confirmations);

            txblock BlockData(entry.op.hash);
            obj.pushKV("blockheight", BlockData.height);
            obj.pushKV("blockindex", BlockData.index);
            obj.pushKV("blocktime", BlockData.time);

            if (hasSpendingKey) {
              obj.pushKV("change", pwalletMain->IsNoteSaplingChange(nullifierSet, entry.address, entry.op));
            }
            result.push_back(obj);
        }
    }
    return result;
}

UniValue z_getbalance(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size()==0 || params.size() >2)
        throw runtime_error(
R"(z_getbalance "address" ( minconf )

Returns the balance of a taddr or zaddr belonging to the node's wallet.
CAUTION: If the wallet has only an incoming viewing key for this address, then spends cannot be"
detected, and so the returned balance may be larger than the actual balance.

Arguments:
1. "address"        (string) The selected address. It may be a transparent or private address.
2. minconf          (numeric, optional, default=1) Only include transactions confirmed at least this many times.

Result:
amount              (numeric) The total amount in )" + CURRENCY_UNIT + R"( received for this address.

Examples:
The total amount received by address "myaddress"
)" + HelpExampleCli("z_getbalance", "\"myaddress\"") + R"(
The total amount received by address \"myaddress\" at least 5 blocks confirmed
)" + HelpExampleCli("z_getbalance", "\"myaddress\" 5") + R"(
As a json rpc call
)" + HelpExampleRpc("z_getbalance", "\"myaddress\", 5")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int nMinDepth = 1;
    if (params.size() > 1) {
        nMinDepth = params[1].get_int();
    }
    if (nMinDepth < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum number of confirmations cannot be less than 0");
    }

    KeyIO keyIO(Params());
    // Check that the from address is valid.
    auto fromaddress = params[0].get_str();
    bool fromTaddr = false;
    CTxDestination taddr = keyIO.DecodeDestination(fromaddress);
    fromTaddr = IsValidDestination(taddr);
    if (!fromTaddr) {
        auto res = keyIO.DecodePaymentAddress(fromaddress);
        if (!IsValidPaymentAddress(res)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a taddr or zaddr.");
        }
        if (!visit(PaymentAddressBelongsToWallet(pwalletMain), res)) {
             throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "From address does not belong to this node, spending key or viewing key not found.");
        }
    }

    CAmount nBalance = 0;
    if (fromTaddr) {
        nBalance = getBalanceTaddr(fromaddress, nMinDepth, false);
    } else {
        nBalance = getBalanceZaddr(fromaddress, nMinDepth, false);
    }

    return ValueFromAmount(nBalance);
}


UniValue z_gettotalbalance(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 2)
        throw runtime_error(
R"(z_gettotalbalance ( minconf includeWatchonly )

Return the total value of funds stored in the node's wallet.
CAUTION: If the wallet contains any addresses for which it only has incoming viewing keys,
the returned private balance may be larger than the actual balance, because spends cannot
be detected with incoming viewing keys.

Arguments:
1. minconf          (numeric, optional, default=1) Only include private and transparent transactions confirmed at least this many times.
2. includeWatchonly (bool, optional, default=false) Also include balance in watchonly addresses (see 'importaddress' and 'z_importviewingkey')

Result:
{
  "transparent": xxxxx,     (numeric) the total balance of transparent funds
  "private": xxxxx,         (numeric) the total balance of private funds (in both Sprout and Sapling addresses)
  "total": xxxxx,           (numeric) the total balance of both transparent and private funds
"}

Examples:
The total amount in the wallet
)" + HelpExampleCli("z_gettotalbalance", "") + R"(
The total amount in the wallet at least 5 blocks confirmed
)" + HelpExampleCli("z_gettotalbalance", "5") + R"(
As a json rpc call
)" + HelpExampleRpc("z_gettotalbalance", "5")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int nMinDepth = 1;
    if (params.size() > 0) {
        nMinDepth = params[0].get_int();
    }
    if (nMinDepth < 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum number of confirmations cannot be less than 0");
    }

    bool fIncludeWatchonly = false;
    if (params.size() > 1) {
        fIncludeWatchonly = params[1].get_bool();
    }

    // getbalance and "getbalance * 1 true" should return the same number
    // but they don't because wtx.GetAmounts() does not handle tx where there are no outputs
    // pwalletMain->GetBalance() does not accept min depth parameter
    // so we use our own method to get balance of utxos.
    CAmount nBalance = getBalanceTaddr("", nMinDepth, !fIncludeWatchonly);
    CAmount nPrivateBalance = getBalanceZaddr("", nMinDepth, !fIncludeWatchonly);
    CAmount nTotalBalance = nBalance + nPrivateBalance;
    UniValue result(UniValue::VOBJ);
    result.pushKV("transparent", FormatMoney(nBalance));
    result.pushKV("private", FormatMoney(nPrivateBalance));
    result.pushKV("total", FormatMoney(nTotalBalance));
    return result;
}

UniValue z_viewtransaction(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() != 1)
        throw runtime_error(
R"(z_viewtransaction "txid"
Get detailed shielded information about in-wallet transaction <txid>

Arguments:
1. "txid"    (string, required) The transaction id

Result:
{
  "txid" : "transactionid",   (string) The transaction id
  "spends" : [
    {
      "type" : "sprout|sapling",      (string) The type of address
      "spend" : n,                    (numeric, sapling) the index of the spend within vShieldedSpend
      "txidPrev" : "transactionid",   (string) The id for the transaction this note was created in
      "jsPrev" : n,                   (numeric, sprout) the index of the JSDescription within vJoinSplit
      "jsOutputPrev" : n,             (numeric, sprout) the index of the output within the JSDescription
      "outputPrev" : n,               (numeric, sapling) the index of the output within the vShieldedOutput
      "address" : "zcashaddress",     (string) The Zcash address involved in the transaction
      "value" : x.xxx                 (numeric) The amount in )" + CURRENCY_UNIT + R"(
      "valuePat" : xxxx               (numeric) The amount in )" + MINOR_CURRENCY_UNIT + R"(
    }
    ,...
  ],
  "outputs" : [
    {
      "type" : "sprout|sapling",      (string) The type of address
      "output" : n,                   (numeric, sapling) the index of the output within the vShieldedOutput
      "address" : "zcashaddress",     (string) The Zcash address involved in the transaction
      "outgoing" : true|false         (boolean, sapling) True if the output is not for an address in the wallet
      "value" : x.xxx                 (numeric) The amount in )" + CURRENCY_UNIT + R"(
      "valuePat" : xxxx               (numeric) The amount in )" + MINOR_CURRENCY_UNIT + R"(
      "memo" : "hexmemo",             (string) Hexademical string representation of the memo field
      "memoStr" : "memo",             (string) Only returned if memo contains valid UTF-8 text.
    }
    ,...
  ],
}
 
Examples:
)" + HelpExampleCli("z_viewtransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\"")
   + HelpExampleRpc("z_viewtransaction", "\"1075db55d416d3ca199f55b6084e2115b9345e16c5cf302fc80e9d5fbf5d48d\""));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    uint256 hash;
    hash.SetHex(params[0].get_str());

    UniValue entry(UniValue::VOBJ);
    if (!pwalletMain->mapWallet.count(hash))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid or non-wallet transaction id");
    const CWalletTx& wtx = pwalletMain->mapWallet[hash];

    entry.pushKV("txid", hash.GetHex());

    UniValue spends(UniValue::VARR);
    UniValue outputs(UniValue::VARR);

    const auto addMemo = [](UniValue &entry, array<unsigned char, ZC_MEMO_SIZE> &memo)
    {
        entry.pushKV("memo", HexStr(memo));

        // If the leading byte is 0xF4 or lower, the memo field should be interpreted as a
        // UTF-8-encoded text string.
        if (memo[0] <= 0xf4)
        {
            // Trim off trailing zeroes
            const auto end = find_if(
                memo.rbegin(),
                memo.rend(),
                [](unsigned char v) { return v != 0; });
            string memoStr(memo.begin(), end.base());
            if (utf8::is_valid(memoStr)) {
                entry.pushKV("memoStr", memoStr);
            }
        }
    };

    KeyIO keyIO(Params());

    // Collect OutgoingViewingKeys for recovering output information
    set<uint256> ovks;
    {
        // Generate the common ovk for recovering t->z outputs.
        HDSeed seed = pwalletMain->GetHDSeedForRPC();
        ovks.insert(ovkForShieldingFromTaddr(seed));
    }

    // Sapling spends
    for (size_t i = 0; i < wtx.vShieldedSpend.size(); ++i) {
        auto spend = wtx.vShieldedSpend[i];

        // Fetch the note that is being spent
        auto res = pwalletMain->mapSaplingNullifiersToNotes.find(spend.nullifier);
        if (res == pwalletMain->mapSaplingNullifiersToNotes.end()) {
            continue;
        }
        auto op = res->second;
        auto wtxPrev = pwalletMain->mapWallet.at(op.hash);

        // We don't need to check the leadbyte here: if wtx exists in
        // the wallet, it must have been successfully decrypted. This
        // means the plaintext leadbyte was valid at the block height
        // where the note was received.
        // https://zips.z.cash/zip-0212#changes-to-the-process-of-receiving-sapling-notes
        auto decrypted = wtxPrev.DecryptSaplingNoteWithoutLeadByteCheck(op).value();
        auto notePt = decrypted.first;
        auto pa = decrypted.second;

        // Store the OutgoingViewingKey for recovering outputs
        libzcash::SaplingExtendedFullViewingKey extfvk;
        assert(pwalletMain->GetSaplingFullViewingKey(wtxPrev.mapSaplingNoteData.at(op).ivk, extfvk));
        ovks.insert(extfvk.fvk.ovk);

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("type", ADDR_TYPE_SAPLING);
        entry.pushKV("spend", (int)i);
        entry.pushKV("txidPrev", op.hash.GetHex());
        entry.pushKV("outputPrev", (int)op.n);
        entry.pushKV("address", keyIO.EncodePaymentAddress(pa));
        entry.pushKV("value", ValueFromAmount(notePt.value()));
        entry.pushKV("valuePat", notePt.value());
        spends.push_back(entry);
    }

    // Sapling outputs
    for (uint32_t i = 0; i < wtx.vShieldedOutput.size(); ++i) {
        auto op = SaplingOutPoint(hash, i);

        SaplingNotePlaintext notePt;
        SaplingPaymentAddress pa;
        bool isOutgoing;

        // We don't need to check the leadbyte here: if wtx exists in
        // the wallet, it must have been successfully decrypted. This
        // means the plaintext leadbyte was valid at the block height
        // where the note was received.
        // https://zips.z.cash/zip-0212#changes-to-the-process-of-receiving-sapling-notes
        auto decrypted = wtx.DecryptSaplingNoteWithoutLeadByteCheck(op);
        if (decrypted) {
            notePt = decrypted->first;
            pa = decrypted->second;
            isOutgoing = false;
        } else {
            // Try recovering the output
            auto recovered = wtx.RecoverSaplingNoteWithoutLeadByteCheck(op, ovks);
            if (recovered) {
                notePt = recovered->first;
                pa = recovered->second;
                isOutgoing = true;
            } else {
                // Unreadable
                continue;
            }
        }
        auto memo = notePt.memo();

        UniValue entry(UniValue::VOBJ);
        entry.pushKV("type", ADDR_TYPE_SAPLING);
        entry.pushKV("output", (int)op.n);
        entry.pushKV("outgoing", isOutgoing);
        entry.pushKV("address", keyIO.EncodePaymentAddress(pa));
        entry.pushKV("value", ValueFromAmount(notePt.value()));
        entry.pushKV("valuePat", notePt.value());
        addMemo(entry, memo);
        outputs.push_back(entry);
    }

    entry.pushKV("spends", spends);
    entry.pushKV("outputs", outputs);

    return entry;
}

UniValue z_getoperationresult(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(z_getoperationresult (["operationid", ... ]) 

Retrieve the result and status of an operation which has finished, and then remove the operation from memory.
)" + HelpRequiringPassphrase() + R"(

Arguments:
1. "operationid"         (array, optional) A list of operation ids we are interested in.  If not provided, examine all operations known to the node.

Result:
"    [object, ...]"      (array) A list of JSON objects

Examples:
)"
+ HelpExampleCli("z_getoperationresult", "'[\"operationid\", ... ]'")
+ HelpExampleRpc("z_getoperationresult", "'[\"operationid\", ... ]'")
);

    // This call will remove finished operations
    return z_getoperationstatus_IMPL(params, true);
}

UniValue z_getoperationstatus(const UniValue& params, bool fHelp)
{
   if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(z_getoperationstatus (["operationid", ... ]) 

Get operation status and any associated result or error data.  The operation will remain in memory.
)" + HelpRequiringPassphrase() + R"("

Arguments:
1. "operationid"         (array, optional) A list of operation ids we are interested in.  If not provided, examine all operations known to the node.

Result:
    [object, ...]      (array) A list of JSON objects
Examples:
)"
+ HelpExampleCli("z_getoperationstatus", "'[\"operationid\", ... ]'")
+ HelpExampleRpc("z_getoperationstatus", "'[\"operationid\", ... ]'")
);

   // This call is idempotent so we don't want to remove finished operations
   return z_getoperationstatus_IMPL(params, false);
}

UniValue z_getoperationstatus_IMPL(const UniValue& params, bool fRemoveFinishedOperations=false)
{
    LOCK2(cs_main, pwalletMain->cs_wallet);

    set<AsyncRPCOperationId> filter;
    if (params.size()==1) {
        UniValue ids = params[0].get_array();
        for (const UniValue & v : ids.getValues()) {
            filter.insert(v.get_str());
        }
    }
    bool useFilter = (filter.size()>0);

    UniValue ret(UniValue::VARR);
    shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    vector<AsyncRPCOperationId> ids = q->getAllOperationIds();

    for (auto id : ids) {
        if (useFilter && !filter.count(id))
            continue;

        shared_ptr<AsyncRPCOperation> operation = q->getOperationForId(id);
        if (!operation) {
            continue;
            // It's possible that the operation was removed from the internal queue and map during this loop
            // throw JSONRPCError(RPC_INVALID_PARAMETER, "No operation exists for that id.");
        }

        UniValue obj = operation->getStatus();
        string s = obj["status"].get_str();
        if (fRemoveFinishedOperations) {
            // Caller is only interested in retrieving finished results
            if ("success"==s || "failed"==s || "cancelled"==s) {
                ret.push_back(obj);
                q->popOperationForId(id);
            }
        } else {
            ret.push_back(obj);
        }
    }

    vector<UniValue> arrTmp = ret.getValues();

    // sort results chronologically by creation_time
    sort(arrTmp.begin(), arrTmp.end(), [](UniValue a, UniValue b) -> bool {
        const int64_t t1 = find_value(a.get_obj(), "creation_time").get_int64();
        const int64_t t2 = find_value(b.get_obj(), "creation_time").get_int64();
        return t1 < t2;
    });

    ret.clear();
    ret.setArray();
    ret.push_backV(arrTmp);

    return ret;
}


// transaction.h comment: spending taddr output requires CTxIn >= 148 bytes and typical taddr txout is 34 bytes
constexpr size_t CTXIN_SPEND_DUST_SIZE = 148;
constexpr size_t CTXOUT_REGULAR_SIZE   = 34;

UniValue z_sendmanyimpl(const UniValue& params, bool fHelp, const bool bReturnChangeToSenderAddr)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    const string functionName = bReturnChangeToSenderAddr ? RPC_METHOD_SENDMANY_CHANGE : RPC_METHOD_SENDMANY;

    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
            functionName + R"( "fromaddress" [{"address":... ,"amount":...},...] ( minconf ) ( fee )

Send multiple times. Amounts are decimal numbers with at most 8 digits of precision.
Change generated from a taddr )" + (bReturnChangeToSenderAddr ? "returns to itself." : "flows to a new taddr address.") + R"(
Change generated from a zaddr returns to itself. 
When sending coinbase UTXOs to a zaddr, change is not allowed.
The entire value of the UTXO(s) must be consumed.)" + HelpRequiringPassphrase() + R"(

Arguments:
1. "fromaddress"         (string, required) The taddr or zaddr to send the funds from.
2. "amounts"             (array, required) An array of json objects representing the amounts to send.
    [{
      "address": address (string, required) The address is a taddr or zaddr
      "amount": amount   (numeric, required) The numeric amount in " + CURRENCY_UNIT + " is the value
      "memo": memo       (string, optional) If the address is a zaddr, raw data represented in hexadecimal string format
    }, ... ]
3. minconf               (numeric, optional, default=1) Only use funds confirmed at least this many times.
4. fee                   (numeric, optional, default=)" + strprintf("%s", FormatMoney(ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE)) + 
R"(The fee amount to attach to this transaction.

Result:
  "operationid"          (string) An operationid to pass to z_getoperationstatus to get the result of the operation.

Examples:
)"
    + HelpExampleCli(functionName, "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" '[{\"address\": \"PzSSk8QJFqjo133DoFZvn9wwcCxt5RYeeLFJZRgws6xgJ3LroqRgXKNkhkG3ENmC8oe82UTr3PHcQB9mw7DSLXhyP6atQQ5\" ,\"amount\": 5.0}]'")
    + HelpExampleRpc(functionName, "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\", [{\"address\": \"PzSSk8QJFqjo133DoFZvn9wwcCxt5RYeeLFJZRgws6xgJ3LroqRgXKNkhkG3ENmC8oe82UTr3PHcQB9mw7DSLXhyP6atQQ5\" ,\"amount\": 5.0}]")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Check that the from address is valid.
    auto fromaddress = params[0].get_str();
    bool fromTaddr = false;
    bool bFromSapling = false;
    KeyIO keyIO(Params());
    CTxDestination taddr = keyIO.DecodeDestination(fromaddress);
    fromTaddr = IsValidDestination(taddr);
    if (!fromTaddr)
    {
        const auto res = keyIO.DecodePaymentAddress(fromaddress);
        if (!IsValidPaymentAddress(res)) // invalid
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a taddr or zaddr.");

        // Check that we have the spending key
        if (!visit(HaveSpendingKeyForPaymentAddress(pwalletMain), res))
             throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "From address does not belong to this node, zaddr spending key not found.");

        // Remember whether this is a Sprout or Sapling address
        bFromSapling = get_if<libzcash::SaplingPaymentAddress>(&res) != nullptr;
    }

    UniValue outputs = params[1].get_array();

    if (outputs.empty())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amounts array is empty.");

    // Keep track of addresses to spot duplicates
    set<string> setAddress;

    // Recipients
    vector<SendManyRecipient> taddrRecipients;
    vector<SendManyRecipient> zaddrRecipients;
    CAmount nTotalOut = 0;

    bool bContainsSaplingOutput = false;
    for (const auto& o : outputs.getValues())
    {
        if (!o.isObject())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected object");

        // sanity check, report error if unknown key-value pairs
        for (const auto& sKey : o.getKeys())
        {
            if (sKey != "address" && sKey != "amount" && sKey != "memo")
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown key: ") + sKey);
        }

        string address = find_value(o, "address").get_str();
        bool isZaddr = false;
        const auto taddr = keyIO.DecodeDestination(address);
        if (!IsValidDestination(taddr))
        {
            auto res = keyIO.DecodePaymentAddress(address);
            if (!IsValidPaymentAddress(res))
                throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown address format: ") + address);

            isZaddr = true;
            bool bToSapling = get_if<libzcash::SaplingPaymentAddress>(&res) != nullptr;
            bContainsSaplingOutput |= bToSapling;
        }

        if (setAddress.count(address))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ")+address);
        setAddress.insert(address);

        const UniValue memoValue = find_value(o, "memo");
        string memo;
        if (!memoValue.isNull())
        {
            memo = memoValue.get_str();
            if (!isZaddr)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Memo cannot be used with a taddr.  It can only be used with a zaddr.");
            if (!IsHex(memo))
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected memo data in hexadecimal format.");
            if (memo.length() > ZC_MEMO_SIZE*2)
                throw JSONRPCError(RPC_INVALID_PARAMETER,  strprintf("Invalid parameter, size of memo is larger than maximum allowed %d", ZC_MEMO_SIZE ));
        }

        const UniValue av = find_value(o, "amount");
        const CAmount nAmount = AmountFromValue( av );
        if (nAmount < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, amount must be positive");

        if (isZaddr)
            zaddrRecipients.push_back( SendManyRecipient(address, nAmount, memo) );
        else
            taddrRecipients.push_back( SendManyRecipient(address, nAmount, memo) );

        nTotalOut += nAmount;
    }

    int nextBlockHeight = chainActive.Height() + 1;
    CMutableTransaction mtx;
    mtx.fOverwintered = true;
    mtx.nVersionGroupId = SAPLING_VERSION_GROUP_ID;
    mtx.nVersion = SAPLING_TX_VERSION;
    constexpr auto max_tx_size = MAX_TX_SIZE_AFTER_SAPLING;
    // If Sapling is not active, do not allow sending from or sending to Sapling addresses.
    if (!NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UpgradeIndex::UPGRADE_SAPLING))
    {
        if (bFromSapling || bContainsSaplingOutput)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, Sapling has not activated");
    }

    // As a sanity check, estimate and verify that the size of the transaction will be valid.
    // Depending on the input notes, the actual tx size may turn out to be larger and perhaps invalid.
    size_t txsize = 0;
    for (int i = 0; i < zaddrRecipients.size(); i++)
    {
        const auto &address = get<0>(zaddrRecipients[i]);
        const auto res = keyIO.DecodePaymentAddress(address);
        const bool bToSapling = get_if<libzcash::SaplingPaymentAddress>(&res) != nullptr;
        if (bToSapling)
            mtx.vShieldedOutput.push_back(OutputDescription());
    }
    CTransaction tx(mtx);
    txsize += GetSerializeSize(tx, SER_NETWORK, tx.nVersion);
    if (fromTaddr)
    {
        txsize += CTXIN_SPEND_DUST_SIZE;
        txsize += CTXOUT_REGULAR_SIZE;      // There will probably be taddr change
    }
    txsize += CTXOUT_REGULAR_SIZE * taddrRecipients.size();
    if (txsize > max_tx_size) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Too many outputs, size of raw transaction would be larger than limit of %d bytes", max_tx_size ));
    }

    // Minimum confirmations
    const int nMinDepth = (params.size() > 2) ? params[2].get_int() : 1;
    if (nMinDepth < 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Minimum number of confirmations cannot be less than 0");

    // Fee in Patoshis, not currency format)
    CAmount nFee        = ASYNC_RPC_OPERATION_DEFAULT_MINERS_FEE;
    CAmount nDefaultFee = nFee;

    if (params.size() > 3) {
        if (params[3].get_real() == 0.0)
            nFee = 0;
        else
            nFee = AmountFromValue(params[3]);

        // Check that the user specified fee is not absurd.
        // This allows amount=0 (and all amount < nDefaultFee) transactions to use the default network fee
        // or anything less than nDefaultFee instead of being forced to use a custom fee and leak metadata
        if (nTotalOut < nDefaultFee)
        {
            if (nFee > nDefaultFee)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Small transaction amount %s has fee %s that is greater than the default fee %s", FormatMoney(nTotalOut), FormatMoney(nFee), FormatMoney(nDefaultFee)));
        } else {
            // Check that the user specified fee is not absurd.
            if (nFee > nTotalOut)
                throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Fee %s is greater than the sum of outputs %s and also greater than the default fee", FormatMoney(nFee), FormatMoney(nTotalOut)));
	    }
    }

    // Use input parameters as the optional context info to be returned by z_getoperationstatus and z_getoperationresult.
    UniValue o(UniValue::VOBJ);
    o.pushKV("fromaddress", params[0]);
    o.pushKV("amounts", params[1]);
    o.pushKV("minconf", nMinDepth);
    o.pushKV("fee", stod(FormatMoney(nFee)));
    UniValue contextInfo = o;

    // Builder (used if Sapling addresses are involved)
    optional<TransactionBuilder> builder = TransactionBuilder(Params().GetConsensus(), nextBlockHeight, pwalletMain);

    // Contextual transaction we will build on
    // (used if no Sapling addresses are involved)
    CMutableTransaction contextualTx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), nextBlockHeight);
    const bool isShielded = !fromTaddr || zaddrRecipients.size() > 0;
    if (contextualTx.nVersion == 1 && isShielded)
        contextualTx.nVersion = 2; // Tx format should support vjoinsplits 

    // Create operation and add to global queue
    auto q = getAsyncRPCQueue();
    auto operation = make_shared<AsyncRPCOperation_sendmany>(builder, contextualTx, fromaddress, taddrRecipients, zaddrRecipients, nMinDepth, nFee, contextInfo, bReturnChangeToSenderAddr);
    q->addOperation(operation);
    AsyncRPCOperationId operationId = operation->getId();
    return operationId;
}

UniValue z_sendmanywithchangetosender(const UniValue& params, bool fHelp)
{
    return z_sendmanyimpl(params, fHelp, true);
}

UniValue z_sendmany(const UniValue& params, bool fHelp)
{
    return z_sendmanyimpl(params, fHelp, false);
}

/**
When estimating the number of coinbase utxos we can shield in a single transaction:
1. Joinsplit description is 1802 bytes.
2. Transaction overhead ~ 100 bytes
3. Spending a typical P2PKH is >=148 bytes, as defined in CTXIN_SPEND_DUST_SIZE.
4. Spending a multi-sig P2SH address can vary greatly:
   https://github.com/bitcoin/bitcoin/blob/c3ad56f4e0b587d8d763af03d743fdfc2d180c9b/src/main.cpp#L517
   In real-world coinbase utxos, we consider a 3-of-3 multisig, where the size is roughly:
    (3*(33+1))+3 = 105 byte redeem script
    105 + 1 + 3*(73+1) = 328 bytes of scriptSig, rounded up to 400 based on testnet experiments.
*/
#define CTXIN_SPEND_P2SH_SIZE 400

#define SHIELD_COINBASE_DEFAULT_LIMIT 50

UniValue z_shieldcoinbase(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() < 2 || params.size() > 4)
        throw runtime_error(
R"(z_shieldcoinbase "fromaddress" "tozaddress" ( fee ) ( limit )

Shield transparent coinbase funds by sending to a shielded zaddr.  This is an asynchronous operation and utxos
selected for shielding will be locked.  If there is an error, they are unlocked.  The RPC call `listlockunspent`
can be used to return a list of locked utxos.  The number of coinbase utxos selected for shielding can be limited
by the caller. Any limit is constrained by the consensus rule defining a maximum
transaction size of )" +
 strprintf("%d bytes before Sapling, and %d bytes once Sapling activates.", MAX_TX_SIZE_BEFORE_SAPLING, MAX_TX_SIZE_AFTER_SAPLING)
 + HelpRequiringPassphrase() + R"(

Arguments:
1. "fromaddress"         (string, required) The address is a taddr or "*" for all taddrs belonging to the wallet.
2. "toaddress"           (string, required) The address is a zaddr.
3. fee                   (numeric, optional, default=)" + strprintf("%s", FormatMoney(SHIELD_COINBASE_DEFAULT_MINERS_FEE)) + R"() The fee amount to attach to this transaction.
4. limit                 (numeric, optional, default=)" + strprintf("%d", SHIELD_COINBASE_DEFAULT_LIMIT) + R"() Limit on the maximum number of utxos to shield.  Set to 0 to use as many as will fit in the transaction.

Result:
{
  "remainingUTXOs": xxx       (numeric) Number of coinbase utxos still available for shielding.
  "remainingValue": xxx       (numeric) Value of coinbase utxos still available for shielding.
  "shieldingUTXOs": xxx       (numeric) Number of coinbase utxos being shielded.
  "shieldingValue": xxx       (numeric) Value of coinbase utxos being shielded.
  "opid": xxx                 (string) An operationid to pass to z_getoperationstatus to get the result of the operation.
}

Examples:
)"
+ HelpExampleCli("z_shieldcoinbase", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\" \"PzSSk8QJFqjo133DoFZvn9wwcCxt5RYeeLFJZRgws6xgJ3LroqRgXKNkhkG3ENmC8oe82UTr3PHcQB9mw7DSLXhyP6atQQ5\"")
+ HelpExampleRpc("z_shieldcoinbase", "\"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\", \"PzSSk8QJFqjo133DoFZvn9wwcCxt5RYeeLFJZRgws6xgJ3LroqRgXKNkhkG3ENmC8oe82UTr3PHcQB9mw7DSLXhyP6atQQ5\"")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    // Validate the from address
    auto fromaddress = params[0].get_str();
    bool isFromWildcard = fromaddress == "*";
    KeyIO keyIO(Params());
    CTxDestination taddr;
    if (!isFromWildcard) {
        taddr = keyIO.DecodeDestination(fromaddress);
        if (!IsValidDestination(taddr)) {
            throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid from address, should be a taddr or \"*\".");
        }
    }

    // Validate the destination address
    auto destaddress = params[1].get_str();
    if (!keyIO.IsValidPaymentAddressString(destaddress)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown address format: ") + destaddress );
    }

    // Convert fee from currency format to patoshis
    CAmount nFee = SHIELD_COINBASE_DEFAULT_MINERS_FEE;
    if (params.size() > 2) {
        if (params[2].get_real() == 0.0) {
            nFee = 0;
        } else {
            nFee = AmountFromValue( params[2] );
        }
    }

    int nMempoolLimit = SHIELD_COINBASE_DEFAULT_LIMIT;
    if (params.size() > 3) {
        nMempoolLimit = params[3].get_int();
        if (nMempoolLimit < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Limit on maximum number of utxos cannot be negative");
        }
    }

    const uint32_t nextBlockHeight = chainActive.Height() + 1;
    bool overwinterActive = NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UpgradeIndex::UPGRADE_OVERWINTER);
    unsigned int max_tx_size = MAX_TX_SIZE_AFTER_SAPLING;
    if (!NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UpgradeIndex::UPGRADE_SAPLING))
        max_tx_size = MAX_TX_SIZE_BEFORE_SAPLING;

    // If Sapling is not active, do not allow sending to a Sapling address.
    if (!NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UpgradeIndex::UPGRADE_SAPLING))
    {
        const auto res = keyIO.DecodePaymentAddress(destaddress);
        if (!IsValidPaymentAddress(res))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown address format: ") + destaddress );
        bool toSapling = get_if<libzcash::SaplingPaymentAddress>(&res) != nullptr;
        if (toSapling)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, Sapling has not activated");
    }

    // Prepare to get coinbase utxos
    vector<ShieldCoinbaseUTXO> inputs;
    CAmount shieldedValue = 0;
    CAmount remainingValue = 0;
    size_t estimatedTxSize = 2000;  // 1802 joinsplit description + tx overhead + wiggle room
    size_t utxoCounter = 0;
    bool maxedOutFlag = false;

    // Set of addresses to filter utxos by
    set<CTxDestination> destinations = {};
    if (!isFromWildcard)
        destinations.insert(taddr);

    // Get available utxos
    vector<COutput> vecOutputs;
    pwalletMain->AvailableCoins(vecOutputs, true, nullptr, false, true);

    // Find unspent coinbase utxos and update estimated size
    for (const auto& out : vecOutputs)
    {
        if (!out.fSpendable) {
            continue;
        }

        CTxDestination address;
        if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address)) {
            continue;
        }
        // If taddr is not wildcard "*", filter utxos
        if (destinations.size() > 0 && !destinations.count(address)) {
            continue;
        }

        if (!out.tx->IsCoinBase()) {
            continue;
        }

        utxoCounter++;
        auto scriptPubKey = out.tx->vout[out.i].scriptPubKey;
        CAmount nValue = out.tx->vout[out.i].nValue;

        if (!maxedOutFlag) {
            size_t increase = (get_if<CScriptID>(&address) != nullptr) ? CTXIN_SPEND_P2SH_SIZE : CTXIN_SPEND_DUST_SIZE;
            if (estimatedTxSize + increase >= max_tx_size ||
                (nMempoolLimit > 0 && utxoCounter > nMempoolLimit))
            {
                maxedOutFlag = true;
            } else {
                estimatedTxSize += increase;
                ShieldCoinbaseUTXO utxo(out.tx->GetHash(), out.i, nValue);
                utxo.scriptPubKey = scriptPubKey;
                inputs.push_back(utxo);
                shieldedValue += nValue;
            }
        }

        if (maxedOutFlag) {
            remainingValue += nValue;
        }
    }

    size_t numUtxos = inputs.size();

    if (numUtxos == 0) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Could not find any coinbase funds to shield.");
    }

    if (shieldedValue < nFee) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient coinbase funds, have %s, which is less than miners fee %s",
            FormatMoney(shieldedValue), FormatMoney(nFee)));
    }

    // Check that the user specified fee is sane (if too high, it can result in error -25 absurd fee)
    CAmount netAmount = shieldedValue - nFee;
    if (nFee > netAmount) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Fee %s is greater than the net amount to be shielded %s", FormatMoney(nFee), FormatMoney(netAmount)));
    }

    // Keep record of parameters in context object
    UniValue contextInfo(UniValue::VOBJ);
    contextInfo.pushKV("fromaddress", params[0]);
    contextInfo.pushKV("toaddress", params[1]);
    contextInfo.pushKV("fee", ValueFromAmount(nFee));

    // Builder (used if Sapling addresses are involved)
    TransactionBuilder builder = TransactionBuilder(
        Params().GetConsensus(), nextBlockHeight, pwalletMain);

    // Contextual transaction we will build on
    // (used if no Sapling addresses are involved)
    CMutableTransaction contextualTx = CreateNewContextualCMutableTransaction(
        Params().GetConsensus(), nextBlockHeight);
    if (contextualTx.nVersion == 1) {
        contextualTx.nVersion = 2; // Tx format should support vjoinsplits 
    }

    // Create operation and add to global queue
    shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    shared_ptr<AsyncRPCOperation> operation( new AsyncRPCOperation_shieldcoinbase(builder, contextualTx, inputs, destaddress, nFee, contextInfo) );
    q->addOperation(operation);
    AsyncRPCOperationId operationId = operation->getId();

    // Return continuation information
    UniValue o(UniValue::VOBJ);
    o.pushKV("remainingUTXOs", static_cast<uint64_t>(utxoCounter - numUtxos));
    o.pushKV("remainingValue", ValueFromAmount(remainingValue));
    o.pushKV("shieldingUTXOs", static_cast<uint64_t>(numUtxos));
    o.pushKV("shieldingValue", ValueFromAmount(shieldedValue));
    o.pushKV("opid", operationId);
    return o;
}


constexpr int MERGE_TO_ADDRESS_DEFAULT_TRANSPARENT_LIMIT = 50;
constexpr int MERGE_TO_ADDRESS_DEFAULT_SAPLING_LIMIT = 200;

#define OUTPUTDESCRIPTION_SIZE GetSerializeSize(OutputDescription(), SER_NETWORK, PROTOCOL_VERSION)
#define SPENDDESCRIPTION_SIZE GetSerializeSize(SpendDescription(), SER_NETWORK, PROTOCOL_VERSION)

UniValue z_mergetoaddress(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    string enableArg = "zmergetoaddress";
    auto fEnableMergeToAddress = fExperimentalMode && GetBoolArg("-" + enableArg, false);
    string strDisabledMsg = "";
    if (!fEnableMergeToAddress) {
        strDisabledMsg = experimentalDisabledHelpMsg("z_mergetoaddress", enableArg);
    }

    if (fHelp || params.size() < 2 || params.size() > 6)
        throw runtime_error(
R"(z_mergetoaddress ["fromaddress", ... ] "toaddress" ( fee ) ( transparent_limit ) ( shielded_limit ) ( memo ))"
+ strDisabledMsg + R"(
Merge multiple UTXOs and notes into a single UTXO or note.  Coinbase UTXOs are ignored; use `z_shieldcoinbase`
to combine those into a single note.
            
This is an asynchronous operation, and UTXOs selected for merging will be locked.  If there is an error, they
are unlocked.  The RPC call `listlockunspent` can be used to return a list of locked UTXOs.

The number of UTXOs and notes selected for merging can be limited by the caller.  If the transparent limit
parameter is set to zero it means limit the number of UTXOs based on the size of the transaction.  Any limit is
constrained by the consensus rule defining a maximum transaction size of )" + strprintf("%u bytes.", MAX_TX_SIZE_AFTER_SAPLING)
+ HelpRequiringPassphrase() + R"(

Arguments:
1. fromaddresses         (array, required) A JSON array with addresses.
                         The following special strings are accepted inside the array:
                             - "ANY_TADDR":   Merge UTXOs from any taddrs belonging to the wallet.
                             - "ANY_SAPLING": Merge notes from any Sapling zaddrs belonging to the wallet.
                         If a special string is given, any given addresses of that type will be counted as duplicates and cause an error.
    [
      address"          (string) Can be a taddr or a zaddr
      ,...
    ]
2. "toaddress"           (string, required) The taddr or zaddr to send the funds to.
3. fee                   (numeric, optional, default=)"
+ strprintf("%s", FormatMoney(MERGE_TO_ADDRESS_OPERATION_DEFAULT_MINERS_FEE)) + R"() The fee amount to attach to this transaction.
4. transparent_limit     (numeric, optional, default=)"
+ strprintf("%d", MERGE_TO_ADDRESS_DEFAULT_TRANSPARENT_LIMIT) + 
R"() Limit on the maximum number of UTXOs to merge.  Set to 0 to use as many as will fit in the transaction (after Overwinter).
5. shielded_limit        (numeric, optional, default=)"
+ strprintf("%d Sapling Notes", MERGE_TO_ADDRESS_DEFAULT_SAPLING_LIMIT) + R"() Limit on the maximum number of notes to merge.  Set to 0 to merge as many as will fit in the transaction.
6. "memo"                (string, optional) Encoded as hex. When toaddress is a zaddr, this will be stored in the memo field of the new note.

Result:
{
  "remainingUTXOs": xxx               (numeric) Number of UTXOs still available for merging.
  "remainingTransparentValue": xxx    (numeric) Value of UTXOs still available for merging.
  "remainingNotes": xxx               (numeric) Number of notes still available for merging.
  "remainingShieldedValue": xxx       (numeric) Value of notes still available for merging.
  "mergingUTXOs": xxx                 (numeric) Number of UTXOs being merged.
  "mergingTransparentValue": xxx      (numeric) Value of UTXOs being merged.
  "mergingNotes": xxx                 (numeric) Number of notes being merged.
  "mergingShieldedValue": xxx         (numeric) Value of notes being merged.
  "opid": xxx                         (string) An operationid to pass to z_getoperationstatus to get the result of the operation.
}

Examples:
)"
            + HelpExampleCli("z_mergetoaddress", "'[\"ANY_SAPLING\", \"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\"]' ptestsapling1zlgc9r90eqapx0vxc00hv3gunpgtm4wj3w9u29ehs4n5dgtdmg406dsemzl5rc7602ravnt3zr6")
            + HelpExampleRpc("z_mergetoaddress", "[\"ANY_SAPLING\", \"PtczsZ91Bt3oDPDQotzUsrx1wjmsFVgf28n\"], \"ptestsapling1zlgc9r90eqapx0vxc00hv3gunpgtm4wj3w9u29ehs4n5dgtdmg406dsemzl5rc7602ravnt3zr6\"")
        );

    if (!fEnableMergeToAddress) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error: z_mergetoaddress is disabled. Run './pascal-cli help z_mergetoaddress' for instructions on how to enable this feature.");
    }

    LOCK2(cs_main, pwalletMain->cs_wallet);

    bool useAnyUTXO = false;
    bool useAnySapling = false;
    set<CTxDestination> taddrs = {};
    set<libzcash::PaymentAddress> zaddrs = {};

    UniValue addresses = params[0].get_array();
    if (addresses.size()==0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, fromaddresses array is empty.");

    // Keep track of addresses to spot duplicates
    set<string> setAddress;

    KeyIO keyIO(Params());
    // Sources
    for (const UniValue& o : addresses.getValues()) {
        if (!o.isStr())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected string");

        string address = o.get_str();

        if (address == "ANY_TADDR") {
            useAnyUTXO = true;
        } else if (address == "ANY_SAPLING") {
            useAnySapling = true;
        } else {
            CTxDestination taddr = keyIO.DecodeDestination(address);
            if (IsValidDestination(taddr)) {
                taddrs.insert(taddr);
            } else {
                auto zaddr = keyIO.DecodePaymentAddress(address);
                if (IsValidPaymentAddress(zaddr)) {
                    zaddrs.insert(zaddr);
                } else {
                    throw JSONRPCError(RPC_INVALID_PARAMETER, string("Unknown address format: ") + address);
                }
            }
        }

        if (setAddress.count(address))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, duplicated address: ") + address);
        setAddress.insert(address);
    }

    if (useAnyUTXO && taddrs.size() > 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify specific taddrs when using \"ANY_TADDR\"");
    }
    if (useAnySapling && zaddrs.size() > 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot specify specific zaddrs when using \"ANY_SAPLING\"");
    }

    const uint32_t nextBlockHeight = chainActive.Height() + 1;
    const bool overwinterActive = NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UpgradeIndex::UPGRADE_OVERWINTER);
    const bool saplingActive = NetworkUpgradeActive(nextBlockHeight, Params().GetConsensus(), Consensus::UpgradeIndex::UPGRADE_SAPLING);

    // Validate the destination address
    auto destaddress = params[1].get_str();
    bool isToSproutZaddr = false;
    bool isToSaplingZaddr = false;
    const auto taddr = keyIO.DecodeDestination(destaddress);
    if (!IsValidDestination(taddr))
    {
        auto decodeAddr = keyIO.DecodePaymentAddress(destaddress);
        if (!IsValidPaymentAddress(decodeAddr))
            throw JSONRPCError(RPC_INVALID_PARAMETER, string("Invalid parameter, unknown address format: ") + destaddress);
        if (get_if<libzcash::SaplingPaymentAddress>(&decodeAddr) != nullptr) {
            isToSaplingZaddr = true;
            // If Sapling is not active, do not allow sending to a sapling addresses.
            if (!saplingActive)
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, Sapling has not activated");
        } else {
            isToSproutZaddr = true;
        }
    }

    // Convert fee from currency format to patoshis
    CAmount nFee = SHIELD_COINBASE_DEFAULT_MINERS_FEE;
    if (params.size() > 2)
    {
        if (params[2].get_real() == 0.0)
            nFee = 0;
        else
            nFee = AmountFromValue( params[2] );
    }

    int nUTXOLimit = MERGE_TO_ADDRESS_DEFAULT_TRANSPARENT_LIMIT;
    if (params.size() > 3) {
        nUTXOLimit = params[3].get_int();
        if (nUTXOLimit < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Limit on maximum number of UTXOs cannot be negative");
        }
    }

    int saplingNoteLimit = MERGE_TO_ADDRESS_DEFAULT_SAPLING_LIMIT;
    if (params.size() > 4) {
        int nNoteLimit = params[4].get_int();
        if (nNoteLimit < 0)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Limit on maximum number of notes cannot be negative");
        saplingNoteLimit = nNoteLimit;
    }

    string memo;
    if (params.size() > 5)
    {
        memo = params[5].get_str();
        if (!isToSaplingZaddr)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Memo can not be used with a taddr.  It can only be used with a zaddr.");
        if (!IsHex(memo))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, expected memo data in hexadecimal format.");
        if (memo.length() > ZC_MEMO_SIZE*2)
            throw JSONRPCError(RPC_INVALID_PARAMETER,  strprintf("Invalid parameter, size of memo is larger than maximum allowed %d", ZC_MEMO_SIZE ));
    }

    MergeToAddressRecipient recipient(destaddress, memo);

    // Prepare to get UTXOs and notes
    vector<MergeToAddressInputUTXO> utxoInputs;
    vector<MergeToAddressInputSaplingNote> saplingNoteInputs;
    CAmount mergedUTXOValue = 0;
    CAmount mergedNoteValue = 0;
    CAmount remainingUTXOValue = 0;
    CAmount remainingNoteValue = 0;
    size_t utxoCounter = 0;
    size_t noteCounter = 0;
    bool maxedOutUTXOsFlag = false;
    bool maxedOutNotesFlag = false;

    unsigned int max_tx_size = saplingActive ? MAX_TX_SIZE_AFTER_SAPLING : MAX_TX_SIZE_BEFORE_SAPLING;
    size_t estimatedTxSize = 200;  // tx overhead + wiggle room
    if (isToSaplingZaddr)
        estimatedTxSize += OUTPUTDESCRIPTION_SIZE;

    if (useAnyUTXO || taddrs.size() > 0)
    {
        // Get available utxos
        vector<COutput> vecOutputs;
        pwalletMain->AvailableCoins(vecOutputs, true, nullptr, false, false);

        // Find unspent utxos and update estimated size
        for (const auto& out : vecOutputs)
        {
            if (!out.fSpendable)
                continue;

            const CScript scriptPubKey = out.tx->vout[out.i].scriptPubKey;

            CTxDestination address;
            if (!ExtractDestination(scriptPubKey, address))
                continue;
            // If taddr is not wildcard "*", filter utxos
            if (taddrs.size() > 0 && !taddrs.count(address))
                continue;

            utxoCounter++;
            CAmount nValue = out.tx->vout[out.i].nValue;

            if (!maxedOutUTXOsFlag)
            {
                size_t increase = (get_if<CScriptID>(&address) != nullptr) ? CTXIN_SPEND_P2SH_SIZE : CTXIN_SPEND_DUST_SIZE;
                if (estimatedTxSize + increase >= max_tx_size ||
                    (nUTXOLimit > 0 && utxoCounter > nUTXOLimit))
                {
                    maxedOutUTXOsFlag = true;
                } else {
                    estimatedTxSize += increase;
                    COutPoint utxo(out.tx->GetHash(), out.i);
                    utxoInputs.emplace_back(utxo, nValue, scriptPubKey);
                    mergedUTXOValue += nValue;
                }
            }

            if (maxedOutUTXOsFlag) {
                remainingUTXOValue += nValue;
            }
        }
    }

    if (useAnySapling || zaddrs.size() > 0)
    {
        // Get available notes
        vector<SaplingNoteEntry> saplingEntries;
        pwalletMain->GetFilteredNotes(saplingEntries, zaddrs);

        // If Sapling is not active, do not allow sending from a sapling addresses.
        if (!saplingActive && !saplingEntries.empty())
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter, Sapling has not activated");
        // If sending between shielded addresses, they must be the same type
        if ((saplingEntries.size() > 0 && isToSproutZaddr))
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot send between Sprout and Sapling addresses using z_mergetoaddress");

        for (const auto& entry : saplingEntries)
        {
            noteCounter++;
            const CAmount nValue = static_cast<CAmount>(entry.note.value());
            if (!maxedOutNotesFlag)
            {
                size_t increase = SPENDDESCRIPTION_SIZE;
                if (estimatedTxSize + increase >= max_tx_size ||
                    (saplingNoteLimit > 0 && noteCounter > saplingNoteLimit))
                {
                    maxedOutNotesFlag = true;
                } else {
                    estimatedTxSize += increase;
                    libzcash::SaplingExtendedSpendingKey extsk;
                    if (!pwalletMain->GetSaplingExtendedSpendingKey(entry.address, extsk)) {
                        throw JSONRPCError(RPC_INVALID_PARAMETER, "Could not find spending key for payment address.");
                    }
                    saplingNoteInputs.emplace_back(entry.op, entry.note, nValue, extsk.expsk);
                    mergedNoteValue += nValue;
                }
            }

            if (maxedOutNotesFlag) {
                remainingNoteValue += nValue;
            }
        }
    }

    const size_t numUtxos = utxoInputs.size();
    const size_t numNotes = saplingNoteInputs.size();

    if (numUtxos == 0 && numNotes == 0) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Could not find any funds to merge.");
    }

    // Sanity check: Don't do anything if:
    // - We only have one from address
    // - It's equal to toaddress
    // - The address only contains a single UTXO or note
    if (setAddress.size() == 1 && setAddress.count(destaddress) && (numUtxos + numNotes) == 1) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Destination address is also the only source address, and all its funds are already merged.");
    }

    CAmount mergedValue = mergedUTXOValue + mergedNoteValue;
    if (mergedValue < nFee) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient funds, have %s, which is less than miners fee %s",
            FormatMoney(mergedValue), FormatMoney(nFee)));
    }

    // Check that the user specified fee is sane (if too high, it can result in error -25 absurd fee)
    CAmount netAmount = mergedValue - nFee;
    if (nFee > netAmount) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, strprintf("Fee %s is greater than the net amount to be shielded %s", FormatMoney(nFee), FormatMoney(netAmount)));
    }

    // Keep record of parameters in context object
    UniValue contextInfo(UniValue::VOBJ);
    contextInfo.pushKV("fromaddresses", params[0]);
    contextInfo.pushKV("toaddress", params[1]);
    contextInfo.pushKV("fee", ValueFromAmount(nFee));

    // Contextual transaction we will build on
    CMutableTransaction contextualTx = CreateNewContextualCMutableTransaction(
        Params().GetConsensus(),
        nextBlockHeight);
    // Builder (used if Sapling addresses are involved)
    optional<TransactionBuilder> builder;
    if (isToSaplingZaddr || saplingNoteInputs.size() > 0) {
        builder = TransactionBuilder(Params().GetConsensus(), nextBlockHeight, pwalletMain);
    }
    // Create operation and add to global queue
    shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    shared_ptr<AsyncRPCOperation> operation(
        new AsyncRPCOperation_mergetoaddress(builder, contextualTx, utxoInputs, saplingNoteInputs, recipient, nFee, contextInfo) );
    q->addOperation(operation);
    AsyncRPCOperationId operationId = operation->getId();

    // Return continuation information
    UniValue o(UniValue::VOBJ);
    o.pushKV("remainingUTXOs", static_cast<uint64_t>(utxoCounter - numUtxos));
    o.pushKV("remainingTransparentValue", ValueFromAmount(remainingUTXOValue));
    o.pushKV("remainingNotes", static_cast<uint64_t>(noteCounter - numNotes));
    o.pushKV("remainingShieldedValue", ValueFromAmount(remainingNoteValue));
    o.pushKV("mergingUTXOs", static_cast<uint64_t>(numUtxos));
    o.pushKV("mergingTransparentValue", ValueFromAmount(mergedUTXOValue));
    o.pushKV("mergingNotes", static_cast<uint64_t>(numNotes));
    o.pushKV("mergingShieldedValue", ValueFromAmount(mergedNoteValue));
    o.pushKV("opid", operationId);
    return o;
}

UniValue z_listoperationids(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(z_listoperationids

Returns the list of operation ids currently known to the wallet.

Arguments:
1. "status"         (string, optional) Filter result by the operation's state e.g. "success".

Result:
[                     (json array of string)
  "operationid"       (string) an operation id belonging to the wallet
  ,...
]

Examples:
)"
+ HelpExampleCli("z_listoperationids", "")
+ HelpExampleRpc("z_listoperationids", "")
);

    LOCK2(cs_main, pwalletMain->cs_wallet);

    string filter;
    bool useFilter = false;
    if (params.size()==1) {
        filter = params[0].get_str();
        useFilter = true;
    }

    UniValue ret(UniValue::VARR);
    shared_ptr<AsyncRPCQueue> q = getAsyncRPCQueue();
    vector<AsyncRPCOperationId> ids = q->getAllOperationIds();
    for (auto id : ids) {
        shared_ptr<AsyncRPCOperation> operation = q->getOperationForId(id);
        if (!operation) {
            continue;
        }
        string state = operation->getStateAsString();
        if (useFilter && filter.compare(state)!=0)
            continue;
        ret.push_back(id);
    }

    return ret;
}


UniValue z_getnotescount(const UniValue& params, bool fHelp)
{
    if (!EnsureWalletIsAvailable(fHelp))
        return NullUniValue;

    if (fHelp || params.size() > 1)
        throw runtime_error(
R"(z_getnotescount
Arguments:
1. minconf      (numeric, optional, default=1) Only include notes in transactions confirmed at least this many times.

Returns the number of sprout and sapling notes available in the wallet.

Result:
{
  "sapling"     (numeric) the number of sapling notes in the wallet
}

Examples:
)" + HelpExampleCli("z_getnotescount", "0")
   + HelpExampleRpc("z_getnotescount", "0"));

    LOCK2(cs_main, pwalletMain->cs_wallet);

    int nMinDepth = 1;
    if (params.size() > 0)
        nMinDepth = params[0].get_int();

    size_t nSaplingNoteCount = 0;
    for (const auto& [txid, wtx] : pwalletMain->mapWallet)
    {
        if (wtx.GetDepthInMainChain() >= nMinDepth)
            nSaplingNoteCount += wtx.mapSaplingNoteData.size();
    }
    UniValue ret(UniValue::VOBJ);
    ret.pushKV("sapling", static_cast<int64_t>(nSaplingNoteCount));

    return ret;
}

extern UniValue dumpprivkey(const UniValue& params, bool fHelp); // in rpcdump.cpp
extern UniValue importprivkey(const UniValue& params, bool fHelp);
extern UniValue importaddress(const UniValue& params, bool fHelp);
extern UniValue dumpwallet(const UniValue& params, bool fHelp);
extern UniValue importwallet(const UniValue& params, bool fHelp);
extern UniValue z_exportkey(const UniValue& params, bool fHelp);
extern UniValue z_importkey(const UniValue& params, bool fHelp);
extern UniValue z_exportviewingkey(const UniValue& params, bool fHelp);
extern UniValue z_importviewingkey(const UniValue& params, bool fHelp);
extern UniValue z_exportwallet(const UniValue& params, bool fHelp);
extern UniValue z_importwallet(const UniValue& params, bool fHelp);

static const CRPCCommand commands[] =
{ //  category              name                        actor (function)           okSafeMode
    //  --------------------- ------------------------    -----------------------    ----------
    { "rawtransactions",    "fundrawtransaction",       &fundrawtransaction,       false },
    { "hidden",             "resendwallettransactions", &resendwallettransactions, true  },
    { "wallet",             "addmultisigaddress",       &addmultisigaddress,       true  },
    { "wallet",             "backupwallet",             &backupwallet,             true  },
    { "wallet",             "dumpprivkey",              &dumpprivkey,              true  },
    { "wallet",             "dumpwallet",               &dumpwallet,               true  },
    { "wallet",             "encryptwallet",            &encryptwallet,            true  },
    { "wallet",             "getaccountaddress",        &getaccountaddress,        true  },
    { "wallet",             "getaccount",               &getaccount,               true  },
    { "wallet",             "getaddressesbyaccount",    &getaddressesbyaccount,    true  },
    { "wallet",             "getbalance",               &getbalance,               false },
    { "wallet",             "getnewaddress",            &getnewaddress,            true  },
    { "wallet",             "getrawchangeaddress",      &getrawchangeaddress,      true  },
    { "wallet",             "getreceivedbyaccount",     &getreceivedbyaccount,     false },
    { "wallet",             "getreceivedbyaddress",     &getreceivedbyaddress,     false },
    { "wallet",             "gettransaction",           &gettransaction,           false },
    { "wallet",             "getunconfirmedbalance",    &getunconfirmedbalance,    false },
    { "wallet",             "getwalletinfo",            &getwalletinfo,            false },
    { "wallet",             "importprivkey",            &importprivkey,            true  },
    { "wallet",             "importwallet",             &importwallet,             true  },
    { "wallet",             "importaddress",            &importaddress,            true  },
    { "wallet",             "keypoolrefill",            &keypoolrefill,            true  },
    { "wallet",             "listaccounts",             &listaccounts,             false },
    { "wallet",             "listaddressgroupings",     &listaddressgroupings,     false },
    { "wallet",             "listaddressamounts",       &listaddressamounts,       false },
    { "wallet",             "listlockunspent",          &listlockunspent,          false },
    { "wallet",             "listreceivedbyaccount",    &listreceivedbyaccount,    false },
    { "wallet",             "listreceivedbyaddress",    &listreceivedbyaddress,    false },
    { "wallet",             "listsinceblock",           &listsinceblock,           false },
    { "wallet",             "listtransactions",         &listtransactions,         false },
    { "wallet",             "listunspent",              &listunspent,              false },
    { "wallet",             "lockunspent",              &lockunspent,              true  },
    { "wallet",             "move",                     &movecmd,                  false },
    { "wallet",             "sendfrom",                 &sendfrom,                 false },
    { "wallet",             "sendmany",                 &sendmany,                 false },
    { "wallet",             "sendtoaddress",            &sendtoaddress,            false },
    { "wallet",             "setaccount",               &setaccount,               true  },
    { "wallet",             "settxfee",                 &settxfee,                 true  },
    { "wallet",             "signmessage",              &signmessage,              true  },
    { "wallet",             "walletlock",               &walletlock,               true  },
    { "wallet",             "walletpassphrasechange",   &walletpassphrasechange,   true  },
    { "wallet",             "walletpassphrase",         &walletpassphrase,         true  },
    { "wallet",             "zcbenchmark",              &zc_benchmark,             true  },
    { "wallet",             "z_listreceivedbyaddress",  &z_listreceivedbyaddress,  false },
    { "wallet",             "z_listunspent",            &z_listunspent,            false },
    { "wallet",             "z_getbalance",             &z_getbalance,             false },
    { "wallet",             "z_gettotalbalance",        &z_gettotalbalance,        false },
    { "wallet",             "z_mergetoaddress",         &z_mergetoaddress,         false },
    { "wallet",             "z_sendmany",               &z_sendmany,               false },
    { "wallet",             "z_sendmanywithchangetosender",  &z_sendmanywithchangetosender,   false },
    { "wallet",             "z_shieldcoinbase",         &z_shieldcoinbase,         false },
    { "wallet",             "z_getoperationstatus",     &z_getoperationstatus,     true  },
    { "wallet",             "z_getoperationresult",     &z_getoperationresult,     true  },
    { "wallet",             "z_listoperationids",       &z_listoperationids,       true  },
    { "wallet",             "z_getnewaddress",          &z_getnewaddress,          true  },
    { "wallet",             "z_listaddresses",          &z_listaddresses,          true  },
    { "wallet",             "z_exportkey",              &z_exportkey,              true  },
    { "wallet",             "z_importkey",              &z_importkey,              true  },
    { "wallet",             "z_exportviewingkey",       &z_exportviewingkey,       true  },
    { "wallet",             "z_importviewingkey",       &z_importviewingkey,       true  },
    { "wallet",             "z_exportwallet",           &z_exportwallet,           true  },
    { "wallet",             "z_importwallet",           &z_importwallet,           true  },
    { "wallet",             "z_viewtransaction",        &z_viewtransaction,        false },
    { "wallet",             "z_getnotescount",          &z_getnotescount,          false }
};

void RegisterWalletRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
